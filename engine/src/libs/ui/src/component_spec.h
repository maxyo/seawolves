// The spec of a component: one entry of the UI pack's components.json (its props and their types, template,
// behaviours), as read. Its template is compiled by the runtime (template.h CompileComponent), so the conversion of
// a script's view to props reads the shapes without the UI library.
#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace storm::ui
{
using Json = nlohmann::json;

struct PropSpec
{
    enum class Type
    {
        Text,
        Bool,
        Int,
        Number,
        Enum,
        Json
    };
    std::string name;      // camelCase, as in components.json
    std::string attribute; // kebab-case RML attribute
    Type type = Type::Text;
    Json fallback;
    bool as_class = false;     // bool → "name", enum → "name-value" on the root element
    std::vector<Json> options; // enum values
    Json shape;                // json: the structure a script's view gives it (screens/script_screen.h)
};

struct ComponentSpec
{
    std::string tag;
    std::vector<PropSpec> props;
    std::string body; // template source
    std::vector<std::string> behaviors;
    Json constants; // sizes and other numbers of the component ("spec" in templates)
    // Root classes computed from props: class name -> template expression.
    std::vector<std::pair<std::string, std::string>> class_rules;
    const PropSpec *FindAttribute(const std::string &attribute) const;
};

// One entry of components.json.
std::unique_ptr<ComponentSpec> ParseComponentSpec(const Json &entry);
// Attributes every component accepts besides its props: id, class, style, props, slot, data-*.
bool IsGenericAttribute(const std::string &attribute);
} // namespace storm::ui
