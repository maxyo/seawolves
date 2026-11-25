#include "internal.hpp"
#include "memory.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <locale.h>
#include <memory>
#include <exception>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
namespace storm_archive
{
namespace
{
struct ArchiveDeleter
{
    CodecArena *arena;
    void operator()(archive *value) const noexcept
    {
        if (!value || arena->poisoned)
            return;
        try
        {
            archive_read_free(value);
        }
        catch (...)
        {
            mark_allocation_failure();
        }
    }
};
// libarchive has no iconv in this build (build.py) and converts every name to the charset of the calling thread's
// locale: UTF-8 on Android (bionic), but whatever the process set on a glibc host, where under the C locale (ASCII) a
// Unicode name of 7z, RAR or a ZIP with the UTF-8 flag cannot be converted at all. The reader runs libarchive under
// C.UTF-8 on its own thread (uselocale), so it reads the same on every platform and the host checks show what the
// device does: libarchive gives the UTF-8 of every format that stores Unicode names and the stored bytes of the
// others, which LegacyNames decodes.
struct Utf8Locale
{
    locale_t utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", static_cast<locale_t>(0));
    locale_t previous = utf8 ? uselocale(utf8) : static_cast<locale_t>(0);
    Utf8Locale()
    {
        if (!utf8)
            throw Failure(STORM_ARCHIVE_IO, "The C.UTF-8 locale is unavailable");
    }
    ~Utf8Locale()
    {
        uselocale(previous);
        freelocale(utf8);
    }
    Utf8Locale(const Utf8Locale &) = delete;
    Utf8Locale &operator=(const Utf8Locale &) = delete;
};
struct Reader;
// One libarchive data node per volume. Multi-volume RAR and raw byte-split
// sets (.001, .002, ...) are read through libarchive's node switching.
struct Volume
{
    Reader *owner;
    Fd fd;
};
struct Reader
{
    Utf8Locale locale; // first: libarchive is freed under it
    CodecArena arena;
    std::unique_ptr<archive, ArchiveDeleter> owner{nullptr, ArchiveDeleter{&arena}};
    archive *value = nullptr;
    std::vector<std::unique_ptr<Volume>> volumes;
    Context *context;
    char buffer[65536];
    std::exception_ptr callback_error;
    std::string container, previous; // previous: the name of the last entry read, for messages
    uint64_t index = 0;
    bool opened = false; // libarchive reads the state of its format only after a successful open
    LegacyNames names;
    ~Reader()
    {
        try
        {
            names.report(container);
        }
        catch (...)
        {
        }
    }
    bool check(archive *a) noexcept
    {
        try
        {
            context->check();
            auto bytes = archive_filter_bytes(a, 0);
            if (bytes > 0 && static_cast<uint64_t>(bytes) > context->limits.max_total_bytes)
                throw Failure(STORM_ARCHIVE_LIMIT, "Decoded archive stream exceeds limit");
            return true;
        }
        catch (...)
        {
            callback_error = std::current_exception();
            archive_set_error(a, ECANCELED, nullptr);
            return false;
        }
    }
    static la_ssize_t read_callback(archive *a, void *data, const void **out)
    {
        auto &v = *static_cast<Volume *>(data);
        auto &r = *v.owner;
        if (!r.check(a))
            return -1;
        ssize_t n;
        do
        {
            n = ::read(v.fd.value, r.buffer, sizeof(r.buffer));
        } while (n < 0 && errno == EINTR);
        if (n < 0)
            archive_set_error(a, errno, nullptr);
        *out = r.buffer;
        return n;
    }
    static la_int64_t seek_callback(archive *a, void *data, la_int64_t offset, int whence)
    {
        auto &v = *static_cast<Volume *>(data);
        if (!v.owner->check(a))
            return ARCHIVE_FATAL;
        auto pos = lseek(v.fd.value, offset, whence);
        if (pos < 0)
            archive_set_error(a, errno, nullptr);
        return pos;
    }
    static int switch_callback(archive *a, void *, void *next)
    {
        // Descriptors stay open; a node read after a switch starts at its beginning.
        auto &v = *static_cast<Volume *>(next);
        if (lseek(v.fd.value, 0, SEEK_SET) < 0)
        {
            archive_set_error(a, errno, nullptr);
            return ARCHIVE_FATAL;
        }
        return ARCHIVE_OK;
    }
    void checked(int status)
    {
        if (arena.poisoned || allocation_failed())
            throw std::bad_alloc();
        if (status != ARCHIVE_OK)
            fail();
    }
    Reader(const std::vector<std::string> &inputs, Context *ctx) : context(ctx)
    {
        if (inputs.empty())
            throw Failure(STORM_ARCHIVE_IO, "Missing archive input");
        container = printable_name(inputs[0].substr(inputs[0].rfind('/') + 1));
        uint64_t input_bytes = 0;
        for (const auto &input : inputs)
        {
            auto v = std::make_unique<Volume>();
            v->owner = this;
            v->fd.value = open(input.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            struct stat st{};
            if (v->fd.value < 0 || fstat(v->fd.value, &st) < 0)
                throw Failure(STORM_ARCHIVE_IO, "Cannot open archive");
            if (!S_ISREG(st.st_mode))
                throw Failure(STORM_ARCHIVE_INVALID, "Archive volume is not a regular file");
            input_bytes += static_cast<uint64_t>(st.st_size);
            volumes.push_back(std::move(v));
        }
        context->begin_container(input_bytes);
        value = archive_read_new();
        owner.reset(value);
        if (!value)
            throw std::bad_alloc();
        // Every setup call can fail, including callback_data's dataset allocation.
        // Do not call open1 unless initialization completed successfully.
        checked(archive_read_support_filter_none(value));
        checked(archive_read_support_filter_gzip(value));
        checked(archive_read_support_filter_bzip2(value));
        checked(archive_read_support_filter_xz(value));
        checked(archive_read_support_format_zip_seekable(value));
        checked(archive_read_support_format_7zip(value));
        checked(archive_read_support_format_tar(value));
        checked(archive_read_support_format_rar(value));
        checked(archive_read_support_format_rar5(value));
        checked(archive_read_support_format_cab(value));
        checked(archive_read_support_format_iso9660(value));
        if (!context->password.empty())
            checked(archive_read_add_passphrase(value, context->password.c_str()));
        checked(archive_read_set_callback_data(value, volumes[0].get()));
        for (size_t i = 1; i < volumes.size(); ++i)
            checked(archive_read_append_callback_data(value, volumes[i].get()));
        checked(archive_read_set_read_callback(value, read_callback));
        checked(archive_read_set_seek_callback(value, seek_callback));
        if (volumes.size() > 1)
            checked(archive_read_set_switch_callback(value, switch_callback));
        checked(archive_read_open1(value));
        opened = true;
    }
    // A libarchive failure, while reading the header of the next entry (entry null) or the data of entry.
    [[noreturn]] void fail(archive_entry *entry = nullptr)
    {
        if (arena.poisoned || allocation_failed())
            throw std::bad_alloc();
        if (callback_error)
            std::rethrow_exception(callback_error);
        std::string error = archive_error_string(value) ? archive_error_string(value) : "Invalid archive data";
        // Encrypted data fails to decode with a wrong or missing password; encrypted headers fail to read.
        const bool encrypted =
            entry ? archive_entry_is_encrypted(entry) != 0 : opened && archive_read_has_encrypted_entries(value) > 0;
        if (encrypted)
            throw Failure(decryptable() && archive_errno(value) != ENOTSUP ? STORM_ARCHIVE_ENCRYPTED
                                                                           : STORM_ARCHIVE_ENCRYPTION_UNSUPPORTED,
                          error);
        // The data of an entry: the player is told which member is damaged.
        throw Failure(STORM_ARCHIVE_INVALID, entry ? error + ": " + previous : error);
    }
    // Built without a crypto library (build.py), libarchive decrypts ZIP's traditional PKWARE encryption only: its
    // reader of 7z and RAR decrypts nothing, and a ZIP entry it cannot decrypt fails with ENOTSUP
    // (patches/libarchive-3.8.10-undecryptable-zip.patch).
    bool decryptable() const
    {
        return (archive_format(value) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_ZIP;
    }
    bool next(archive_entry **e)
    {
        int rc = archive_read_next_header(value, e);
        if (arena.poisoned || allocation_failed())
            throw std::bad_alloc();
        if (rc == ARCHIVE_EOF)
            return false;
        ++index;
        // A warning is a header libarchive could not read in full: a name that is not valid in the encoding its
        // format declares (a ZIP with the UTF-8 flag, 7z UTF-16). libarchive keeps no form of such a name.
        if (rc == ARCHIVE_WARN && !callback_error)
            throw Failure(STORM_ARCHIVE_INVALID,
                          std::string(archive_error_string(value) ? archive_error_string(value) : "Damaged entry") +
                              ": entry " + std::to_string(index) +
                              (previous.empty() ? std::string() : " (after " + previous + ")"));
        if (rc != ARCHIVE_OK)
            fail();
        // With a password the reader decrypts; a wrong one fails when data is read.
        if (context->password.empty() &&
            (archive_entry_is_encrypted(*e) || archive_read_has_encrypted_entries(value) > 0))
            throw Failure(decryptable() ? STORM_ARCHIVE_ENCRYPTED : STORM_ARCHIVE_ENCRYPTION_UNSUPPORTED,
                          decryptable() ? "Encrypted archives need a password"
                                        : "Encrypted archive this reader cannot open");
        return true;
    }
    std::string path(archive_entry *e)
    {
        // Under C.UTF-8 (Utf8Locale) this is the UTF-8 of a Unicode name or the stored bytes of a legacy one.
        const char *raw = archive_entry_pathname(e);
        if (!raw)
            throw Failure(STORM_ARCHIVE_INVALID, "Missing archive path");
        auto type = archive_entry_filetype(e);
        if (archive_entry_symlink(e) || archive_entry_hardlink(e) || (type != AE_IFREG && type != AE_IFDIR))
            throw Failure(STORM_ARCHIVE_INVALID,
                          "Links and special archive entries are forbidden: " + printable_name(raw));
        previous = names.decode(raw);
        return previous;
    }
};
std::string format(archive *a)
{
    switch (archive_format(a) & ARCHIVE_FORMAT_BASE_MASK)
    {
    case ARCHIVE_FORMAT_ZIP:
        return "zip";
    case ARCHIVE_FORMAT_7ZIP:
        return "7z";
    case ARCHIVE_FORMAT_TAR:
        return "tar";
    case ARCHIVE_FORMAT_RAR:
    case ARCHIVE_FORMAT_RAR_V5:
        return "rar";
    case ARCHIVE_FORMAT_CAB:
        return "cab";
    case ARCHIVE_FORMAT_ISO9660:
        return "iso9660";
    default:
        throw Failure(STORM_ARCHIVE_UNSUPPORTED, "Unsupported archive format");
    }
}
} // namespace
void archive_extract(const std::vector<std::string> &volumes, Context &ctx)
{
    Reader r(volumes, &ctx);
    archive_entry *e = nullptr;
    while (r.next(&e))
    {
        std::string path = ctx.select(r.path(e));
        if (archive_entry_filetype(e) == AE_IFDIR)
            continue;
        auto size = archive_entry_size(e);
        // A member outside the selected root is skipped (decoded only when solid).
        const auto file_limit = path.empty() ? ctx.limits.max_total_bytes : ctx.limits.max_file_bytes;
        if (size < 0 || static_cast<uint64_t>(size) > file_limit)
            throw Failure(STORM_ARCHIVE_LIMIT, "Archive entry size exceeds limit");
        if (path.empty())
        {
            if (archive_read_data_skip(r.value) != ARCHIVE_OK)
                r.fail(e);
            continue;
        }
        Fd output(ctx.create(path));
        uint64_t written = 0;
        char buffer[65536];
        while (true)
        {
            auto n = archive_read_data(r.value, buffer, sizeof(buffer));
            if (n < 0 || allocation_failed())
                r.fail(e);
            if (!n)
                break;
            if (static_cast<uint64_t>(n) > file_limit - written)
                throw Failure(STORM_ARCHIVE_LIMIT, "Expanded file exceeds limit");
            written += n;
            ctx.account(n, path);
            ctx.write(output.value, buffer, n);
        }
        if (archive_entry_size_is_set(e) && written != static_cast<uint64_t>(size))
            r.fail(e);
    }
}
std::string archive_walk(const std::vector<std::string> &volumes, Context &ctx, ArchiveVisitor &visitor)
{
    Reader r(volumes, &ctx);
    archive_entry *e = nullptr;
    char buffer[65536];
    while (r.next(&e))
    {
        auto raw = r.path(e);
        ctx.check(raw);
        if (++ctx.entries > ctx.limits.max_entries)
            throw Failure(STORM_ARCHIVE_LIMIT, "Too many archive entries");
        if (archive_entry_filetype(e) == AE_IFDIR)
            continue;
        auto size = archive_entry_size(e);
        if (size < 0 || !archive_entry_size_is_set(e))
            throw Failure(STORM_ARCHIVE_INVALID, "Archive entry without a size");
        if (!visitor.entry(raw, static_cast<uint64_t>(size)))
            continue;
        uint64_t read = 0;
        while (true)
        {
            auto n = archive_read_data(r.value, buffer, sizeof(buffer));
            if (n < 0 || allocation_failed())
                r.fail(e);
            if (!n)
                break;
            if (static_cast<uint64_t>(n) > static_cast<uint64_t>(size) - read)
                throw Failure(STORM_ARCHIVE_INVALID, "Archive member exceeds its declared size");
            read += n;
            ctx.account(n, raw);
            if (!visitor.chunk(buffer, static_cast<size_t>(n)))
                break;
        }
        visitor.done();
    }
    return format(r.value);
}
bool inno_volume_signature(const char *p, size_t n)
{
    return n >= 8 && (!memcmp(p, "idska16\032", 8) || !memcmp(p, "idska32\032", 8));
}
bool container_signature(const char *p, size_t n)
{
    if (n >= 512)
    {
        // POSIX/v7 tar: v7 has no ustar magic, so verify the header checksum.
        unsigned sum = 0, stored = 0;
        bool valid = true, digits = false;
        for (size_t i = 0; i < 512; ++i)
            sum += (i >= 148 && i < 156) ? 32 : static_cast<unsigned char>(p[i]);
        for (size_t i = 148; i < 156; ++i)
        {
            if (p[i] >= '0' && p[i] <= '7')
            {
                stored = stored * 8 + (p[i] - '0');
                digits = true;
            }
            else if (p[i] != 0 && p[i] != ' ')
                valid = false;
        }
        if (valid && digits && stored && sum == stored)
            return true;
    }
    auto is = [&](size_t at, const char *s, size_t len) { return n >= at + len && !memcmp(p + at, s, len); };
    return is(0, "MZ", 2) || is(0, "PK\003\004", 4) || is(0, "PK\005\006", 4) || is(0, "7z\274\257\047\034", 6) ||
           is(0, "Rar!\032\007", 6) || is(0, "MSCF", 4) || is(0, "\037\213", 2) || is(0, "BZh", 3) ||
           is(0, "\3757zXZ\000", 6) || is(32769, "CD001", 5);
}
} // namespace storm_archive
