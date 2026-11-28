// The spec of a component: one entry of the UI pack's components.json (its props and their types, template,
// behaviours).
#pragma once

#include "template.h"

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
    // Compiled by CompileComponentSpec: the names its template and class rules read (the props first, in order,
    // then spec, children, dp, safe), the template and the class rules.
    TemplateNames names;
    CompiledTemplate compiled;
    std::vector<std::pair<std::string, Expression>> classes;
    const PropSpec *FindAttribute(const std::string &attribute) const;
};

// The host's facts every template reads besides the props (TemplateNames: right after the props).
enum class SpecName
{
    Spec,
    Children,
    Dp,
    Safe,
    Count
};

// One entry of components.json.
std::unique_ptr<ComponentSpec> ParseComponentSpec(const Json &entry);
// Compiles the template and class rules of a parsed spec; `component` finds the spec of a nested tag. Throws
// std::runtime_error naming the component and the line.
void CompileComponentSpec(ComponentSpec &spec,
                          const std::function<const ComponentSpec *(const std::string &tag)> &component);
// Attributes every component accepts besides its props: id, class, style, props, slot, data-*.
bool IsGenericAttribute(const std::string &attribute);
} // namespace storm::ui
