#pragma once

// The script state of a save (COMPILER::SaveState): written by StateWriter (below), read in two steps.
//
// parseState reads the whole state and checks its structure before anything of the running
// game is touched: CoreImpl refuses a state that does not parse while the game is still there
// (StateFileProblem, ProcessStateLoading). Every value is consumed by the type the save wrote,
// so one variable the program cannot take no longer shifts the reading of all later ones.
//
// applyVariables then puts the values into the freshly loaded program in three passes: the
// arrays take their saved sizes, then the values, then the `ref` / `aref` variables. A reference
// is therefore bound to an array that already has its final storage (a resize reallocates the
// elements), and to an element index that exists.
//
// A saved variable the program cannot take (no longer declared, an object saved where the
// program declares a number, an array where it declares a single value, a reference to an
// element that does not exist) is reported and keeps its declared initial value; the other
// variables are restored. A scalar saved with another scalar type than declared is restored
// as saved: the script language changes a variable's type at run time (`int i; i += 0.5;`
// makes i a float, DATA::Plus), and the save wrote that run-time type.
//
// Layout (unchanged since the original engine; sizes are those of the arm64 and x86-64 builds):
//   string            vdword n (0 = null string), n bytes including the terminating zero
//   state             string program directory, vdword n + n strings (name codec),
//                     vdword n + n strings (segments), vdword n + n variables
//   variable          string name, uint32 type, uint32 elements, `elements` values of `type` (an emptied array: 0)
//   value             int32 | float | uintptr_t | string | object | ref | aref
//   object            uint64 legacy entity handle, attribute
//   attribute         vdword children, vdword name code, string value, `children` attributes
//   ref               vdword variable (0xffffffff = empty), vdword element (0xffffffff = none)
//   aref              vdword variable (0xffffffff = empty), vdword element, string path
// The `new` arrays the references reach follow the globals as variables of their own (save_references.hpp); a
// reference to one names its place, kNewArrayPlace + n.

#include "attributes.h"
#include "data.h"
#include "s_vartab.h"
#include "save_references.hpp"
#include "platform/jni_text.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace storm::save
{
inline constexpr uint32_t kNoIndex = 0xffffffff;
// Deeper attribute trees than any game writes; bounds the recursion on a damaged file.
inline constexpr size_t kMaxAttributeDepth = 512;

static_assert(sizeof(S_TOKEN_TYPE) == sizeof(uint32_t), "the save stores the variable type as 4 bytes");

inline const char *typeName(uint32_t type)
{
    switch (type)
    {
    case VAR_INTEGER:
        return "int";
    case VAR_FLOAT:
        return "float";
    case VAR_STRING:
        return "string";
    case VAR_OBJECT:
        return "object";
    case VAR_REFERENCE:
        return "ref";
    case VAR_AREFERENCE:
        return "aref";
    case VAR_PTR:
        return "pointer";
    default:
        return "an unknown type";
    }
}

// The value types a state holds.
inline bool knownType(uint32_t type)
{
    switch (type)
    {
    case VAR_INTEGER:
    case VAR_FLOAT:
    case VAR_STRING:
    case VAR_OBJECT:
    case VAR_REFERENCE:
    case VAR_AREFERENCE:
    case VAR_PTR:
        return true;
    default:
        return false;
    }
}

// A bounds-checked cursor over the state bytes. A read past the end fails and leaves the value.
class StateReader
{
  public:
    StateReader(const char *data, size_t size, size_t position = 0) : data_(data), size_(size), position_(position)
    {
    }

    template <typename T> bool Raw(T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (size_ - position_ < sizeof(T))
            return false;
        memcpy(&value, data_ + position_, sizeof(T));
        position_ += sizeof(T);
        return true;
    }

    bool VDword(uint32_t &value)
    {
        uint8_t first;
        if (!Raw(first))
            return false;
        if (first < 0xfe)
        {
            value = first;
            return true;
        }
        if (first == 0xfe)
        {
            uint16_t word;
            if (!Raw(word))
                return false;
            value = word;
            return true;
        }
        return Raw(value);
    }

    // A null string (length 0) is nullopt. The stored length counts the terminating zero.
    bool String(std::optional<std::string> &value)
    {
        uint32_t length;
        if (!VDword(length))
            return false;
        if (length == 0)
        {
            value.reset();
            return true;
        }
        if (size_ - position_ < length || data_[position_ + length - 1] != '\0')
            return false;
        value.emplace(data_ + position_);
        position_ += length;
        return true;
    }

    size_t Position() const
    {
        return position_;
    }
    size_t Remaining() const
    {
        return size_ - position_;
    }

  private:
    const char *data_;
    size_t size_;
    size_t position_;
};

struct SavedVariable
{
    std::string name;
    uint32_t type = 0;
    uint32_t elements = 0;
    size_t values = 0; // offset of the first value
};

struct ParsedState
{
    std::optional<std::string> programDirectory;
    std::vector<std::string> codec; // name codec strings in saved order (null strings are not registered)
    std::vector<std::string> segments;
    std::vector<SavedVariable> variables;
};

// Where a value goes; NoSink only consumes it.
struct NoSink
{
    void Integer(int32_t)
    {
    }
    void Float(float)
    {
    }
    void Pointer(uintptr_t)
    {
    }
    void String(const std::optional<std::string> &)
    {
    }
    ATTRIBUTES *Object()
    {
        return nullptr;
    }
    void Reference(uint32_t, uint32_t)
    {
    }
    void AReference(uint32_t, uint32_t, const std::optional<std::string> &)
    {
    }
};

namespace detail
{
inline bool fail(std::string &error, const std::string &what, const StateReader &in)
{
    error = what + " (at byte " + std::to_string(in.Position()) + ")";
    return false;
}

// An attribute and its children. `self` (the object's root) takes the name and value itself;
// a child is created under `parent`. Both null: the attribute is only consumed.
inline bool readAttribute(StateReader &in, ATTRIBUTES *self, ATTRIBUTES *parent, size_t depth, std::string &error)
{
    if (depth > kMaxAttributeDepth)
        return fail(error, "attributes nested deeper than " + std::to_string(kMaxAttributeDepth), in);
    uint32_t children, code;
    std::optional<std::string> value;
    if (!in.VDword(children) || !in.VDword(code) || !in.String(value))
        return fail(error, "truncated attribute", in);
    if (value && !jni_text::IsUtf8(*value))
        spdlog::warn("Deserializing invalid utf8 string: {}", *value);
    // Every child takes at least three bytes.
    if (children > in.Remaining() / 3)
        return fail(error, "attribute with " + std::to_string(children) + " children beyond the end of the data", in);
    ATTRIBUTES *node = self;
    if (parent != nullptr)
    {
        parent->SetAttribute(code, value ? value->c_str() : nullptr);
        node = parent->GetAttributeClassByCode(code);
    }
    else if (self != nullptr)
    {
        self->SetNameCode(code);
        self->SetValue(value ? value->c_str() : nullptr);
    }
    for (uint32_t n = 0; n < children; ++n)
        if (!readAttribute(in, nullptr, node, depth + 1, error))
            return false;
    return true;
}
} // namespace detail

// One value of `type`, delivered to `sink`.
template <typename Sink> bool readValue(StateReader &in, uint32_t type, Sink &sink, std::string &error)
{
    switch (type)
    {
    case VAR_INTEGER: {
        int32_t value;
        if (!in.Raw(value))
            return detail::fail(error, "truncated int", in);
        sink.Integer(value);
        return true;
    }
    case VAR_FLOAT: {
        float value;
        if (!in.Raw(value))
            return detail::fail(error, "truncated float", in);
        sink.Float(value);
        return true;
    }
    case VAR_PTR: {
        uintptr_t value;
        if (!in.Raw(value))
            return detail::fail(error, "truncated pointer", in);
        sink.Pointer(value);
        return true;
    }
    case VAR_STRING: {
        std::optional<std::string> value;
        if (!in.String(value))
            return detail::fail(error, "truncated string", in);
        if (value && !jni_text::IsUtf8(*value))
            spdlog::warn("Deserializing invalid utf8 string: {}", *value);
        sink.String(value);
        return true;
    }
    case VAR_OBJECT: {
        // Saves contain script attributes, not the runtime entities they once referenced
        // (OnLoad recreates those): the handle is consumed for file compatibility only.
        uint64_t legacyEntity;
        if (!in.Raw(legacyEntity))
            return detail::fail(error, "truncated object", in);
        return detail::readAttribute(in, sink.Object(), nullptr, 0, error);
    }
    case VAR_REFERENCE: {
        uint32_t variable, element = kNoIndex;
        if (!in.VDword(variable) || (variable != kNoIndex && !in.VDword(element)))
            return detail::fail(error, "truncated ref", in);
        sink.Reference(variable, element);
        return true;
    }
    case VAR_AREFERENCE: {
        uint32_t variable, element = kNoIndex;
        std::optional<std::string> path;
        if (!in.VDword(variable) || (variable != kNoIndex && (!in.VDword(element) || !in.String(path))))
            return detail::fail(error, "truncated aref", in);
        sink.AReference(variable, element, path);
        return true;
    }
    default:
        return detail::fail(error, "unknown value type " + std::to_string(type), in);
    }
}

// The variables section: a count, then the variables. Only the structure is read.
inline bool parseVariables(StateReader &in, std::vector<SavedVariable> &variables, std::string &error)
{
    uint32_t count;
    if (!in.VDword(count))
        return detail::fail(error, "truncated variable count", in);
    // A variable takes at least ten bytes (name, type, elements, value).
    if (count > in.Remaining() / 10)
        return detail::fail(error, std::to_string(count) + " variables beyond the end of the data", in);
    variables.clear();
    variables.reserve(count);
    NoSink skip;
    for (uint32_t n = 0; n < count; ++n)
    {
        SavedVariable variable;
        std::optional<std::string> name;
        if (!in.String(name) || !name || name->empty())
            return detail::fail(error, "variable #" + std::to_string(n) + " has no name", in);
        variable.name = std::move(*name);
        const auto where = [&] { return "variable #" + std::to_string(n) + " '" + variable.name + "': "; };
        if (!in.Raw(variable.type) || !in.Raw(variable.elements))
            return detail::fail(error, where() + "truncated header", in);
        // Checked here also for an array of no elements, whose type no value reads.
        if (!knownType(variable.type))
            return detail::fail(error, where() + "unknown value type " + std::to_string(variable.type), in);
        if (variable.elements > in.Remaining())
            return detail::fail(error, where() + std::to_string(variable.elements) + " elements", in);
        variable.values = in.Position();
        for (uint32_t element = 0; element < variable.elements; ++element)
        {
            std::string why;
            if (!readValue(in, variable.type, skip, why))
                return detail::fail(error, where() + "element " + std::to_string(element) + ": " + why, in);
        }
        variables.push_back(std::move(variable));
    }
    return true;
}

// The whole state; it must end exactly where its last variable ends.
inline bool parseState(const char *data, size_t size, ParsedState &state, std::string &error)
{
    StateReader in(data, size);
    state = {};
    if (!in.String(state.programDirectory))
        return detail::fail(error, "truncated program directory", in);
    uint32_t count;
    if (!in.VDword(count) || count > in.Remaining())
        return detail::fail(error, "truncated name codec", in);
    for (uint32_t n = 0; n < count; ++n)
    {
        std::optional<std::string> name;
        if (!in.String(name))
            return detail::fail(error, "truncated name codec string #" + std::to_string(n), in);
        if (name)
            state.codec.push_back(std::move(*name));
    }
    if (!in.VDword(count) || count > in.Remaining())
        return detail::fail(error, "truncated segment list", in);
    for (uint32_t n = 0; n < count; ++n)
    {
        std::optional<std::string> segment;
        if (!in.String(segment) || !segment)
            return detail::fail(error, "segment #" + std::to_string(n) + " has no name", in);
        state.segments.push_back(std::move(*segment));
    }
    if (!parseVariables(in, state.variables, error))
        return false;
    if (in.Remaining() != 0)
        return detail::fail(error, std::to_string(in.Remaining()) + " bytes after the last variable", in);
    return true;
}

// Puts the parsed variables into `table` (the program the state was saved from, just loaded).
// `data` is the buffer that parseVariables / parseState accepted. Reports every saved value that
// is left (see the top of this file); `report` receives one line each.
// `newArray` makes an empty value in the program's pool of `new` arrays, for each one the state saved.
inline void applyVariables(const char *data, size_t size, const std::vector<SavedVariable> &saved, VarTable &table,
                           VSTRING_CODEC *codec, const std::function<void(const std::string &)> &report,
                           const std::function<DATA *()> &newArray)
{
    const auto isScalar = [](uint32_t type) { return type == VAR_INTEGER || type == VAR_FLOAT || type == VAR_STRING; };
    struct Target
    {
        const SavedVariable *saved;
        DATA *value;
    };
    std::vector<Target> targets;
    targets.reserve(saved.size());

    std::vector<DATA *> newArrays;

    // 1. Where each variable goes, and the saved array sizes.
    for (const auto &variable : saved)
    {
        if (variable.name.starts_with('#'))
        {
            if (variable.name != NewArrayName(static_cast<uint32_t>(newArrays.size())))
            {
                report("Load: '" + variable.name + "' is no saved new array of this engine; its saved value is left");
                continue;
            }
            DATA *value = newArray();
            value->SetType(static_cast<S_TOKEN_TYPE>(variable.type));
            value->SetArraySize(variable.elements);
            newArrays.push_back(value);
            targets.push_back({&variable, value});
            continue;
        }
        const size_t code = table.FindVar(variable.name);
        const VarInfo *info = code == INVALID_VAR_CODE ? nullptr : table.GetVarX(code);
        if (info == nullptr || info->value == nullptr)
        {
            report("Load: variable '" + variable.name + "' is not declared by the program; its saved value is left");
            continue;
        }
        if (info->type != variable.type && !(isScalar(info->type) && isScalar(variable.type)))
        {
            report("Load: variable '" + variable.name + "' is declared " + typeName(info->type) + " but saved as " +
                   typeName(variable.type) + "; its saved value is left");
            continue;
        }
        DATA *value = info->value.get();
        if (value->IsArray())
        {
            if (variable.elements != value->GetArraySize())
            {
                value->SetElementsNum(variable.elements);
                table.SetElementsNum(code, variable.elements);
            }
        }
        else if (variable.elements != 1)
        {
            report("Load: variable '" + variable.name + "' is declared a single value but saved as an array of " +
                   std::to_string(variable.elements) + "; its saved value is left");
            continue;
        }
        if (info->type != variable.type)
            spdlog::warn("Load: variable '{}' is declared {} and was saved as {} (its type changed at run time, or "
                         "the scripts changed); restored as saved",
                         variable.name, typeName(info->type), typeName(variable.type));
        targets.push_back({&variable, value});
    }

    // A reference target: a global variable or one element of a global array, as it is now.
    const auto place = [&](const std::string &name, uint32_t variable, uint32_t element) -> DATA * {
        const bool isNew = variable >= kNewArrayPlace;
        const VarInfo *info = isNew ? nullptr : table.GetVarX(variable);
        DATA *value =
            isNew ? (variable - kNewArrayPlace < newArrays.size() ? newArrays[variable - kNewArrayPlace] : nullptr)
                  : (info ? info->value.get() : nullptr);
        if (value == nullptr)
        {
            report("Load: reference '" + name + "' names variable #" + std::to_string(variable) +
                   ", which the program does not have; it is left empty");
            return nullptr;
        }
        if (element == kNoIndex)
            return value;
        if (!value->IsArray() || element >= value->GetArraySize())
        {
            report("Load: reference '" + name + "' names element " + std::to_string(element) + " of '" +
                   (isNew ? NewArrayName(variable - kNewArrayPlace) : info->name) + "', which has " +
                   std::to_string(value->GetArraySize()) + "; it is left empty");
            return nullptr;
        }
        return value->GetArrayElement(element);
    };

    struct Sink
    {
        DATA *target;
        const std::string *name;
        VSTRING_CODEC *codec;
        const decltype(place) *resolve;

        void Integer(int32_t value)
        {
            target->Set(value);
        }
        void Float(float value)
        {
            target->Set(value);
        }
        void Pointer(uintptr_t value)
        {
            target->SetPtr(value);
        }
        void String(const std::optional<std::string> &value)
        {
            target->Set(value ? value->c_str() : nullptr);
        }
        ATTRIBUTES *Object()
        {
            target->Set(invalid_entity);
            if (target->AttributesClass == nullptr)
                target->AttributesClass = new ATTRIBUTES(codec);
            return target->AttributesClass;
        }
        void Reference(uint32_t variable, uint32_t element)
        {
            if (variable == kNoIndex)
                return; // saved empty
            if (DATA *value = (*resolve)(*name, variable, element))
                target->SetReference(value);
        }
        void AReference(uint32_t variable, uint32_t element, const std::optional<std::string> &path)
        {
            if (variable == kNoIndex)
                return; // saved empty
            DATA *owner = (*resolve)(*name, variable, element);
            if (owner == nullptr)
                return;
            if (owner->AttributesClass == nullptr)
                owner->AttributesClass = new ATTRIBUTES(codec);
            // No path: the object's root attribute itself.
            target->SetAReference(path ? owner->AttributesClass->CreateSubAClass(owner->AttributesClass, path->c_str())
                                       : owner->AttributesClass);
        }
    };

    // 2. The values, 3. the references, now that every array has its saved size.
    for (const bool references : {false, true})
        for (const auto &[variable, value] : targets)
        {
            if ((variable->type == VAR_REFERENCE || variable->type == VAR_AREFERENCE) != references)
                continue;
            StateReader in(data, size, variable->values);
            for (uint32_t element = 0; element < variable->elements; ++element)
            {
                Sink sink{value->IsArray() ? value->GetArrayElement(element) : value, &variable->name, codec, &place};
                std::string error;
                // parseVariables accepted these bytes: a failure here is a defect of this reader.
                if (!readValue(in, variable->type, sink, error))
                    throw std::logic_error("Load: variable '" + variable->name + "' no longer reads: " + error);
            }
        }
}
// The writer of the layout above; COMPILER::SerializeState writes the state with it. A value the load could not take
// back as it is (an element of another type than its array, a reference to no global) is reported through the
// compiler and written in the form the load reads.
class StateWriter
{
  public:
    explicit StateWriter(VIRTUAL_COMPILER &compiler) : compiler_(compiler)
    {
    }
    std::vector<char> take()
    {
        return std::move(bytes_);
    }

    void data(const void *data, uint32_t size)
    {
        if (data == nullptr)
            return;
        const auto *bytes = static_cast<const char *>(data);
        bytes_.insert(bytes_.end(), bytes, bytes + size);
    }
    void vdword(uint32_t v)
    {
        if (v < 0xfe)
        {
            const auto byte = static_cast<uint8_t>(v);
            data(&byte, sizeof(byte));
        }
        else if (v < 0xffff)
        {
            const uint8_t mark = 0xfe;
            data(&mark, sizeof(mark));
            const auto word = static_cast<uint16_t>(v);
            data(&word, sizeof(word));
        }
        else
        {
            const uint8_t mark = 0xff;
            data(&mark, sizeof(mark));
            data(&v, sizeof(v));
        }
    }
    void string(const char *text)
    {
        if (text == nullptr)
        {
            vdword(0);
            return;
        }
        const uint32_t n = static_cast<uint32_t>(strlen(text)) + 1;
        vdword(n);
        data(text, n);
    }
    // The global variable `name`; its references are written as the places of `references`.
    void variable(DATA *value, const char *name, const ReferenceIndex &references)
    {
        references_ = &references;
        name_ = name;
        this->value(value, false);
        references_ = nullptr;
        name_ = "";
    }
    void attributes(ATTRIBUTES *root)
    {
        if (root == nullptr)
        {
            vdword(0);       // number of subclasses
            string(nullptr); // attribute name
            string(nullptr); // attribute value
            return;
        }
        vdword(root->GetAttributesNum()); // number of subclasses
        vdword(root->GetThisNameCode());
        string(root->GetThisAttr());
        for (uint32_t n = 0; n < root->GetAttributesNum(); n++)
            attributes(root->GetAttributeClass(n));
    }

  private:
    // element: an array's element, written without the type and count its array has.
    void value(DATA *pV, bool element)
    {
        if (pV == nullptr)
        {
            compiler_.SetError("Zero Variable Pointer");
            return;
        }
        if (!element)
        {
            const S_TOKEN_TYPE type = pV->GetType();
            data(&type, sizeof(type));
            const uint32_t n = pV->IsArray() ? pV->GetArraySize() : 1;
            data(&n, sizeof(n));
        }
        if (pV->IsArray())
        {
            // Elements carry no type of their own in the save: the load reads each with the array's
            // type. An element whose type changed at run time (`a[i] += 0.5` in an int array,
            // DATA::Plus) is written converted to it, or every value after it would be misread.
            const S_TOKEN_TYPE arrayType = pV->GetType();
            for (uint32_t n = 0; n < pV->GetArraySize(); n++)
            {
                DATA *item = pV->GetArrayElement(n);
                const S_TOKEN_TYPE itemType = item->GetType();
                if (itemType == arrayType)
                {
                    value(item, true);
                    continue;
                }
                DATA converted(arrayType);
                converted.SetVCompiler(&compiler_);
                const auto isScalar = [](S_TOKEN_TYPE type) {
                    return type == VAR_INTEGER || type == VAR_FLOAT || type == VAR_STRING;
                };
                const bool scalar = isScalar(arrayType) && isScalar(itemType);
                if (scalar)
                {
                    int32_t integer;
                    float real;
                    const char *text;
                    if (itemType == VAR_INTEGER && item->Get(integer))
                        converted.Set(integer);
                    else if (itemType == VAR_FLOAT && item->Get(real))
                        converted.Set(real);
                    else if (itemType == VAR_STRING && item->Get(text))
                        converted.Set(text);
                    converted.Convert(arrayType);
                }
                compiler_.SetError("Save: element %u of '%s' is %s in an array of %s; saved %s", n, name_,
                                   typeName(itemType), typeName(arrayType), scalar ? "converted" : "as an empty value");
                value(&converted, true);
            }
            return;
        }
        switch (pV->GetType())
        {
        case VAR_INTEGER: {
            int32_t integer;
            pV->Get(integer);
            data(&integer, sizeof(integer));
            break;
        }
        case VAR_PTR: {
            uintptr_t pointer;
            pV->GetPtr(pointer);
            data(&pointer, sizeof(pointer));
            break;
        }
        case VAR_FLOAT: {
            float real;
            pV->Get(real);
            data(&real, sizeof(real));
            break;
        }
        case VAR_STRING: {
            const char *text;
            pV->Get(text);
            string(text);
            break;
        }
        case VAR_OBJECT: {
            entid_t id;
            pV->Get(id);
            data(&id, sizeof(id));
            attributes(pV->AttributesClass);
            break;
        }
        case VAR_REFERENCE: {
            if (pV->GetReference() == nullptr)
            {
                vdword(0xffffffff); // uninitialized reference
                return;
            }
            // The chain is resolved by address, never followed through memory that may be freed
            // (save_references.hpp).
            const auto place = references_->Variable(pV);
            if (!place)
            {
                compiler_.SetError("Ghost reference: '%s' names no global variable and is saved empty", name_);
                vdword(0xffffffff); // reference to none
                return;
            }
            vdword(place->variable);
            vdword(place->element);
            break;
        }
        case VAR_AREFERENCE: {
            if (pV->AReferenceDangling())
            {
                // The game leaves such arefs behind (reload.c reload_locator_ref names the quest reload's fake
                // locator, deleted once the location loaded); no script may read them. Cleared: the next save is
                // silent, and the variable holds what loading this save gives it.
                compiler_.SetWarning("aref '%s' names a deleted attribute: saved empty and cleared", name_);
                pV->AttributesClass = nullptr;
            }
            if (pV->AttributesClass == nullptr)
            {
                vdword(0xffffffff);
                break;
            }
            const AttributePlace *place = references_->Attribute(pV->AttributesClass);
            if (place == nullptr)
            {
                // An attribute of a local object or of an entity.
                compiler_.SetError("Ghost A reference: '%s' names no attribute of a global object and is saved empty",
                                   name_);
                vdword(0xffffffff);
                break;
            }
            if (place->nameless)
            {
                vdword(0xffffffff);
                break;
            }
            vdword(place->owner.variable);
            vdword(place->owner.element);
            string(place->path.empty() ? nullptr : place->path.c_str());
            break;
        }
        default:
            break;
        }
    }

    VIRTUAL_COMPILER &compiler_;
    std::vector<char> bytes_;
    const ReferenceIndex *references_ = nullptr;
    const char *name_ = "";
};

// The `new` arrays the references of the state reach, after its globals.
inline void writeNewArrays(StateWriter &out, const ReferenceIndex &references)
{
    for (uint32_t n = 0; n < references.NewArrays().size(); ++n)
    {
        const std::string name = NewArrayName(n);
        out.string(name.c_str());
        out.variable(references.NewArrays()[n], name.c_str(), references);
    }
}

// The head of the state, up to its variables: the program directory, the names of `codec` in the order its Get and
// GetNext walk them (registered again in that order on load, every name gets its saved code back), the segment the
// program is loaded from (the scripts load the others themselves), and the number of variables that follow.
template <class Codec>
void writeHead(StateWriter &out, const char *programDirectory, Codec &codec, const std::string &segment,
               uint32_t variables)
{
    out.string(programDirectory);
    out.vdword(codec.GetNum());
    for (uint32_t n = 0; n < codec.GetNum(); n++)
        out.string(n == 0 ? codec.Get() : codec.GetNext());
    out.vdword(1);
    out.string(segment.c_str());
    out.vdword(variables);
}
} // namespace storm::save
