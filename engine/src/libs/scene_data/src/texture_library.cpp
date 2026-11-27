// Resource/texture_library.json: the cloud presets (the other presets' keys are reported as unknown).
#include "json_reader.hpp"

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

// The layer keys of `object`; false for a key that is not one.
bool ReadLayerKey(Reader &reader, const std::string &key, const Json &value, const std::string &path,
                  CloudLayerFiles &layer)
{
    if (key == "albedo_texture" || key == "lighting_texture")
        return reader.String(value, path, key == "albedo_texture" ? layer.albedo_texture : layer.lighting_texture),
               true;
    if (key == "radiance_scale" || key == "ambient_scale")
        return reader.Float(value, path, key == "radiance_scale" ? layer.radiance_scale : layer.ambient_scale), true;
    if (key == "premultiplied_alpha" || key == "has_ambient")
        return reader.Bool(value, path, key == "has_ambient" ? layer.has_ambient : layer.premultiplied_alpha), true;
    return false;
}

void ReadLayer(Reader &reader, const Json &object, const std::string &path, CloudLayerFiles &layer)
{
    reader.Require(object, path, {"albedo_texture", "lighting_texture", "radiance_scale", "premultiplied_alpha"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        return ReadLayerKey(reader, key, value, child, layer);
    });
}
} // namespace

TextureLibrary ParseTextureLibrary(std::string_view json, Report &report)
{
    Reader reader(report);
    TextureLibrary out;
    Json root;
    if (!reader.Parse(json, root))
        return out;
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        if (key != "cloud_preset")
            return false;
        reader.Object(value, path, [&](const std::string &variant, const Json &v, const std::string &p) {
            CloudPreset &preset = out.clouds[variant];
            reader.Require(
                v, p,
                {"albedo_texture", "lighting_texture", "radiance_scale", "premultiplied_alpha", "ambient_strength"});
            reader.Object(v, p, [&](const std::string &k, const Json &x, const std::string &q) {
                if (k == "upper_layer" || k == "rain_layer")
                    return ReadLayer(reader, x, q, k == "upper_layer" ? preset.upper : preset.rain), true;
                if (k == "ambient_strength")
                    return reader.Float(x, q, preset.ambient_strength), true;
                return ReadLayerKey(reader, k, x, q, preset.base);
            });
            return true;
        });
        return true;
    });
    return out;
}

} // namespace storm::scene_data
