#include "natives_texts.h"

#include "compiler.h"
#include "core_impl.h"
#include "s_deftab.h"
#include "s_functab.h"
#include "s_vartab.h"
#include "script_texts.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace storm::texts
{
namespace
{
TextTemplates *g_templates = nullptr;
}

void InstallTextTemplates(TextTemplates *templates)
{
    g_templates = templates;
}

void SetScriptLanguage(std::string language)
{
    core_internal.Compiler->SetScriptLanguage(std::move(language));
}
} // namespace storm::texts

namespace storm::natives
{
namespace
{
using texts::ScriptArg;
using texts::Who;

std::string scalarText(DATA *v)
{
    v = v ? v->GetVarPointer() : nullptr;
    if (!v)
        return {};
    switch (v->GetType())
    {
    case VAR_INTEGER:
        return std::to_string(v->GetInt());
    case VAR_FLOAT:
        return fmt::format("{}", v->GetFloat());
    case VAR_STRING: {
        const char *s = v->GetString();
        return s ? s : "";
    }
    default:
        return {};
    }
}

// The context argument: an object, a reference to one or an attribute reference; nullptr, NullObject or a missing
// argument give no context.
ATTRIBUTES *contextOf(DATA *v)
{
    if (!v || (v->GetType() == VAR_REFERENCE && !v->GetReference()))
        return nullptr;
    v = v->GetVarPointer();
    if (!v || (v->GetType() != VAR_OBJECT && v->GetType() != VAR_AREFERENCE))
        return nullptr;
    return v->GetAClass();
}

class VmAccess final : public texts::ScriptAccess
{
  public:
    VmAccess(NativeCall &call, ATTRIBUTES *context) : call_(call), context_(context)
    {
    }

    std::optional<std::string> Attribute(Who who, std::string_view path) override
    {
        ATTRIBUTES *root = object(who);
        ATTRIBUTES *a = root ? root->FindAClass(root, std::string(path).c_str()) : nullptr;
        if (!a || !a->HasValue())
            return std::nullopt;
        return a->GetValue();
    }

    std::optional<std::string> Call(std::string_view function, std::span<const ScriptArg> args) override
    {
        const size_t code = call_.functions.FindFunc(function);
        const FuncInfo *fi = code == INVALID_FUNC_CODE ? nullptr : call_.functions.GetFuncPtr(code);
        if (!fi || fi->segment_id == INTERNAL_SEGMENT_ID || fi->segment_id == IMPORTED_SEGMENT_ID)
            return std::nullopt;
        // the last arguments may be left to the function's defaults
        if (args.size() > fi->arguments || args.size() + fi->default_args.size() < fi->arguments)
        {
            call_.compiler.SetError("template calls %s with %zu arguments, it takes %u", fi->name.c_str(), args.size(),
                                    fi->arguments);
            return std::string{};
        }
        const uint32_t before = call_.stack.GetDataNum();
        std::deque<DATA> texts; // the text a `ref` parameter refers to, for the call
        for (uint32_t n = 0; n < args.size(); ++n)
        {
            DATA *v = call_.stack.Push();
            const ScriptArg &arg = args[n];
            if (arg.kind != ScriptArg::Kind::Text)
            {
                pushReference(v, arg.kind);
                continue;
            }
            const S_TOKEN_TYPE type = n < fi->local_vars.size() ? fi->local_vars[n].type : VAR_STRING;
            if (type == VAR_REFERENCE)
            {
                DATA &text = texts.emplace_back();
                text.SetVCompiler(&call_.compiler);
                text.Set(arg.text);
                v->SetType(VAR_REFERENCE);
                v->SetReference(&text);
                continue;
            }
            v->Set(arg.text);
            if (type == VAR_INTEGER || type == VAR_FLOAT)
                v->Convert(type);
        }
        DATA *result = nullptr;
        const bool ok =
            call_.compiler.BC_Execute(static_cast<uint32_t>(code), result, nullptr, static_cast<uint32_t>(args.size()));
        std::string text = ok && result ? scalarText(result) : std::string{};
        if (call_.stack.GetDataNum() > before)
            call_.stack.InvalidateFrom(before);
        return text;
    }

    std::optional<std::string> Value(std::string_view name) override
    {
        DEFINFO di;
        const uint32_t code = call_.defines.FindDef(std::string(name).c_str());
        if (code == INVALID_DEF_CODE || !call_.defines.GetDef(di, code))
            return Variable(name);
        switch (di.deftype)
        {
        case NUMBER: {
            int32_t value;
            std::memcpy(&value, &di.data4b, sizeof(value));
            return std::to_string(value);
        }
        case FLOAT_NUMBER: {
            float value;
            std::memcpy(&value, &di.data4b, sizeof(value));
            return fmt::format("{}", value);
        }
        case STRING:
            return std::string(reinterpret_cast<const char *>(di.data4b));
        default:
            return std::nullopt;
        }
    }

  private:
    // a global variable, or an attribute of one ("var.attr.sub")
    std::optional<std::string> Variable(std::string_view name) const
    {
        const size_t dot = name.find('.');
        const size_t code = call_.variables.FindVar(name.substr(0, dot));
        const VarInfo *vi = code == INVALID_VAR_CODE ? nullptr : call_.variables.GetVar(code);
        DATA *v = vi && vi->value ? vi->value->GetVarPointer() : nullptr;
        if (!v)
            return std::nullopt;
        if (dot == std::string_view::npos)
        {
            if (v->GetType() != VAR_INTEGER && v->GetType() != VAR_FLOAT && v->GetType() != VAR_STRING)
                return std::nullopt;
            return scalarText(v);
        }
        ATTRIBUTES *root = v->GetType() == VAR_OBJECT || v->GetType() == VAR_AREFERENCE ? v->GetAClass() : nullptr;
        ATTRIBUTES *a = root ? root->FindAClass(root, std::string(name.substr(dot + 1)).c_str()) : nullptr;
        if (!a || !a->HasValue())
            return std::nullopt;
        return a->GetValue();
    }

    DATA *global(Who who) const
    {
        const size_t code = call_.variables.FindVar(who == Who::Player ? "pchar" : "CharacterRef");
        const VarInfo *vi = code == INVALID_VAR_CODE ? nullptr : call_.variables.GetVar(code);
        return vi && vi->value ? vi->value->GetVarPointer() : nullptr;
    }

    ATTRIBUTES *object(Who who) const
    {
        if (who == Who::Context)
            return context_;
        DATA *v = global(who);
        return v ? v->GetAClass() : nullptr;
    }

    void pushReference(DATA *v, ScriptArg::Kind kind) const
    {
        if (kind == ScriptArg::Kind::Context)
        {
            v->SetType(VAR_AREFERENCE);
            v->SetAReference(context_);
            return;
        }
        v->SetType(VAR_REFERENCE);
        v->SetReference(global(kind == ScriptArg::Kind::Player ? Who::Player : Who::Npc));
    }

    NativeCall &call_;
    ATTRIBUTES *context_;
};

// DLG_ReplaceAllMatches(ref input, key, replacement[, from]): every occurrence at or after byte `from`, replacements
// are not searched again.
void replaceAllMatches(std::string &input, std::string_view key, std::string_view replacement, size_t from)
{
    if (key.empty())
        return;
    for (size_t at = input.find(key, from); at != std::string::npos; at = input.find(key, at + replacement.size()))
        input.replace(at, key.size(), replacement);
}
} // namespace

DATA *callTextNative(TextNative native, NativeCall &call, DATA *&result)
{
    result = nullptr;
    const uint32_t base = call.stack.GetDataNum() - call.arguments;
    const auto arg = [&](uint32_t n) { return n < call.arguments ? call.stack.Read(base, n) : nullptr; };
    std::string out;
    if (native == TextNative::ReplaceAllMatches)
    {
        DATA *target = arg(0) ? arg(0)->GetVarPointer() : nullptr;
        if (!target || target->GetType() != VAR_STRING)
            call.compiler.SetError("DLG_ReplaceAllMatches: the first argument is not a string variable");
        else
        {
            std::string text = scalarText(target);
            const int32_t from = call.arguments > 3 ? std::max(arg(3)->GetVarPointer()->GetInt(), 0) : 0;
            replaceAllMatches(text, scalarText(arg(1)), scalarText(arg(2)), static_cast<size_t>(from));
            target->Set(text);
        }
    }
    else if (!texts::g_templates)
        call.compiler.SetError("localisation natives: no string service");
    else
    {
        switch (native)
        {
        case TextNative::Dlgo: {
            VmAccess scripts(call, contextOf(arg(1)));
            out = texts::g_templates->Expand(scalarText(arg(0)), scripts);
            break;
        }
        case TextNative::Convert:
        case TextNative::ConvertE: {
            VmAccess scripts(call, contextOf(arg(2)));
            out = texts::g_templates->Convert(scalarText(arg(1)), scalarText(arg(0)), native == TextNative::ConvertE,
                                              scripts);
            break;
        }
        case TextNative::StringFromKey: {
            std::vector<std::string> parameters;
            for (uint32_t n = 1; n < call.arguments; ++n)
                parameters.push_back(scalarText(arg(n)));
            out = texts::g_templates->StringFromKey(scalarText(arg(0)), parameters);
            break;
        }
        case TextNative::ReplaceAllMatches:
            break;
        }
    }
    for (uint32_t n = 0; n < call.arguments; ++n)
        call.stack.Pop();
    if (native == TextNative::ReplaceAllMatches)
        return nullptr;
    DATA *v = call.stack.Push();
    v->Set(out);
    return result = v;
}
} // namespace storm::natives
