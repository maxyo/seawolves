#include "effects/layer_arrays.hpp"

#include "shim/device.hpp"
#include "shim/texture_tier.hpp"
#include "storm/render/log.hpp"
#include "texture/core/cache.hpp"
#include "texture/core/service.hpp"
#include "texture/core/texture_file.hpp"
#include "texture/core/tx_header.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

namespace storm::effects
{
namespace
{
uint32_t g_pending = 0; // game thread

constexpr uint32_t kLayerArrayVersion = 1; // bump when the arrays' texels change
constexpr char kMagic[8] = {'S', 'T', 'L', 'A', 'Y', 'E', 'R', 'A'};

const char *KindName(render::EffectsLayerKind kind)
{
    return kind == render::EffectsLayerKind::Albedo ? "albedo"
           : kind == render::EffectsLayerKind::Rma  ? "rma"
                                                    : "normal";
}

// A file that cannot be a layer, once per path.
void ReportOnce(const std::string &file, const std::string &why)
{
    static std::mutex mutex;
    static std::set<std::string> reported;
    std::lock_guard lock(mutex);
    if (reported.insert(file).second)
        render::Log().error("layers: {} is not drawn: {}", file, why);
}

struct LayerFile
{
    std::vector<uint8_t> bytes;
    texture::LayerSource source;
};

std::optional<LayerFile> ReadLayer(const std::string &path)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
    {
        ReportOnce(path, "it cannot be read");
        return std::nullopt;
    }
    LayerFile layer;
    layer.bytes.assign(std::istreambuf_iterator<char>(in), {});
    const texture::TextureContainer container = texture::DetectTextureContainer(layer.bytes);
    if (container == texture::TextureContainer::Tx)
    {
        texture::TxHeader header;
        if (layer.bytes.size() < texture::kTxHeaderBytes)
        {
            ReportOnce(path, "a damaged .tx file");
            return std::nullopt;
        }
        std::memcpy(&header, layer.bytes.data(), sizeof(header));
        const auto format = texture::TxFormat(header.format);
        if (const char *problem = texture::tx_header_problem(header))
        {
            ReportOnce(path, problem);
            return std::nullopt;
        }
        if (header.cube() || (format != texture::TxFormat::Dxt1 && format != texture::TxFormat::Dxt3 &&
                              format != texture::TxFormat::Dxt5))
        {
            ReportOnce(path, "a layer is a DXT1, DXT3 or DXT5 texture");
            return std::nullopt;
        }
        if (layer.bytes.size() < texture::kTxHeaderBytes + texture::tx_payload_bytes(header))
        {
            ReportOnce(path, "a damaged .tx file");
            return std::nullopt;
        }
        layer.source.layout.format = format == texture::TxFormat::Dxt1   ? texture::DxtFormat::Bc1
                                     : format == texture::TxFormat::Dxt3 ? texture::DxtFormat::Bc2
                                                                         : texture::DxtFormat::Bc3;
        layer.source.layout.width = header.width;
        layer.source.layout.height = header.height;
        layer.source.layout.levels = header.levels;
        layer.source.levels = std::span<const uint8_t>(layer.bytes).subspan(texture::kTxHeaderBytes);
        return layer;
    }
    texture::TextureFile file;
    const auto problem = container == texture::TextureContainer::Dds
                             ? texture::ParseDdsTexture(std::move(layer.bytes), file)
                             : texture::ParseTxxTexture(std::move(layer.bytes), file);
    if (problem || file.layout.faces != 1)
    {
        ReportOnce(path, problem ? problem->reason : "a cube map");
        return std::nullopt;
    }
    layer.source.layout = file.layout;
    layer.bytes = std::move(file.bytes);
    layer.source.levels = std::span<const uint8_t>(layer.bytes).subspan(file.payload_offset);
    return layer;
}
} // namespace

uint32_t LayerArraysPending()
{
    return g_pending;
}

LayerArrayResult MakeLayerArray(const std::array<std::string, 3> &files, render::EffectsLayerKind kind,
                                uint32_t max_side, texture::GpuFormat format)
{
    const auto started = std::chrono::steady_clock::now();
    std::array<std::optional<LayerFile>, 3> read;
    std::array<std::optional<texture::LayerSource>, 3> sources;
    const texture::TexelSpace texels =
        kind == render::EffectsLayerKind::Albedo ? texture::TexelSpace::Colour : texture::TexelSpace::Data;
    const uint32_t shape_key[4] = {kLayerArrayVersion, uint32_t(kind), max_side, uint32_t(format)};
    uint64_t key = texture::hash64(shape_key, sizeof(shape_key), texture::kEncoderVersion);
    LayerArrayResult result;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (!files[i].empty())
            read[i] = ReadLayer(files[i]);
        if (read[i])
            sources[i] = read[i]->source;
        result.present[i] = sources[i].has_value();
        key = result.present[i] ? texture::hash64(read[i]->bytes.data(), read[i]->bytes.size(), key)
                                : texture::hash64("absent", 6, key);
    }
    result.shape = texture::layer_array_shape(sources, max_side);
    if (!result.shape.layers)
        return result;
    const std::filesystem::path &directory = texture::Service::instance().cache_directory();
    texture::DerivedShape shape;
    std::memcpy(shape.magic, kMagic, sizeof(kMagic));
    shape.version = kLayerArrayVersion;
    shape.width = result.shape.width;
    shape.height = result.shape.height;
    shape.levels = result.shape.levels;
    const size_t bytes = texture::layer_array_bytes(result.shape, format);
    if (!directory.empty())
        if (std::optional<texture::Blob> blob = texture::read_derived_file(directory, key, shape, bytes);
            blob && blob->size == bytes)
        {
            result.texels.assign(blob->data.get(), blob->data.get() + blob->size);
            result.cached = true;
        }
    if (!result.cached)
    {
        result.texels = texture::build_layer_array(sources, result.shape, format, texels);
        if (!directory.empty() &&
            texture::write_derived_file(directory, key, shape, result.texels.data(), result.texels.size()) == 0)
            render::Log().warn("layers: the {} array of {} could not be written to the cache", KindName(kind),
                               files[0]);
    }
    render::Log().info(
        "layers: {} array {}x{}x3 ({} MB) of {} | {} | {} {} in {} ms", KindName(kind), result.shape.width,
        result.shape.height, (bytes + (1 << 19)) >> 20, files[0].empty() ? "-" : files[0],
        files[1].empty() ? "-" : files[1], files[2].empty() ? "-" : files[2], result.cached ? "read" : "made",
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    return result;
}
} // namespace storm::effects

namespace storm::render
{

struct EffectsLayerSet::Build
{
    std::atomic<bool> done{false};
    effects::LayerArrayResult result;
};

EffectsLayerSet::EffectsLayerSet(const EffectsLayerFiles &files, uint32_t skipped_levels)
    : files_(files), max_side_(std::max(kMaxSide >> std::min(skipped_levels, 4u), 1u))
{
}

EffectsLayerSet::~EffectsLayerSet()
{
    for (const bool pending : pending_)
        effects::g_pending -= pending ? 1 : 0;
    for (const Array &array : arrays_)
        if (bgfx::isValid(array.handle))
            bgfx::destroy(array.handle);
}

EffectsLayerSet::Array EffectsLayerSet::array(EffectsLayerKind kind)
{
    const size_t k = size_t(kind);
    Array &array = arrays_[k];
    if (bgfx::isValid(array.handle))
        return array;
    std::shared_ptr<Build> &build = builds_[k];
    if (!build)
    {
        static bool unsupported_logged = false;
        const std::optional<texture::GpuFormat> format = shim::compressed_2d_format(false);
        if (!format || shim::Context::instance().caps()->limits.maxTextureLayers < 3)
        {
            if (Once(unsupported_logged))
                Log().error("layers: this GPU has no {}: the layered materials draw their base only",
                            format ? "texture arrays" : "BC or ETC2 textures");
            build = std::make_shared<Build>(); // never done
            return array;
        }
        build = std::make_shared<Build>();
        pending_[k] = true;
        ++effects::g_pending;
        texture::Service::instance().post([build, files = files_[k], kind, side = max_side_, format = *format] {
            build->result = effects::MakeLayerArray(files, kind, side, format);
            build->done.store(true, std::memory_order_release);
        });
        return array;
    }
    if (!pending_[k] || !build->done.load(std::memory_order_acquire))
        return array; // made (an array without any layer file has no texture), or still being made
    pending_[k] = false;
    --effects::g_pending;
    effects::LayerArrayResult &made = build->result;
    if (made.shape.layers)
    {
        const bgfx::TextureFormat::Enum format = *shim::compressed_2d_format(false) == texture::GpuFormat::Bc1
                                                     ? bgfx::TextureFormat::BC1
                                                     : bgfx::TextureFormat::ETC2;
        array.handle = bgfx::createTexture2D(uint16_t(made.shape.width), uint16_t(made.shape.height), true,
                                             uint16_t(made.shape.layers), format, BGFX_TEXTURE_NONE,
                                             bgfx::copy(made.texels.data(), uint32_t(made.texels.size())));
        if (!bgfx::isValid(array.handle))
            Log().error("layers: the {}x{} {} array could not be made: drawn from the base", made.shape.width,
                        made.shape.height, files_[k][0]);
        else
            array.present = made.present;
    }
    build->result = {};
    return array;
}

std::shared_ptr<EffectsLayerSet> MakeEffectsLayerSet(const EffectsLayerFiles &files, uint32_t skipped_levels)
{
    // Models with the same layers share their arrays (a town's terrain pieces).
    static std::map<std::pair<EffectsLayerFiles, uint32_t>, std::weak_ptr<EffectsLayerSet>> made;
    std::erase_if(made, [](const auto &entry) { return entry.second.expired(); });
    std::weak_ptr<EffectsLayerSet> &entry = made[{files, skipped_levels}];
    std::shared_ptr<EffectsLayerSet> set = entry.lock();
    if (!set)
        entry = set = std::make_shared<EffectsLayerSet>(files, skipped_levels);
    return set;
}

} // namespace storm::render
