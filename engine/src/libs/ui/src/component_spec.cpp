#include "component_spec.h"

#include <stdexcept>
#include <unordered_map>

namespace storm::ui
{
namespace
{
std::string Kebab(const std::string &camel)
{
    std::string out;
    for (const char c : camel)
    {
        if (c >= 'A' && c <= 'Z')
        {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        }
        else
            out += c;
    }
    return out;
}
} // namespace

std::unique_ptr<ComponentSpec> ParseComponentSpec(const Json &entry)
{
    static const std::unordered_map<std::string, PropSpec::Type> types = {
        {"text", PropSpec::Type::Text},     {"bool", PropSpec::Type::Bool}, {"int", PropSpec::Type::Int},
        {"number", PropSpec::Type::Number}, {"enum", PropSpec::Type::Enum}, {"json", PropSpec::Type::Json}};
    auto spec = std::make_unique<ComponentSpec>();
    spec->tag = entry.at("tag").get<std::string>();
    spec->body = entry.value("template", std::string());
    spec->constants = entry.value("constants", Json::object());
    // Named locals: a range-for over a temporary's items() would outlive the temporary.
    const Json behaviors = entry.value("behaviors", Json::array());
    const Json classes = entry.value("classes", Json::object());
    const Json props = entry.value("props", Json::object());
    for (const Json &name : behaviors)
        spec->behaviors.push_back(name.get<std::string>());
    for (const auto &rule : classes.items())
        spec->class_rules.emplace_back(rule.key(), rule.value().get<std::string>());
    for (const auto &item : props.items())
    {
        PropSpec prop;
        prop.name = item.key();
        prop.attribute = Kebab(prop.name);
        const std::string type_name = item.value().value("type", std::string("text"));
        const auto type = types.find(type_name);
        if (type == types.end())
            throw std::runtime_error(spec->tag + ": prop " + prop.name + " has unknown type " + type_name);
        prop.type = type->second;
        prop.fallback = item.value().value("default", Json());
        prop.as_class =
            item.value().value("class", prop.type == PropSpec::Type::Bool || prop.type == PropSpec::Type::Enum);
        const Json options = item.value().value("options", Json::array());
        for (const Json &option : options)
            prop.options.push_back(option);
        prop.shape = item.value().value("shape", Json());
        if (prop.type == PropSpec::Type::Json && prop.shape.is_null())
            throw std::runtime_error(spec->tag + ": json prop " + prop.name + " has no shape");
        spec->props.push_back(std::move(prop));
    }
    return spec;
}

bool IsGenericAttribute(const std::string &attribute)
{
    return attribute == "id" || attribute == "class" || attribute == "style" || attribute == "props" ||
           attribute == "slot" || attribute == "rmlui-layout-boundary" || attribute.rfind("data-", 0) == 0;
}

const PropSpec *ComponentSpec::FindAttribute(const std::string &attribute) const
{
    for (const PropSpec &prop : props)
        if (prop.attribute == attribute)
            return &prop;
    return nullptr;
}
} // namespace storm::ui
