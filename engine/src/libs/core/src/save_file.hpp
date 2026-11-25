#pragma once

// Save file container and crash-safe replacement.
//
// On-disk layout (unchanged from the original engine, so old saves load and
// new saves load in old builds):
//
//   Header                     sFileInfo[32], extOffset, extSize
//   uint32 rawSize             size of the uncompressed script state
//   uint32 packedSize
//   byte   packed[packedSize]  zlib stream (any level)
//   --- optional, at extOffset ---
//   uint32 extPackedSize
//   byte   extPacked[...]      zlib stream of extSize bytes (description + thumbnail)
//
// zlib streams end with an Adler-32 of the uncompressed data, so a stream that
// inflates with Z_OK to the declared size is intact. Nothing here trusts a
// size field before checking it against the file.
//
// writeAtomic() never exposes a partially written save under the target name:
// data goes to "<name>.storm-tmp", is fsync'ed and read back, the previous
// save is kept as "<name>.storm-bak" until the new file is in place and the
// directory entry is durable, and only then the backup is removed.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include <zlib.h>

#include <fcntl.h>
#include <unistd.h>

namespace storm::save
{
namespace fs = std::filesystem;

// Level 9 cost ~165 ms for a 5 MiB state on a desktop core; level 4 costs
// ~23 ms for a file about 16% larger. Loading accepts every level.
inline constexpr int kCompressionLevel = 4;
inline constexpr uint32_t kMaxSectionSize = 0x8000000; // limit inherited from the original loader
inline constexpr uint64_t kMaxFileSize = 2ull * kMaxSectionSize;
inline constexpr char kTempSuffix[] = ".storm-tmp";
inline constexpr char kBackupSuffix[] = ".storm-bak";

struct Header
{
    char sFileInfo[32];
    uint32_t dwExtDataOffset;
    uint32_t dwExtDataSize;
};
static_assert(sizeof(Header) == 40, "save header layout is part of the file format");

using Bytes = std::vector<char>;

inline bool isAuxiliaryName(const std::string &name)
{
    const auto ends = [&](const char *suffix) {
        const auto n = std::strlen(suffix);
        return name.size() >= n && name.compare(name.size() - n, n, suffix) == 0;
    };
    return ends(kTempSuffix) || ends(kBackupSuffix);
}

inline fs::path withSuffix(const fs::path &path, const char *suffix)
{
    auto result = path;
    result += suffix;
    return result;
}

inline Header makeHeader(const char *info)
{
    Header header{};
    std::snprintf(header.sFileInfo, sizeof(header.sFileInfo), "%s", info && info[0] ? info : "save");
    return header;
}

inline bool pack(const void *data, uint32_t size, Bytes &out, int level = kCompressionLevel)
{
    uLongf packed = compressBound(size);
    const auto offset = out.size();
    out.resize(offset + packed);
    if (compress2(reinterpret_cast<Bytef *>(out.data() + offset), &packed, static_cast<const Bytef *>(data), size,
                  level) != Z_OK)
    {
        out.resize(offset);
        return false;
    }
    out.resize(offset + packed);
    return true;
}

inline void append(Bytes &out, const void *data, size_t size)
{
    const auto *bytes = static_cast<const char *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

inline void patch(Bytes &out, size_t offset, uint32_t value)
{
    std::memcpy(out.data() + offset, &value, sizeof(value));
}

// Complete file image for a script state without extended data.
inline bool buildState(const char *info, const void *raw, uint32_t rawSize, Bytes &image, std::string &error,
                       int level = kCompressionLevel)
{
    image.clear();
    if (raw == nullptr || rawSize == 0 || rawSize > kMaxSectionSize)
    {
        error = "empty or oversized script state";
        return false;
    }
    const Header header = makeHeader(info);
    append(image, &header, sizeof(header));
    append(image, &rawSize, sizeof(rawSize));
    const auto packedSizeAt = image.size();
    const uint32_t zero = 0;
    append(image, &zero, sizeof(zero));
    if (!pack(raw, rawSize, image, level))
    {
        error = "cannot compress script state";
        return false;
    }
    patch(image, packedSizeAt, static_cast<uint32_t>(image.size() - packedSizeAt - sizeof(uint32_t)));
    return true;
}

// The names of a section's size fields in the layout above: errors name the field that is wrong.
struct Section
{
    const char *declared; // the uncompressed size
    const char *packed;
};
inline constexpr Section kState{"rawSize", "packedSize"};
inline constexpr Section kExtData{"extSize", "extPackedSize"};

inline std::string field(const char *name, uint64_t value)
{
    return std::string(name) + " " + std::to_string(value);
}

// Inflate a section and require a clean stream of at most `declared` bytes.
inline bool unpack(const char *packed, uint32_t packedSize, uint32_t declared, Bytes &out, std::string &error,
                   const Section &section)
{
    if (packedSize == 0 || packedSize > kMaxSectionSize)
    {
        error = field(section.packed, packedSize) + " is out of range";
        return false;
    }
    if (declared == 0 || declared > kMaxSectionSize)
    {
        error = field(section.declared, declared) + " is out of range";
        return false;
    }
    out.resize(declared);
    uLongf size = declared;
    const int result =
        uncompress(reinterpret_cast<Bytef *>(out.data()), &size, reinterpret_cast<const Bytef *>(packed), packedSize);
    if (result != Z_OK)
    {
        out.clear();
        const auto stream = field(section.packed, packedSize) + " bytes ";
        error = result == Z_BUF_ERROR    ? stream + "do not inflate within " + field(section.declared, declared)
                : result == Z_DATA_ERROR ? stream + "are corrupt"
                                         : stream + "fail in zlib (" + std::to_string(result) + ")";
        return false;
    }
    out.resize(size);
    return true;
}

// Extract and validate the script state of a file image.
inline bool readState(const Bytes &image, Bytes &raw, std::string &error)
{
    raw.clear();
    constexpr size_t fixed = sizeof(Header) + 2 * sizeof(uint32_t);
    if (image.size() < fixed)
    {
        error = field("file size", image.size()) + " is shorter than the save header";
        return false;
    }
    uint32_t rawSize, packedSize;
    std::memcpy(&rawSize, image.data() + sizeof(Header), sizeof(rawSize));
    std::memcpy(&packedSize, image.data() + sizeof(Header) + sizeof(uint32_t), sizeof(packedSize));
    if (packedSize > image.size() - fixed)
    {
        error = field("packedSize", packedSize) + " runs past the end of the file (" +
                std::to_string(image.size() - fixed) + " bytes after the header)";
        return false;
    }
    return unpack(image.data() + fixed, packedSize, rawSize, raw, error, kState);
}

// Extended data (may legitimately be absent: `data` stays empty and the call succeeds).
inline bool readExtData(const Bytes &image, Bytes &data, std::string &error)
{
    data.clear();
    if (image.size() < sizeof(Header) + sizeof(uint32_t))
    {
        error = field("file size", image.size()) + " is shorter than the save header";
        return false;
    }
    Header header;
    std::memcpy(&header, image.data(), sizeof(header));
    if (header.dwExtDataSize == 0)
        return true;
    if (header.dwExtDataOffset < sizeof(Header) || header.dwExtDataOffset > image.size() - sizeof(uint32_t))
    {
        error = field("extOffset", header.dwExtDataOffset) + " is outside the file of " + std::to_string(image.size()) +
                " bytes";
        return false;
    }
    uint32_t packedSize;
    std::memcpy(&packedSize, image.data() + header.dwExtDataOffset, sizeof(packedSize));
    const size_t start = header.dwExtDataOffset + sizeof(uint32_t);
    if (packedSize > image.size() - start)
    {
        error = field("extPackedSize", packedSize) + " runs past the end of the file";
        return false;
    }
    return unpack(image.data() + start, packedSize, header.dwExtDataSize, data, error, kExtData);
}

// New image = old image + extended data appended at the end (the original
// SetSaveData semantics), header rewritten to point at it.
inline bool appendExtData(Bytes &image, const char *info, const void *data, uint32_t size, std::string &error,
                          int level = kCompressionLevel)
{
    if (image.size() < sizeof(Header) || image.size() > UINT32_MAX - sizeof(uint32_t))
    {
        error = "file is shorter than a save header";
        return false;
    }
    if (data == nullptr || size == 0 || size > kMaxSectionSize)
    {
        error = "empty or oversized extended data";
        return false;
    }
    Header header = makeHeader(info);
    header.dwExtDataOffset = static_cast<uint32_t>(image.size());
    header.dwExtDataSize = size;
    const auto packedSizeAt = image.size();
    const uint32_t zero = 0;
    append(image, &zero, sizeof(zero));
    if (!pack(data, size, image, level))
    {
        image.resize(packedSizeAt);
        error = "cannot compress extended data";
        return false;
    }
    patch(image, packedSizeAt, static_cast<uint32_t>(image.size() - packedSizeAt - sizeof(uint32_t)));
    std::memcpy(image.data(), &header, sizeof(header));
    return true;
}

inline bool readFile(const fs::path &path, Bytes &data, std::string &error)
{
    data.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "cannot open file";
        return false;
    }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size < 0 || static_cast<uint64_t>(size) > kMaxFileSize)
    {
        error = field("file size", static_cast<uint64_t>(size)) + " is over the " + std::to_string(kMaxFileSize) +
                "-byte limit";
        return false;
    }
    data.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(data.data(), size))
    {
        data.clear();
        error = "cannot read file";
        return false;
    }
    return true;
}

// Extended data of a save on disk without reading the script state: the header and the
// section at its offset only (save lists read the descriptions of every record).
inline bool readExtDataFromFile(const fs::path &path, Bytes &data, std::string &error)
{
    data.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "cannot open file";
        return false;
    }
    const auto size = static_cast<uint64_t>(static_cast<std::streamoff>(file.tellg()));
    Header header;
    if (size < sizeof(Header) + sizeof(uint32_t) || !file.seekg(0) ||
        !file.read(reinterpret_cast<char *>(&header), sizeof(header)))
    {
        error = field("file size", size) + " is shorter than the save header";
        return false;
    }
    if (header.dwExtDataSize == 0)
        return true;
    uint32_t packedSize = 0;
    if (header.dwExtDataOffset < sizeof(Header) || header.dwExtDataOffset > size - sizeof(uint32_t) ||
        !file.seekg(header.dwExtDataOffset) || !file.read(reinterpret_cast<char *>(&packedSize), sizeof(packedSize)))
    {
        error =
            field("extOffset", header.dwExtDataOffset) + " is outside the file of " + std::to_string(size) + " bytes";
        return false;
    }
    if (packedSize > size - header.dwExtDataOffset - sizeof(uint32_t) || packedSize > kMaxSectionSize)
    {
        error = field("extPackedSize", packedSize) + " runs past the end of the file";
        return false;
    }
    Bytes packed(packedSize);
    if (packedSize > 0 && !file.read(packed.data(), packedSize))
    {
        error = "cannot read file";
        return false;
    }
    return unpack(packed.data(), packedSize, header.dwExtDataSize, data, error, kExtData);
}

// Whether a file image would load: the script state and the extended data.
inline bool checkImage(const Bytes &image, std::string &error)
{
    Bytes scratch;
    return readState(image, scratch, error) && readExtData(image, scratch, error);
}

// Whether a file would load: the size limit and checkImage. Reads only.
inline bool checkFile(const fs::path &path, std::string &error)
{
    Bytes image;
    return readFile(path, image, error) && checkImage(image, error);
}

namespace detail
{
inline std::string lastError(const char *what)
{
    return std::string(what) + ": " + std::generic_category().message(errno);
}

// Write and flush to stable storage. Returns false with `error` set.
inline bool writeDurable(const fs::path &path, const char *data, size_t size, std::string &error)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        error = lastError("create temporary save");
        return false;
    }
    bool ok = true;
    size_t written = 0;
    while (written < size)
    {
        const auto n = ::write(fd, data + written, size - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            ok = false;
            error = lastError("write temporary save");
            break;
        }
        written += static_cast<size_t>(n);
    }
    if (ok && ::fsync(fd) != 0)
    {
        ok = false;
        error = lastError("sync temporary save");
    }
    if (::close(fd) != 0 && ok)
    {
        ok = false;
        error = lastError("close temporary save");
    }
    return ok;
}

// Make a rename durable. Some filesystems refuse directory fsync; that is not
// a reason to fail a save whose data is already synced.
inline void syncDirectory(const fs::path &directory)
{
    const int fd = ::open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0)
    {
        (void)::fsync(fd);
        ::close(fd);
    }
}

inline void removeQuietly(const fs::path &path)
{
    std::error_code ignored;
    fs::remove(path, ignored);
}
} // namespace detail

// Test seam: lets a test stop the sequence after a named step to model a crash.
// Steps, in order: "temp-written", "backup-made", "renamed".
using CrashHook = std::function<bool(const char *step)>;

// Replace `target` with `image`. `validate` is run on the bytes read back from
// the temporary file; the target is only touched after it accepts them.
// On failure the previous target (if any) is left untouched and still loadable.
inline bool writeAtomic(const fs::path &target, const Bytes &image,
                        const std::function<bool(const Bytes &, std::string &)> &validate, std::string &error,
                        const CrashHook &crash = {})
{
    const auto temp = withSuffix(target, kTempSuffix);
    const auto backup = withSuffix(target, kBackupSuffix);
    const auto stop = [&](const char *step) { return crash && crash(step); };

    if (!detail::writeDurable(temp, image.data(), image.size(), error))
    {
        detail::removeQuietly(temp);
        return false;
    }

    // Verify what actually reached the file, not the buffer we meant to write.
    Bytes stored;
    if (!readFile(temp, stored, error) || stored.size() != image.size() ||
        std::memcmp(stored.data(), image.data(), image.size()) != 0)
    {
        if (error.empty())
            error = "temporary save differs from the written data";
        detail::removeQuietly(temp);
        return false;
    }
    if (validate && !validate(stored, error))
    {
        detail::removeQuietly(temp);
        return false;
    }
    if (stop("temp-written"))
        return false;

    // Keep the previous save reachable under the backup name without ever
    // removing the target name: hard link where supported, copy otherwise.
    std::error_code ec;
    const bool hadTarget = fs::is_regular_file(target, ec) && !ec;
    bool hasBackup = false;
    if (hadTarget)
    {
        detail::removeQuietly(backup);
        fs::create_hard_link(target, backup, ec);
        if (ec)
        {
            ec.clear();
            fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
        }
        hasBackup = !ec;
        if (!hasBackup)
            detail::removeQuietly(backup); // never leave a partial copy that could be mistaken for a save
    }
    if (stop("backup-made"))
        return false;

    fs::rename(temp, target, ec); // atomic replacement on POSIX; MoveFileEx(REPLACE_EXISTING) on Windows
    if (ec)
    {
        error = "replace save: " + ec.message();
        detail::removeQuietly(temp);
        if (hasBackup)
            detail::removeQuietly(backup);
        return false;
    }
    detail::syncDirectory(target.parent_path());
    if (stop("renamed"))
        return false;

    // The verified file now owns the name durably; the backup has done its job.
    if (hasBackup)
    {
        detail::removeQuietly(backup);
        detail::syncDirectory(target.parent_path());
    }
    return true;
}

enum class Source
{
    None,    // nothing loadable
    Primary, // the save itself
    Backup   // the save was missing or corrupt; the previous version was restored
};

// Read a save for loading. If the target is missing or fails validation and an
// intact backup from an interrupted write exists, the backup is promoted to
// the target name and returned instead.
inline Source loadWithRecovery(const fs::path &target, Bytes &image, Bytes &raw, std::string &error)
{
    std::string primaryError;
    if (readFile(target, image, primaryError) && readState(image, raw, primaryError))
        return Source::Primary;

    const auto backup = withSuffix(target, kBackupSuffix);
    std::string backupError;
    if (readFile(backup, image, backupError) && readState(image, raw, backupError))
    {
        std::error_code ec;
        fs::rename(backup, target, ec);
        if (!ec)
            detail::syncDirectory(target.parent_path());
        error = "primary save unusable (" + primaryError + "); restored previous version";
        return Source::Backup;
    }
    image.clear();
    raw.clear();
    error = primaryError;
    return Source::None;
}
} // namespace storm::save
