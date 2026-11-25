#include "cache.hpp"

#include "mips.hpp"
#include "storm/render/log.hpp"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/readerwriter.h>
#include <xxhash.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace storm::texture
{
namespace fs = std::filesystem;
namespace
{
constexpr char kMagic[8] = {'S', 'T', 'X', 'C', 'E', 'T', 'C', '2'};

bimg::TextureFormat::Enum bimg_format(GpuFormat format)
{
    switch (format)
    {
    case GpuFormat::Etc2Rgb8:
        return bimg::TextureFormat::ETC2;
    case GpuFormat::Etc2Rgba8:
        return bimg::TextureFormat::ETC2A;
    case GpuFormat::Bc1:
        return bimg::TextureFormat::BC1;
    case GpuFormat::Bc2:
        return bimg::TextureFormat::BC2;
    case GpuFormat::Bc3:
        return bimg::TextureFormat::BC3;
    }
    return bimg::TextureFormat::Unknown;
}

// The KTX 1.1 container, written by bimg: `levels` holds the encoded data mip level-major (all faces of a level, then
// the next level), as KTX stores it. With a sizer (and no data) it only counts the container's bytes.
size_t write_ktx(bx::WriterI *writer, GpuFormat format, const Layout &layout, const uint8_t *levels)
{
    bx::Error error;
    const int32_t size = bimg::imageWriteKtx(writer, bimg_format(format), layout.faces == 6, layout.width,
                                             layout.height, 0, uint8_t(layout.levels), 1, false, levels, &error);
    if (!error.isOk())
    {
        render::Fatal("bimg KTX write failed: {}",
                      std::string_view(error.getMessage().getCPtr(), error.getMessage().getLength()));
    }
    return size_t(size);
}
} // namespace

uint64_t hash64(const void *data, size_t size, uint64_t seed)
{
    return XXH64(data, size, seed);
}

KeyBuilder::KeyBuilder(const Layout &layout, bool bc)
{
    const uint32_t shape[7] = {kEncoderVersion, uint32_t(layout.format), layout.width,
                               layout.height,   layout.levels,           layout.faces,
                               bc ? 1u : 0u};
    state_ = hash64(shape, sizeof(shape), 0x53544f524d544558ull);
}

void KeyBuilder::add(const uint8_t *data, size_t size)
{
    state_ = hash64(data, size, state_);
}

Layout output_layout(const Layout &layout)
{
    Layout out = layout;
    out.levels = full_mip_levels(layout.width, layout.height);
    return out;
}

size_t ktx_size(GpuFormat format, const Layout &out)
{
    bx::SizerWriter sizer;
    return write_ktx(&sizer, format, out, nullptr);
}

Blob transcode_to_ktx(const Layout &layout, const uint8_t *payload, GpuFormat format)
{
    const Layout out = output_layout(layout);
    // level-major, as KTX: each level's offset, then its faces
    std::vector<size_t> level_offset(out.levels);
    size_t total = 0;
    for (uint32_t level = 0; level < out.levels; ++level)
    {
        level_offset[level] = total;
        total += gpu_level_size(format, out.level_width(level), out.level_height(level)) * out.faces;
    }
    std::vector<uint8_t> encoded(total);
    const size_t face_payload = dxt_payload_size(layout) / layout.faces;
    const uint32_t aw0 = blocks_x(layout.width) * 4, ah0 = blocks_x(layout.height) * 4;
    std::vector<uint8_t> blocks(size_t(aw0) * ah0 * 4), top(size_t(layout.width) * layout.height * 4), aligned;
    const bool bc = !is_etc2(format);
    for (uint32_t face = 0; face < layout.faces; ++face)
    {
        const uint8_t *source = payload + face * face_payload;
        // The top level, cropped to its texels (the decoder writes whole 4x4 blocks).
        decode_level(layout.format, source, layout.width, layout.height, blocks.data());
        for (uint32_t y = 0; y < layout.height; ++y)
            memcpy(top.data() + size_t(y) * layout.width * 4, blocks.data() + size_t(y) * aw0 * 4,
                   size_t(layout.width) * 4);
        const size_t texels = size_t(layout.width) * layout.height;
        // A punch-through BC1 is a cut-out by its nature; other alpha by its values.
        const bool punch = layout.format == DxtFormat::Bc1 &&
                           bc1_has_transparency(source, dxt_level_size(layout.format, layout.width, layout.height));
        const bool alpha = format != GpuFormat::Etc2Rgb8 && (format != GpuFormat::Bc1 || punch);
        const bool cutout = alpha && (punch || is_cutout_alpha(top.data(), texels));
        const std::vector<std::vector<uint8_t>> chain =
            build_mip_chain(top.data(), layout.width, layout.height, out.levels, cutout);
        for (uint32_t level = 0; level < out.levels; ++level)
        {
            const uint32_t w = out.level_width(level), h = out.level_height(level);
            const size_t face_bytes = gpu_level_size(format, w, h);
            uint8_t *destination = encoded.data() + level_offset[level] + face * face_bytes;
            if (level == 0 && bc)
            {
                memcpy(destination, source, face_bytes); // the game's own top level
                continue;
            }
            if (level == 0)
            {
                encode_gpu(format, blocks.data(), aw0, ah0, destination);
                continue;
            }
            // Edge texels repeated up to whole blocks.
            const uint8_t *rgba = chain[level - 1].data();
            const uint32_t aw = blocks_x(w) * 4, ah = blocks_x(h) * 4;
            aligned.resize(size_t(aw) * ah * 4);
            for (uint32_t y = 0; y < ah; ++y)
                for (uint32_t x = 0; x < aw; ++x)
                    memcpy(aligned.data() + (size_t(y) * aw + x) * 4,
                           rgba + (size_t(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4, 4);
            // BC1 makes a texel transparent below 128: the game tests its alpha against kCutoutReference.
            if (format == GpuFormat::Bc1 && punch)
                for (size_t i = 3; i < aligned.size(); i += 4)
                    aligned[i] = aligned[i] >= kCutoutReference ? 255 : 0;
            encode_gpu(format, aligned.data(), aw, ah, destination);
        }
    }
    Blob blob;
    blob.size = ktx_size(format, out);
    blob.data = std::make_unique<uint8_t[]>(blob.size);
    bx::StaticMemoryBlockWriter writer(blob.data.get(), uint32_t(blob.size));
    write_ktx(&writer, format, out, encoded.data());
    return blob;
}

Blob encode_rgba_to_ktx(const uint8_t *rgba, uint32_t width, uint32_t height, GpuFormat format)
{
    Layout out;
    out.width = width;
    out.height = height;
    out.levels = full_mip_levels(width, height);
    const std::vector<std::vector<uint8_t>> chain = build_mip_chain(rgba, width, height, out.levels, false);
    size_t total = 0;
    for (uint32_t level = 0; level < out.levels; ++level)
        total += gpu_level_size(format, out.level_width(level), out.level_height(level));
    std::vector<uint8_t> encoded(total), aligned;
    size_t offset = 0;
    for (uint32_t level = 0; level < out.levels; ++level)
    {
        const uint32_t w = out.level_width(level), h = out.level_height(level);
        const uint8_t *source = level == 0 ? rgba : chain[level - 1].data();
        // Edge texels repeated up to whole blocks.
        const uint32_t aw = blocks_x(w) * 4, ah = blocks_x(h) * 4;
        aligned.resize(size_t(aw) * ah * 4);
        for (uint32_t y = 0; y < ah; ++y)
            for (uint32_t x = 0; x < aw; ++x)
                memcpy(aligned.data() + (size_t(y) * aw + x) * 4,
                       source + (size_t(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4, 4);
        encode_gpu(format, aligned.data(), aw, ah, encoded.data() + offset);
        offset += gpu_level_size(format, w, h);
    }
    Blob blob;
    blob.size = ktx_size(format, out);
    blob.data = std::make_unique<uint8_t[]>(blob.size);
    bx::StaticMemoryBlockWriter writer(blob.data.get(), uint32_t(blob.size));
    write_ktx(&writer, format, out, encoded.data());
    return blob;
}

fs::path cache_file_path(const fs::path &directory, uint64_t key)
{
    char name[32];
    snprintf(name, sizeof(name), "%016llx%s", static_cast<unsigned long long>(key), kCacheExtension);
    return directory / name;
}

FileHeader make_header(uint64_t key, const Layout &layout, GpuFormat format, const Blob &ktx)
{
    FileHeader header{};
    memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kEncoderVersion;
    header.gpu_format = uint32_t(format);
    header.width = layout.width;
    header.height = layout.height;
    header.levels = layout.levels;
    header.faces = layout.faces;
    header.key = key;
    header.payload_size = ktx.size;
    header.checksum = hash64(ktx.data.get(), ktx.size, key);
    return header;
}

bool header_valid(const FileHeader &header, uint64_t key, const Layout &layout)
{
    if (memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kEncoderVersion || header.key != key ||
        header.width != layout.width || header.height != layout.height || header.levels != layout.levels ||
        header.faces != layout.faces)
        return false;
    if (header.gpu_format < uint32_t(GpuFormat::Etc2Rgb8) || header.gpu_format > uint32_t(GpuFormat::Bc3))
        return false;
    return header.payload_size == ktx_size(GpuFormat(header.gpu_format), layout);
}

namespace
{
// Header + payload, atomically (temporary file + rename). Returns bytes written or 0.
uint64_t write_file(const fs::path &directory, uint64_t key, const FileHeader &header, const uint8_t *data, size_t size)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    const fs::path target = cache_file_path(directory, key);
    fs::path temp = target;
    temp += "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffff) + ".tmp";
    FILE *file = fopen(temp.c_str(), "wb");
    if (!file)
        return 0;
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (ok)
        fs::rename(temp, target, ec);
    if (!ok || ec)
    {
        fs::remove(temp, ec);
        return 0;
    }
    return sizeof(header) + size;
}

// The payload of the file for `key` when `valid(header)` accepts its header and the checksum matches; a mismatching
// or damaged file is deleted, a good one gets a fresh LRU stamp.
template <typename Valid> std::optional<Blob> read_file(const fs::path &directory, uint64_t key, Valid &&valid)
{
    const fs::path path = cache_file_path(directory, key);
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return std::nullopt;
    FileHeader header{};
    Blob blob;
    bool ok = fread(&header, sizeof(header), 1, file) == 1 && valid(header);
    if (ok)
    {
        blob.size = header.payload_size;
        blob.data = std::make_unique<uint8_t[]>(blob.size);
        ok = fread(blob.data.get(), 1, blob.size, file) == blob.size && fgetc(file) == EOF &&
             hash64(blob.data.get(), blob.size, key) == header.checksum;
    }
    fclose(file);
    std::error_code ec;
    if (!ok)
    {
        fs::remove(path, ec);
        return std::nullopt;
    }
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec); // LRU stamp
    return blob;
}
} // namespace

uint64_t write_cache_file(const fs::path &directory, uint64_t key, const Layout &layout, GpuFormat format,
                          const Blob &ktx)
{
    return write_file(directory, key, make_header(key, layout, format, ktx), ktx.data.get(), ktx.size);
}

std::optional<Blob> read_cache_file(const fs::path &directory, uint64_t key, const Layout &layout)
{
    return read_file(directory, key, [&](const FileHeader &header) { return header_valid(header, key, layout); });
}

uint64_t write_derived_file(const fs::path &directory, uint64_t key, const DerivedShape &shape, const uint8_t *data,
                            size_t size)
{
    FileHeader header{};
    memcpy(header.magic, shape.magic, sizeof(header.magic));
    header.version = shape.version;
    header.width = shape.width;
    header.height = shape.height;
    header.levels = shape.levels;
    header.faces = 1;
    header.key = key;
    header.payload_size = size;
    header.checksum = hash64(data, size, key);
    return write_file(directory, key, header, data, size);
}

std::optional<Blob> read_derived_file(const fs::path &directory, uint64_t key, const DerivedShape &shape,
                                      size_t max_size)
{
    return read_file(directory, key, [&](const FileHeader &header) {
        return memcmp(header.magic, shape.magic, sizeof(header.magic)) == 0 && header.version == shape.version &&
               header.key == key && header.width == shape.width && header.height == shape.height &&
               header.levels == shape.levels && header.faces == 1 && header.payload_size <= max_size;
    });
}

uint64_t scan_cache(const fs::path &directory)
{
    uint64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        const fs::path &path = it->path();
        if (path.extension() == ".tmp")
            fs::remove(path, ec);
        else if (path.extension() == kCacheExtension)
            total += it->file_size(ec);
        ec.clear();
    }
    return total;
}

uint64_t evict_cache(const fs::path &directory, uint64_t target_bytes, std::string_view extension)
{
    struct Entry
    {
        fs::file_time_type time;
        uint64_t size;
        fs::path path;
    };
    std::vector<Entry> entries;
    uint64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->path().extension() == ".tmp")
        {
            fs::remove(it->path(), ec); // left by a write that was cut off
            ec.clear();
            continue;
        }
        if (it->path().extension() != extension)
            continue;
        Entry entry{it->last_write_time(ec), it->file_size(ec), it->path()};
        if (ec)
        {
            ec.clear();
            continue;
        }
        total += entry.size;
        entries.push_back(std::move(entry));
    }
    if (total <= target_bytes)
        return total;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.time < b.time; });
    for (const Entry &entry : entries)
    {
        if (total <= target_bytes)
            break;
        if (fs::remove(entry.path, ec))
            total -= entry.size;
        ec.clear();
    }
    return total;
}

} // namespace storm::texture
