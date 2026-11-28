// Component templates, compiled once when the pack loads (CompileTemplate) into a tree of element nodes with
// expressions attached; components.cpp binds that tree to live elements and runs only the expressions whose
// inputs changed. The syntax is described in app/mobile-ui/design/README.md.
//
// Expressions: literals ('text', 12, true, null, [a, b], {k: v}), paths (a.b[0].c), ! - + * / % comparisons
// && || ?: and calls (see template.cpp for the helper list). Null prints nothing. A path that names nothing (an
// unknown name, a missing key, an index past the end) is an error; a key present with null is defined. A path
// that may be missing is marked: opt(path), or the arguments of or(path, fallback), and is null when missing.
#pragma once

#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace storm::ui
{

using Json = nlohmann::json;
struct ComponentSpec;
struct PropSpec;

// The names one component's templates read, by index: its props first (in the spec's order), then the host's
// facts (spec, children, dp, safe), then any other name a template writes (it names nothing: null).
class TemplateNames
{
  public:
    static constexpr size_t kMax = 256;
    int Find(const std::string &name) const;
    int Add(const std::string &name);
    const std::vector<std::string> &All() const
    {
        return names_;
    }

  private:
    std::vector<std::string> names_;
};

// What a part of a template reads: names of the component (by index), the variables of the enclosing loops (a bit
// per depth), the string table (tr()). The same type says what changed in an update.
struct Reads
{
    std::bitset<TemplateNames::kMax> names;
    uint32_t loops = 0;
    bool strings = false;
    Reads &operator|=(const Reads &other)
    {
        names |= other.names;
        loops |= other.loops;
        strings = strings || other.strings;
        return *this;
    }
    bool Meets(const Reads &changed) const
    {
        return (loops & changed.loops) != 0 || (strings && changed.strings) || (names & changed.names).any();
    }
};

// Where an expression finds its names: the component's values by name index (null: not defined) and the rows of
// the enclosing loops, outermost first.
struct LoopFrame
{
    const Json *list = nullptr;
    size_t index = 0;
};
struct Scope
{
    const std::vector<const Json *> &names;
    const std::vector<LoopFrame> &loops;
};

struct ExpressionNode;
class Expression
{
  public:
    Expression();
    ~Expression();
    Expression(Expression &&) noexcept;
    Expression &operator=(Expression &&) noexcept;
    // `loops`: the `as` names of the enclosing loops, outermost first. `values`: the expression gives a nested
    // component's prop, where json(x) is x itself. Throws std::runtime_error naming what is wrong.
    static Expression Compile(std::string_view text, TemplateNames &names, const std::vector<std::string> &loops,
                              bool values = false);
    Json Evaluate(const Scope &scope) const;
    const Reads &Read() const
    {
        return reads_;
    }
    explicit operator bool() const
    {
        return root_ != nullptr;
    }

  private:
    std::unique_ptr<ExpressionNode> root_;
    Reads reads_;
    std::string text_;
};

// Literal text with holes: "a {{ x }} b". Literals are decoded RML.
struct TemplateText
{
    std::vector<std::string> literals; // holes.size() + 1 pieces
    std::vector<Expression> holes;
    Reads reads;
    // `shown`: text content, whose values bind short words (BindShortWords); attribute values do not.
    std::string Evaluate(const Scope &scope, bool shown) const;
};

struct TemplateAttribute
{
    std::string name;
    TemplateText value;
    // name?="{{ expr }}": absent while the value is not true (Truthy); true is written as 1.
    bool conditional = false;
    // On a nested component: the prop it fills (null: an attribute of the element), or all its props (props=).
    const PropSpec *prop = nullptr;
    bool props = false;
    Json fixed; // a json prop's literal value, parsed at load
};

struct TemplateNode
{
    enum class Kind
    {
        Element,
        Text,
        If,
        For
    };
    Kind kind = Kind::Element;
    int line = 0;
    std::string tag;                          // element
    const ComponentSpec *component = nullptr; // element: a nested component's spec
    std::vector<TemplateAttribute> attributes;
    TemplateText text;                   // text
    Expression test;                     // if
    Expression each, key;                // for (key: the row's identity; none: rows by position)
    std::vector<TemplateNode> children;  // element; if: the branch taken; for: the row
    std::vector<TemplateNode> otherwise; // if: <t-else/>
    Reads reads;                         // of the node and everything in it
};

struct CompiledTemplate
{
    std::vector<TemplateNode> nodes;
    Reads reads;
};

// `component`: the spec of a <c-…>/<s-…> tag, null for any other tag. Throws std::runtime_error with the line.
CompiledTemplate CompileTemplate(std::string_view source, TemplateNames &names,
                                 const std::function<const ComponentSpec *(const std::string &tag)> &component);

// The host's facts every template reads besides the props (TemplateNames: right after the props).
enum class SpecName
{
    Spec,
    Children,
    Dp,
    Safe,
    Count
};

// A component's template and class rules, compiled: the names they read (the props first, in the spec's order,
// then spec, children, dp, safe), the template and the class rules.
struct CompiledComponent
{
    const ComponentSpec *spec = nullptr;
    TemplateNames names;
    CompiledTemplate compiled;
    std::vector<std::pair<std::string, Expression>> classes;
};
// `component` finds the spec of a nested tag. Throws std::runtime_error naming the component and the line.
CompiledComponent CompileComponent(const ComponentSpec &spec,
                                   const std::function<const ComponentSpec *(const std::string &tag)> &component);

// The lookup behind the template helper tr('key'): the UI string of the current language
// (pack strings/<lang>.json). The host installs it; without one tr() is a template error.
void SetTemplateStrings(std::function<std::string(const std::string &key)> lookup);
// Counts changes of the language: a component runs the programs that call tr() again when it changed.
void TemplateStringsChanged();
unsigned TemplateStringsRevision();
// One expression over a scope object (its keys are the names).
Json EvaluateExpression(std::string_view expression, const Json &scope);
bool Truthy(const Json &value);
std::string ToText(const Json &value);
// Text as it is shown: a one-letter Russian word (в, к, с, у, о, и, а, я) is bound to the next
// word by a no-break space, so a line never ends on it. Applied to the text the templates
// show (not to attribute values) and to the text of ui-clamp, ui-pages and the tooltip.
std::string BindShortWords(std::string_view text);

} // namespace storm::ui
