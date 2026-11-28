#include "template.h"
#include "component_spec.h"

#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace storm::ui
{

namespace
{
std::function<std::string(const std::string &)> &Strings()
{
    static std::function<std::string(const std::string &)> lookup;
    return lookup;
}
unsigned strings_revision = 0;
} // namespace

void SetTemplateStrings(std::function<std::string(const std::string &key)> lookup)
{
    Strings() = std::move(lookup);
    ++strings_revision;
}

void TemplateStringsChanged()
{
    ++strings_revision;
}

unsigned TemplateStringsRevision()
{
    return strings_revision;
}

bool Truthy(const Json &value)
{
    switch (value.type())
    {
    case Json::value_t::null:
    case Json::value_t::discarded:
        return false;
    case Json::value_t::boolean:
        return value.get<bool>();
    case Json::value_t::string:
        return !value.get_ref<const std::string &>().empty();
    case Json::value_t::array:
    case Json::value_t::object:
        return !value.empty();
    default:
        return value.get<double>() != 0.0;
    }
}

static std::string NumberText(double number)
{
    if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 1e15)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
        return buffer;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.4f", number);
    std::string text = buffer;
    while (!text.empty() && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

std::string ToText(const Json &value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_null() || value.is_discarded())
        return {};
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (value.is_number())
        return NumberText(value.get<double>());
    return value.dump();
}

std::string BindShortWords(std::string_view text)
{
    // The one-letter words as UTF-8 (all two bytes: 0xD0 or 0xD1 and one more).
    static constexpr const char *kWords[] = {"\xD0\xB2", "\xD0\xBA", "\xD1\x81", "\xD1\x83", "\xD0\xBE", "\xD0\xB8",
                                             "\xD0\xB0", "\xD1\x8F", "\xD0\x92", "\xD0\x9A", "\xD0\xA1", "\xD0\xA3",
                                             "\xD0\x9E", "\xD0\x98", "\xD0\x90", "\xD0\xAF"};
    const auto word_start = [&text](size_t at) {
        if (at == 0)
            return true;
        const char before = text[at - 1];
        if (before == ' ' || before == '\n' || before == '(' || before == '"')
            return true;
        // «, or a no-break space bound before this word (\xC2\xAB, \xC2\xA0).
        return at >= 2 && text[at - 2] == '\xC2' && (before == '\xAB' || before == '\xA0');
    };
    std::string out;
    size_t copied = 0;
    for (size_t at = 0; at + 2 < text.size(); ++at)
    {
        if ((text[at] != '\xD0' && text[at] != '\xD1') || text[at + 2] != ' ' || !word_start(at))
            continue;
        for (const char *word : kWords)
            if (text[at] == word[0] && text[at + 1] == word[1])
            {
                out.append(text, copied, at + 2 - copied);
                out += "\xC2\xA0";
                copied = at + 3;
                break;
            }
    }
    if (copied == 0)
        return std::string(text);
    out.append(text, copied, std::string_view::npos);
    return out;
}

namespace
{

// "3 000 495" (space, no-break, thin or narrow no-break space between groups of three digits,
// as the game and GroupDigits write amounts) → "3000495"; any other text stays as it is.
std::string Ungrouped(const std::string &text)
{
    static constexpr std::string_view separators[] = {" ", "\xC2\xA0", "\xE2\x80\x89", "\xE2\x80\xAF"};
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    std::string out;
    for (size_t i = 0; i < text.size();)
    {
        size_t skip = 0;
        if (!out.empty() && digit(out.back()))
            for (const std::string_view separator : separators)
                if (text.compare(i, separator.size(), separator) == 0)
                {
                    const size_t group = i + separator.size();
                    if (group + 3 <= text.size() && digit(text[group]) && digit(text[group + 1]) &&
                        digit(text[group + 2]) && (group + 3 == text.size() || !digit(text[group + 3])))
                        skip = separator.size();
                    break;
                }
        if (skip)
            i += skip;
        else
            out += text[i++];
    }
    return out;
}

double ToNumber(const Json &value)
{
    if (value.is_number())
        return value.get<double>();
    if (value.is_boolean())
        return value.get<bool>() ? 1.0 : 0.0;
    if (value.is_string())
        return std::strtod(Ungrouped(value.get_ref<const std::string &>()).c_str(), nullptr);
    return 0.0;
}

bool LooseEqual(const Json &a, const Json &b)
{
    if (a.is_number() && b.is_number())
        return a.get<double>() == b.get<double>();
    if (a.is_null() || b.is_null())
        return a.is_null() && b.is_null();
    if (a.is_string() != b.is_string() && (a.is_primitive() && b.is_primitive()))
        return ToText(a) == ToText(b);
    return a == b;
}

// Thousands separated by a thin no-break space, the way the design writes amounts.
std::string GroupDigits(const Json &value)
{
    std::string text = ToText(value);
    size_t start = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    size_t end = text.find_first_not_of("0123456789", start);
    if (end == std::string::npos)
        end = text.size();
    if (end - start < 5) // 4-digit numbers stay solid, as in the mock-ups
        return text;
    std::string grouped = text.substr(0, start);
    for (size_t i = start; i < end; ++i)
    {
        if (i > start && (end - i) % 3 == 0)
            grouped += "\xE2\x80\xAF";
        grouped += text[i];
    }
    return grouped + text.substr(end);
}

enum class Op : uint8_t
{
    Literal,
    Name,
    LoopItem,
    LoopIndex,
    LoopLast,
    Member,
    Index,
    Not,
    Negate,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    Choose,
    Call,
    Array,
    Object
};

enum class Function : uint8_t
{
    Or,
    Opt,
    Len,
    Num,
    Str,
    Int,
    Round,
    Min,
    Max,
    Clamp,
    Abs,
    IsObject,
    IsArray,
    IsString,
    Has,
    In,
    Starts,
    Contains,
    Ends,
    Replace,
    Split,
    Range,
    Digits,
    Json,
    Tr,
    ImageKind,
    Pick
};

const std::vector<std::pair<std::string_view, Function>> &Functions()
{
    static const std::vector<std::pair<std::string_view, Function>> functions = {{"or", Function::Or},
                                                                                 {"opt", Function::Opt},
                                                                                 {"len", Function::Len},
                                                                                 {"num", Function::Num},
                                                                                 {"str", Function::Str},
                                                                                 {"int", Function::Int},
                                                                                 {"round", Function::Round},
                                                                                 {"min", Function::Min},
                                                                                 {"max", Function::Max},
                                                                                 {"clamp", Function::Clamp},
                                                                                 {"abs", Function::Abs},
                                                                                 {"is_object", Function::IsObject},
                                                                                 {"is_array", Function::IsArray},
                                                                                 {"is_string", Function::IsString},
                                                                                 {"has", Function::Has},
                                                                                 {"in", Function::In},
                                                                                 {"starts", Function::Starts},
                                                                                 {"contains", Function::Contains},
                                                                                 {"ends", Function::Ends},
                                                                                 {"replace", Function::Replace},
                                                                                 {"split", Function::Split},
                                                                                 {"range", Function::Range},
                                                                                 {"digits", Function::Digits},
                                                                                 {"json", Function::Json},
                                                                                 {"tr", Function::Tr},
                                                                                 {"image_kind", Function::ImageKind},
                                                                                 {"pick", Function::Pick}};
    return functions;
}

} // namespace

struct ExpressionNode
{
    Op op = Op::Literal;
    Function function = Function::Or;
    int slot = 0;          // Name: the name's index; Loop*: the loop's depth
    bool optional = false; // a path inside opt() or or(): it may name nothing
    Json literal;          // Literal; Member: the key; Object: the keys
    std::string path;      // a path step: the path's text up to it (its errors)
    std::vector<std::unique_ptr<ExpressionNode>> args;
};

namespace
{

using Node = std::unique_ptr<ExpressionNode>;

Node Make(Op op)
{
    auto node = std::make_unique<ExpressionNode>();
    node->op = op;
    return node;
}

Node Make(Op op, Node left, Node right)
{
    Node node = Make(op);
    node->args.push_back(std::move(left));
    node->args.push_back(std::move(right));
    return node;
}

class Parser
{
  public:
    Parser(std::string_view text, TemplateNames &names, const std::vector<std::string> &loops, bool values,
           Reads &reads)
        : text_(text), names_(names), loops_(loops), values_(values), reads_(reads)
    {
    }

    Node Run()
    {
        Node node = Ternary();
        Skip();
        if (pos_ != text_.size())
            Fail("unexpected text");
        return node;
    }

  private:
    [[noreturn]] void Fail(const char *what) const
    {
        throw std::runtime_error(std::string("template expression: ") + what + " in '" + std::string(text_) + "'");
    }

    void Skip()
    {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\t' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool Eat(std::string_view token)
    {
        Skip();
        if (text_.substr(pos_, token.size()) != token)
            return false;
        pos_ += token.size();
        return true;
    }

    Node Ternary()
    {
        Node condition = Or();
        if (!Eat("?"))
            return condition;
        Node yes = Ternary();
        if (!Eat(":"))
            Fail("expected ':'");
        Node no = Ternary();
        Node node = Make(Op::Choose, std::move(condition), std::move(yes));
        node->args.push_back(std::move(no));
        return node;
    }

    Node Or()
    {
        Node left = And();
        while (Eat("||"))
            left = Make(Op::Or, std::move(left), And());
        return left;
    }

    Node And()
    {
        Node left = Compare();
        while (Eat("&&"))
            left = Make(Op::And, std::move(left), Compare());
        return left;
    }

    Node Compare()
    {
        Node left = Sum();
        while (true)
        {
            Op op;
            if (Eat("=="))
                op = Op::Eq;
            else if (Eat("!="))
                op = Op::Ne;
            else if (Eat("<="))
                op = Op::Le;
            else if (Eat(">="))
                op = Op::Ge;
            else if (Eat("<"))
                op = Op::Lt;
            else if (Eat(">"))
                op = Op::Gt;
            else
                return left;
            left = Make(op, std::move(left), Sum());
        }
    }

    Node Sum()
    {
        Node left = Product();
        while (true)
        {
            if (Eat("+"))
                left = Make(Op::Add, std::move(left), Product());
            else if (Eat("-"))
                left = Make(Op::Sub, std::move(left), Product());
            else
                return left;
        }
    }

    Node Product()
    {
        Node left = Unary();
        while (true)
        {
            if (Eat("*"))
                left = Make(Op::Mul, std::move(left), Unary());
            else if (Eat("/"))
                left = Make(Op::Div, std::move(left), Unary());
            else if (Eat("%"))
                left = Make(Op::Mod, std::move(left), Unary());
            else
                return left;
        }
    }

    Node Unary()
    {
        Skip();
        if (pos_ < text_.size() && text_[pos_] == '!' && text_.substr(pos_, 2) != "!=")
        {
            ++pos_;
            Node node = Make(Op::Not);
            node->args.push_back(Unary());
            return node;
        }
        if (Eat("-"))
        {
            Node node = Make(Op::Negate);
            node->args.push_back(Unary());
            return node;
        }
        return Postfix();
    }

    // The path's text from `start` up to here, for its errors.
    std::string PathText(size_t start) const
    {
        std::string_view path = text_.substr(start, pos_ - start);
        while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back())))
            path.remove_suffix(1);
        return std::string(path);
    }

    Node Postfix()
    {
        Skip();
        const size_t start = pos_;
        Node value = Primary();
        const bool from_name = value->op == Op::Name || value->op == Op::LoopItem || value->op == Op::LoopIndex ||
                               value->op == Op::LoopLast;
        if (from_name)
            value->path = PathText(start);
        while (true)
        {
            Skip();
            const size_t step = from_name ? start : pos_;
            if (pos_ < text_.size() && text_[pos_] == '.')
            {
                ++pos_;
                Node node = Make(Op::Member);
                node->literal = Identifier();
                node->args.push_back(std::move(value));
                node->optional = optional_ > 0;
                node->path = PathText(step);
                value = std::move(node);
            }
            else if (pos_ < text_.size() && text_[pos_] == '[')
            {
                ++pos_;
                Node index = Ternary();
                if (!Eat("]"))
                    Fail("expected ']'");
                Node node = Make(Op::Index, std::move(value), std::move(index));
                node->optional = optional_ > 0;
                node->path = PathText(step);
                value = std::move(node);
            }
            else
                return value;
        }
    }

    std::string Identifier()
    {
        Skip();
        const size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_' || text_[pos_] == '$'))
            ++pos_;
        if (start == pos_)
            Fail("expected a name");
        return std::string(text_.substr(start, pos_ - start));
    }

    Node Name(const std::string &name)
    {
        for (size_t depth = loops_.size(); depth-- > 0;)
        {
            const std::string &as = loops_[depth];
            const Op op = name == as             ? Op::LoopItem
                          : name == as + "_i"    ? Op::LoopIndex
                          : name == as + "_last" ? Op::LoopLast
                                                 : Op::Literal;
            if (op == Op::Literal)
                continue;
            Node node = Make(op);
            node->slot = static_cast<int>(depth);
            node->optional = optional_ > 0;
            reads_.loops |= 1u << depth;
            return node;
        }
        Node node = Make(Op::Name);
        node->slot = names_.Add(name);
        node->optional = optional_ > 0;
        reads_.names.set(static_cast<size_t>(node->slot));
        return node;
    }

    Node Primary()
    {
        Skip();
        if (pos_ >= text_.size())
            Fail("unexpected end");
        const char c = text_[pos_];
        if (c == '(')
        {
            ++pos_;
            Node value = Ternary();
            if (!Eat(")"))
                Fail("expected ')'");
            return value;
        }
        if (c == '[')
        {
            ++pos_;
            Node node = Make(Op::Array);
            if (!Eat("]"))
            {
                do
                    node->args.push_back(Ternary());
                while (Eat(","));
                if (!Eat("]"))
                    Fail("expected ']'");
            }
            return node;
        }
        if (c == '{')
        {
            ++pos_;
            Node node = Make(Op::Object);
            node->literal = Json::array();
            if (!Eat("}"))
            {
                do
                {
                    Skip();
                    std::string key;
                    if (pos_ < text_.size() && (text_[pos_] == '\'' || text_[pos_] == '"'))
                        key = Primary()->literal.get<std::string>();
                    else
                        key = Identifier();
                    if (!Eat(":"))
                        Fail("expected ':'");
                    node->literal.push_back(key);
                    node->args.push_back(Ternary());
                } while (Eat(","));
                if (!Eat("}"))
                    Fail("expected '}'");
            }
            return node;
        }
        if (c == '\'' || c == '"')
        {
            const size_t end = text_.find(c, pos_ + 1);
            if (end == std::string_view::npos)
                Fail("unterminated string");
            Node node = Make(Op::Literal);
            node->literal = std::string(text_.substr(pos_ + 1, end - pos_ - 1));
            pos_ = end + 1;
            return node;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && pos_ + 1 < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_ + 1]))))
        {
            const std::string rest(text_.substr(pos_));
            char *end = nullptr;
            const double number = std::strtod(rest.c_str(), &end);
            pos_ += static_cast<size_t>(end - rest.c_str());
            Node node = Make(Op::Literal);
            node->literal = number;
            return node;
        }
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'))
            Fail("unexpected character");
        const std::string name = Identifier();
        Node node = Make(Op::Literal);
        if (name == "true")
            node->literal = true;
        else if (name == "false")
            node->literal = false;
        else if (name == "null")
            node->literal = Json();
        else
        {
            Skip();
            if (pos_ < text_.size() && text_[pos_] == '(')
                return Call(name);
            return Name(name);
        }
        return node;
    }

    Node Call(const std::string &name)
    {
        ++pos_;
        const auto &functions = Functions();
        const auto found = std::find_if(functions.begin(), functions.end(),
                                        [&name](const auto &entry) { return entry.first == name; });
        if (found == functions.end())
            Fail(("unknown function " + name).c_str());
        Node node = Make(Op::Call);
        node->function = found->second;
        // opt(path) and or(…): explicitly optional, their paths may name nothing
        const bool optional = node->function == Function::Or || node->function == Function::Opt;
        optional_ += optional;
        if (!Eat(")"))
        {
            do
                node->args.push_back(Ternary());
            while (Eat(","));
            if (!Eat(")"))
                Fail("expected ')'");
        }
        optional_ -= optional;
        if (node->function == Function::Tr)
            reads_.strings = true;
        // A nested component's prop takes the structure itself: json(x) is x.
        if (node->function == Function::Json && values_)
        {
            if (node->args.size() != 1)
                Fail("json() takes one value");
            return std::move(node->args[0]);
        }
        return node;
    }

    std::string_view text_;
    TemplateNames &names_;
    const std::vector<std::string> &loops_;
    bool values_;
    Reads &reads_;
    size_t pos_ = 0;
    int optional_ = 0;
};

// A value of an expression: a reference into the scope where it is one (a path), else its own.
struct Value
{
    const Json *ref = nullptr;
    Json own;
    const Json &Get() const
    {
        return ref ? *ref : own;
    }
    Json Take() &&
    {
        return ref ? *ref : std::move(own);
    }
};

Value Own(Json value)
{
    Value out;
    out.own = std::move(value);
    return out;
}

const Json &None()
{
    static const Json none;
    return none;
}

class Evaluator
{
  public:
    Evaluator(const Scope &scope, const std::string &text) : scope_(scope), text_(text)
    {
    }

    Value Run(const ExpressionNode &node) const
    {
        switch (node.op)
        {
        case Op::Literal: {
            Value out;
            out.ref = &node.literal;
            return out;
        }
        case Op::Name: {
            const Json *found =
                static_cast<size_t>(node.slot) < scope_.names.size() ? scope_.names[node.slot] : nullptr;
            Value out;
            out.ref = found ? found : Undefined(node);
            return out;
        }
        case Op::LoopItem: {
            const LoopFrame &frame = scope_.loops.at(static_cast<size_t>(node.slot));
            Value out;
            out.ref = &(*frame.list)[frame.index];
            return out;
        }
        case Op::LoopIndex:
            return Own(scope_.loops.at(static_cast<size_t>(node.slot)).index);
        case Op::LoopLast: {
            const LoopFrame &frame = scope_.loops.at(static_cast<size_t>(node.slot));
            return Own(frame.index + 1 == frame.list->size());
        }
        case Op::Member:
        case Op::Index: {
            Value object = Run(*node.args[0]);
            const Json *member = nullptr;
            const Json &from = object.Get();
            if (node.op == Op::Member)
                member = MemberRef(from, node.literal.get_ref<const std::string &>());
            else
            {
                const Value index = Run(*node.args[1]);
                const Json &at = index.Get();
                if (at.is_number() && from.is_array())
                {
                    const auto i = static_cast<long long>(at.get<double>());
                    member = i >= 0 && static_cast<size_t>(i) < from.size() ? &from[static_cast<size_t>(i)] : nullptr;
                }
                else
                    member = MemberRef(from, ToText(at));
            }
            if (!member)
                member = Undefined(node);
            if (object.ref)
            {
                Value out;
                out.ref = member;
                return out;
            }
            return Own(*member);
        }
        case Op::Not:
            return Own(!Truthy(Run(*node.args[0]).Get()));
        case Op::Negate:
            return Own(-ToNumber(Run(*node.args[0]).Get()));
        case Op::And:
        case Op::Or: {
            Value left = Run(*node.args[0]);
            if (Truthy(left.Get()) == (node.op == Op::Or))
                return left;
            return Run(*node.args[1]);
        }
        case Op::Choose:
            return Run(*node.args[Truthy(Run(*node.args[0]).Get()) ? 1 : 2]);
        case Op::Call:
            return Call(node);
        case Op::Array: {
            Json list = Json::array();
            for (const auto &item : node.args)
                list.push_back(Run(*item).Take());
            return Own(std::move(list));
        }
        case Op::Object: {
            Json object = Json::object();
            for (size_t i = 0; i < node.args.size(); ++i)
                object[node.literal[i].get<std::string>()] = Run(*node.args[i]).Take();
            return Own(std::move(object));
        }
        default:
            break;
        }
        const Value a = Run(*node.args[0]);
        const Value b = Run(*node.args[1]);
        const Json &left = a.Get(), &right = b.Get();
        switch (node.op)
        {
        case Op::Add:
            if (left.is_string() || right.is_string())
                return Own(ToText(left) + ToText(right));
            return Own(ToNumber(left) + ToNumber(right));
        case Op::Sub:
            return Own(ToNumber(left) - ToNumber(right));
        case Op::Mul:
            return Own(ToNumber(left) * ToNumber(right));
        case Op::Div: {
            const double divisor = ToNumber(right);
            return Own(divisor == 0.0 ? 0.0 : ToNumber(left) / divisor);
        }
        case Op::Mod: {
            const double divisor = ToNumber(right);
            return Own(divisor == 0.0 ? 0.0 : std::fmod(ToNumber(left), divisor));
        }
        case Op::Eq:
            return Own(LooseEqual(left, right));
        case Op::Ne:
            return Own(!LooseEqual(left, right));
        case Op::Lt:
            return Own(ToNumber(left) < ToNumber(right));
        case Op::Le:
            return Own(ToNumber(left) <= ToNumber(right));
        case Op::Gt:
            return Own(ToNumber(left) > ToNumber(right));
        case Op::Ge:
            return Own(ToNumber(left) >= ToNumber(right));
        default:
            throw std::runtime_error("template expression: bad node in '" + text_ + "'");
        }
    }

  private:
    // The member, or nullptr when the object has no such key (or is no object).
    static const Json *MemberRef(const Json &object, const std::string &key)
    {
        if (object.is_object())
        {
            const auto found = object.find(key);
            return found != object.end() ? &*found : nullptr;
        }
        return nullptr;
    }

    // A path that names nothing: an error unless the expression marks it optional (opt(), or()), then null.
    const Json *Undefined(const ExpressionNode &node) const
    {
        if (node.optional)
            return &None();
        throw std::runtime_error("template expression: '" + node.path + "' is not defined in '" + text_ +
                                 "' (opt() marks a path that may be missing)");
    }

    Value Call(const ExpressionNode &node) const
    {
        const auto &a = node.args;
        // or() returns one of its arguments as it is, evaluated in turn.
        if (node.function == Function::Or) // first non-null argument: or(size, 'md')
        {
            for (const auto &argument : a)
            {
                Value value = Run(*argument);
                const Json &v = value.Get();
                if (!v.is_null() && !(v.is_string() && v.get_ref<const std::string &>().empty()))
                    return value;
            }
            return Own(Json());
        }
        if (node.function == Function::Opt) // opt(path): the value, null when the path names nothing
            return a.empty() ? Own(Json()) : Run(*a[0]);
        std::vector<Value> values;
        values.reserve(a.size());
        for (const auto &argument : a)
            values.push_back(Run(*argument));
        const auto arg = [&values](size_t i) -> const Json & { return i < values.size() ? values[i].Get() : None(); };
        switch (node.function)
        {
        case Function::Len:
            return Own(arg(0).is_string() ? arg(0).get_ref<const std::string &>().size()
                                          : (arg(0).is_structured() ? arg(0).size() : size_t(0)));
        case Function::Num: // amount with grouped thousands
            return Own(GroupDigits(arg(0)));
        case Function::Str:
            return Own(ToText(arg(0)));
        case Function::Int:
            return Own(std::floor(ToNumber(arg(0))));
        case Function::Round:
            return Own(std::round(ToNumber(arg(0))));
        case Function::Min:
            return Own(std::min(ToNumber(arg(0)), ToNumber(arg(1))));
        case Function::Max:
            return Own(std::max(ToNumber(arg(0)), ToNumber(arg(1))));
        case Function::Clamp:
            return Own(std::min(std::max(ToNumber(arg(0)), ToNumber(arg(1))), ToNumber(arg(2))));
        case Function::Abs:
            return Own(std::fabs(ToNumber(arg(0))));
        case Function::IsObject:
            return Own(arg(0).is_object());
        case Function::IsArray:
            return Own(arg(0).is_array());
        case Function::IsString:
            return Own(arg(0).is_string());
        case Function::Has:
            return Own(arg(0).is_object() && arg(0).contains(ToText(arg(1))));
        case Function::In: // in(value, 'a', 'b', …)
            for (size_t i = 1; i < values.size(); ++i)
                if (LooseEqual(arg(0), arg(i)))
                    return Own(true);
            return Own(false);
        case Function::Starts:
            return Own(ToText(arg(0)).rfind(ToText(arg(1)), 0) == 0);
        case Function::Contains:
            return Own(ToText(arg(0)).find(ToText(arg(1))) != std::string::npos);
        case Function::Ends: {
            const std::string text = ToText(arg(0)), tail = ToText(arg(1));
            return Own(text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0);
        }
        case Function::Replace: { // replace(text, from, to): every occurrence
            std::string text = ToText(arg(0));
            const std::string from = ToText(arg(1)), to = ToText(arg(2));
            if (!from.empty())
                for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
                    text.replace(at, from.size(), to);
            return Own(text);
        }
        case Function::Split: { // split('2/3', '/') → ['2', '3']
            const std::string text = ToText(arg(0)), separator = ToText(arg(1));
            Json parts = Json::array();
            size_t at = 0;
            while (!separator.empty())
            {
                const size_t end = text.find(separator, at);
                if (end == std::string::npos)
                    break;
                parts.push_back(text.substr(at, end - at));
                at = end + separator.size();
            }
            parts.push_back(text.substr(at));
            return Own(std::move(parts));
        }
        case Function::Range: { // range(3) → [0, 1, 2]
            Json list = Json::array();
            const int count = static_cast<int>(std::min(ToNumber(arg(0)), 1000.0));
            for (int i = 0; i < count; ++i)
                list.push_back(i);
            return Own(std::move(list));
        }
        case Function::Digits: { // number written with spaces or thin spaces → number
            std::string kept;
            for (const char c : ToText(arg(0)))
                if ((c >= '0' && c <= '9') || c == '-' || c == '.')
                    kept += c;
                else if (c == ',')
                    kept += '.';
            return Own(kept.empty() ? Json() : Json(std::strtod(kept.c_str(), nullptr)));
        }
        case Function::Json:
            return Own(arg(0).dump());
        case Function::Tr: // tr('trade.price'): the UI string of the current language
            if (!Strings())
                throw std::runtime_error("template expression: tr() without a string table in '" + text_ + "'");
            return Own(Strings()(ToText(arg(0))));
        case Function::ImageKind: { // image_kind(image, icon): the lead of a row (one row-image API)
            // image = {kind: icon|token|item|portrait|emblem, icon, enamel, texture, cell}; a bare
            // `icon` is the shorthand for {kind: 'icon'}; nothing at all = no lead ('').
            const Json &image = arg(0);
            if (image.is_object())
            {
                const std::string kind = image.contains("kind") ? ToText(image["kind"]) : std::string();
                if (!kind.empty())
                    return Own(kind);
                if (!ToText(image.value("texture", Json())).empty())
                    return Own("item");
                if (!ToText(image.value("enamel", Json())).empty())
                    return Own("token");
                if (!ToText(image.value("icon", Json())).empty())
                    return Own("icon");
            }
            return Own(ToText(arg(1)).empty() ? "" : "icon");
        }
        case Function::Pick: { // pick(key, 'a', x, 'b', y, fallback)
            size_t i = 1;
            for (; i + 1 < values.size(); i += 2)
                if (LooseEqual(arg(0), arg(i)))
                    return Own(arg(i + 1));
            return Own(i < values.size() ? arg(i) : Json());
        }
        default:
            throw std::runtime_error("template expression: bad call in '" + text_ + "'");
        }
    }

    const Scope &scope_;
    const std::string &text_;
};

} // namespace

int TemplateNames::Find(const std::string &name) const
{
    const auto found = std::find(names_.begin(), names_.end(), name);
    return found == names_.end() ? -1 : static_cast<int>(found - names_.begin());
}

int TemplateNames::Add(const std::string &name)
{
    if (const int found = Find(name); found >= 0)
        return found;
    if (names_.size() >= kMax)
        throw std::runtime_error("template: more than " + std::to_string(kMax) + " names in one component");
    names_.push_back(name);
    return static_cast<int>(names_.size() - 1);
}

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(Expression &&) noexcept = default;
Expression &Expression::operator=(Expression &&) noexcept = default;

Expression Expression::Compile(std::string_view text, TemplateNames &names, const std::vector<std::string> &loops,
                               bool values)
{
    Expression expression;
    expression.text_ = std::string(text);
    expression.root_ = Parser(text, names, loops, values, expression.reads_).Run();
    return expression;
}

Json Expression::Evaluate(const Scope &scope) const
{
    return Evaluator(scope, text_).Run(*root_).Take();
}

namespace
{
// The walk of an expression over the spec shapes (component_spec.h; a shape's values as screens/script_props.cpp
// converts them): where a path step leaves its shape, the template is wrong for every value it can be given.
class ShapeWalk
{
  public:
    ShapeWalk(const Shapes &shapes, const std::string &text) : shapes_(shapes), text_(text)
    {
    }

    const Json *Run(const ExpressionNode &node)
    {
        switch (node.op)
        {
        case Op::Literal:
        case Op::LoopIndex:
        case Op::LoopLast:
            return nullptr;
        case Op::Name:
            if (static_cast<size_t>(node.slot) >= shapes_.names.size())
                Fail("'" + node.path + "' is not a prop");
            return shapes_.names[static_cast<size_t>(node.slot)];
        case Op::LoopItem:
            return shapes_.loops.at(static_cast<size_t>(node.slot));
        case Op::Member:
            return Field(Run(*node.args[0]), node.literal.get_ref<const std::string &>(), node.path);
        case Op::Index: {
            const Json *base = Structure(Run(*node.args[0]));
            Run(*node.args[1]);
            if (base && base->is_array())
                return Structure(&base->at(0));
            if (base && base->is_object() && node.args[1]->op == Op::Literal && node.args[1]->literal.is_string())
                return Field(base, node.args[1]->literal.get_ref<const std::string &>(), node.path);
            return nullptr;
        }
        case Op::Call:
            return Call(node);
        case Op::Choose: {
            Run(*node.args[0]);
            const Json *yes = Run(*node.args[1]), *no = Run(*node.args[2]);
            return !yes ? no : !no || *yes == *no ? yes : nullptr;
        }
        default:
            for (const auto &arg : node.args)
                Run(*arg);
            return nullptr;
        }
    }

    // What a shape holds where a template looks into it: {$null: s} as s, a table cell's {$either: [text, s]} as
    // s, the converted fields of a picture or a texture, "numbers" as a list. Null stays null.
    const Json *Structure(const Json *shape)
    {
        if (!shape || !shape->is_object() || shape->size() != 1)
        {
            if (shape && *shape == "numbers")
                return &Keep(Json::array({"number"}));
            if (shape && *shape == "picture")
                return &Keep(Json{{"kind", "text"}, {"texture", "text"}, {"icon", "text"}, {"enamel", "text"}});
            if (shape && *shape == "texture")
                return &Keep(Json{{"texture", "text"}, {"uv", "text"}});
            return shape;
        }
        if (shape->contains("$null"))
            return Structure(&shape->at("$null"));
        if (shape->contains("$either"))
            return Structure(&shape->at("$either").at(1));
        if (shape->contains("$texture"))
        {
            Json fields = shape->at("$texture");
            fields["texture"] = "text";
            fields["uv"] = "text";
            return &Keep(std::move(fields));
        }
        return shape;
    }

  private:
    [[noreturn]] void Fail(const std::string &what) const
    {
        throw std::runtime_error("template expression: " + what + " in '" + text_ + "'");
    }

    const Json &Keep(Json shape)
    {
        return shapes_.made.emplace_back(std::move(shape));
    }

    const Json *Field(const Json *shape, const std::string &key, const std::string &path)
    {
        const Json *base = Structure(shape);
        if (!base)
            return nullptr;
        if (base->is_object())
        {
            const auto found = base->find(key);
            if (found == base->end())
                Fail("'" + path + "': its shape has no field '" + key + "'");
            return &*found;
        }
        Fail("'" + path + "': its shape is " + (base->is_array() ? std::string("a list") : base->dump()) +
             ", not fields");
    }

    const Json *Call(const ExpressionNode &node)
    {
        std::vector<const Json *> args;
        for (const auto &arg : node.args)
            args.push_back(Run(*arg));
        switch (node.function)
        {
        case Function::Opt:
        case Function::Or:
        case Function::Json:
            return args.empty() ? nullptr : args[0];
        case Function::Has:
            if (args.size() == 2 && node.args[1]->op == Op::Literal && node.args[1]->literal.is_string())
                Field(args[0], node.args[1]->literal.get_ref<const std::string &>(), node.args[0]->path);
            return nullptr;
        default:
            return nullptr;
        }
    }

    const Shapes &shapes_;
    const std::string &text_;
};
} // namespace

const Json *Expression::Shape(const Shapes &shapes) const
{
    return ShapeWalk(shapes, text_).Run(*root_);
}

std::string TemplateText::Evaluate(const Scope &scope, bool shown) const
{
    std::string out = literals[0];
    for (size_t i = 0; i < holes.size(); ++i)
    {
        const std::string value = ToText(holes[i].Evaluate(scope));
        out += shown ? BindShortWords(value) : value;
        out += literals[i + 1];
    }
    return out;
}

Json EvaluateExpression(std::string_view expression, const Json &scope)
{
    TemplateNames names;
    const Expression compiled = Expression::Compile(expression, names, {});
    std::vector<const Json *> values;
    for (const std::string &name : names.All())
    {
        const auto found = scope.is_object() ? scope.find(name) : scope.end();
        values.push_back(scope.is_object() && found != scope.end() ? &*found : nullptr);
    }
    const std::vector<LoopFrame> loops;
    return compiled.Evaluate(Scope{values, loops});
}

// --- the template's markup -----------------------------------------------------------------------------------

namespace
{

bool IsCodeDrawn(const std::string &tag)
{
    return tag.rfind("ui-", 0) == 0 && tag != "ui-wheel"; // the wheel only places the template's children
}

class TemplateParser
{
  public:
    TemplateParser(std::string_view source, const ComponentSpec &spec, TemplateNames &names,
                   const std::function<const ComponentSpec *(const std::string &)> &component)
        : s_(source), names_(names), component_(component)
    {
        static const Json text = "text", flag = "bool", whole = "int", number = "number";
        for (const PropSpec &prop : spec.props)
            name_shapes_.push_back(prop.type == PropSpec::Type::Json   ? (prop.shape.is_null() ? nullptr : &prop.shape)
                                   : prop.type == PropSpec::Type::Bool ? &flag
                                   : prop.type == PropSpec::Type::Int  ? &whole
                                   : prop.type == PropSpec::Type::Number ? &number
                                                                         : &text);
        name_shapes_.resize(name_shapes_.size() + static_cast<size_t>(SpecName::Count)); // the host's facts
    }

    // Checks the expression's paths against the shapes; its shape.
    const Json *Check(const Expression &expression)
    {
        return expression.Shape(Shapes{name_shapes_, loop_shapes_, made_});
    }

    CompiledTemplate Run()
    {
        CompiledTemplate out;
        Nodes(out.nodes, {}, nullptr);
        for (const TemplateNode &node : out.nodes)
            out.reads |= node.reads;
        return out;
    }

  private:
    [[noreturn]] void Fail(size_t at, const std::string &what) const
    {
        const int line = 1 + static_cast<int>(std::count(s_.begin(), s_.begin() + std::min(at, s_.size()), '\n'));
        throw std::runtime_error("line " + std::to_string(line) + ": " + what);
    }

    int Line(size_t at) const
    {
        return 1 + static_cast<int>(std::count(s_.begin(), s_.begin() + std::min(at, s_.size()), '\n'));
    }

    bool At(std::string_view token) const
    {
        return s_.substr(pos_, token.size()) == token;
    }

    // The position of the "}}" closing the hole whose expression starts at `from`: quotes and braces inside the
    // expression (an object literal) are skipped.
    size_t HoleEnd(size_t from) const
    {
        int depth = 0;
        for (size_t at = from; at + 1 < s_.size(); ++at)
        {
            const char c = s_[at];
            if (c == '\'' || c == '"')
            {
                const size_t end = s_.find(c, at + 1);
                if (end == std::string_view::npos)
                    break;
                at = end;
            }
            else if (c == '{')
                ++depth;
            else if (c == '}')
            {
                if (depth == 0 && s_[at + 1] == '}')
                    return at;
                if (depth > 0)
                    --depth;
            }
        }
        Fail(from, "unterminated {{");
    }

    // Literal text and holes from `from` up to `end` (a text run or an attribute value).
    TemplateText Text(size_t from, size_t end, bool values)
    {
        TemplateText text;
        std::string literal;
        size_t at = from;
        while (at < end)
        {
            const size_t open = s_.find("{{", at);
            if (open == std::string_view::npos || open >= end)
            {
                literal += s_.substr(at, end - at);
                break;
            }
            if (s_.substr(open, 3) == "{{{")
                Fail(open, "a raw insertion {{{ }}}: write the attribute as name?=\"{{ expr }}\"");
            literal += s_.substr(at, open - at);
            text.literals.push_back(Rml::StringUtilities::DecodeRml(literal));
            literal.clear();
            const size_t close = HoleEnd(open + 2);
            try
            {
                text.holes.push_back(
                    Expression::Compile(s_.substr(open + 2, close - open - 2), names_, loops_, values));
                Check(text.holes.back());
            }
            catch (const std::runtime_error &error)
            {
                Fail(open, error.what());
            }
            text.reads |= text.holes.back().Read();
            at = close + 2;
        }
        text.literals.push_back(Rml::StringUtilities::DecodeRml(literal));
        return text;
    }

    // The end of a text run: the next '<' outside a hole.
    size_t TextEnd(size_t from) const
    {
        size_t at = from;
        while (at < s_.size())
        {
            const size_t tag = s_.find('<', at), hole = s_.find("{{", at);
            if (hole == std::string_view::npos || (tag != std::string_view::npos && tag < hole))
                return tag == std::string_view::npos ? s_.size() : tag;
            at = HoleEnd(hole + 2) + 2;
        }
        return s_.size();
    }

    void SkipSpace()
    {
        while (pos_ < s_.size() && Rml::StringUtilities::IsWhitespace(s_[pos_]))
            ++pos_;
    }

    std::string Word()
    {
        const size_t start = pos_;
        while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '-' ||
                                    s_[pos_] == '_' || s_[pos_] == ':'))
            ++pos_;
        return std::string(s_.substr(start, pos_ - start));
    }

    // name="value" pairs up to '>' or "/>"; true when the tag closes itself. `values`: the positions of each value.
    bool Attributes(std::vector<std::pair<std::string, std::pair<size_t, size_t>>> &out, bool control)
    {
        while (true)
        {
            SkipSpace();
            if (pos_ >= s_.size())
                Fail(pos_, "unterminated tag");
            if (At(">"))
            {
                ++pos_;
                return false;
            }
            if (At("/>"))
            {
                pos_ += 2;
                return true;
            }
            if (At("{{") || At("<t-"))
                Fail(pos_, "an insertion or a control tag inside a tag: write the attribute as name?=\"{{ expr }}\"");
            const size_t start = pos_;
            std::string name = Word();
            if (At("?"))
            {
                name += '?';
                ++pos_;
            }
            if (name.empty())
                Fail(start, "an attribute name expected");
            SkipSpace();
            if (!At("="))
            {
                out.push_back({name, {pos_, pos_}});
                continue;
            }
            ++pos_;
            SkipSpace();
            const char quote = pos_ < s_.size() ? s_[pos_] : 0;
            if (quote != '"' && quote != '\'')
                Fail(pos_, "attribute " + name + ": a quoted value expected");
            const size_t value = ++pos_;
            while (pos_ < s_.size() && s_[pos_] != quote)
                pos_ = !control && At("{{") ? HoleEnd(pos_ + 2) + 2 : pos_ + 1;
            if (pos_ >= s_.size())
                Fail(value, "attribute " + name + ": unterminated value");
            out.push_back({name, {value, pos_}});
            ++pos_;
        }
    }

    // Nodes up to "</closing>" (consumed; empty: the end of the source). In a t-if, <t-else/> switches to
    // `otherwise`.
    void Nodes(std::vector<TemplateNode> &out, const std::string &closing, std::vector<TemplateNode> *otherwise)
    {
        std::vector<TemplateNode> *target = &out;
        while (pos_ < s_.size())
        {
            if (!At("<"))
            {
                const size_t end = TextEnd(pos_);
                TemplateText text = Text(pos_, end, false);
                const bool blank =
                    text.holes.empty() && std::all_of(text.literals[0].begin(), text.literals[0].end(),
                                                      [](char c) { return Rml::StringUtilities::IsWhitespace(c); });
                if (!blank) // RmlUi makes no element of white space alone
                {
                    TemplateNode node;
                    node.kind = TemplateNode::Kind::Text;
                    node.line = Line(pos_);
                    node.reads = text.reads;
                    node.text = std::move(text);
                    target->push_back(std::move(node));
                }
                pos_ = end;
                continue;
            }
            if (At("<!--"))
            {
                const size_t end = s_.find("-->", pos_);
                if (end == std::string_view::npos)
                    Fail(pos_, "unterminated comment");
                pos_ = end + 3;
                continue;
            }
            if (At("</"))
            {
                const size_t start = pos_;
                pos_ += 2;
                const std::string name = Word();
                SkipSpace();
                if (!At(">"))
                    Fail(start, "bad closing tag");
                ++pos_;
                if (closing.empty() || Rml::StringUtilities::ToLower(name) != closing)
                    Fail(start,
                         "</" + name + "> closes nothing open" + (closing.empty() ? "" : ", <" + closing + "> is"));
                return;
            }
            const size_t start = pos_;
            ++pos_;
            const std::string tag = Rml::StringUtilities::ToLower(Word());
            if (tag.empty())
                Fail(start, "a tag name expected");
            if (tag == "t-else")
            {
                std::vector<std::pair<std::string, std::pair<size_t, size_t>>> none;
                if (!otherwise || target == otherwise || !Attributes(none, true) || !none.empty())
                    Fail(start, "<t-else/> outside a <t-if>");
                target = otherwise;
                continue;
            }
            TemplateNode node;
            node.line = Line(start);
            if (tag == "t-if" || tag == "t-for")
                Control(node, tag, start);
            else
                Element(node, tag, start);
            target->push_back(std::move(node));
        }
        if (!closing.empty())
            Fail(s_.size(), "<" + closing + "> is not closed");
    }

    std::string ControlAttribute(const std::vector<std::pair<std::string, std::pair<size_t, size_t>>> &attributes,
                                 const std::string &name, size_t at, bool required) const
    {
        for (const auto &[key, span] : attributes)
            if (key == name)
                return std::string(s_.substr(span.first, span.second - span.first));
        if (required)
            Fail(at, "control tag lacks attribute " + name);
        return {};
    }

    Expression ControlExpression(const std::string &text, size_t at)
    {
        try
        {
            Expression expression = Expression::Compile(text, names_, loops_);
            Check(expression);
            return expression;
        }
        catch (const std::runtime_error &error)
        {
            Fail(at, error.what());
        }
    }

    void Control(TemplateNode &node, const std::string &tag, size_t start)
    {
        std::vector<std::pair<std::string, std::pair<size_t, size_t>>> attributes;
        if (Attributes(attributes, true))
            Fail(start, "<" + tag + "> cannot close itself");
        if (tag == "t-if")
        {
            node.kind = TemplateNode::Kind::If;
            node.test = ControlExpression(ControlAttribute(attributes, "test", start, true), start);
            node.reads = node.test.Read();
            Nodes(node.children, tag, &node.otherwise);
            for (const auto *branch : {&node.children, &node.otherwise})
                for (const TemplateNode &child : *branch)
                    node.reads |= child.reads;
            return;
        }
        node.kind = TemplateNode::Kind::For;
        node.each = ControlExpression(ControlAttribute(attributes, "each", start, true), start);
        node.reads = node.each.Read();
        loops_.push_back(ControlAttribute(attributes, "as", start, true));
        const Json *each = ShapeWalk(Shapes{name_shapes_, loop_shapes_, made_}, {}).Structure(Check(node.each));
        if (each && !each->is_array())
            Fail(start, "<t-for> each: its shape is " + each->dump() + ", not a list");
        loop_shapes_.push_back(each ? &each->at(0) : nullptr);
        if (const std::string key = ControlAttribute(attributes, "key", start, false); !key.empty())
        {
            node.key = ControlExpression(key, start);
            node.reads |= node.key.Read();
        }
        Nodes(node.children, tag, nullptr);
        loops_.pop_back();
        loop_shapes_.pop_back();
        for (const TemplateNode &child : node.children)
            node.reads |= child.reads;
    }

    void Element(TemplateNode &node, const std::string &tag, size_t start)
    {
        node.kind = TemplateNode::Kind::Element;
        node.tag = tag;
        node.component = component_(tag);
        if (!node.component && (tag.rfind("c-", 0) == 0 || tag.rfind("s-", 0) == 0))
            Fail(start, "unknown component <" + tag + ">: no spec in components.json");
        std::vector<std::pair<std::string, std::pair<size_t, size_t>>> attributes;
        const bool closed = Attributes(attributes, false);
        bool slot = false;
        for (const auto &[written, span] : attributes)
        {
            TemplateAttribute attribute;
            attribute.conditional = !written.empty() && written.back() == '?';
            attribute.name = attribute.conditional ? written.substr(0, written.size() - 1) : written;
            slot = slot || attribute.name == "slot";
            if (node.component)
            {
                attribute.props = attribute.name == "props";
                attribute.prop = attribute.props ? nullptr : node.component->FindAttribute(attribute.name);
                if (!attribute.props && !attribute.prop && !IsGenericAttribute(attribute.name))
                    Fail(span.first, "<" + tag + "> has no prop '" + attribute.name + "'");
            }
            const bool structure = attribute.props || (attribute.prop && attribute.prop->type == PropSpec::Type::Json);
            attribute.value = Text(span.first, span.second, structure);
            const bool single = attribute.value.holes.size() == 1 && attribute.value.literals[0].empty() &&
                                attribute.value.literals[1].empty();
            if (attribute.conditional && !single)
                Fail(span.first, "a conditional attribute " + attribute.name + "?= takes one {{ expr }} alone");
            if (structure && !single)
            {
                if (!attribute.value.holes.empty())
                    Fail(span.first, "<" + tag + "> " + attribute.name + " takes one {{ expr }} or JSON text");
                attribute.fixed = Json::parse(attribute.value.literals[0], nullptr, false);
                if (attribute.fixed.is_discarded() || (attribute.props && !attribute.fixed.is_object()))
                    Fail(span.first, "<" + tag + "> " + attribute.name + " is not JSON");
            }
            node.reads |= attribute.value.reads;
            node.attributes.push_back(std::move(attribute));
        }
        if (closed)
            return;
        Nodes(node.children, tag, nullptr);
        if (!node.children.empty() && (slot || IsCodeDrawn(tag)))
            Fail(start, "<" + tag + "> owns its children: the template gives it none");
        for (const TemplateNode &child : node.children)
            node.reads |= child.reads;
    }

    std::string_view s_;
    TemplateNames &names_;
    const std::function<const ComponentSpec *(const std::string &)> &component_;
    std::vector<std::string> loops_;
    std::vector<const Json *> name_shapes_, loop_shapes_;
    std::deque<Json> made_;
    size_t pos_ = 0;
};

} // namespace

CompiledComponent CompileComponent(const ComponentSpec &spec,
                                   const std::function<const ComponentSpec *(const std::string &tag)> &component)
{
    CompiledComponent out;
    out.spec = &spec;
    for (const PropSpec &prop : spec.props)
        out.names.Add(prop.name);
    for (const char *name : {"spec", "children", "dp", "safe"})
        out.names.Add(name);
    try
    {
        TemplateParser parser(spec.body, spec, out.names, component);
        out.compiled = parser.Run();
        for (const auto &[name, rule] : spec.class_rules)
        {
            out.classes.emplace_back(name, Expression::Compile(rule, out.names, {}));
            parser.Check(out.classes.back().second);
        }
    }
    catch (const std::runtime_error &error)
    {
        throw std::runtime_error("template of <" + spec.tag + ">: " + error.what());
    }
    return out;
}

} // namespace storm::ui
