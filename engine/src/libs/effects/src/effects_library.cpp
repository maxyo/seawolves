// EFFECTS_LIBRARY: the natives a game declares next to its Effects entity (README.md).
#include "effects.hpp"

#include "core.h"
#include "s_import_func.h"
#include "script_libriary.h"
#include "shared/messages.h"
#include "storm_glm.h"
#include "v_data.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <spdlog/spdlog.h>

#include <set>
#include <utility>

namespace efk = storm::render::effekseer;
using storm::effects::EffectEntry;
using storm::effects::Table;

namespace
{
constexpr int32_t kNoHandle = -1; // the scripts' INVALID_EFF_HANDLE

VDATA *Pop(VS_STACK *stack)
{
    return static_cast<VDATA *>(stack->Pop());
}

uint32_t Return(VS_STACK *stack, int32_t value)
{
    static_cast<VDATA *>(stack->Push())->Set(value);
    return IFUNCRESULT_OK;
}

// The Effekseer entry of an id, or nullptr (logged).
const EffectEntry *EffekseerEntry(const char *native, const std::string &id)
{
    const EffectEntry *entry = Table().Find(id);
    if (!entry)
        spdlog::error("{} '{}': no such id in effects.json", native, id);
    else if (entry->kind != EffectEntry::Kind::Effekseer)
    {
        spdlog::error("{} '{}': {} is a particle system, not an Effekseer effect", native, id, entry->file);
        return nullptr;
    }
    return entry;
}

// CheckAndCreateEffect(string id, float x, y, z, float ax, ay, az, int lifetime, bool isDirect): an Effekseer id plays
// and returns its handle; a particle system id is created by the "particles" entity (PS_CREATE, or PS_CREATEX when
// isDirect) and returns -1, as do ids the table does not name (nothing is created for them). isDirect: (ax, ay, az) is
// a direction. An Effekseer effect takes no lifetime: the scripts end one through its dynamic input 3.
uint32_t CheckAndCreateEffect(VS_STACK *stack)
{
    const bool direct = Pop(stack)->GetInt() != 0;
    const int32_t lifetime = Pop(stack)->GetInt();
    glm::vec3 angles;
    angles.z = Pop(stack)->GetFloat();
    angles.y = Pop(stack)->GetFloat();
    angles.x = Pop(stack)->GetFloat();
    efk::Vec3 position;
    position.z = Pop(stack)->GetFloat();
    position.y = Pop(stack)->GetFloat();
    position.x = Pop(stack)->GetFloat();
    const std::string id = Pop(stack)->GetString();

    const EffectEntry *entry = Table().Find(id);
    if (!entry)
    {
        static std::set<std::string> reported;
        if (reported.insert(id).second)
            spdlog::error("CheckAndCreateEffect '{}': no such id in effects.json; nothing is created", id);
        return Return(stack, kNoHandle);
    }
    if (entry->kind == EffectEntry::Kind::ParticleSystem)
    {
        const entid_t particles = core.GetEntityId("particles");
        if (!particles)
        {
            spdlog::error("CheckAndCreateEffect '{}': no particles entity to create {} with", id, entry->file);
            return Return(stack, kNoHandle);
        }
        core.Send_Message(particles, "lsffffffl", direct ? PS_CREATEX : PS_CREATE, entry->file.c_str(), position.x,
                          position.y, position.z, angles.x, angles.y, angles.z, lifetime);
        return Return(stack, kNoHandle);
    }
    if (direct)
        angles = storm::math::directionAngles(angles);
    return Return(stack, storm::effects::PlayEntry(*entry, position, {angles.x, angles.y, angles.z}));
}

// ReloadEffect(string id): 1 when the effect was read again (or loaded for the first time), else 0.
uint32_t ReloadEffect(VS_STACK *stack)
{
    const std::string id = Pop(stack)->GetString();
    bool done = false;
    if (const EffectEntry *entry = EffekseerEntry("ReloadEffect", id))
    {
        const std::string path = entry->EffectPath();
        const efk::EffectId effect = efk::FindEffect(path);
        done = effect ? efk::ReloadEffect(effect) : efk::LoadEffect(path) != 0;
    }
    return Return(stack, done ? 1 : 0);
}

// ReloadAllEffects(): reads every loaded effect of the table again.
uint32_t ReloadAllEffects(VS_STACK *)
{
    for (const auto &[id, entry] : Table().entries)
        if (entry.kind == EffectEntry::Kind::Effekseer)
            if (const efk::EffectId effect = efk::FindEffect(entry.EffectPath()))
                efk::ReloadEffect(effect);
    return IFUNCRESULT_OK;
}

// UnloadEffect(string id): 1 when the effect was loaded and is released now, else 0.
uint32_t UnloadEffect(VS_STACK *stack)
{
    const std::string id = Pop(stack)->GetString();
    efk::EffectId effect = 0;
    if (const EffectEntry *entry = EffekseerEntry("UnloadEffect", id))
        effect = efk::FindEffect(entry->EffectPath());
    if (effect)
        efk::UnloadEffect(effect);
    return Return(stack, effect ? 1 : 0);
}

uint32_t UnloadAllEffects(VS_STACK *)
{
    efk::UnloadAllEffects();
    return IFUNCRESULT_OK;
}

// SetEffectPointLightGroup(handle, group): Caribbean Legend ties an effect (a torch, candle or heater flame) to a
// group of the location's night lights; this engine draws the effect and the group's lights independently.
uint32_t SetEffectPointLightGroup(VS_STACK *stack)
{
    Pop(stack);
    Pop(stack);
    static bool said = false;
    if (!std::exchange(said, true))
        spdlog::error("not implemented: SetEffectPointLightGroup; the game continues without tying the flames of "
                      "torches, candles and heaters to the location's night light groups");
    return IFUNCRESULT_OK;
}

void Register(const char *name, uint32_t arguments, const char *result, SIMPORTFUNC function)
{
    IFUNCINFO info;
    info.pFuncName = name;
    info.nArguments = arguments;
    info.pReturnValueName = result;
    info.pFuncAddress = function;
    core.SetScriptFunction(&info);
}
} // namespace

class EFFECTS_LIBRARY final : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        Register("CheckAndCreateEffect", 9, "int", CheckAndCreateEffect);
        Register("ReloadEffect", 1, "int", ReloadEffect);
        Register("ReloadAllEffects", 0, "void", ReloadAllEffects);
        Register("UnloadEffect", 1, "int", UnloadEffect);
        Register("UnloadAllEffects", 0, "void", UnloadAllEffects);
        Register("SetEffectPointLightGroup", 2, "void", SetEffectPointLightGroup);
        return true;
    }
};

CREATE_SCRIPTLIBRIARY(EFFECTS_LIBRARY)
CREATE_CLASS(Effects)
