#include "components.h"
#include "numbers.h"

#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace storm::ui
{

namespace
{

std::unordered_map<std::string, BehaviorFactory> &Factories()
{
    static std::unordered_map<std::string, BehaviorFactory> factories;
    return factories;
}

// A prop's value from attribute text (an RML document's attribute, a template's text attribute). Empty `error`: it
// parsed.
Json ParseText(const PropSpec &prop, const std::string &text, std::string &error)
{
    switch (prop.type)
    {
    case PropSpec::Type::Bool:
        return !(text == "false" || text == "0" || text == "no");
    case PropSpec::Type::Int:
    case PropSpec::Type::Number:
        if (text.empty())
            return Json();
        if (const std::optional<double> number = ParseNumber<double>(text))
            return *number;
        error = "attribute " + prop.attribute + "='" + text + "' is not a number";
        return Json();
    case PropSpec::Type::Json: {
        Json parsed = Json::parse(text, nullptr, false);
        if (parsed.is_discarded())
            error = "attribute " + prop.attribute + " is not JSON";
        return parsed.is_discarded() ? Json() : parsed;
    }
    default:
        return text;
    }
}

// The slot of a component is the first descendant marked with the `slot` attribute that
// does not belong to a nested component.
Rml::Element *FindSlot(Rml::Element *root)
{
    for (int i = 0; i < root->GetNumChildren(); ++i)
    {
        Rml::Element *child = root->GetChild(i);
        if (child->HasAttribute("slot"))
            return child;
        if (dynamic_cast<ComponentElement *>(child))
            continue;
        if (Rml::Element *found = FindSlot(child))
            return found;
    }
    return nullptr;
}

// "height: 35%; left: 12dp" → {height: "35%", left: "12dp"}
std::unordered_map<std::string, std::string> ParseStyle(const std::string &style)
{
    std::unordered_map<std::string, std::string> out;
    size_t at = 0;
    while (at < style.size())
    {
        size_t end = style.find(';', at);
        if (end == std::string::npos)
            end = style.size();
        const size_t colon = style.find(':', at);
        if (colon != std::string::npos && colon < end)
        {
            out[Rml::StringUtilities::StripWhitespace(style.substr(at, colon - at))] =
                Rml::StringUtilities::StripWhitespace(style.substr(colon + 1, end - colon - 1));
        }
        at = end + 1;
    }
    return out;
}

bool ParseLength(const std::string &text, Rml::Property &out)
{
    char *end = nullptr;
    const float number = std::strtof(text.c_str(), &end);
    if (end == text.c_str())
        return false;
    const std::string unit = end;
    if (unit == "%")
        out = Rml::Property(number, Rml::Unit::PERCENT);
    else if (unit == "dp")
        out = Rml::Property(number, Rml::Unit::DP);
    else if (unit == "px")
        out = Rml::Property(number, Rml::Unit::PX);
    else
        return false;
    return true;
}

// An element marked data-tween="seconds" glides to changed inline lengths (fill levels,
// knob positions) instead of jumping.
void TweenStyle(Rml::Element *element, const std::string &before, const std::string &after)
{
    const float seconds = element->GetAttribute<float>("data-tween", 0.0f);
    if (seconds <= 0.0f)
        return;
    const auto old_values = ParseStyle(before);
    for (const auto &entry : ParseStyle(after))
    {
        const auto old_value = old_values.find(entry.first);
        Rml::Property from, to;
        if (old_value == old_values.end() || old_value->second == entry.second ||
            !ParseLength(old_value->second, from) || !ParseLength(entry.second, to) || from.unit != to.unit)
            continue;
        element->Animate(entry.first, to, seconds, Rml::Tween(Rml::Tween::Cubic, Rml::Tween::Out), 1, true, 0.0f,
                         &from);
    }
}

bool Blank(const std::string &text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return Rml::StringUtilities::IsWhitespace(c); });
}

// A conditional attribute's value that leaves it out (name?="{{ expr }}"): as `cond ? attribute : ''` did.
bool Absent(const Json &value)
{
    return !Truthy(value);
}

std::vector<std::string> Words(const std::optional<std::string> &text)
{
    std::vector<std::string> words;
    if (text)
        Rml::StringUtilities::ExpandString(words, *text, ' ');
    return words;
}

} // namespace

Rml::Element *ComponentSlot(ComponentElement &component)
{
    return FindSlot(&component);
}

// --- the compiled template bound to live elements ---------------------------------------------------------------

struct BoundRow;

// A node of the compiled template as it is shown: its element (a text node's while its text is not blank), what
// its attributes were set to last, and what is under it.
struct BoundNode
{
    const TemplateNode *node = nullptr;
    Rml::Element *element = nullptr;
    std::vector<std::optional<std::string>> values; // per attribute of an element: its text (nullopt: absent)
    std::vector<std::optional<Json>> passed;        // per attribute of a nested component that fills its props
    std::string text;                               // a text node's text
    std::vector<BoundNode> children;                // an element's children; the branch an if shows
    int branch = -1;                                // an if: 0 its first branch, 1 the other
    Json list;                                      // a for: its rows as shown
    std::vector<BoundRow> rows;
};

struct BoundRow
{
    std::string key;
    std::vector<BoundNode> nodes;
};

struct BoundTemplate
{
    std::vector<BoundNode> nodes;
};

namespace
{

Rml::Element *First(const BoundNode &node);

Rml::Element *FirstOf(const std::vector<BoundNode> &nodes, size_t from = 0)
{
    for (size_t i = from; i < nodes.size(); ++i)
        if (Rml::Element *element = First(nodes[i]))
            return element;
    return nullptr;
}

Rml::Element *First(const BoundNode &node)
{
    if (node.element)
        return node.element;
    if (Rml::Element *element = FirstOf(node.children))
        return element;
    for (const BoundRow &row : node.rows)
        if (Rml::Element *element = FirstOf(row.nodes))
            return element;
    return nullptr;
}

void Collect(const std::vector<BoundNode> &nodes, std::vector<Rml::Element *> &out)
{
    for (const BoundNode &node : nodes)
    {
        if (node.element)
        {
            out.push_back(node.element); // what is under it moves with it
            continue;
        }
        Collect(node.children, out);
        for (const BoundRow &row : node.rows)
            Collect(row.nodes, out);
    }
}

// What follows a node among its parent's children: the first element of the nodes after it, then of the rows after
// its row, then what follows the enclosing node.
struct Anchor
{
    const std::vector<BoundNode> *nodes = nullptr;
    size_t index = 0;
    const std::vector<BoundRow> *rows = nullptr;
    size_t row = 0;
    const Anchor *outer = nullptr;
    Rml::Element *Next() const
    {
        if (nodes)
            if (Rml::Element *element = FirstOf(*nodes, index + 1))
                return element;
        if (rows)
            for (size_t r = row + 1; r < rows->size(); ++r)
                if (Rml::Element *element = FirstOf((*rows)[r].nodes))
                    return element;
        return outer ? outer->Next() : nullptr;
    }
};

// `before` null: at the end, before the scroll fade the runtime keeps last (ScrollEdgesChanged).
Rml::Element *Insert(Rml::Element *parent, Rml::ElementPtr element, Rml::Element *before)
{
    if (!before)
        if (const int count = parent->GetNumChildren();
            count > 0 && parent->GetChild(count - 1)->IsClassSet("scroll-fade"))
            before = parent->GetChild(count - 1);
    return before ? parent->InsertBefore(std::move(element), before) : parent->AppendChild(std::move(element));
}

// The template's next sibling of `element` (the scroll fade is the runtime's).
Rml::Element *Following(Rml::Element *element)
{
    Rml::Element *next = element->GetNextSibling();
    return next && next->IsClassSet("scroll-fade") ? nullptr : next;
}

Rml::ElementPtr MakeText(const std::string &text)
{
    Rml::ElementPtr element = Rml::Factory::InstanceElement(nullptr, "#text", "#text", Rml::XMLAttributes());
    auto *node = rmlui_dynamic_cast<Rml::ElementText *>(element.get());
    if (!node)
        throw std::runtime_error("RmlUi made no text element");
    node->SetText(text);
    return element;
}

const std::vector<TemplateNode> &Branch(const TemplateNode &node, int branch)
{
    return branch == 0 ? node.children : node.otherwise;
}

} // namespace

// Makes and updates the live elements of one component's compiled template.
struct Binder
{
    ComponentElement &self;
    FrameStats &stats;
    std::vector<LoopFrame> loops;
    bool structure = false; // an element was made or removed

    Scope Here() const
    {
        return Scope{self.names_, loops};
    }

    std::optional<std::string> AttributeText(const TemplateAttribute &attribute) const
    {
        if (!attribute.conditional)
            return attribute.value.Evaluate(Here(), false);
        const Json value = attribute.value.holes[0].Evaluate(Here());
        if (Absent(value))
            return std::nullopt;
        return value.is_boolean() ? std::string("1") : ToText(value);
    }

    // What an attribute gives a nested component: one prop's value, or (props=) an object of them.
    std::optional<Json> Passed(const TemplateNode &node, const TemplateAttribute &attribute) const
    {
        if (attribute.props || attribute.prop->type == PropSpec::Type::Json)
        {
            if (attribute.value.holes.empty())
                return attribute.fixed;
            Json value = attribute.value.holes[0].Evaluate(Here());
            if (attribute.conditional && Absent(value))
                return std::nullopt;
            if (attribute.props && !value.is_object())
            {
                self.ReportError("props of <" + node.tag + "> (line " + std::to_string(node.line) +
                                 ") is not an object");
                return std::nullopt;
            }
            return value;
        }
        const std::optional<std::string> text = AttributeText(attribute);
        if (!text)
            return std::nullopt;
        std::string error;
        Json value = ParseText(*attribute.prop, *text, error);
        if (!error.empty())
            self.ReportError("<" + node.tag + "> (line " + std::to_string(node.line) + "): " + error);
        return value;
    }

    static void Give(ComponentElement &component, const TemplateAttribute &attribute, const std::optional<Json> &before,
                     const std::optional<Json> &after)
    {
        if (!attribute.props)
        {
            component.SetProp(*attribute.prop, after ? *after : Json());
            return;
        }
        // Keys the object no longer has return to their defaults.
        Json props = after ? *after : Json::object();
        if (before)
            for (const auto &item : before->items())
                if (!props.contains(item.key()))
                    props[item.key()] = nullptr;
        component.SetProps(props);
    }

    void SetAttribute(Rml::Element *element, const std::string &name, const std::optional<std::string> &before,
                      const std::optional<std::string> &after)
    {
        if (before == after)
            return;
        ++stats.touched;
        if (name == "class")
        {
            // Only the template's own classes change: those the component and the runtime set stay.
            const std::vector<std::string> old_names = Words(before), new_names = Words(after);
            for (const std::string &old_name : old_names)
                if (std::find(new_names.begin(), new_names.end(), old_name) == new_names.end())
                    element->SetClass(old_name, false);
            for (const std::string &new_name : new_names)
                element->SetClass(new_name, true);
            return;
        }
        if (name == "style")
        {
            // The attribute only adds properties: drop the ones the new text no longer has.
            const auto kept = after ? ParseStyle(*after) : std::unordered_map<std::string, std::string>();
            if (before)
                for (const auto &old : ParseStyle(*before))
                    if (!kept.count(old.first))
                        element->RemoveProperty(old.first);
            if (!after)
            {
                element->RemoveAttribute(name);
                return;
            }
            element->SetAttribute(name, *after);
            if (before)
                TweenStyle(element, *before, *after);
            return;
        }
        if (after)
            element->SetAttribute(name, *after);
        else
            element->RemoveAttribute(name);
    }

    void CreateAll(const std::vector<TemplateNode> &nodes, std::vector<BoundNode> &out, Rml::Element *parent,
                   Rml::Element *before)
    {
        out.reserve(out.size() + nodes.size());
        for (const TemplateNode &node : nodes)
            out.push_back(Create(node, parent, before));
    }

    void CreateRow(const TemplateNode &node, BoundNode &bound, size_t index, Rml::Element *parent, Rml::Element *before)
    {
        BoundRow &row = bound.rows[index];
        loops.push_back({&bound.list, index});
        CreateAll(node.children, row.nodes, parent, before);
        loops.pop_back();
    }

    BoundNode Create(const TemplateNode &node, Rml::Element *parent, Rml::Element *before)
    {
        structure = true;
        BoundNode bound;
        bound.node = &node;
        switch (node.kind)
        {
        case TemplateNode::Kind::Text:
            bound.text = node.text.Evaluate(Here(), true);
            if (!Blank(bound.text))
            {
                bound.element = Insert(parent, MakeText(bound.text), before);
                ++stats.touched;
            }
            break;
        case TemplateNode::Kind::If:
            bound.branch = Truthy(node.test.Evaluate(Here())) ? 0 : 1;
            CreateAll(Branch(node, bound.branch), bound.children, parent, before);
            break;
        case TemplateNode::Kind::For:
            bound.list = node.each.Evaluate(Here());
            if (!bound.list.is_array())
                bound.list = Json::array();
            bound.rows.resize(bound.list.size());
            Keys(node, bound.list, bound.rows);
            for (size_t i = 0; i < bound.rows.size(); ++i)
                CreateRow(node, bound, i, parent, before);
            break;
        case TemplateNode::Kind::Element: {
            Rml::XMLAttributes attributes;
            bound.values.resize(node.attributes.size());
            bound.passed.resize(node.attributes.size());
            for (size_t i = 0; i < node.attributes.size(); ++i)
            {
                const TemplateAttribute &attribute = node.attributes[i];
                if (attribute.prop || attribute.props)
                    bound.passed[i] = Passed(node, attribute);
                else if ((bound.values[i] = AttributeText(attribute)))
                    attributes[attribute.name] = *bound.values[i];
            }
            Rml::ElementPtr element = Rml::Factory::InstanceElement(parent, node.tag, node.tag, attributes);
            if (!element)
                throw std::runtime_error("RmlUi made no <" + node.tag + ">");
            if (node.component)
            {
                auto *component = dynamic_cast<ComponentElement *>(element.get());
                if (!component)
                    throw std::runtime_error("<" + node.tag + "> is not a component element");
                for (size_t i = 0; i < node.attributes.size(); ++i)
                    if (bound.passed[i])
                        Give(*component, node.attributes[i], std::nullopt, bound.passed[i]);
            }
            // Made before it is placed: a nested component takes these as its content at its first build.
            CreateAll(node.children, bound.children, element.get(), nullptr);
            bound.element = Insert(parent, std::move(element), before);
            ++stats.touched;
            break;
        }
        }
        return bound;
    }

    // The rows' keys: by the template's key expression, else their positions.
    void Keys(const TemplateNode &node, const Json &list, std::vector<BoundRow> &rows)
    {
        std::set<std::string> seen;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!node.key)
            {
                rows[i].key = std::to_string(i);
                continue;
            }
            loops.push_back({&list, i});
            rows[i].key = ToText(node.key.Evaluate(Here()));
            loops.pop_back();
            if (!seen.insert(rows[i].key).second)
            {
                self.ReportError("line " + std::to_string(node.line) + ": two rows have the key '" + rows[i].key + "'");
                rows[i].key += "\n" + std::to_string(i); // still one element each
            }
        }
    }

    void Evacuate(Rml::Element *element)
    {
        // The slot goes with it: the content a parent template keeps there waits aside (ContentParent).
        Rml::Element *slot = self.slot_.get();
        if (!slot || slot->GetNumChildren() == 0)
            return;
        Rml::Element *at = slot;
        while (at && at != element && at != &self)
            at = at->GetParentNode();
        if (at != element)
            return;
        if (!self.held_)
            self.held_ = Rml::Factory::InstanceElement(nullptr, "div", "div", Rml::XMLAttributes());
        while (slot->GetNumChildren() > 0)
            self.held_->AppendChild(slot->RemoveChild(slot->GetFirstChild()));
        self.slot_.reset();
    }

    void RemoveAll(std::vector<BoundNode> &nodes)
    {
        for (BoundNode &node : nodes)
            Remove(node);
        nodes.clear();
    }

    void Remove(BoundNode &node)
    {
        structure = true;
        if (node.element)
        {
            Evacuate(node.element);
            node.element->GetParentNode()->RemoveChild(node.element);
            node.element = nullptr;
            ++stats.touched;
            return;
        }
        RemoveAll(node.children);
        for (BoundRow &row : node.rows)
            RemoveAll(row.nodes);
        node.rows.clear();
    }

    void UpdateAll(std::vector<BoundNode> &nodes, const Reads &changed, Rml::Element *parent, const Anchor *outer)
    {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].node->reads.Meets(changed))
            {
                Anchor after;
                after.nodes = &nodes;
                after.index = i;
                after.outer = outer;
                Update(nodes[i], changed, parent, after);
            }
    }

    void Update(BoundNode &bound, const Reads &changed, Rml::Element *parent, const Anchor &after)
    {
        const TemplateNode &node = *bound.node;
        switch (node.kind)
        {
        case TemplateNode::Kind::Text: {
            std::string text = node.text.Evaluate(Here(), true);
            if (text == bound.text)
                return;
            bound.text = std::move(text);
            ++stats.touched;
            if (Blank(bound.text))
            {
                if (bound.element)
                    Remove(bound);
            }
            else if (bound.element)
                static_cast<Rml::ElementText *>(bound.element)->SetText(bound.text);
            else
            {
                bound.element = Insert(parent, MakeText(bound.text), after.Next());
                structure = true;
            }
            return;
        }
        case TemplateNode::Kind::If: {
            const int branch =
                node.test.Read().Meets(changed) ? (Truthy(node.test.Evaluate(Here())) ? 0 : 1) : bound.branch;
            if (branch == bound.branch)
            {
                UpdateAll(bound.children, changed, parent, &after);
                return;
            }
            RemoveAll(bound.children);
            bound.branch = branch;
            CreateAll(Branch(node, branch), bound.children, parent, after.Next());
            return;
        }
        case TemplateNode::Kind::For:
            UpdateRows(bound, changed, parent, after);
            return;
        case TemplateNode::Kind::Element:
            break;
        }
        auto *component = node.component ? static_cast<ComponentElement *>(bound.element) : nullptr;
        if (component && Renamed(bound, changed))
        {
            // A component that takes another id is another component (the events of the screen name it): it is
            // made afresh, without the state its behaviours kept.
            Rml::Element *next = Following(bound.element);
            Remove(bound);
            bound = Create(node, parent, next);
            return;
        }
        for (size_t i = 0; i < node.attributes.size(); ++i)
        {
            const TemplateAttribute &attribute = node.attributes[i];
            if (!attribute.value.reads.Meets(changed))
                continue;
            if (attribute.prop || attribute.props)
            {
                std::optional<Json> value = Passed(node, attribute);
                if (value != bound.passed[i])
                {
                    Give(*component, attribute, bound.passed[i], value);
                    bound.passed[i] = std::move(value);
                    ++stats.touched;
                }
                continue;
            }
            std::optional<std::string> value = AttributeText(attribute);
            SetAttribute(bound.element, attribute.name, bound.values[i], value);
            bound.values[i] = std::move(value);
        }
        if (bound.children.empty() && node.children.empty())
            return;
        UpdateAll(bound.children, changed, component ? component->ContentParent() : bound.element, nullptr);
        if (component)
            component->ContentChanged();
    }

    bool Renamed(const BoundNode &bound, const Reads &changed) const
    {
        const auto &attributes = bound.node->attributes;
        for (size_t i = 0; i < attributes.size(); ++i)
            if (attributes[i].name == "id" && attributes[i].value.reads.Meets(changed))
                return AttributeText(attributes[i]) != bound.values[i];
        return false;
    }

    void UpdateRows(BoundNode &bound, const Reads &changed, Rml::Element *parent, const Anchor &after)
    {
        const TemplateNode &node = *bound.node;
        const uint32_t own = 1u << loops.size();
        Json fresh;
        const bool relist = node.each.Read().Meets(changed);
        if (relist)
        {
            fresh = node.each.Evaluate(Here());
            if (!fresh.is_array())
                fresh = Json::array();
        }
        if (!relist || fresh == bound.list)
        {
            for (size_t r = 0; r < bound.rows.size(); ++r)
            {
                Anchor row;
                row.rows = &bound.rows;
                row.row = r;
                row.outer = &after;
                loops.push_back({&bound.list, r});
                UpdateAll(bound.rows[r].nodes, changed, parent, &row);
                loops.pop_back();
            }
            return;
        }
        // The rows that stay keep their elements (matched by key, else by position); the others are made or removed.
        std::vector<BoundRow> rows(fresh.size());
        Keys(node, fresh, rows);
        std::unordered_map<std::string, size_t> old_rows;
        for (size_t j = 0; j < bound.rows.size(); ++j)
            old_rows.emplace(bound.rows[j].key, j);
        std::vector<long> from(rows.size(), -1);
        std::vector<bool> taken(bound.rows.size(), false);
        for (size_t i = 0; i < rows.size(); ++i)
            if (const auto found = old_rows.find(rows[i].key); found != old_rows.end() && !taken[found->second])
            {
                from[i] = static_cast<long>(found->second);
                taken[found->second] = true;
            }
        for (size_t j = 0; j < bound.rows.size(); ++j)
            if (!taken[j])
                RemoveAll(bound.rows[j].nodes);
        for (size_t i = 0; i < rows.size(); ++i)
            if (from[i] >= 0)
                rows[i].nodes = std::move(bound.rows[static_cast<size_t>(from[i])].nodes);
        const size_t old_count = bound.rows.size();
        const Json old_list = std::move(bound.list);
        bound.rows = std::move(rows);
        bound.list = std::move(fresh);
        // From the last row to the first, each row is put just before what follows it.
        Rml::Element *following = after.Next();
        for (size_t i = bound.rows.size(); i-- > 0;)
        {
            BoundRow &row = bound.rows[i];
            Anchor place;
            place.rows = &bound.rows;
            place.row = i;
            place.outer = &after;
            loops.push_back({&bound.list, i});
            if (from[i] < 0)
                CreateAll(node.children, row.nodes, parent, following);
            else
            {
                const auto j = static_cast<size_t>(from[i]);
                std::vector<Rml::Element *> elements;
                Collect(row.nodes, elements);
                Rml::Element *target = following;
                for (size_t k = elements.size(); k-- > 0;)
                {
                    Rml::Element *element = elements[k];
                    if (Following(element) != target)
                    {
                        Rml::Element *owner = element->GetParentNode();
                        Insert(owner, owner->RemoveChild(element), target);
                        ++stats.touched;
                    }
                    target = element;
                }
                Reads row_changed = changed;
                if (j != i || old_list[j] != bound.list[i] || (j + 1 == old_count) != (i + 1 == bound.rows.size()))
                    row_changed.loops |= own;
                UpdateAll(row.nodes, row_changed, parent, &place);
            }
            loops.pop_back();
            if (Rml::Element *first = FirstOf(row.nodes))
                following = first;
        }
    }
};

std::string CheckPropValue(const PropSpec &prop, const Json &value)
{
    if (prop.type != PropSpec::Type::Enum || value.is_null() || prop.options.empty())
        return {};
    for (const Json &option : prop.options)
        if (option == value || ToText(option) == ToText(value))
            return {};
    return "'" + ToText(value) + "' is not a value of " + prop.name;
}

int ReportUnknownComponents(Rml::Element *root)
{
    int found = 0;
    for (int i = 0; root && i < root->GetNumChildren(); ++i)
    {
        Rml::Element *child = root->GetChild(i);
        if (dynamic_cast<ComponentElement *>(child))
            continue;
        if (child->GetTagName().rfind("c-", 0) == 0)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Unknown component <%s> (%s): no spec in components.json",
                              child->GetTagName().c_str(), child->GetAddress().c_str());
            child->SetClass("ui-error", true);
            child->SetClass("ui-unknown", true); // an unknown tag is inline and empty: give it a box
            ++found;
        }
        found += ReportUnknownComponents(child);
    }
    return found;
}

void RegisterBehavior(const std::string &name, BehaviorFactory factory)
{
    Factories()[name] = factory;
}

ComponentElement::ComponentElement(const Rml::String &tag, const CompiledComponent &component, Services &services)
    : ShadowedElement(tag), spec_(*component.spec), compiled_(component), services_(services)
{
    for (const std::string &name : spec_.behaviors)
    {
        const auto factory = Factories().find(name);
        if (factory == Factories().end())
            Rml::Log::Message(Rml::Log::LT_ERROR, "Component %s: unknown behaviour '%s'", tag.c_str(), name.c_str());
        else
            behaviors_.push_back(factory->second());
    }
}

ComponentElement::~ComponentElement()
{
    services_.Forget(*this);
}

Json ComponentElement::Prop(const std::string &name) const
{
    const auto found = props_.find(name);
    if (found != props_.end() && !found->is_null())
        return *found;
    for (const PropSpec &prop : spec_.props)
        if (prop.name == name)
            return prop.fallback;
    return Json();
}

void ComponentElement::SetProps(const Json &props)
{
    // Also with nothing in it: the host's facts (safe area, density) are compared on the next update.
    dirty_ = true;
    if (!props.is_object())
    {
        ReportError("props must be a JSON object");
        return;
    }
    for (const auto &item : props.items())
    {
        const int index = compiled_.names.Find(item.key());
        if (index < 0 || static_cast<size_t>(index) >= spec_.props.size())
        {
            ReportError("unknown prop '" + item.key() + "'");
            continue;
        }
        SetProp(spec_.props[static_cast<size_t>(index)], item.value());
    }
}

void ComponentElement::ReplaceProps(const Json &props)
{
    SetProps(props);
    if (!props.is_object())
        return;
    for (const PropSpec &prop : spec_.props)
        if (!props.contains(prop.name))
            SetProp(prop, Json());
}

void ComponentElement::GiveProps(const Json &props)
{
    if (!props.is_object())
    {
        ReportError("props must be a JSON object");
        return;
    }
    std::vector<bool> given(spec_.props.size());
    for (const auto &item : props.items())
    {
        const int index = compiled_.names.Find(item.key());
        if (index < 0 || static_cast<size_t>(index) >= spec_.props.size())
        {
            ReportError("unknown prop '" + item.key() + "'");
            continue;
        }
        given[static_cast<size_t>(index)] = true;
        SetProp(spec_.props[static_cast<size_t>(index)], item.value());
    }
    for (size_t i = 0; i < given_.size(); ++i)
        if (given_[i] && !given[i])
            SetProp(spec_.props[i], Json());
    given_ = std::move(given);
}

void ComponentElement::SetProp(const PropSpec &prop, const Json &value)
{
    const auto found = props_.find(prop.name);
    const Json &before = found != props_.end() && !found->is_null() ? *found : prop.fallback;
    if (before == (value.is_null() ? prop.fallback : value))
        return;
    changed_.names.set(static_cast<size_t>(&prop - spec_.props.data()));
    dirty_ = true;
    props_[prop.name] = value;
}

void ComponentElement::ReportError(const std::string &what)
{
    error_ = true;
    ApplyClasses();
    services_.ComponentError(*this, what);
}

void ComponentElement::SetLive(const std::string &name, bool on)
{
    const auto found = std::find(live_classes_.begin(), live_classes_.end(), name);
    if (on == (found != live_classes_.end()))
        return;
    if (on)
        live_classes_.push_back(name);
    else
        live_classes_.erase(found);
    ApplyClasses();
}

void ComponentElement::Emit(const std::string &verb, const std::string &value)
{
    services_.Emit(*this, verb, value);
}

// Attributes as an RML document writes them (a gallery page, a toast the host makes): props from their text.
void ComponentElement::OnAttributeChange(const Rml::ElementAttributes &changed)
{
    Rml::Element::OnAttributeChange(changed);
    for (const auto &entry : changed)
    {
        if (entry.first == "props") // a whole props object at once
        {
            const Json parsed = Json::parse(entry.second.Get<Rml::String>(), nullptr, false);
            if (parsed.is_object())
                SetProps(parsed);
            else
                ReportError("props is not a JSON object");
            continue;
        }
        if (entry.first == "data-behavior") // one more behaviour for this element (HUD layout: hold, swipe)
        {
            const std::string name = entry.second.Get<Rml::String>();
            const auto factory = Factories().find(name);
            if (factory == Factories().end())
                ReportError("unknown behaviour '" + name + "'");
            else if (std::find(spec_.behaviors.begin(), spec_.behaviors.end(), name) == spec_.behaviors.end() &&
                     extra_behavior_ != name)
            {
                extra_behavior_ = name;
                behaviors_.push_back(factory->second());
            }
            continue;
        }
        if (const PropSpec *prop = spec_.FindAttribute(entry.first))
        {
            // A removed attribute returns the prop to its default; it is not an empty, "present" value.
            std::string error;
            Json value = HasAttribute(entry.first) ? ParseText(*prop, entry.second.Get<Rml::String>(), error) : Json();
            if (!error.empty())
                ReportError(error);
            SetProp(*prop, std::move(value));
        }
        else if (!IsGenericAttribute(entry.first))
            ReportError("unknown attribute '" + entry.first + "'");
    }
}

void ComponentElement::OnUpdate()
{
    Rml::Element::OnUpdate();
    if (dirty_ || !bound_)
        Update();
}

namespace
{
// Components laid out in the current context update (NoteLaidOut). A flex container formats
// its items several times in one pass (trial widths), and a dp-ratio change reaches an element
// before its descendants' styles: only the boxes once the pass is over are measured.
std::vector<Rml::ObserverPtr<Rml::Element>> laid_out_components;
} // namespace

void ComponentElement::OnLayout()
{
    Rml::Element::OnLayout();
    ++services_.Counters().laid_out;
    NoteLaidOut();
}

void ComponentElement::NoteLaidOut()
{
    if (laid_out_noted_)
        return;
    laid_out_noted_ = true;
    laid_out_components.push_back(GetObserverPtr());
}

bool SettleLaidOutComponents()
{
    std::vector<Rml::ObserverPtr<Rml::Element>> laid = std::move(laid_out_components);
    laid_out_components.clear();
    bool changed = false;
    for (const auto &entry : laid)
        if (auto *component = static_cast<ComponentElement *>(entry.get()))
            changed = component->SettleLayout() || changed;
    return changed;
}

bool ComponentElement::SettleLayout()
{
    laid_out_noted_ = false;
    bool changed = false;
    for (const auto &behavior : behaviors_)
        changed = behavior->LaidOut(*this) || changed;
    return changed;
}

void ComponentElement::RebuildNow()
{
    Update();
}

void ComponentElement::BindNames()
{
    const size_t props = spec_.props.size();
    names_.assign(compiled_.names.All().size(), nullptr);
    for (size_t i = 0; i < props; ++i)
    {
        const auto found = props_.find(spec_.props[i].name);
        names_[i] = found != props_.end() && !found->is_null() ? &*found : &spec_.props[i].fallback;
    }
    names_[props + size_t(SpecName::Spec)] = &spec_.constants;
    names_[props + size_t(SpecName::Children)] = &children_;
    names_[props + size_t(SpecName::Dp)] = &dp_;
    names_[props + size_t(SpecName::Safe)] = &safe_;
}

// Builds the elements of the template on the first call, then runs only what reads what changed since.
void ComponentElement::Update()
{
    dirty_ = false;
    const bool first = !bound_;
    const size_t props = spec_.props.size();
    const auto fact = [&](Json &shown, Json now, SpecName name) {
        if (shown == now)
            return;
        shown = std::move(now);
        changed_.names.set(props + size_t(name));
    };
    fact(dp_, services_.Density(), SpecName::Dp);
    fact(safe_, services_.SafeArea(), SpecName::Safe);
    if (strings_ != TemplateStringsRevision())
    {
        strings_ = TemplateStringsRevision();
        changed_.strings = true;
    }
    if (!first && changed_.names.none() && !changed_.strings)
        return;
    const auto started = std::chrono::steady_clock::now();
    FrameStats &stats = services_.Counters();
    // Counted for every update, also one that changes no element: a behaviour may still change what is shown in
    // Rebuilt (Prepare draws a frame for it).
    ++stats.rebuilds;
    if (first)
    {
        // The element's own children are the content for the slot.
        held_ = Rml::Factory::InstanceElement(nullptr, "div", "div", Rml::XMLAttributes());
        while (GetNumChildren() > 0)
            held_->AppendChild(RemoveChild(GetFirstChild()));
        children_ = held_->GetNumChildren() > 0;
        // constants.layoutBoundary: the component's size never depends on its contents (a header bar of fixed
        // height across the screen), so a change inside it lays out only it (patches/rmlui-layout-boundary.patch).
        if (Truthy(spec_.constants.value("layoutBoundary", Json(false))))
            SetAttribute("rmlui-layout-boundary", "");
    }
    BindNames();
    prop_classes_.clear();
    for (size_t i = 0; i < props; ++i)
    {
        const PropSpec &prop = spec_.props[i];
        const Json &value = *names_[i];
        if (first || changed_.names.test(i))
            if (const std::string wrong = CheckPropValue(prop, value); !wrong.empty())
                ReportError(wrong);
        if (!prop.as_class)
            continue;
        if (prop.type == PropSpec::Type::Bool)
        {
            if (Truthy(value))
                prop_classes_.push_back(prop.attribute);
        }
        else if (!ToText(value).empty())
            prop_classes_.push_back(prop.attribute + "-" + ToText(value));
    }
    const std::vector<LoopFrame> no_loops;
    for (const auto &[name, rule] : compiled_.classes)
    {
        try
        {
            if (Truthy(rule.Evaluate(Scope{names_, no_loops})))
                prop_classes_.push_back(name);
        }
        catch (const std::exception &error)
        {
            ReportError("class " + name + ": " + error.what());
        }
    }
    Binder binder{*this, stats, {}};
    try
    {
        if (first)
        {
            bound_ = std::make_unique<BoundTemplate>();
            binder.CreateAll(compiled_.compiled.nodes, bound_->nodes, this, nullptr);
        }
        else
            binder.UpdateAll(bound_->nodes, changed_, this, nullptr);
    }
    catch (const std::exception &error)
    {
        ReportError(std::string("template: ") + error.what());
    }
    changed_ = Reads();
    if (first || binder.structure)
        PlaceContent();
    ApplyBoxSize();
    ApplyClasses();
    for (const auto &behavior : behaviors_)
        behavior->Rebuilt(*this);
    NoteLaidOut(); // a change inside a layout boundary may not lay the component itself out
    stats.rebuild_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

// After the template's elements changed: the content waiting aside goes into the slot the template shows now.
void ComponentElement::PlaceContent()
{
    Rml::Element *slot = FindSlot(this);
    slot_ = slot ? slot->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();
    if (!held_ || held_->GetNumChildren() == 0)
        return;
    if (!slot)
    {
        if (children_.get<bool>())
            ReportError("content passed to a component whose template has no slot");
        return;
    }
    while (held_->GetNumChildren() > 0)
        slot->AppendChild(held_->RemoveChild(held_->GetFirstChild()));
}

Rml::Element *ComponentElement::ContentParent()
{
    if (!bound_)
        return this;
    if (Rml::Element *slot = slot_.get())
        return slot;
    if (!held_)
        held_ = Rml::Factory::InstanceElement(nullptr, "div", "div", Rml::XMLAttributes());
    return held_.get();
}

bool ComponentElement::HasContent()
{
    Rml::Element *content = ContentParent();
    for (int i = 0; i < content->GetNumChildren(); ++i)
        if (!content->GetChild(i)->IsClassSet("scroll-fade"))
            return true;
    return false;
}

void ComponentElement::ContentChanged()
{
    if (!bound_)
        return; // the first build counts it
    const bool now = HasContent();
    if (now == children_.get<bool>())
        return;
    // The template decides on `children` whether it has a slot at all.
    children_ = now;
    changed_.names.set(spec_.props.size() + size_t(SpecName::Children));
    dirty_ = true;
}

// constants.boxSize: the name of a prop that is the component's width and height in dp (C-Icon:
// its size). A box whose size is set needs no formatting of its content to be measured: an
// icon in a header's nested flex rows was formatted about 500 times per layout of the header.
void ComponentElement::ApplyBoxSize()
{
    const auto prop = spec_.constants.find("boxSize");
    if (prop == spec_.constants.end() || !prop->is_string())
        return;
    const int index = compiled_.names.Find(prop->get<std::string>());
    const Json *value = index >= 0 && static_cast<size_t>(index) < spec_.props.size() ? names_[index] : nullptr;
    const float size = value && value->is_number() ? value->get<float>() : -1.0f;
    if (size == box_size_)
        return;
    box_size_ = size;
    if (size > 0.0f)
    {
        SetProperty(Rml::PropertyId::Width, Rml::Property(size, Rml::Unit::DP));
        SetProperty(Rml::PropertyId::Height, Rml::Property(size, Rml::Unit::DP));
    }
    else
    {
        RemoveProperty(Rml::PropertyId::Width);
        RemoveProperty(Rml::PropertyId::Height);
        ReportError("constants.boxSize names prop '" + prop->get<std::string>() + "', which is not a number");
    }
}

void ComponentElement::ApplyClasses()
{
    std::vector<std::string> wanted = prop_classes_;
    if (error_)
        wanted.push_back("ui-error");
    for (const std::string &name : live_classes_)
        if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            wanted.push_back(name);
    if (wanted == applied_classes_)
        return;
    for (const std::string &name : applied_classes_)
        if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            SetClass(name, false);
    for (const std::string &name : wanted)
        SetClass(name, true);
    applied_classes_ = std::move(wanted);
}

bool ComponentElement::PointerExclusive(const Pointer &pointer) const
{
    for (const auto &behavior : behaviors_)
        if (behavior->Exclusive(pointer))
            return true;
    return false;
}

bool ComponentElement::PointerDown(const Pointer &pointer)
{
    bool taken = false;
    for (const auto &behavior : behaviors_)
        taken = behavior->Down(*this, pointer) || taken;
    return taken;
}

void ComponentElement::PointerMove(const Pointer &pointer)
{
    for (const auto &behavior : behaviors_)
        behavior->Move(*this, pointer);
}

void ComponentElement::PointerUp(const Pointer &pointer, bool cancelled)
{
    for (const auto &behavior : behaviors_)
        behavior->Up(*this, pointer, cancelled);
}

void ComponentElement::TouchRegions(std::vector<Rml::Element *> &out)
{
    for (const auto &behavior : behaviors_)
        behavior->Regions(*this, out);
}

void ComponentElement::Taps(std::vector<Tap> &taps)
{
    for (const auto &behavior : behaviors_)
        behavior->Taps(*this, taps);
}

bool ComponentElement::Refuse()
{
    bool shown = false;
    for (const auto &behavior : behaviors_)
        shown = behavior->Refused(*this) || shown;
    return shown;
}

void ComponentElement::RequestTicks(Behavior &owner, bool enable)
{
    const bool was_ticking = !ticking_behaviors_.empty();
    if (enable)
        ticking_behaviors_.insert(&owner);
    else
        ticking_behaviors_.erase(&owner);
    if (was_ticking != !ticking_behaviors_.empty())
        services_.RequestTicks(*this, !ticking_behaviors_.empty());
}

void ComponentElement::Tick(double now)
{
    for (const auto &behavior : behaviors_)
        if (ticking_behaviors_.count(behavior.get()))
            behavior->Tick(*this, now);
}

class ComponentRegistry::Instancer final : public Rml::ElementInstancer
{
  public:
    Instancer(const CompiledComponent &component, Services &services) : component_(component), services_(services)
    {
    }
    Rml::ElementPtr InstanceElement(Rml::Element *, const Rml::String &tag, const Rml::XMLAttributes &) override
    {
        return Rml::ElementPtr(new ComponentElement(tag, component_, services_));
    }
    void ReleaseElement(Rml::Element *element) override
    {
        delete element;
    }

  private:
    const CompiledComponent &component_;
    Services &services_;
};

bool ComponentRegistry::Load(const Json &pack, std::string *error)
{
    try
    {
        for (const Json &entry : pack.at("components"))
        {
            std::unique_ptr<ComponentSpec> spec = ParseComponentSpec(entry);
            const std::string tag = spec->tag;
            specs_[tag] = std::move(spec);
        }
        // Every template at once, at load: a broken one stops the start instead of failing when a screen reaches it.
        for (const auto &[tag, spec] : specs_)
            compiled_.emplace(tag, CompileComponent(*spec, [this](const std::string &nested) { return Find(nested); }));
    }
    catch (const std::exception &failure)
    {
        if (error)
            *error = std::string("UI pack: ") + failure.what();
        return false;
    }
    for (const auto &[tag, component] : compiled_)
    {
        auto instancer = std::make_unique<Instancer>(component, services_);
        Rml::Factory::RegisterElementInstancer(tag, instancer.get());
        instancers_.push_back(std::move(instancer));
    }
    return true;
}

const ComponentSpec *ComponentRegistry::Find(const std::string &tag) const
{
    const auto found = specs_.find(tag);
    return found == specs_.end() ? nullptr : found->second.get();
}

ComponentElement *InteractiveComponentAt(Rml::Element *element, const Pointer &pointer)
{
    ComponentElement *closest = nullptr;
    for (; element; element = element->GetParentNode())
        if (auto *component = dynamic_cast<ComponentElement *>(element))
            if (component->HasBehaviors())
            {
                if (component->PointerExclusive(pointer))
                    return component;
                if (!closest)
                    closest = component;
            }
    return closest;
}

} // namespace storm::ui
