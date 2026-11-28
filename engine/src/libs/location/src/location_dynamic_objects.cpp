#include "location_dynamic_objects.hpp"

#include "character.h"
#include "core.h"
#include "geometry.h"
#include "istring.hpp"
#include "model.h"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <limits>

namespace
{
// How often the scripts refresh the action hint (DynamicObject_UpdateLocation): the 250 ms of Caribbean Legend's own
// engine.
constexpr float kUpdatePeriod = 0.25f;
// How far the walking patch may lie above or below the point in front of a seat for one to walk there.
constexpr float kPatchTolerance = 0.25f;

// The script variable of a character: its element of the game's Characters array.
DATA *CharacterVariable(const Character &character)
{
    ATTRIBUTES *attributes = character.AttributesPointer;
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    const auto index = attributes ? static_cast<int32_t>(attributes->GetAttributeAsDword("index", 0xffffffff)) : -1;
    if (characters == nullptr || index < 0 || static_cast<uint32_t>(index) >= characters->GetElementsNum())
        return nullptr;
    DATA *element = characters->GetArrayElement(static_cast<uint32_t>(index));
    if (reinterpret_cast<VDATA *>(element)->GetAClass() != attributes)
    {
        spdlog::error("Dynamic objects: character '{}' is not Characters[{}]",
                      attributes->GetAttribute("id") ? attributes->GetAttribute("id") : "", index);
        return nullptr;
    }
    return element;
}

MODEL *Model(entid_t id)
{
    return static_cast<MODEL *>(core.GetEntityPointer(id));
}

std::optional<bool> Answer(const std::optional<VDATA *> &result)
{
    if (!result)
        return std::nullopt;
    return *result != nullptr && (*result)->GetInt() != 0;
}
} // namespace

Location::DynamicObjectsLink::DynamicObjectsLink(Location &location, VSTRING_CODEC &codec, std::string id)
    : location(location), registry(codec, std::move(id), *location.objectLibrary, *this)
{
}

std::optional<std::string> Location::DynamicObjectsLink::ReadPrefab(const std::string &model)
{
    const std::string file = "resource\\models\\object\\" + model + ".json";
    if (!fio->_FileOrDirectoryExists(file.c_str()))
        return std::nullopt;
    char *data = nullptr;
    uint32_t size = 0;
    if (!fio->LoadFile(file.c_str(), &data, &size))
    {
        spdlog::error("Dynamic objects: {} cannot be read", file);
        return std::nullopt;
    }
    const std::unique_ptr<char[]> owned(data);
    return std::string(data, size);
}

bool Location::DynamicObjectsLink::ModelAlive(entid_t model)
{
    return core.GetEntityPointer(model) != nullptr;
}

std::optional<storm::location::DynamicObjectActor> Location::DynamicObjectsLink::Player()
{
    Character *player = location.supervisor.player;
    DATA *variable = player ? CharacterVariable(*player) : nullptr;
    if (variable == nullptr)
        return std::nullopt;
    glm::vec3 position;
    player->GetPosition(position);
    return storm::location::DynamicObjectActor{variable, position, player->GetHeight()};
}

std::optional<storm::location::DynamicObjectActor> Location::DynamicObjectsLink::Actor(DATA *actor)
{
    const ATTRIBUTES *attributes = reinterpret_cast<VDATA *>(actor)->GetAClass();
    for (const auto &entry : location.supervisor.character)
        if (entry.c->AttributesPointer == attributes)
        {
            glm::vec3 position;
            entry.c->GetPosition(position);
            return storm::location::DynamicObjectActor{actor, position, entry.c->GetHeight()};
        }
    return std::nullopt;
}

std::vector<storm::location::ContainerLocator> Location::DynamicObjectsLink::ModelLocators(entid_t model)
{
    std::vector<storm::location::ContainerLocator> locators;
    MODEL *prop = Model(model);
    NODE *root = prop ? prop->GetNode(0) : nullptr;
    if (root == nullptr || root->geo == nullptr)
        return locators;
    GEOS::INFO info;
    root->geo->GetInfo(info);
    for (int32_t i = 0; i < info.nlabels; i++)
    {
        GEOS::LABEL label;
        root->geo->GetLabel(i, label);
        locators.push_back({label.group_name ? label.group_name : "", label.name ? label.name : "",
                            *reinterpret_cast<const glm::mat4 *>(label.m)});
    }
    return locators;
}

std::optional<float> Location::DynamicObjectsLink::ModelFloor(entid_t model)
{
    MODEL *prop = Model(model);
    NODE *root = prop ? prop->GetNode(0) : nullptr;
    if (root == nullptr || root->geo == nullptr)
        return std::nullopt;
    GEOS::INFO info;
    root->geo->GetInfo(info);
    const glm::vec3 centre(info.boxcenter.x, info.boxcenter.y, info.boxcenter.z);
    const glm::vec3 half = glm::vec3(info.boxsize.x, info.boxsize.y, info.boxsize.z) * 0.5f;
    float floor = std::numeric_limits<float>::max();
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 local =
            centre + half * glm::vec3(corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f, corner & 4 ? 1.0f : -1.0f);
        floor = std::min(floor, (prop->mtx * glm::vec4(local, 1.0f)).y);
    }
    return floor;
}

bool Location::DynamicObjectsLink::Reachable(const storm::location::DynamicObjectActor &actor, glm::vec3 &point)
{
    float actorHeight, pointHeight;
    const int32_t from = location.ptc.FindNode(actor.position, actorHeight);
    const int32_t to = location.ptc.FindNode(point, pointHeight);
    if (from < 0 || to < 0 || std::fabs(pointHeight - point.y) > kPatchTolerance)
        return false;
    point.y = pointHeight;
    int32_t next;
    glm::vec3 step;
    return from == to || location.ptc.FindPathDir(from, actor.position, to, point, next, step);
}

float Location::DynamicObjectsLink::Time()
{
    return time;
}

std::optional<bool> Location::DynamicObjectsLink::CanOfferInteractionAt(
    ATTRIBUTES &owner, const std::string &action, const storm::location::DynamicObjectActor &actor, ATTRIBUTES &result)
{
    return Answer(core.CallFunction("DynamicObject_CanOfferInteractionAt", "asefffa", &owner, action.c_str(),
                                    reinterpret_cast<VDATA *>(actor.variable), actor.position.x, actor.position.y,
                                    actor.position.z, &result));
}

std::optional<bool> Location::DynamicObjectsLink::Check(const char *function, ATTRIBUTES &owner, ATTRIBUTES &context)
{
    return Answer(core.CallFunction(function, "aa", &owner, &context));
}

std::optional<bool> Location::DynamicObjectsLink::UpdateSeatSession(DATA *actor, float seconds)
{
    return Answer(
        core.CallFunction("DynamicObject_HasSeats_UpdateActor", "ef", reinterpret_cast<VDATA *>(actor), seconds));
}

void Location::AddDynamicObject(const std::string &container, const storm::location::ObjectInstance &placement,
                                entid_t model)
{
    if (!dynamicObjects)
    {
        const char *id = AttributesPointer ? AttributesPointer->GetAttribute("id") : nullptr;
        dynamicObjects = std::make_unique<DynamicObjectsLink>(*this, AttributesPointer->GetStringCodec(), id ? id : "");
    }
    dynamicObjects->registry.AddPlacement(container, placement, model);
}

void Location::RemoveDynamicObjects(const std::vector<entid_t> &models)
{
    dynamicObjects->registry.RemoveModels(models);
}

uint64_t Location::DynamicObjectMessage(MESSAGE &message)
{
    const std::string action = message.String();
    if (!dynamicObjects)
    {
        spdlog::warn("Dynamic objects: '{}' asked of a location without object containers", action);
        return 0;
    }
    auto &registry = dynamicObjects->registry;
    if (message.GetCurrentFormatType() == 's') // "lssea" record, "lsseaa" entity
    {
        const std::string id = message.String();
        auto *result = static_cast<VDATA *>(message.ScriptVariablePointer());
        ATTRIBUTES *instance = action == "entity" ? message.AttributePointer() : nullptr;
        ATTRIBUTES *data = message.GetCurrentFormatType() == 'a' ? message.AttributePointer() : nullptr;
        if (action == "record")
        {
            ATTRIBUTES *record = registry.Record(id, data);
            result->SetAReference(record);
            return record != nullptr;
        }
        if (action == "entity")
        {
            const auto entity = registry.Entity(id, instance, data);
            result->Set(entity.value_or(entid_t{}));
            return entity.has_value();
        }
        storm::location::ReportUnknownCommand("MSG_LOCATION_DYNAMIC_OBJECT", action);
        return 0;
    }
    ATTRIBUTES *data = message.AttributePointer();
    ATTRIBUTES *instance = message.GetCurrentFormatType() == 'a' ? message.AttributePointer() : nullptr;
    const int32_t answer = registry.Request(action, *data, instance);
    if (answer == 0 && action != "interaction" && action != "exists" && data->GetAttribute("error") &&
        !data->GetAttribute("error")[0])
        data->SetAttribute("error", std::string_view("failed"));
    return answer;
}

void Location::UpdateDynamicObjects(float seconds)
{
    if (!dynamicObjects || !dynamicObjects->scripted)
        return;
    dynamicObjects->time += seconds;
    dynamicObjects->registry.Tick();
    dynamicObjects->sinceUpdate += seconds;
    if (dynamicObjects->sinceUpdate < kUpdatePeriod)
        return;
    dynamicObjects->sinceUpdate = 0.0f;
    if (!dynamicObjects->announced)
    {
        const char *id = AttributesPointer->GetAttribute("id");
        spdlog::info("Dynamic objects: {} in '{}'", dynamicObjects->registry.Count(), id ? id : "");
        dynamicObjects->announced = true;
    }
    if (!core.CallFunction("DynamicObject_UpdateLocation", "af", AttributesPointer, dynamicObjects->time))
    {
        // A game whose scripts have no dynamic objects (an older Caribbean Legend): its props stay props.
        spdlog::info("Dynamic objects: the scripts have no DynamicObject_UpdateLocation; none in this game");
        dynamicObjects->scripted = false;
    }
}
