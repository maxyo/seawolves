// Resource/INI/aces.json: the camera's tone mapping and colour grade.
#include "json_reader.hpp"

namespace storm::scene_data
{

namespace detail
{
void ReadToneMapping(Reader &reader, const Json &object, const std::string &path, ToneMapping &out)
{
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "type")
            reader.String(value, child, out.type);
        else if (key == "version")
            reader.Int(value, child, out.version);
        else if (key == "exposure_ev")
            reader.Float(value, child, out.exposure_ev);
        else if (key == "contrast")
            reader.Float(value, child, out.contrast);
        else if (key == "saturation")
            reader.Float(value, child, out.saturation);
        else if (key == "temperature")
            reader.Float(value, child, out.temperature);
        else if (key == "tint")
            reader.Float(value, child, out.tint);
        else if (key == "color_filter")
            reader.Color(value, child, out.color_filter);
        else if (key == "lift")
            reader.Color(value, child, out.lift);
        else if (key == "gamma")
            reader.Color(value, child, out.gamma);
        else if (key == "gain")
            reader.Color(value, child, out.gain);
        else
            return false;
        return true;
    });
}
} // namespace detail

ToneMapping ParseToneMapping(std::string_view json, Report &report)
{
    detail::Reader reader(report);
    ToneMapping out;
    detail::Json root;
    if (reader.Parse(json, root))
        detail::ReadToneMapping(reader, root, "$", out);
    return out;
}

} // namespace storm::scene_data
