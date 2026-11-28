#pragma once
// The game as engine code sees it through `core` (core.h), for checks of code that reads script variables, entities
// and event handlers: the check fills the maps; a question it did not answer fails the check (std::logic_error).
// The check defines `Core &core = <its FakeCore>;`.
#include "core_private.h"
#include "data.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace storm::test
{
class Codec final : public VSTRING_CODEC
{
    std::vector<std::string> names_;

  public:
    uint32_t GetNum() override
    {
        return static_cast<uint32_t>(names_.size());
    }
    uint32_t Convert(const char *name) override
    {
        return Convert(name, static_cast<int32_t>(std::char_traits<char>::length(name)));
    }
    uint32_t Convert(const char *name, int32_t length) override
    {
        const std::string_view value(name, length);
        if (const auto code = Find(value))
            return *code;
        names_.emplace_back(value);
        return static_cast<uint32_t>(names_.size() - 1);
    }
    const char *Convert(uint32_t code) override
    {
        return names_.at(code).c_str();
    }
    std::optional<uint32_t> Find(std::string_view name) const override
    {
        const auto found = std::find(names_.begin(), names_.end(), name);
        return found == names_.end() ? std::nullopt : std::optional<uint32_t>(found - names_.begin());
    }
    void VariableChanged() override
    {
    }
};

// Script variables of the real DATA kind, made with the codec; a script error fails the check.
class Scripts final : public VIRTUAL_COMPILER
{
    std::map<std::string, std::unique_ptr<DATA>, std::less<>> variables_;

  public:
    Codec codec;
    VSTRING_CODEC *GetVSC() override
    {
        return &codec;
    }
    void SetError(const char *text, ...) override
    {
        throw std::logic_error(std::string("script error: ") + text);
    }
    DATA &variable(std::string_view name, S_TOKEN_TYPE type, uint32_t elements = 1)
    {
        auto &slot = variables_[std::string(name)];
        slot = elements > 1 ? std::make_unique<DATA>(elements, type) : std::make_unique<DATA>(type);
        slot->SetVCompiler(this);
        return *slot;
    }
    DATA &integer(std::string_view name, int32_t value)
    {
        auto &data = variable(name, VAR_INTEGER);
        data.Set(value);
        return data;
    }
    DATA &text(std::string_view name, const char *value)
    {
        auto &data = variable(name, VAR_STRING);
        data.Set(value);
        return data;
    }
    // An object variable's attributes.
    ATTRIBUTES &object(std::string_view name)
    {
        return *variable(name, VAR_OBJECT).GetAClass();
    }
    void remove(std::string_view name)
    {
        if (const auto found = variables_.find(name); found != variables_.end())
            variables_.erase(found);
    }
    DATA *find(std::string_view name)
    {
        const auto found = variables_.find(name);
        return found == variables_.end() ? nullptr : found->second.get();
    }
};

class FakeCore final : public CorePrivate
{
  public:
    Scripts scripts;
    std::map<std::string, entid_t, std::less<>> entities;
    std::map<entid_t, ATTRIBUTES *> entityAttributes;
    std::map<entid_t, Entity *> entityPointers;
    std::map<std::string, std::vector<entid_t>, std::less<>> entityLists;
    std::map<std::string, void *, std::less<>> services;
    std::set<std::pair<std::string, std::string>> eventHandlers;
    // Send_Message: the arguments after the format, as text ('l' int32, 'f' float, 's' string, 'i'/'e' entity id).
    std::function<uint64_t(entid_t, const std::vector<std::string> &)> message;
    std::function<void(std::string_view)> event;
    // An event with arguments (Event(name, format, ...)) and the handler's answer; without it the arguments go unread
    // to `event`.
    std::function<VDATA *(std::string_view, MESSAGE &)> messageEvent;
    // GetSaveData: the save files' bytes by path, and how often they were read.
    std::map<std::string, std::vector<uint8_t>, std::less<>> saveData;
    int saveDataReads = 0;
    std::vector<std::string> traces;

    [[noreturn]] static void unmodelled(const char *what)
    {
        throw std::logic_error(std::string("FakeCore: the check does not model ") + what);
    }

    void *GetScriptVariable(const char *name, uint32_t *) override
    {
        return scripts.find(name);
    }
    entid_t GetEntityId(const char *name) const override
    {
        const auto found = entities.find(name);
        return found == entities.end() ? invalid_entity : found->second;
    }
    ATTRIBUTES *Entity_GetAttributePointer(entid_t id) override
    {
        const auto found = entityAttributes.find(id);
        return found == entityAttributes.end() ? nullptr : found->second;
    }
    ATTRIBUTES *Entity_GetAttributeClass(entid_t id, const char *name) override
    {
        auto *attributes = Entity_GetAttributePointer(id);
        return attributes ? attributes->FindAClass(attributes, name) : nullptr;
    }
    entptr_t GetEntityPointer(entid_t id) const override
    {
        const auto found = entityPointers.find(id);
        return found == entityPointers.end() ? nullptr : found->second;
    }
    entptr_t GetEntityPointerSafe(entid_t id) const override
    {
        return GetEntityPointer(id);
    }
    bool IsEntityValid(entid_t id) const override
    {
        return entityPointers.contains(id);
    }
    entity_container_cref GetEntityIds(const char *name) const override
    {
        static const std::vector<entid_t> none;
        const auto found = entityLists.find(name);
        return found == entityLists.end() ? none : found->second;
    }
    void *GetService(const char *name) override
    {
        const auto found = services.find(name);
        return found == services.end() ? nullptr : found->second;
    }
    uint64_t Send_Message(entid_t to, const char *format, ...) override
    {
        if (!message)
            unmodelled("Send_Message");
        std::vector<std::string> arguments;
        va_list list;
        va_start(list, format);
        for (const char *kind = format; *kind; ++kind)
            switch (*kind)
            {
            case 'l':
                arguments.push_back(std::to_string(va_arg(list, int32_t)));
                break;
            case 'f':
                arguments.push_back(std::to_string(va_arg(list, double)));
                break;
            case 's':
                arguments.emplace_back(va_arg(list, const char *));
                break;
            case 'i':
            case 'e':
                arguments.push_back(std::to_string(va_arg(list, entid_t)));
                break;
            default:
                va_end(list);
                unmodelled("this Send_Message format");
            }
        va_end(list);
        return message(to, arguments);
    }
    VDATA *Event(const std::string_view &name) override
    {
        if (!event)
            unmodelled("Event");
        event(name);
        return nullptr;
    }
    VDATA *Event(const std::string_view &name, MESSAGE &message) override
    {
        if (!messageEvent)
            return Event(name);
        return messageEvent(name, message);
    }
    std::optional<VDATA *> CallFunction(const std::string_view &, MESSAGE &) override
    {
        unmodelled("CallFunction");
        return std::nullopt;
    }
    bool HasActiveEventHandler(const char *event_name, const char *func_name) override
    {
        return eventHandlers.contains({event_name, func_name});
    }
    void Trace(const char *format, ...) override
    {
        char line[512];
        va_list list;
        va_start(list, format);
        std::vsnprintf(line, sizeof(line), format, list);
        va_end(list);
        traces.emplace_back(line);
    }

    // Not modelled.
    storm::OSWindow *GetWindow() override
    {
        unmodelled("GetWindow");
    }
    void SetTimeScale(float) override
    {
        unmodelled("SetTimeScale");
    }
    float GetTimeScale() const override
    {
        unmodelled("GetTimeScale");
    }
    const char *Entity_GetAttribute(entid_t, const char *) override
    {
        unmodelled("Entity_GetAttribute");
    }
    uint32_t Entity_GetAttributeAsDword(entid_t, const char *, uint32_t) override
    {
        unmodelled("Entity_GetAttributeAsDword");
    }
    float Entity_GetAttributeAsFloat(entid_t, const char *, float) override
    {
        unmodelled("Entity_GetAttributeAsFloat");
    }
    bool Entity_SetAttribute(entid_t, const char *, const char *) override
    {
        unmodelled("Entity_SetAttribute");
    }
    bool Entity_SetAttributeUseDword(entid_t, const char *, uint32_t) override
    {
        unmodelled("Entity_SetAttributeUseDword");
    }
    bool Entity_SetAttributeUseFloat(entid_t, const char *, float) override
    {
        unmodelled("Entity_SetAttributeUseFloat");
    }
    void Entity_SetAttributePointer(entid_t, ATTRIBUTES *) override
    {
        unmodelled("Entity_SetAttributePointer");
    }
    uint32_t Entity_AttributeChanged(entid_t, ATTRIBUTES *) override
    {
        unmodelled("Entity_AttributeChanged");
    }
    bool SaveState(const char *) override
    {
        unmodelled("SaveState");
    }
    bool InitiateStateLoading(const char *) override
    {
        unmodelled("InitiateStateLoading");
    }
    std::string StateFileProblem(const char *) override
    {
        unmodelled("StateFileProblem");
    }
    uint32_t EngineFps() override
    {
        unmodelled("EngineFps");
    }
    void SetDeltaTime(int32_t) override
    {
        unmodelled("SetDeltaTime");
    }
    uint32_t GetDeltaTime() override
    {
        unmodelled("GetDeltaTime");
    }
    uint32_t GetRDeltaTime() override
    {
        unmodelled("GetRDeltaTime");
    }
    uint32_t PostEvent(const char *, uint32_t, const char *, ...) override
    {
        unmodelled("PostEvent");
    }
    bool EventPosted(const char *) override
    {
        unmodelled("EventPosted");
    }
    void *GetSaveData(const char *path, int32_t &size) override
    {
        ++saveDataReads;
        const auto found = saveData.find(std::string_view(path));
        size = found == saveData.end() ? 0 : static_cast<int32_t>(found->second.size());
        if (size == 0)
            return nullptr;
        auto *bytes = new char[found->second.size()];
        std::memcpy(bytes, found->second.data(), found->second.size());
        return bytes;
    }
    bool SetSaveData(const char *, void *, int32_t) override
    {
        unmodelled("SetSaveData");
    }
    uint32_t SetScriptFunction(IFUNCINFO *) override
    {
        unmodelled("SetScriptFunction");
    }
    const char *EngineIniFileName() override
    {
        unmodelled("EngineIniFileName");
    }
    std::optional<int32_t> GetScriptIntegerDefine(const char *) override
    {
        unmodelled("GetScriptIntegerDefine");
    }
    storm::ENGINE_VERSION GetTargetEngineVersion() const noexcept override
    {
        return {};
    }
    ScreenSize GetScreenSize() const noexcept override
    {
        return {};
    }
    void stopFrameProcessing() override
    {
        unmodelled("stopFrameProcessing");
    }
    void AddToLayer(layer_index_t, entid_t, priority_t) override
    {
        unmodelled("AddToLayer");
    }
    void EraseEntity(entid_t) override
    {
        unmodelled("EraseEntity");
    }
    entid_t CreateEntity(const char *, ATTRIBUTES *) override
    {
        unmodelled("CreateEntity");
    }
    entity_container_cref GetEntityIds(layer_type_t) const override
    {
        unmodelled("GetEntityIds(layer type)");
    }
    entity_container_cref GetEntityIds(layer_index_t) const override
    {
        unmodelled("GetEntityIds(layer)");
    }
    void SetLayerType(layer_index_t, layer_type_t) override
    {
        unmodelled("SetLayerType");
    }
    void SetLayerFrozen(layer_index_t, bool) override
    {
        unmodelled("SetLayerFrozen");
    }
    void RemoveFromLayer(layer_index_t, entid_t) override
    {
        unmodelled("RemoveFromLayer");
    }
    hash_t GetClassCode(entid_t) const override
    {
        unmodelled("GetClassCode");
    }
    bool IsLayerFrozen(layer_index_t) const override
    {
        unmodelled("IsLayerFrozen");
    }
    void ForEachEntity(const std::function<void(entptr_t)> &) override
    {
        unmodelled("ForEachEntity");
    }
    void Init() override
    {
        unmodelled("Init");
    }
    void InitBase() override
    {
        unmodelled("InitBase");
    }
    void ReleaseBase() override
    {
        unmodelled("ReleaseBase");
    }
    void CleanUp() override
    {
        unmodelled("CleanUp");
    }
    bool Run() override
    {
        unmodelled("Run");
    }
    bool initialized() const override
    {
        return true;
    }
    void AppState(bool) override
    {
        unmodelled("AppState");
    }
    void SetWindow(std::shared_ptr<storm::OSWindow>) override
    {
        unmodelled("SetWindow");
    }
    int ScriptCallDepth(const char *) override
    {
        unmodelled("ScriptCallDepth");
    }
    VDATA *ScriptCallArgument(const char *, uint32_t) override
    {
        unmodelled("ScriptCallArgument");
    }
};
} // namespace storm::test
