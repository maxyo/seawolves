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

Json Scalar(const ViewNode &node, const std::string &type, const std::string &path)
{
    if (node.Size() != 0)
        throw Mismatch(path, "has children, the spec says " + type);
    const std::string_view value = node.Value();
    if (type == "text")
        return value;
    if (type == "bool")
    {
        if (value != "0" && value != "1")
            throw Mismatch(path, "'" + std::string(value) + "' is not a bool (0 or 1)");
        return value == "1";
    }
    if (type == "int")
        if (const std::optional<int> number = ParseNumber<int>(value))
            return *number;
    if (type == "number")
        if (const std::optional<double> number = ParseNumber<double>(value))
            return *number;
    if (type == "numbers")
    {
        // "2048,2048": a list of numbers in one text, as the engine's context of a widget gives them.
        Json list = Json::array();
        for (size_t at = 0; at <= value.size() && !value.empty();)
        {
            const size_t end = std::min(value.find(',', at), value.size());
            const std::optional<double> number = ParseNumber<double>(value.substr(at, end - at));
            if (!number)
                throw Mismatch(path, "'" + std::string(value) + "' is not a list of numbers");
            list.push_back(*number);
            at = end + 1;
        }
        return list;
    }
    if (type == "int" || type == "number")
        throw Mismatch(path, "'" + std::string(value) + "' is not a number");
    throw Mismatch(path, "the spec's shape names an unknown type '" + type + "'");
}

ImageRef Image(const ViewNode &node, const std::string &path);

// A picture that is our enamel token {kind "token", icon, enamel} instead of a game image (a perk with an icon of
// ours); null for any other kind.
Json Token(const ViewNode &node, const std::string &path)
{
    const std::optional<ViewNode> kind = node.Field("kind");
    if (!kind || kind->Value() != "token")
        return Json();
    Json token{{"kind", "token"}, {"icon", ""}, {"enamel", ""}};
    for (size_t i = 0; i < node.Size(); ++i)
    {
        const ViewNode child = node[i];
        if (SameNode(child.Name(), "icon") || SameNode(child.Name(), "enamel"))
            token[SameNode(child.Name(), "icon") ? "icon" : "enamel"] = child.Value();
        else if (!SameNode(child.Name(), "kind"))
            throw Mismatch(path + "/" + std::string(child.Name()), "is not kind, icon or enamel of a token");
    }
    return token;
}

// A shape object with one of these keys alone says how to read a node, not a field of it.
const Json *Meta(const Json &shape, const char *key)
{
    return shape.is_object() && shape.size() == 1 && shape.contains(key) ? &shape.at(key) : nullptr;
}

// The scalar type a reading takes where `shape` is, or "" where the shape is a structure.
std::string ScalarOf(const Json &shape)
{
    if (const Json *inner = Meta(shape, "$null"))
        return ScalarOf(*inner);
    if (const Json *either = Meta(shape, "$either"))
        return either->is_array() && !either->empty() ? ScalarOf(either->at(0)) : "";
    if (shape == "text" || shape == "bool" || shape == "int" || shape == "number")
        return shape.get<std::string>();
    return "";
}

Json Convert(const ViewNode &node, const Json &shape, const std::string &path, const Readings &readings);

// A node where the shape is `shape`: converted, or a HUD reading's value (none: left out).
std::optional<Json> Place(const ViewNode &node, const Json &shape, const std::string &path, const Readings &readings)
{
    if (!readings || !node.Field("reading"))
        return Convert(node, shape, path, readings);
    const std::string type = ScalarOf(shape);
    if (type.empty())
        throw Mismatch(path, "a reading is one value; the spec's shape is a structure here");
    std::optional<Json> value = readings(node, path, type);
    if (!value)
        return std::nullopt;
    return ReadingAs(*value, type, path);
}

// The fields of `node` but `skip` by `shape`'s field list.
Json Object(const ViewNode &node, const Json &shape, const std::string &path, const Readings &readings,
            std::string_view skip = {})
{
    Json object = Json::object();
    for (size_t i = 0; i < node.Size(); ++i)
    {
        const ViewNode child = node[i];
        if (!skip.empty() && SameNode(child.Name(), skip))
            continue;
        const Json *field_shape = nullptr;
        std::string field;
        for (const auto &item : shape.items())
            if (SameNode(item.key(), child.Name()))
            {
                field = item.key();
                field_shape = &item.value();
            }
        if (!field_shape)
            throw Mismatch(path + "/" + std::string(child.Name()), "is not a field of the spec's shape");
        if (std::optional<Json> value = Place(child, *field_shape, path + "/" + field, readings))
            object[field] = std::move(*value);
    }
    return object;
}

// `shape`: a type name, an object of fields, a one-element array: a list of items of that shape; {"$null": shape}:
// that shape, or null where the view gives the node no value and no children; {"$either": [scalar, structure]}:
// the scalar where the node has no children, else the structure (a table's cells: plain text or {text, bold});
// "picture": a game image {kind, group, image, uv} as the pack draws it in a row (PictureOf: null for none or a kind
// it does not draw), or our token {kind "token", icon, enamel}; "texture": the same image as a backdrop or a map draws
// it (TextureOf: {texture, uv}, or null);
// {"$texture": fields}: fields beside an `image` drawn as a texture: those fields with its texture and uv;
// "numbers": "1,2,3" as a list of numbers.
Json Convert(const ViewNode &node, const Json &shape, const std::string &path, const Readings &readings)
{
    if (shape == "picture" || shape == "texture")
    {
        if (!node.Value().empty())
            throw Mismatch(path, "a picture has no value, only kind, group, image and uv");
        if (node.Size() == 0)
            return Json();
        if (shape == "picture")
            if (Json token = Token(node, path); !token.is_null())
                return token;
        const ImageRef image = Image(node, path);
        return shape == "picture" ? PictureOf(&image) : TextureOf(&image);
    }
    if (const Json *fields = Meta(shape, "$texture"))
    {
        const std::optional<ViewNode> image = node.Field("image");
        if (!image)
            throw Mismatch(path, "has no image");
        const ImageRef ref = Image(*image, path + "/image");
        const Json texture = TextureOf(&ref);
        if (texture.is_null())
            throw Mismatch(path + "/image", "'" + ref.kind + "' '" + ref.image + "' is not drawn as a texture");
        if (!fields->is_object())
            throw Mismatch(path, "the spec's $texture is not a field list");
        Json object = Object(node, *fields, path, readings, "image");
        object["texture"] = texture["texture"];
        object["uv"] = texture["uv"];
        return object;
    }
    if (const Json *inner = Meta(shape, "$null"))
        return node.Size() == 0 && node.Value().empty() ? Json() : Convert(node, *inner, path, readings);
    if (const Json *either = Meta(shape, "$either"))
    {
        if (!either->is_array() || either->size() != 2 || !either->at(0).is_string())
            throw Mismatch(path, "the spec's $either is not [a type, a structure]");
        return Convert(node, node.Size() == 0 ? either->at(0) : either->at(1), path, readings);
    }
    if (shape.is_string())
        return Scalar(node, shape.get<std::string>(), path);
    if (shape.is_array() && shape.size() == 1)
    {
        Json list = Json::array();
        for (size_t i = 0; i < node.Size(); ++i)
            if (std::optional<Json> item = Place(node[i], shape[0], path + "/" + std::to_string(i), readings))
                list.push_back(std::move(*item));
        return list;
    }
    if (!shape.is_object())
        throw Mismatch(path, "the spec's shape is neither a type, a field list nor a one-item list");
    return Object(node, shape, path, readings);
}

ImageRef Image(const ViewNode &node, const std::string &path)
{
    ImageRef image;
    for (size_t i = 0; i < node.Size(); ++i)
    {
        const ViewNode child = node[i];
        std::string *field = SameNode(child.Name(), "kind")    ? &image.kind
                             : SameNode(child.Name(), "group") ? &image.group
                             : SameNode(child.Name(), "image") ? &image.image
                                                               : nullptr;
        if (child.Size() != 0 || (!field && !SameNode(child.Name(), "uv")))
            throw Mismatch(path + "/" + std::string(child.Name()), "is not kind, group, image or uv of a picture");
        const std::string value(child.Value());
        if (field)
            *field = value;
        else if (std::sscanf(value.c_str(), "%f,%f,%f,%f", &image.uv[0], &image.uv[1], &image.uv[2], &image.uv[3]) != 4)
            throw Mismatch(path + "/uv", "'" + value + "' is not left,top,right,bottom");
    }
    return image;
}

const ViewNode::Reader kViewTree{
    [](const void *node) { return std::string_view(static_cast<const ViewTree *>(node)->name); },
    [](const void *node) { return std::string_view(static_cast<const ViewTree *>(node)->value); },
    [](const void *node) { return static_cast<const ViewTree *>(node)->children.size(); },
    [](const void *node, size_t index) -> const void * {
        return &static_cast<const ViewTree *>(node)->children[index];
    },
};

} // namespace

Json ScriptProps(const ViewNode &view, const ComponentSpec &spec, const Readings &readings)
{
    Json props = Json::object();
    for (size_t i = 0; i < view.Size(); ++i)
    {
        const ViewNode child = view[i];
        const auto prop = std::find_if(spec.props.begin(), spec.props.end(), [&](const PropSpec &candidate) {
            return SameNode(candidate.name, child.Name());
        });
        if (prop == spec.props.end())
            throw Mismatch(std::string(child.Name()), "is not a prop of " + spec.tag);
        Json shape;
        switch (prop->type)
        {
        case PropSpec::Type::Text:
        case PropSpec::Type::Enum:
            shape = "text";
            break;
        case PropSpec::Type::Bool:
            shape = "bool";
            break;
        case PropSpec::Type::Int:
            shape = "int";
            break;
        case PropSpec::Type::Number:
            shape = "number";
            break;
        case PropSpec::Type::Json:
            break;
        }
        if (std::optional<Json> value = Place(child, shape.is_null() ? prop->shape : shape, prop->name, readings))
            props[prop->name] = std::move(*value);
    }
    return props;
}

Json ReadingAs(const Json &value, const std::string &type, const std::string &at)
{
    if (type == "text" && value.is_string())
        return value;
    if (type == "text" && value.is_number_integer())
        return std::to_string(value.get<int64_t>());
    if ((type == "int" && value.is_number_integer()) || (type == "number" && value.is_number()))
        return value;
    if (type == "int" && value.is_string())
        if (const std::optional<int> number = ParseNumber<int>(value.get_ref<const std::string &>()))
            return *number;
    if (type == "number" && value.is_string())
        if (const std::optional<double> number = ParseNumber<double>(value.get_ref<const std::string &>()))
            return *number;
    throw Mismatch(at, "the reading gives " + value.dump() + ", not a " + type);
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

namespace storm::ui
{
ViewNode::ViewNode(const ViewTree &tree) : ViewNode(screens::kViewTree, &tree)
{
}

std::optional<ViewNode> ViewNode::Field(std::string_view name) const
{
    for (size_t i = 0; i < Size(); ++i)
        if (const ViewNode child = (*this)[i]; SameNode(child.Name(), name))
            return child;
    return std::nullopt;
}
} // namespace storm::ui
