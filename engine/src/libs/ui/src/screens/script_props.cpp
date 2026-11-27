#include "script_props.h"

#include "../numbers.h"
#include "storm_ui/interface_model.h"

#include <algorithm>
#include <stdexcept>

namespace storm::ui::screens
{
namespace
{
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
    if (type == "int" || type == "number")
        throw Mismatch(path, "'" + node.value + "' is not a number");
    throw Mismatch(path, "the spec's shape names an unknown type '" + type + "'");
}

// `shape`: a type name, an object of fields, or a one-element array: a list of items of that shape.
Json Convert(const ViewTree &node, const Json &shape, const std::string &path)
{
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

} // namespace storm::ui::screens
