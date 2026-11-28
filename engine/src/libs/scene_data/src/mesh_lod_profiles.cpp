// Resource/INI/mesh_lod_profiles.json: when a model's objects named <name>_lod1.._lodN take over from <name>.
#include "json_reader.hpp"

#include <algorithm>

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

void ReadProfile(Reader &reader, const Json &object, const std::string &path, MeshLodProfile &profile)
{
    reader.Require(object, path, {"screen_size"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "hysteresis")
        {
            if (reader.Float(value, child, profile.hysteresis) &&
                !(profile.hysteresis >= 0.0f && profile.hysteresis < 1.0f))
                reader.Error(child, "must be in 0..1");
            return true;
        }
        if (key == "transition_ms")
            return reader.Float(value, child, profile.transition_ms), true;
        if (key != "screen_size")
            return false;
        if (!value.is_array() || value.empty() ||
            !std::ranges::all_of(value, [](const Json &v) { return v.is_number() && v.get<float>() > 0.0f; }))
        {
            reader.Error(child, "expected a list of numbers above 0");
            return true;
        }
        profile.screen_size.clear();
        for (const Json &v : value)
            profile.screen_size.push_back(v.get<float>());
        if (!std::ranges::is_sorted(profile.screen_size, std::greater<>()))
        {
            reader.Error(child, "must fall from the first level to the last");
            profile.screen_size.clear();
        }
        return true;
    });
}
} // namespace

MeshLodProfiles ParseMeshLodProfiles(std::string_view json, Report &report)
{
    Reader reader(report);
    MeshLodProfiles out;
    Json root;
    if (!reader.Parse(json, root))
        return out;
    reader.Require(root, "$", {"profiles"});
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        if (key == "schema_version")
            return reader.Int(value, path, out.schema_version), true;
        if (key != "profiles")
            return false;
        reader.Object(value, path, [&](const std::string &name, const Json &object, const std::string &child) {
            MeshLodProfile profile;
            ReadProfile(reader, object, child, profile);
            if (!profile.screen_size.empty())
                out.profiles[name] = std::move(profile);
            return true;
        });
        return true;
    });
    return out;
}
} // namespace storm::scene_data
