#include "effects.hpp"

#include "effect_messages.hpp"

#include "core.h"
#include "message.h"
#include "renderer.h"
#include "shared/layers.h"
#include "shared/messages.h"
#include "storm_glm.h"
#include "v_data.h"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <optional>
#include <stdexcept>

namespace efk = storm::render::effekseer;

namespace storm::effects
{
namespace
{
// Drawn after the opaque scene, at the place the scripts give the game's own particles (Program/particles.c).
constexpr priority_t kRealizePriority = 65536;

std::optional<std::vector<uint8_t>> ReadGameFile(const std::string &path)
{
    char *data = nullptr;
    uint32_t size = 0;
    if (!fio->LoadFile(path.c_str(), &data, &size))
        return std::nullopt;
    const std::unique_ptr<char[]> owned(data);
    return std::vector<uint8_t>(data, data + size);
}

void SetFloat(MESSAGE &message, float value)
{
    VDATA *variable = message.ScriptVariablePointer();
    if (!variable)
        throw std::runtime_error("Effects: a message wants a float variable it was not given");
    variable->Set(value);
}

uint64_t Handle(efk::InstanceId instance)
{
    return static_cast<uint64_t>(static_cast<int64_t>(instance)); // the scripts' INVALID_EFF_HANDLE is -1
}
} // namespace

const EffectTable &Table()
{
    static const EffectTable table = [] {
        const std::optional<std::vector<uint8_t>> json = ReadGameFile(std::string(kEffectTableFile));
        if (!json)
        {
            spdlog::error("Effects: {} cannot be read: no effect id is known", kEffectTableFile);
            return EffectTable{};
        }
        EffectTable parsed =
            ParseEffectTable(std::string_view(reinterpret_cast<const char *>(json->data()), json->size()));
        for (const std::string &error : parsed.errors)
            spdlog::error("Effects: {}: {}", kEffectTableFile, error);
        spdlog::info("Effects: {} effect ids", parsed.entries.size());
        return parsed;
    }();
    return table;
}

efk::InstanceId PlayEntry(const EffectEntry &entry, const efk::Vec3 &position, const efk::Vec3 &angles,
                          const efk::Vec3 &scale, int32_t start_frame)
{
    const efk::EffectId effect = efk::LoadEffect(entry.EffectPath());
    if (effect == 0)
        return efk::kNoInstance; // logged by the loader
    const efk::InstanceId instance = efk::Play(effect, position, angles, scale, start_frame);
    efk::SetSpeed(instance, entry.speed);
    return instance;
}
} // namespace storm::effects

Effects::~Effects()
{
    efk::StopAll();
}

bool Effects::Init()
{
    renderer_ = static_cast<VRenderer *>(core.GetService("renderer"));
    if (!renderer_)
        throw std::runtime_error("Effects: no renderer service");
    efk::SetFileReader(storm::effects::ReadGameFile);
    // The scripts create the entity without layers. The land and sea pairs are never active together.
    core.AddToLayer(EXECUTE, GetId(), 0);
    core.AddToLayer(SEA_EXECUTE, GetId(), 0);
    core.AddToLayer(REALIZE, GetId(), storm::effects::kRealizePriority);
    core.AddToLayer(SEA_REALIZE, GetId(), storm::effects::kRealizePriority);
    return true;
}

void Effects::ProcessStage(Stage stage, uint32_t delta)
{
    if (stage == Stage::execute)
        efk::Update(static_cast<float>(delta) * 0.001f);
    else if (stage == Stage::realize)
    {
        glm::mat4 view, projection;
        renderer_->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
        renderer_->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(projection));
        efk::Draw(&view[0][0], &projection[0][0]);
    }
}

uint64_t Effects::ProcessMessage(MESSAGE &message)
{
    using storm::effects::Placement;
    using storm::effects::Table;
    const int32_t code = message.Long();
    switch (code)
    {
    case MSG_EFFECT_PLAY: {
        const std::string id = message.String();
        const std::optional<Placement> placement = storm::effects::ReadPlacement(message);
        if (!placement)
        {
            spdlog::error("Effects: MSG_EFFECT_PLAY '{}': format {} is not lsfff[fff[fff[l]]]", id,
                          message.GetFormat());
            return storm::effects::Handle(efk::kNoInstance);
        }
        const storm::effects::EffectEntry *entry = Table().Find(id);
        if (!entry || entry->kind != storm::effects::EffectEntry::Kind::Effekseer)
        {
            spdlog::error("Effects: MSG_EFFECT_PLAY '{}': {}", id,
                          entry ? "a particle system, not an Effekseer effect" : "no such id in effects.json");
            return storm::effects::Handle(efk::kNoInstance);
        }
        return storm::effects::Handle(storm::effects::PlayEntry(*entry, placement->position, placement->angles,
                                                                placement->scale, placement->start_frame));
    }
    case MSG_EFFECT_PLAY_FILE: {
        const std::string path = message.String();
        const std::optional<Placement> placement = storm::effects::ReadPlacement(message);
        if (!placement)
        {
            spdlog::error("Effects: MSG_EFFECT_PLAY_FILE '{}': format {} is not lsfff[fff[fff[l]]]", path,
                          message.GetFormat());
            return storm::effects::Handle(efk::kNoInstance);
        }
        const efk::EffectId effect = efk::LoadEffect(path);
        return storm::effects::Handle(
            effect ? efk::Play(effect, placement->position, placement->angles, placement->scale, placement->start_frame)
                   : efk::kNoInstance);
    }
    case MSG_EFFECT_DELETE:
        efk::Stop(message.Long());
        return 0;
    case MSG_EFFECT_DELETE_ALL:
        efk::StopAll();
        return 0;
    case MSG_EFFECT_STOP_ROOT:
        efk::StopRoot(message.Long());
        return 0;
    case MSG_EFFECT_SET_PARAMETER: {
        const int32_t instance = message.Long();
        const int32_t index = message.Long();
        efk::SetDynamicInput(instance, index, message.Float());
        return 0;
    }
    case MSG_EFFECT_GET_PARAMETER: {
        const int32_t instance = message.Long();
        const int32_t index = message.Long();
        storm::effects::SetFloat(message, efk::GetDynamicInput(instance, index));
        return 0;
    }
    case MSG_EFFECT_SEND_TRIGGER: {
        const int32_t instance = message.Long();
        efk::SendTrigger(instance, message.Long());
        return 0;
    }
    case MSG_EFFECT_IS_PLAYING:
        return efk::Exists(message.Long()) ? 1 : 0;
    case MSG_EFFECT_PAUSE: {
        const int32_t instance = message.Long();
        efk::SetPaused(instance, message.Long() != 0);
        return 0;
    }
    case MSG_EFFECT_PAUSE_ALL:
        efk::SetPausedAll(message.Long() != 0);
        return 0;
    case MSG_EFFECT_UPDATE_POS: {
        const int32_t instance = message.Long();
        efk::SetLocation(instance, storm::effects::ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_ADD_SHIFT: {
        const int32_t instance = message.Long();
        efk::AddLocation(instance, storm::effects::ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_GET_POS: {
        const efk::Vec3 position = efk::GetLocation(message.Long());
        storm::effects::SetFloat(message, position.x);
        storm::effects::SetFloat(message, position.y);
        storm::effects::SetFloat(message, position.z);
        return 0;
    }
    case MSG_EFFECT_UPDATE_ROT: {
        const int32_t instance = message.Long();
        efk::SetRotation(instance, storm::effects::ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_TARGET_POS: {
        const int32_t instance = message.Long();
        efk::SetTargetLocation(instance, storm::effects::ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_SCALE: {
        const int32_t instance = message.Long();
        efk::SetScale(instance, storm::effects::ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_SPEED: {
        const int32_t instance = message.Long();
        efk::SetSpeed(instance, message.Float());
        return 0;
    }
    case MSG_EFFECT_GET_SPEED:
        storm::effects::SetFloat(message, efk::GetSpeed(message.Long()));
        return 0;
    case MSG_EFFECT_SET_COLOR: {
        const int32_t instance = message.Long();
        const auto argb = static_cast<uint32_t>(message.Long());
        efk::SetColor(instance, static_cast<uint8_t>(argb >> 16), static_cast<uint8_t>(argb >> 8),
                      static_cast<uint8_t>(argb), static_cast<uint8_t>(argb >> 24));
        return 0;
    }
    default:
        // MSG_EFFECT_SET_CAPTURE and MSG_EFFECT_STOP_CAPTURED (the editor's recording) included: no script sends them.
        spdlog::error("Effects: message {} is not handled", code);
        return 0;
    }
}
