#pragma once

// A zip file read in place: its central directory once, an entry's bytes on demand (stored or deflated, checked against
// the entry's CRC). Multi-disk, zip64 and encrypted archives are refused. Users link zlib. The engine has no zip reader
// of its own besides this one; libarchive lives only in the launcher's archive library.

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::zip
{

struct Entry
{
    uint16_t method = 0; // 0 stored, 8 deflated
    uint32_t crc = 0;
    uint32_t compressed_size = 0, size = 0;
    uint32_t local_header = 0; // offset of the entry's local header
};

class Archive
{
  public:
    // The archive's directory, or nothing with `error` set.
    static std::optional<Archive> Open(const std::filesystem::path &path, std::string &error)
    {
        Archive archive;
        archive.path_ = path;
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "cannot be opened";
            return std::nullopt;
        }
        file.seekg(0, std::ios::end);
        const uint64_t file_size = static_cast<uint64_t>(file.tellg());
        // The end record (22 bytes) is followed by a comment of at most 65535 bytes.
        const uint64_t tail_size = std::min<uint64_t>(file_size, 22 + 65535);
        std::vector<uint8_t> tail(tail_size);
        file.seekg(static_cast<std::streamoff>(file_size - tail_size));
        if (!file.read(reinterpret_cast<char *>(tail.data()), static_cast<std::streamsize>(tail_size)))
        {
            error = "cannot be read";
            return std::nullopt;
        }
        int64_t end = -1;
        for (int64_t i = static_cast<int64_t>(tail_size) - 22; i >= 0; --i)
        {
            if (U32(tail.data() + i) == 0x06054b50u)
            {
                end = i;
                break;
            }
        }
        if (end < 0)
        {
            error = "has no zip end record";
            return std::nullopt;
        }
        const uint8_t *record = tail.data() + end;
        const uint16_t entries = U16(record + 10);
        const uint32_t directory_size = U32(record + 12), directory_offset = U32(record + 16);
        if (U16(record + 4) != 0 || U16(record + 6) != 0 || U16(record + 8) != entries)
        {
            error = "is a multi-disk zip";
            return std::nullopt;
        }
        if (entries == 0xffffu || directory_offset == 0xffffffffu ||
            uint64_t{directory_offset} + directory_size > file_size)
        {
            error = "is a zip64 archive or its directory lies outside the file";
            return std::nullopt;
        }
        std::vector<uint8_t> directory(directory_size);
        file.seekg(directory_offset);
        if (!file.read(reinterpret_cast<char *>(directory.data()), directory_size))
        {
            error = "has an unreadable directory";
            return std::nullopt;
        }
        size_t at = 0;
        for (uint32_t i = 0; i < entries; ++i)
        {
            if (at + 46 > directory.size() || U32(directory.data() + at) != 0x02014b50u)
            {
                error = "has a broken directory at entry " + std::to_string(i);
                return std::nullopt;
            }
            const uint8_t *h = directory.data() + at;
            const size_t name_size = U16(h + 28), next = at + 46 + name_size + U16(h + 30) + U16(h + 32);
            if (next > directory.size())
            {
                error = "has a broken directory at entry " + std::to_string(i);
                return std::nullopt;
            }
            std::string name(reinterpret_cast<const char *>(h + 46), name_size);
            Entry entry{U16(h + 10), U32(h + 16), U32(h + 20), U32(h + 24), U32(h + 42)};
            if (U16(h + 8) & 1u)
            {
                error = "has an encrypted entry " + name;
                return std::nullopt;
            }
            if ((entry.method != 0 && entry.method != 8) || entry.compressed_size == 0xffffffffu ||
                entry.size == 0xffffffffu || entry.local_header == 0xffffffffu)
            {
                error = "has an entry " + name + " in method " + std::to_string(entry.method) + " or zip64";
                return std::nullopt;
            }
            archive.entries_[std::move(name)] = entry;
            at = next;
        }
        return archive;
    }

    const Entry *find(const std::string &name) const
    {
        const auto it = entries_.find(name);
        return it == entries_.end() ? nullptr : &it->second;
    }
    const std::unordered_map<std::string, Entry> &entries() const
    {
        return entries_;
    }

    // The whole entry, or nothing with `error` set (missing, unreadable, not inflating to its size, wrong CRC).
    std::optional<std::vector<uint8_t>> read(const std::string &name, std::string &error) const
    {
        const Entry *entry = find(name);
        if (!entry)
        {
            error = name + " is missing";
            return std::nullopt;
        }
        std::ifstream file(path_, std::ios::binary);
        uint8_t local[30];
        file.seekg(entry->local_header);
        if (!file.read(reinterpret_cast<char *>(local), sizeof(local)) || U32(local) != 0x04034b50u)
        {
            error = name + " has no local header";
            return std::nullopt;
        }
        file.seekg(entry->local_header + 30u + U16(local + 26) + U16(local + 28));
        std::vector<uint8_t> packed(entry->compressed_size);
        if (!file.read(reinterpret_cast<char *>(packed.data()), static_cast<std::streamsize>(packed.size())))
        {
            error = name + " is cut short";
            return std::nullopt;
        }
        std::vector<uint8_t> bytes;
        if (entry->method == 0)
        {
            if (entry->compressed_size != entry->size)
            {
                error = name + " is stored with two sizes";
                return std::nullopt;
            }
            bytes = std::move(packed);
        }
        else
        {
            bytes.resize(entry->size);
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            {
                error = name + ": zlib cannot start";
                return std::nullopt;
            }
            stream.next_in = packed.data();
            stream.avail_in = static_cast<uInt>(packed.size());
            stream.next_out = bytes.data();
            stream.avail_out = static_cast<uInt>(bytes.size());
            const int result = inflate(&stream, Z_FINISH);
            const uLong out = stream.total_out;
            inflateEnd(&stream);
            if (result != Z_STREAM_END || out != entry->size)
            {
                error = name + " does not inflate to its " + std::to_string(entry->size) + " bytes";
                return std::nullopt;
            }
        }
        if (crc32(0, bytes.data(), static_cast<uInt>(bytes.size())) != entry->crc)
        {
            error = name + " fails its CRC";
            return std::nullopt;
        }
        return bytes;
    }

  private:
    static uint16_t U16(const uint8_t *p)
    {
        return static_cast<uint16_t>(p[0] | p[1] << 8);
    }
    static uint32_t U32(const uint8_t *p)
    {
        return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
    }

    std::filesystem::path path_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace storm::zip
