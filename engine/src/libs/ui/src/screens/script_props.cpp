#include "script_props.h"

#include "../numbers.h"
#include "storm_ui/interface_model.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace storm::ui::screens
{
namespace
{
// The unified row image {kind:"item", texture:"atlas:GROUP/Picture"} for a picture the
// engine names by its pictures.ini group, {kind:"portrait", texture:"INTERFACES/PORTRAITS/
// 128/face_N"} for a game portrait, {kind:"item", texture:"<name under RESOURCE/Textures>"} for any
// other game texture; null when the widget carries no such reference.
Json PictureOf(const ImageRef *image)
{
    if (image && image->kind == "portrait" && !image->image.empty())
        return Json{{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_" + image->image}};
    if (image && image->kind == "texture" && !image->image.empty())
        return Json{{"kind", "item"}, {"texture", image->image}};
    if (!image || image->kind != "atlas" || image->group.empty() || image->image.empty())
        return Json();
    return Json{{"kind", "item"}, {"texture", "atlas:" + image->group + "/" + image->image}};
}

// {texture, uv} for <ui-game-image> (c-map-view, backdrops): a texture file of the game with
// the rectangle shown ("interfaces/maps/kayman", "0,0,1,1") or an atlas picture ("atlas:GROUP/
// Picture", ""); null for any other picture.
Json TextureOf(const ImageRef *image)
{
    if (image && image->kind == "texture" && !image->image.empty())
    {
        char uv[96];
        std::snprintf(uv, sizeof uv, "%g,%g,%g,%g", image->uv[0], image->uv[1], image->uv[2], image->uv[3]);
        return Json{{"texture", image->image}, {"uv", uv}};
    }
    if (image && image->kind == "atlas" && !image->group.empty() && !image->image.empty())
        return Json{{"texture", "atlas:" + image->group + "/" + image->image}, {"uv", ""}};
    return Json();
}

std::runtime_error Mismatch(const std::string &path, const std::string &what)
{
    return std::runtime_error("view " + path + ": " + what);
}

Json Scalar(const ViewTree &node, const std::string &type, const std::string &path)
{
    if (!node.children.empty())
        throw Mismatch(path, "has children, the spec says " + type);
    if (type == "text")
        return node.value;
    if (type == "bool")
    {
        if (node.value != "0" && node.value != "1")
            throw Mismatch(path, "'" + node.value + "' is not a bool (0 or 1)");
        return node.value == "1";
    }
    if (type == "int")
        if (const std::optional<int> value = ParseNumber<int>(node.value))
            return *value;
    if (type == "number")
        if (const std::optional<double> value = ParseNumber<double>(node.value))
            return *value;
    if (type == "numbers")
    {
        // "2048,2048": a list of numbers in one text, as the engine's context of a widget gives them.
        Json list = Json::array();
        for (size_t at = 0; at <= node.value.size() && !node.value.empty();)
        {
            const size_t end = std::min(node.value.find(',', at), node.value.size());
            const std::optional<double> value = ParseNumber<double>(node.value.substr(at, end - at));
            if (!value)
                throw Mismatch(path, "'" + node.value + "' is not a list of numbers");
            list.push_back(*value);
            at = end + 1;
        }
        return list;
    }
    if (type == "int" || type == "number")
        throw Mismatch(path, "'" + node.value + "' is not a number");
    throw Mismatch(path, "the spec's shape names an unknown type '" + type + "'");
}

ImageRef Image(const ViewTree &node, const std::string &path);

// A picture that is our enamel token {kind "token", icon, enamel} instead of a game image (a perk with an icon of
// ours); null for any other kind.
Json Token(const ViewTree &node, const std::string &path)
{
    const auto kind = std::find_if(node.children.begin(), node.children.end(),
                                   [](const ViewTree &child) { return SameNode(child.name, "kind"); });
    if (kind == node.children.end() || kind->value != "token")
        return Json();
    Json token{{"kind", "token"}, {"icon", ""}, {"enamel", ""}};
    for (const ViewTree &child : node.children)
        if (SameNode(child.name, "icon") || SameNode(child.name, "enamel"))
            token[SameNode(child.name, "icon") ? "icon" : "enamel"] = child.value;
        else if (!SameNode(child.name, "kind"))
            throw Mismatch(path + "/" + child.name, "is not kind, icon or enamel of a token");
    return token;
}

// A shape object with one of these keys alone says how to read a node, not a field of it.
const Json *Meta(const Json &shape, const char *key)
{
    return shape.is_object() && shape.size() == 1 && shape.contains(key) ? &shape.at(key) : nullptr;
}

// `shape`: a type name, an object of fields, a one-element array: a list of items of that shape; {"$null": shape}:
// that shape, or null where the view gives the node no value and no children; {"$either": [scalar, structure]}:
// the scalar where the node has no children, else the structure (a table's cells: plain text or {text, bold});
// "picture": a game image {kind, group, image, uv} as the pack draws it in a row (PictureOf: null for none or a kind
// it does not draw), or our token {kind "token", icon, enamel}; "texture": the same image as a backdrop or a map draws
// it (TextureOf: {texture, uv}, or null);
// {"$texture": fields}: fields beside an `image` drawn as a texture: those fields with its texture and uv;
// "numbers": "1,2,3" as a list of numbers.
Json Convert(const ViewTree &node, const Json &shape, const std::string &path)
{
    if (shape == "picture" || shape == "texture")
    {
        if (!node.value.empty())
            throw Mismatch(path, "a picture has no value, only kind, group, image and uv");
        if (node.children.empty())
            return Json();
        if (shape == "picture")
            if (Json token = Token(node, path); !token.is_null())
                return token;
        const ImageRef image = Image(node, path);
        return shape == "picture" ? PictureOf(&image) : TextureOf(&image);
    }
    if (const Json *fields = Meta(shape, "$texture"))
    {
        // Fields beside an `image` drawn as a texture: those fields with its texture and uv.
        ViewTree rest;
        const ViewTree *image = nullptr;
        for (const ViewTree &child : node.children)
            if (SameNode(child.name, "image"))
                image = &child;
            else
                rest.children.push_back(child);
        if (!image)
            throw Mismatch(path, "has no image");
        const ImageRef ref = Image(*image, path + "/image");
        const Json texture = TextureOf(&ref);
        if (texture.is_null())
            throw Mismatch(path + "/image", "'" + ref.kind + "' '" + ref.image + "' is not drawn as a texture");
        Json object = Convert(rest, *fields, path);
        object["texture"] = texture["texture"];
        object["uv"] = texture["uv"];
        return object;
    }
    if (const Json *inner = Meta(shape, "$null"))
        return node.children.empty() && node.value.empty() ? Json() : Convert(node, *inner, path);
    if (const Json *either = Meta(shape, "$either"))
    {
        if (!either->is_array() || either->size() != 2 || !either->at(0).is_string())
            throw Mismatch(path, "the spec's $either is not [a type, a structure]");
        return Convert(node, node.children.empty() ? either->at(0) : either->at(1), path);
    }
    if (shape.is_string())
        return Scalar(node, shape.get<std::string>(), path);
    if (shape.is_array() && shape.size() == 1)
    {
        Json list = Json::array();
        for (size_t i = 0; i < node.children.size(); ++i)
            list.push_back(Convert(node.children[i], shape[0], path + "/" + std::to_string(i)));
        return list;
    }
    if (!shape.is_object())
        throw Mismatch(path, "the spec's shape is neither a type, a field list nor a one-item list");
    Json object = Json::object();
    for (const ViewTree &child : node.children)
    {
        const Json *field_shape = nullptr;
        std::string field;
        for (const auto &item : shape.items())
            if (SameNode(item.key(), child.name))
            {
                field = item.key();
                field_shape = &item.value();
            }
        if (!field_shape)
            throw Mismatch(path + "/" + child.name, "is not a field of the spec's shape");
        object[field] = Convert(child, *field_shape, path + "/" + field);
    }
    return object;
}

ImageRef Image(const ViewTree &node, const std::string &path)
{
    ImageRef image;
    for (const ViewTree &child : node.children)
    {
        std::string *field = SameNode(child.name, "kind")    ? &image.kind
                             : SameNode(child.name, "group") ? &image.group
                             : SameNode(child.name, "image") ? &image.image
                                                             : nullptr;
        if (!child.children.empty() || (!field && !SameNode(child.name, "uv")))
            throw Mismatch(path + "/" + child.name, "is not kind, group, image or uv of a picture");
        if (field)
            *field = child.value;
        else if (std::sscanf(child.value.c_str(), "%f,%f,%f,%f", &image.uv[0], &image.uv[1], &image.uv[2],
                             &image.uv[3]) != 4)
            throw Mismatch(path + "/uv", "'" + child.value + "' is not left,top,right,bottom");
    }
    return image;
}

} // namespace

Json ScriptProps(const ViewTree &view, const ComponentSpec &spec)
{
    Json props = Json::object();
    for (const ViewTree &child : view.children)
    {
        const auto prop = std::find_if(spec.props.begin(), spec.props.end(),
                                       [&](const PropSpec &candidate) { return SameNode(candidate.name, child.name); });
        if (prop == spec.props.end())
            throw Mismatch(child.name, "is not a prop of " + spec.tag);
        Json value;
        switch (prop->type)
        {
        case PropSpec::Type::Text:
        case PropSpec::Type::Enum:
            value = Scalar(child, "text", prop->name);
            break;
        case PropSpec::Type::Bool:
            value = Scalar(child, "bool", prop->name);
            break;
        case PropSpec::Type::Int:
            value = Scalar(child, "int", prop->name);
            break;
        case PropSpec::Type::Number:
            value = Scalar(child, "number", prop->name);
            break;
        case PropSpec::Type::Json:
            if (prop->shape.is_null())
                throw Mismatch(prop->name, "the spec of " + spec.tag + " gives this json prop no shape");
            value = Convert(child, prop->shape, prop->name);
            break;
        }
        props[prop->name] = std::move(value);
    }
    return props;
}

ViewTree StateTree(const std::string &name, const Json &json)
{
    ViewTree tree{name, {}, {}};
    if (json.is_object())
        for (const auto &[key, value] : json.items())
            tree.children.push_back(StateTree(key, value));
    else if (json.is_array())
        for (size_t n = 0; n < json.size(); n++)
            tree.children.push_back(StateTree(std::to_string(n), json[n]));
    else if (json.is_string())
        tree.value = json.get<std::string>();
    else if (json.is_boolean())
        tree.value = json.get<bool>() ? "1" : "0";
    else if (!json.is_null())
        tree.value = json.dump();
    return tree;
}

} // namespace storm::ui::screens
