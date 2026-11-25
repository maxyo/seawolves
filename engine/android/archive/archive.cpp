#include "internal.hpp"
#include "memory.hpp"
#include "platform/jni_text.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <memory>
#include <vector>
#ifdef STORM_ARCHIVE_TESTING
static uint64_t simulated_free_bytes = 0;
extern "C" void storm_archive_test_free_bytes(uint64_t bytes)
{
    simulated_free_bytes = bytes;
}
#endif
namespace storm_archive
{
Fd::~Fd()
{
    if (value >= 0)
        close(value);
}
static void io(const char *what)
{
    if (errno == ENOSPC || errno == EDQUOT)
        throw Failure(STORM_ARCHIVE_NO_SPACE, std::string(what) + ": no space left on device");
    throw Failure(STORM_ARCHIVE_IO, std::string(what) + ": " + strerror(errno));
}
uint64_t free_bytes(int fd)
{
#ifdef STORM_ARCHIVE_TESTING
    if (simulated_free_bytes)
        return simulated_free_bytes;
#endif
    struct statvfs st{};
    if (fstatvfs(fd, &st) < 0)
        io("Read free space");
    return static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
}
std::string checked_input(const char *input)
{
    if (!input || !*input)
        throw Failure(STORM_ARCHIVE_IO, "Missing input path");
    Fd fd(open(input, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat st{};
    if (fd.value < 0 || fstat(fd.value, &st) < 0)
        io("Open input");
    if (!S_ISREG(st.st_mode))
        throw Failure(STORM_ARCHIVE_INVALID, "Input must be a regular local file");
    return input;
}
Context::Context(const char *output, const storm_archive_source_limits &l, storm_archive_stage_progress p, void *u)
    : root(output ? open(output, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1), limits(l), progress(p),
      userdata(u)
{
    if (!output)
        return;
    if (root.value < 0)
        io("Open staging");
    DIR *dir = fdopendir(dup(root.value));
    if (!dir)
        io("Read staging");
    bool empty = true;
    while (auto *e = readdir(dir))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
        {
            empty = false;
            break;
        }
    closedir(dir);
    if (!empty)
        throw Failure(STORM_ARCHIVE_INVALID, "Staging directory must be empty");
}
void Context::check(const std::string &path)
{
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count() >=
        static_cast<int64_t>(limits.max_seconds))
        throw Failure(STORM_ARCHIVE_LIMIT, "Extraction time limit exceeded");
    if (progress && progress(userdata, stage, path.c_str(), total, stage_total, extracted))
        throw Failure(STORM_ARCHIVE_CANCELLED, "Import cancelled");
}
void Context::account(uint64_t n, const std::string &path)
{
    if (n > limits.max_total_bytes - total)
        throw Failure(STORM_ARCHIVE_LIMIT, "Expanded data limit exceeded");
    total += n;
    container_decoded += n;
    // Bomb guard: decoded bytes of one container are bounded by its input size.
    if (container_input && container_decoded > limits.max_ratio * container_input + (1ULL << 20))
        throw Failure(STORM_ARCHIVE_LIMIT,
                      "Compression ratio exceeds 1:" + std::to_string(limits.max_ratio) + " (possible archive bomb)");
    check(path);
}
std::vector<std::string> path_parts(const std::string &raw)
{
    std::string s = raw;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty() || s.size() > 1024 || s.front() == '/' || s.find(':') != s.npos)
        throw Failure(STORM_ARCHIVE_INVALID, "Unsafe archive path");
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t end = s.find('/', pos);
        if (end == s.npos)
            end = s.size();
        auto part = s.substr(pos, end - pos);
        if (part.empty() || part == "." || part == ".." || part.size() > 255 || part.back() == '.' ||
            part.back() == ' ')
            throw Failure(STORM_ARCHIVE_INVALID, "Unsafe archive component");
        for (size_t at = 0; at < part.size();)
        {
            const auto [c, length] = storm::jni_text::DecodeUtf8(part, at);
            if (!length)
                throw Failure(STORM_ARCHIVE_INVALID, "Archive path is not UTF-8: " + printable_name(raw));
            if (c < 32 || c == 127 || (c >= 0x80 && c <= 0x9F))
                throw Failure(STORM_ARCHIVE_INVALID, "Control character in archive path: " + printable_name(raw));
            at += length;
        }
        parts.push_back(part);
        if (parts.size() > 32)
            throw Failure(STORM_ARCHIVE_LIMIT, "Archive path too deep");
        pos = end + 1;
    }
    if (parts.empty())
        throw Failure(STORM_ARCHIVE_INVALID, "Empty archive path");
    return parts;
}
std::string join_parts(const std::vector<std::string> &parts, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count && i < parts.size(); ++i)
        out += parts[i] + "/";
    return out;
}
Anchor anchor_at(const std::vector<std::string> &parts, size_t i)
{
    auto key = lower(parts[i]);
    bool leaf = i + 1 == parts.size();
    if (!leaf && key == "resource")
        return RESOURCE_DIR;
    if (!leaf && key == "program")
        return PROGRAM_DIR;
    if (leaf && key == "engine.ini")
        return ENGINE_INI;
    if (leaf && key == "options")
        return OPTIONS_FILE;
    return NO_ANCHOR;
}
std::string root_output(const std::vector<std::string> &parts, size_t i)
{
    auto kind = anchor_at(parts, i);
    std::string out = kind == RESOURCE_DIR ? "RESOURCE" : kind == PROGRAM_DIR ? "PROGRAM" : lower(parts[i]);
    for (size_t j = i + 1; j < parts.size(); ++j)
        out += "/" + parts[j];
    auto ext = lower(parts.back());
    auto dot = ext.rfind('.');
    ext = dot == ext.npos ? "" : ext.substr(dot);
    static const std::set<std::string> blocked = {".exe", ".dll", ".com", ".bat", ".cmd", ".msi", ".scr",
                                                  ".ps1", ".sh",  ".so",  ".apk", ".jar", ".lnk"};
    return blocked.count(ext) ? std::string{} : out;
}
std::string Context::select(const std::string &raw)
{
    check(raw);
    if (++entries > limits.max_entries)
        throw Failure(STORM_ARCHIVE_LIMIT, "Too many archive entries");
    auto parts = path_parts(raw);
    // Exactly the discovered root: members of other roots or wrappers are skipped.
    auto depth = static_cast<size_t>(std::count(selected_root.begin(), selected_root.end(), '/'));
    if (parts.size() <= depth || join_parts(parts, depth) != selected_root || anchor_at(parts, depth) == NO_ANCHOR)
        return {};
    return root_output(parts, depth);
}
int Context::create(const std::string &path)
{
    if (!paths.insert(lower(path)).second)
        throw Failure(STORM_ARCHIVE_INVALID, "Duplicate output path: " + path);
    Fd parent(dup(root.value));
    if (parent.value < 0)
        io("Duplicate staging descriptor");
    size_t pos = 0;
    while (true)
    {
        size_t end = path.find('/', pos);
        auto name = path.substr(pos, end == path.npos ? end : end - pos);
        if (end == path.npos)
        {
            int fd = openat(parent.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (fd < 0)
                io("Create output");
            if (path.rfind("RESOURCE/", 0) == 0)
                resources = true;
            if (path.rfind("PROGRAM/", 0) == 0)
                programs = true;
            if (path == "engine.ini")
                engine = true;
            ++extracted;
            return fd;
        }
        if (mkdirat(parent.value, name.c_str(), 0700) < 0 && errno != EEXIST)
            io("Create directory");
        int next = openat(parent.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
            io("Open output directory");
        close(parent.value);
        parent.value = next;
        pos = end + 1;
    }
}
void Context::write(int fd, const char *data, size_t n)
{
    // Stop before the device is full instead of failing in the middle of a write.
    written_since_space_check += n;
    if (written_since_space_check >= (16ULL << 20))
    {
        written_since_space_check = 0;
        if (free_bytes(fd) < limits.reserve_bytes + n)
            throw Failure(STORM_ARCHIVE_NO_SPACE, "Not enough free storage while writing");
    }
    while (n)
    {
        auto done = ::write(fd, data, n);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            io("Write output");
        data += done;
        n -= done;
    }
}
void Context::finish()
{
    if (!resources || !programs || !engine)
        throw Failure(STORM_ARCHIVE_NO_GAME_DATA, "Archive must contain engine.ini, RESOURCE and PROGRAM game data");
    check();
}
std::string to_hex(const std::string &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes)
    {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
std::string from_hex(const std::string &text)
{
    auto value = [](char c) -> int {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    if (text.size() % 2)
        throw Failure(STORM_ARCHIVE_INVALID, "Invalid game root id");
    std::string out;
    for (size_t i = 0; i < text.size(); i += 2)
    {
        int a = value(text[i]), b = value(text[i + 1]);
        if (a < 0 || b < 0)
            throw Failure(STORM_ARCHIVE_INVALID, "Invalid game root id");
        out += static_cast<char>(a * 16 + b);
    }
    return out;
}
std::string json_quote(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (c < 32)
            out += "\\u00" + to_hex(std::string(1, static_cast<char>(c)));
        else
            out += c;
    }
    return out + '"';
}
char *result_json(const std::string &text)
{
    auto *p = static_cast<char *>(allocate_result(text.size() + 1));
    if (!p)
        throw std::bad_alloc();
    memcpy(p, text.c_str(), text.size() + 1);
    return p;
}
static int message(char *out, size_t n, int code, const char *text)
{
    if (out && n)
    {
        size_t size = strnlen(text, n - 1);
        if (text[size])
            while (size && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
                --size;
        memcpy(out, text, size);
        out[size] = 0;
    }
    return code;
}
int boundary(char **json, char *error, size_t cap, const std::function<void(std::string &failure)> &call)
{
    if (json)
        *json = nullptr;
    clear_allocation_failure();
    std::string failure;
    auto finish = [&](int code, const char *text) {
        // The failure document is extra detail: when it cannot be allocated, the status and the message still go back.
        if (json && !failure.empty() && !*json)
        {
            try
            {
                *json = result_json(failure);
            }
            catch (...)
            {
            }
        }
        return message(error, cap, code, text);
    };
    try
    {
        call(failure);
        if (allocation_failed())
            throw std::bad_alloc();
        return message(error, cap, STORM_ARCHIVE_OK, "");
    }
    catch (const Failure &x)
    {
        if (failure.empty() && !x.missing.empty())
        {
            failure = "{\"missing_volumes\":[";
            for (size_t i = 0; i < x.missing.size(); ++i)
                failure += (i ? "," : "") + json_quote(x.missing[i]);
            failure += "]}";
        }
        return finish(allocation_failed() ? STORM_ARCHIVE_MEMORY : x.status, x.what());
    }
    catch (const std::bad_alloc &)
    {
        return finish(STORM_ARCHIVE_MEMORY, "Archive memory limit exceeded");
    }
    catch (const std::exception &x)
    {
        return finish(allocation_failed() ? STORM_ARCHIVE_MEMORY : STORM_ARCHIVE_INVALID, x.what());
    }
    catch (...)
    {
        return finish(STORM_ARCHIVE_INVALID, "Unknown archive failure");
    }
}
} // namespace storm_archive
namespace storm_archive
{
namespace
{
// Removes `name` in the directory `parent` and everything below it, by the bytes of each name: a tree written by
// an older reader may hold names that are not UTF-8, which Java cannot list (File.list aborts under CheckJNI and
// garbles them otherwise). Links are removed, never followed.
[[noreturn]] void failed(const char *what, const std::string &shown)
{
    const int saved = errno;
    const std::string message = std::string(what) + " " + printable_name(shown);
    errno = saved;
    io(message.c_str());
    throw Failure(STORM_ARCHIVE_IO, message);
}
void remove_below(int parent, const std::string &name, const std::string &shown, unsigned depth)
{
    if (depth > 256)
        throw Failure(STORM_ARCHIVE_LIMIT, "Directory tree too deep to remove: " + printable_name(shown));
    struct stat st{};
    if (fstatat(parent, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) < 0)
    {
        if (errno == ENOENT)
            return;
        failed("Inspect", shown);
    }
    if (S_ISDIR(st.st_mode))
    {
        int fd = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            failed("Open", shown);
        std::unique_ptr<DIR, int (*)(DIR *)> dir(fdopendir(fd), closedir);
        if (!dir)
        {
            close(fd);
            failed("Read", shown);
        }
        std::vector<std::string> children;
        errno = 0;
        while (auto *e = readdir(dir.get()))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                children.emplace_back(e->d_name);
        if (errno)
            failed("Read", shown);
        for (const auto &child : children)
            remove_below(dirfd(dir.get()), child, shown + "/" + child, depth + 1);
    }
    if (unlinkat(parent, name.c_str(), S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) < 0 && errno != ENOENT)
        failed("Remove", shown);
}
} // namespace
} // namespace storm_archive
extern "C" int storm_archive_remove_tree(const char *path, char *error, size_t cap)
{
    return storm_archive::boundary(nullptr, error, cap, [&](std::string &) {
        if (!path || *path != '/')
            throw storm_archive::Failure(STORM_ARCHIVE_IO, "Removal needs an absolute path");
        std::string text(path);
        while (text.size() > 1 && text.back() == '/')
            text.pop_back();
        const auto slash = text.rfind('/');
        const auto name = text.substr(slash + 1);
        if (name.empty() || name == "." || name == "..")
            throw storm_archive::Failure(STORM_ARCHIVE_IO, "Removal needs a directory entry");
        storm_archive::Fd parent(open(slash ? text.substr(0, slash).c_str() : "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (parent.value < 0)
        {
            if (errno == ENOENT)
                return;
            storm_archive::io("Open the parent of the tree to remove");
        }
        storm_archive::remove_below(parent.value, name, text, 0);
    });
}
