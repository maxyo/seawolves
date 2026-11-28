#include "foliage_files.hpp"
#include "selection.hpp"

#include <zip_archive.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace storm::foliage
{

const std::array<std::string_view, kAttributeChannels> kChannelNames = {
    "uv0",
    "st_lightmap",
    "st_anchor_xy",
    "st_anchor_z_ao",
    "st_lod_offset_xy",
    "st_lod_offset_z_ripple",
    "st_wind1_origin_xy",
    "st_wind1_origin_z_weight",
    "st_wind1_tip_xy",
    "st_wind1_tip_z_dirx",
    "st_wind1_dir_yz",
    "st_wind2_origin_xy",
    "st_wind2_origin_z_weight",
    "st_wind2_tip_xy",
    "st_wind2_tip_z_dirx",
    "st_wind2_dir_yz",
    "st_geometry",
    "st_blend_twosided",
};

namespace
{
using nlohmann::json;

// Typed access to a JSON object: the first missing or mistyped key is kept as the error.
class Fields
{
  public:
    Fields(const json &object, std::string where) : object_(object), where_(std::move(where))
    {
    }
    const json *get(const char *key, json::value_t type)
    {
        const auto it = object_.find(key);
        const bool number = type == json::value_t::number_float;
        if (it == object_.end() || !(number ? it->is_number() : it->type() == type))
        {
            fail(std::string(it == object_.end() ? "lacks " : "has a mistyped ") + key);
            return nullptr;
        }
        return &*it;
    }
    float number(const char *key)
    {
        const json *value = get(key, json::value_t::number_float);
        const float result = value ? value->get<float>() : 0.f;
        if (value && !std::isfinite(result))
            fail(std::string("has a non-finite ") + key);
        return result;
    }
    bool boolean(const char *key)
    {
        const json *value = get(key, json::value_t::boolean);
        return value && value->get<bool>();
    }
    std::string text(const char *key)
    {
        const json *value = get(key, json::value_t::string);
        return value ? value->get<std::string>() : std::string();
    }
    void expect(const char *key, std::string_view wanted)
    {
        const std::string value = text(key);
        if (ok() && value != wanted)
            fail(std::string(key) + " is " + value + ", not " + std::string(wanted));
    }
    void fail(const std::string &problem)
    {
        if (error_.empty())
            error_ = where_ + problem;
    }
    bool ok() const
    {
        return error_.empty();
    }
    const std::string &error() const
    {
        return error_;
    }

  private:
    const json &object_;
    std::string where_;
    std::string error_;
};

std::optional<json> ParseObject(std::string_view text, std::string &error)
{
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        error = "is not a JSON object";
        return std::nullopt;
    }
    return document;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return text;
}

// "BFLM0001": vertex count, index count, vertex stride (40); the vertices; u32 indices.
bool ReadGeometry(const std::vector<uint8_t> &bytes, Mesh &mesh, std::string &error)
{
    uint32_t header[3];
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "BFLM0001", 8) != 0)
    {
        error = "is not a BFLM0001 mesh";
        return false;
    }
    std::memcpy(header, bytes.data() + 8, sizeof(header));
    const auto [vertices, indices, stride] = header;
    if (stride != sizeof(MeshVertex))
    {
        error = "has a vertex stride of " + std::to_string(stride);
        return false;
    }
    if (vertices == 0 || indices == 0 || indices % 3 != 0 ||
        bytes.size() != 20 + uint64_t{vertices} * stride + uint64_t{indices} * 4)
    {
        error = "has " + std::to_string(vertices) + " vertices and " + std::to_string(indices) +
                " indices that do not fill its " + std::to_string(bytes.size()) + " bytes";
        return false;
    }
    mesh.vertices.resize(vertices);
    mesh.indices.resize(indices);
    std::memcpy(mesh.vertices.data(), bytes.data() + 20, size_t{vertices} * stride);
    std::memcpy(mesh.indices.data(), bytes.data() + 20 + size_t{vertices} * stride, size_t{indices} * 4);
    for (const MeshVertex &v : mesh.vertices)
    {
        for (const float f : {v.position[0], v.position[1], v.position[2], v.normal[0], v.normal[1], v.normal[2],
                              v.uv[0], v.uv[1], v.occlusion})
        {
            if (!std::isfinite(f))
            {
                error = "has a non-finite vertex";
                return false;
            }
        }
    }
    if (*std::max_element(mesh.indices.begin(), mesh.indices.end()) >= vertices)
    {
        error = "has an index past its " + std::to_string(vertices) + " vertices";
        return false;
    }
    return true;
}

// "BFLA0001": vertex count, floats per vertex (kAttributeChannels * 2); the floats.
bool ReadAttributes(const std::vector<uint8_t> &bytes, Mesh &mesh, std::string &error)
{
    uint32_t header[2];
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "BFLA0001", 8) != 0)
    {
        error = "is not a BFLA0001 attribute list";
        return false;
    }
    std::memcpy(header, bytes.data() + 8, sizeof(header));
    const auto [vertices, floats] = header;
    if (vertices != mesh.vertices.size() || floats != kAttributeChannels * 2 ||
        bytes.size() != 16 + uint64_t{vertices} * floats * 4)
    {
        error = "has " + std::to_string(vertices) + " vertices of " + std::to_string(floats) +
                " floats for a mesh of " + std::to_string(mesh.vertices.size());
        return false;
    }
    mesh.attributes.resize(size_t{vertices} * floats);
    std::memcpy(mesh.attributes.data(), bytes.data() + 16, mesh.attributes.size() * 4);
    return true;
}
} // namespace

std::optional<Settings> ReadSettings(std::string_view text, std::string &error)
{
    const std::optional<json> document = ParseObject(text, error);
    if (!document)
        return std::nullopt;
    Fields fields(*document, "");
    Settings settings;
    settings.enabled = fields.boolean("enabled");
    settings.draw_distance = fields.number("draw_distance");
    settings.impostor_distance = fields.number("impostor_distance");
    settings.lod_scale = fields.number("lod_scale");
    settings.island_draw_distance = fields.number("island_draw_distance");
    settings.island_density = fields.number("island_density");
    if (fields.ok() && (settings.island_density < 0 || settings.island_density > 1))
        fields.fail("has island_density " + std::to_string(settings.island_density) + " outside 0..1");
    if (fields.ok() && (settings.draw_distance <= 0 || settings.impostor_distance < 0 || settings.lod_scale <= 0 ||
                        settings.island_draw_distance <= 0))
        fields.fail("has a distance or lod_scale that is not positive");
    if (!fields.ok())
    {
        error = fields.error();
        return std::nullopt;
    }
    return settings;
}

std::optional<Place> ReadPlace(std::string_view text, std::string &error)
{
    const std::optional<json> document = ParseObject(text, error);
    if (!document)
        return std::nullopt;
    Fields fields(*document, "");
    if (const json *version = fields.get("version", json::value_t::number_unsigned); version && *version != 1)
        fields.fail("has version " + version->dump());
    fields.expect("coordinates", "ENGINE_Y_UP");
    const json *instances = fields.get("instances", json::value_t::array);
    if (!fields.ok())
    {
        error = fields.error();
        return std::nullopt;
    }
    Place place;
    place.instances.reserve(instances->size());
    for (size_t i = 0; i < instances->size(); ++i)
    {
        const json &item = (*instances)[i];
        Fields instance(item, "instance " + std::to_string(i) + " ");
        if (!item.is_object())
        {
            error = "instance " + std::to_string(i) + " is not an object";
            return std::nullopt;
        }
        const std::string id = instance.text("id");
        const std::string resource = Lower(instance.text("resource"));
        const json *transform = instance.get("transform", json::value_t::array);
        if (instance.ok() && id.empty())
            instance.fail("has an empty id");
        if (instance.ok() && (!resource.starts_with("foliage/") || !resource.ends_with(".bmfoliage")))
            instance.fail("names " + resource + ", not foliage/<name>.bmfoliage");
        float m[16] = {};
        if (instance.ok() && transform->size() != 16)
            instance.fail("has a transform of " + std::to_string(transform->size()) + " numbers");
        for (size_t k = 0; instance.ok() && k < 16; ++k)
        {
            const json &value = (*transform)[k];
            m[k] = value.is_number() ? value.get<float>() : NAN;
            if (!std::isfinite(m[k]))
                instance.fail("has a transform with a non-number");
        }
        // Column-major: the fourth row (3, 7, 11, 15) of an affine placement is 0 0 0 1.
        if (instance.ok() && (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1))
            instance.fail("has a transform that is not affine");
        if (!instance.ok())
        {
            error = instance.error();
            return std::nullopt;
        }
        auto asset = std::find(place.assets.begin(), place.assets.end(), resource);
        if (asset == place.assets.end())
            asset = place.assets.insert(place.assets.end(), resource);
        Instance out;
        out.asset = static_cast<uint32_t>(asset - place.assets.begin());
        out.rank = Rank(id);
        std::memcpy(&out.transform[0][0], m, sizeof(m));
        place.instances.push_back(out);
    }
    return place;
}

std::optional<Asset> ReadAsset(const zip::Archive &archive, std::string &error)
{
    const std::optional<std::vector<uint8_t>> manifest_bytes = archive.read("manifest.json", error);
    if (!manifest_bytes)
        return std::nullopt;
    const std::optional<json> manifest = ParseObject(
        std::string_view(reinterpret_cast<const char *>(manifest_bytes->data()), manifest_bytes->size()), error);
    if (!manifest)
    {
        error = "manifest.json " + error;
        return std::nullopt;
    }
    Fields fields(*manifest, "manifest.json ");
    Asset asset;
    if (const json *version = fields.get("Version", json::value_t::number_unsigned); version && *version != 1)
        fields.fail("has Version " + version->dump());
    fields.expect("Kind", "foliage");
    fields.expect("Coordinates", "RH_Y_UP_METERS");
    fields.expect("AttributeCoordinates", "SPEEDTREE_SOURCE_Z_UP");
    if (fields.ok() && !fields.boolean("Cooked"))
        fields.fail("is not cooked");
    asset.name = fields.text("Name");
    const json *heights = fields.get("LodScreenHeights", json::value_t::array);
    const json *channels = fields.get("AttributeChannels", json::value_t::array);
    const json *materials = fields.get("Materials", json::value_t::array);
    const json *meshes = fields.get("Meshes", json::value_t::array);
    const json *textures = fields.get("Textures", json::value_t::array);
    if (fields.ok())
    {
        for (const json &h : *heights)
            asset.lod_screen_heights.push_back(h.is_number() ? h.get<float>() : NAN);
        for (size_t i = 0; i < asset.lod_screen_heights.size(); ++i)
        {
            const float h = asset.lod_screen_heights[i];
            if (!(h > 0 && h <= 1) || (i > 0 && !(h < asset.lod_screen_heights[i - 1])))
                fields.fail("has LodScreenHeights that do not decrease within (0, 1]");
        }
        if (asset.lod_screen_heights.empty())
            fields.fail("has no LodScreenHeights");
    }
    if (fields.ok())
    {
        bool same = channels->size() == kAttributeChannels;
        for (size_t i = 0; same && i < kAttributeChannels; ++i)
            same = (*channels)[i].is_string() && (*channels)[i].get<std::string>() == kChannelNames[i];
        if (!same)
            fields.fail("has AttributeChannels other than the 18 this reader decodes");
    }
    for (size_t i = 0; fields.ok() && i < textures->size(); ++i)
    {
        Fields texture((*textures)[i], "texture " + std::to_string(i) + " ");
        const json *bytes = texture.get("Bytes", json::value_t::number_unsigned);
        Texture out{texture.text("Path"), texture.text("Sha256"), bytes ? bytes->get<uint32_t>() : 0};
        const zip::Entry *entry = texture.ok() ? archive.find(out.path) : nullptr;
        if (texture.ok() && (!entry || entry->size != out.bytes))
            texture.fail(out.path + " is not an entry of " + std::to_string(out.bytes) + " bytes");
        if (!texture.ok())
            fields.fail(texture.error());
        asset.textures.push_back(std::move(out));
    }
    const auto has_texture = [&](const std::string &path) {
        return std::any_of(asset.textures.begin(), asset.textures.end(),
                           [&](const Texture &t) { return t.path == path; });
    };
    for (size_t i = 0; fields.ok() && i < materials->size(); ++i)
    {
        Fields material((*materials)[i], "material " + std::to_string(i) + " ");
        Material out;
        out.name = material.text("Name");
        out.two_sided = material.boolean("TwoSided");
        out.billboard = material.boolean("Billboard");
        out.alpha_cutoff = material.number("AlphaCutoff");
        out.color = material.text("Color");
        out.normal_gloss = material.text("NormalGloss");
        out.subsurface_ao = material.text("SubsurfaceAo");
        if (material.ok() && material.boolean("VertexOpacity"))
            material.fail("uses vertex opacity, which this engine does not draw");
        for (const std::string *path : {&out.color, &out.normal_gloss, &out.subsurface_ao})
        {
            if (material.ok() && !has_texture(*path))
                material.fail("names " + *path + ", not one of the Textures");
        }
        if (!material.ok())
            fields.fail(material.error());
        asset.materials.push_back(std::move(out));
    }
    for (size_t i = 0; fields.ok() && i < meshes->size(); ++i)
    {
        Fields mesh((*meshes)[i], "mesh " + std::to_string(i) + " ");
        Mesh out;
        const json *lod = mesh.get("Lod", json::value_t::number_unsigned);
        const json *material = mesh.get("Material", json::value_t::number_unsigned);
        const std::string geometry = mesh.text("Geometry"), attributes = mesh.text("Attributes");
        if (mesh.ok())
        {
            out.lod = lod->get<uint32_t>();
            out.material = material->get<uint32_t>();
            if (out.material >= asset.materials.size())
                mesh.fail("names material " + std::to_string(out.material));
        }
        std::string problem;
        if (mesh.ok())
        {
            const auto bytes = archive.read(geometry, problem);
            if (!bytes || !ReadGeometry(*bytes, out, problem))
                mesh.fail(geometry + " " + problem);
        }
        if (mesh.ok())
        {
            const auto bytes = archive.read(attributes, problem);
            if (!bytes || !ReadAttributes(*bytes, out, problem))
                mesh.fail(attributes + " " + problem);
        }
        if (!mesh.ok())
            fields.fail(mesh.error());
        asset.meshes.push_back(std::move(out));
    }
    // Levels 0..n-1 each with meshes; billboard materials on the last level only, and only there.
    if (fields.ok())
    {
        std::set<uint32_t> levels;
        for (const Mesh &mesh : asset.meshes)
            levels.insert(mesh.lod);
        asset.lods = static_cast<uint32_t>(levels.size());
        if (levels.empty() || *levels.rbegin() + 1 != asset.lods)
            fields.fail("has levels that are not 0.." + std::to_string(asset.lods) + "-1");
        else if (asset.lods < 2)
            fields.fail("has no mesh level before its billboard level");
        for (const Mesh &mesh : asset.meshes)
        {
            if (fields.ok() && asset.materials[mesh.material].billboard != (mesh.lod + 1 == asset.lods))
                fields.fail("has a billboard material off its last level, or another material on it");
        }
    }
    if (!fields.ok())
    {
        error = fields.error();
        return std::nullopt;
    }
    return asset;
}

} // namespace storm::foliage
