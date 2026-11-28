#pragma once

// Where the script references of a save point.
//
// A save stores a `ref` as the global variable (and array element) it names, and an `aref`
// as the global object variable that owns the attribute plus the attribute's path. The
// script language does not track references: deleting an attribute (DeleteAttribute, a
// rebuilt branch), resizing an array or reloading a segment leaves every `ref` / `aref` to it
// pointing at freed memory. A save visits every global variable, so it meets those pointers
// even when no script would use them again.
//
// The pointers are therefore never followed. Before the variables are written, the index
// records the address of every global variable and array element, and looks for the target
// of every global `aref` among the attributes that global object variables own. A pointer
// found there is alive (or names the live variable or attribute that now occupies that
// address, which is also what a script reading the reference would get); a pointer not found
// is a reference to nothing that can be saved and is written as empty, with an error line.
//
// Arrays made by `new type[n]` belong to no variable: the ones a saved reference reaches
// (directly, or through the references in another such array) are written after the globals as
// entries of their own, named so that no script can declare them, and their places are numbered
// from kNewArrayPlace on.
//
// The original engine walked up from the attribute through its parents instead
// (TraceARoot) and relied on Windows structured exceptions for the dangling case. Elsewhere
// that walk is a call through freed memory: the save crashed (Android, SIGSEGV in
// COMPILER::SaveVariable).

#include "attributes.h"
#include "s_vartab.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::save
{
inline constexpr uint32_t kNoElement = 0xffffffff;
// The place of the first saved `new` array; a program has fewer global variables.
inline constexpr uint32_t kNewArrayPlace = 0x40000000;
// The saved name of the `new` array at place kNewArrayPlace + n.
inline std::string NewArrayName(uint32_t n)
{
    return "#new " + std::to_string(n);
}

// A global variable, or one element of a global array.
struct VariablePlace
{
    uint32_t variable = 0;
    uint32_t element = kNoElement;
};

// An attribute of a global object variable.
struct AttributePlace
{
    VariablePlace owner;
    std::string path; // "a.b.c" below the object's root attribute; empty for the root itself
    // An attribute on the way has name code 0. The save format has always written such a
    // reference as empty (TraceARoot, "fix crash at NewGame start"); files stay compatible.
    bool nameless = false;
};

class ReferenceIndex
{
  public:
    // `saved`: whether a variable enters the save; a reference to one that does not is saved empty.
    // `pool`: the arrays made by `new type[n]` (COMPILER::newArrays_).
    explicit ReferenceIndex(const VarTable &variables, const std::function<bool(const VarInfo &)> &saved = {},
                            std::deque<DATA> *pool = nullptr)
    {
        const auto count = static_cast<uint32_t>(variables.GetVarNum());
        for (uint32_t n = 0; n < count; ++n)
        {
            const VarInfo *info = variables.GetVarX(n);
            if (info == nullptr || info->value == nullptr || (saved && !saved(*info)))
                continue;
            DATA *value = info->value.get();
            if (!value->IsArray())
            {
                Add(value, VariablePlace{n, kNoElement});
                continue;
            }
            // The array as it is now: `@a = n` resizes it without its declaration.
            for (uint32_t i = 0; i < value->GetArraySize(); ++i)
                Add(value->GetArrayElement(i), VariablePlace{n, i});
        }
        if (pool != nullptr)
            IndexNewArrays(variables, saved, *pool);

        // Only attributes of global object variables can be saved as a reference target.
        unresolved_ = attributes_.size();
        for (uint32_t n = 0; n < count && unresolved_ != 0; ++n)
        {
            const VarInfo *info = variables.GetVarX(n);
            if (info == nullptr || info->value == nullptr || info->type != VAR_OBJECT || (saved && !saved(*info)))
                continue;
            DATA *value = info->value.get();
            if (!value->IsArray())
            {
                Visit(value->AttributesClass, VariablePlace{n, kNoElement});
                continue;
            }
            for (uint32_t i = 0; i < value->GetArraySize() && unresolved_ != 0; ++i)
                Visit(value->GetArrayElement(i)->AttributesClass, VariablePlace{n, i});
        }
    }

    // The variable the reference chain starting at the `ref` variable `reference` ends in;
    // nothing when the chain is empty or leaves the global variables (a ghost reference).
    // `reference` itself must be alive; a chain never loops (DATA::SetReference).
    std::optional<VariablePlace> Variable(DATA *reference) const
    {
        for (DATA *current = reference;;)
        {
            DATA *next = current->GetReference();
            if (next == nullptr)
                return {};
            const auto found = variables_.find(next);
            if (found == variables_.end())
                return {};
            if (next->GetType() != VAR_REFERENCE || next->IsArray())
                return found->second;
            current = next;
        }
    }

    // Where the target of an `aref` is; nullptr when no global object variable owns it.
    const AttributePlace *Attribute(const ATTRIBUTES *target) const
    {
        const auto found = attributes_.find(target);
        return found == attributes_.end() || !found->second ? nullptr : &*found->second;
    }

    // The `new` arrays to save, in the order of their places.
    const std::vector<DATA *> &NewArrays() const
    {
        return new_arrays_;
    }

  private:
    // The `new` arrays the saved references reach, breadth first from the global `ref` variables.
    void IndexNewArrays(const VarTable &variables, const std::function<bool(const VarInfo &)> &saved,
                        std::deque<DATA> &pool)
    {
        std::unordered_map<const DATA *, std::pair<DATA *, uint32_t>> in_pool; // element or array -> array, element
        for (DATA &array : pool)
        {
            in_pool.try_emplace(&array, &array, kNoElement);
            for (uint32_t i = 0; array.IsArray() && i < array.GetArraySize(); ++i)
                in_pool.try_emplace(array.GetArrayElement(i), &array, i);
        }
        std::vector<DATA *> references;
        const auto collect = [&](DATA *value) {
            if (value->GetType() != VAR_REFERENCE)
                return;
            if (!value->IsArray())
            {
                references.push_back(value);
                return;
            }
            for (uint32_t i = 0; i < value->GetArraySize(); ++i)
                references.push_back(value->GetArrayElement(i));
        };
        for (uint32_t n = 0; n < static_cast<uint32_t>(variables.GetVarNum()); ++n)
            if (const VarInfo *info = variables.GetVarX(n); info && info->value && (!saved || saved(*info)))
                collect(info->value.get());
        for (size_t next = 0; next < references.size(); ++next)
        {
            const auto found = in_pool.find(references[next]->GetReference());
            if (found == in_pool.end())
                continue;
            DATA *array = found->second.first;
            if (variables_.contains(array))
                continue;
            const auto place = kNewArrayPlace + static_cast<uint32_t>(new_arrays_.size());
            new_arrays_.push_back(array);
            variables_.try_emplace(array, VariablePlace{place, kNoElement});
            for (uint32_t i = 0; array->IsArray() && i < array->GetArraySize(); ++i)
                Add(array->GetArrayElement(i), VariablePlace{place, i});
            collect(array);
        }
    }

    void Add(DATA *value, VariablePlace place)
    {
        variables_.try_emplace(value, place);
        if (value->GetType() == VAR_AREFERENCE && value->AttributesClass != nullptr && !value->AReferenceDangling())
            attributes_.try_emplace(value->AttributesClass, std::nullopt);
    }

    void Visit(const ATTRIBUTES *root, VariablePlace owner)
    {
        if (root == nullptr)
            return;
        std::string path;
        Visit(root, owner, path, false, true);
    }

    void Visit(const ATTRIBUTES *node, VariablePlace owner, std::string &path, bool nameless, bool is_root)
    {
        const size_t length = path.size();
        if (!is_root)
        {
            nameless = nameless || node->GetThisNameCode() == 0;
            if (!path.empty())
                path += '.';
            path += node->GetThisName();
        }
        if (const auto found = attributes_.find(node); found != attributes_.end() && !found->second)
        {
            found->second = AttributePlace{owner, path, nameless};
            --unresolved_;
        }
        const auto children = static_cast<uint32_t>(node->GetAttributesNum());
        for (uint32_t n = 0; n < children && unresolved_ != 0; ++n)
            Visit(node->GetAttributeClass(n), owner, path, nameless, false);
        path.resize(length);
    }

    std::unordered_map<const DATA *, VariablePlace> variables_;
    std::vector<DATA *> new_arrays_;
    // The targets of the global `aref` variables; a value once a global object owns the address.
    std::unordered_map<const ATTRIBUTES *, std::optional<AttributePlace>> attributes_;
    size_t unresolved_ = 0;
};
} // namespace storm::save
