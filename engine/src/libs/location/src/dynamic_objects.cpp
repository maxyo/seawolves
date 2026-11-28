#include "dynamic_objects.hpp"

#include "istring.hpp"
#include "land_protocol.hpp"
#include "string_compare.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <limits>

namespace storm::location
{
namespace
{
using json = nlohmann::json;

// How far above or below the approach point a sitting character may stand when it arrives: the height tolerance of
// the reachability test of the walking patch.
constexpr float kSeatHeightTolerance = 0.25f;

// The base types of DynamicObject_ConfigureComponentsNative and the lifetime modes they choose.
constexpr std::array<std::pair<int32_t, const char *>, 4> kLifetimeModes{
    {{0, "permanent"}, {1, "days"}, {2, "visit"}, {4, "load"}}};

// How a component's field is kept: in the prefab only, on the instance as a setting, or as the instance's state.
enum class Field
{
    Definition,
    Setting,
    State,
};

struct ComponentType
{
    const char *key;
    const char *dependency; // a component this one needs available, or nullptr
    std::vector<std::pair<const char *, Field>> fields;
    json defaults;
};

const std::vector<ComponentType> &ComponentTypes()
{
    static const std::vector<ComponentType> types = [] {
        const json blend = {
            {"animation", ""}, {"blendIn", 0.15}, {"blendOut", 0.15}, {"alignStart", 0.0}, {"alignEnd", 1.0}};
        return std::vector<ComponentType>{
            {"hasslots",
             nullptr,
             {{"enabled", Field::Definition},
              {"config", Field::Definition},
              {"state.slots", Field::State},
              {"state.slotLocators", Field::State}},
             {{"state", {{"slots", json::object()}}}}},
            {"hasitems",
             "hasslots",
             {{"enabled", Field::Setting},
              {"config", Field::Definition},
              {"canPlaceItem", Field::Setting},
              {"placeItem", Field::Setting},
              {"canTakeItem", Field::Setting},
              {"takeItem", Field::Setting}},
             {{"enabled", true},
              {"canPlaceItem", true},
              {"placeItem", nullptr},
              {"canTakeItem", true},
              {"takeItem", nullptr}}},
            {"hasseats",
             nullptr,
             {{"enabled", Field::Setting},
              {"config", Field::Definition},
              {"canSit", Field::Setting},
              {"sit", Field::Setting},
              {"canStand", Field::Setting},
              {"stand", Field::Setting},
              {"state.seats", Field::State}},
             {{"enabled", true},
              {"config",
               {{"distance", 2.0},
                {"approachDistance", 0.8},
                {"sitStartDistance", 0.25},
                {"enter", blend},
                {"exit", blend},
                {"animations",
                 {"Sit_Bench_Idle01", "Sit_Bench_Idle02", "Sit_Bench_Idle03", "Sit_Bench_Idle04", "Sit_Bench_Idle05",
                  "Sit_Bench_Idle06"}},
                {"seats", json::object()}}},
              {"canSit", true},
              {"sit", nullptr},
              {"canStand", true},
              {"stand", nullptr},
              {"state", {{"seats", json::object()}}}}},
            {"haswater", nullptr, {{"enabled", Field::Definition}, {"config", Field::Definition}}, json::object()},
            {"interaction", nullptr, {}, {{"enabled", true}, {"actions", json::object()}}},
            {"lifetime",
             nullptr,
             {},
             {{"enabled", true}, {"config", {{"mode", "permanent"}}}, {"state", json::object()}}},
        };
    }();
    return types;
}

const ComponentType *FindComponentType(std::string_view key)
{
    for (const auto &type : ComponentTypes())
        if (key == type.key)
            return &type;
    return nullptr;
}

void SetText(ATTRIBUTES &attributes, const char *name, std::string_view value)
{
    attributes.SetAttribute(name, value);
}

std::string Text(const ATTRIBUTES *attributes, const char *path, std::string_view fallback = {})
{
    if (attributes == nullptr)
        return std::string(fallback);
    const ATTRIBUTES *found =
        const_cast<ATTRIBUTES *>(attributes)->FindAClass(const_cast<ATTRIBUTES *>(attributes), path);
    return found && found->HasValue() ? found->GetValue() : std::string(fallback);
}

float Number(const ATTRIBUTES *attributes, const char *path, float fallback)
{
    const std::string text = Text(attributes, path);
    if (text.empty())
        return fallback;
    return static_cast<float>(std::atof(text.c_str()));
}

bool Flag(const ATTRIBUTES *attributes, const char *path, bool fallback)
{
    const std::string text = Text(attributes, path);
    if (text.empty())
        return fallback;
    if (iEquals(text, "true"))
        return true;
    if (iEquals(text, "false"))
        return false;
    return std::atof(text.c_str()) != 0.0;
}

ATTRIBUTES &Child(ATTRIBUTES &parent, const char *path)
{
    return *parent.CreateSubAClass(&parent, path);
}

void DeleteChild(ATTRIBUTES &parent, const char *name)
{
    if (ATTRIBUTES *child = parent.GetAttributeClass(name))
        parent.DeleteAttributeClassX(child);
}

void SetVector(ATTRIBUTES &record, const char *name, const glm::vec3 &value)
{
    ATTRIBUTES &vector = Child(record, name);
    vector.SetAttributeUseFloat("x", value.x);
    vector.SetAttributeUseFloat("y", value.y);
    vector.SetAttributeUseFloat("z", value.z);
}

// Adds the keys of `defaults` that `into` does not have, recursively.
void MergeMissing(ATTRIBUTES &into, const json &defaults)
{
    for (const auto &[key, value] : defaults.items())
    {
        ATTRIBUTES *existing = into.GetAttributeClass(key);
        if (existing == nullptr)
            JsonToAttributes(value, into.CreateAttribute(key));
        else if (value.is_object())
            MergeMissing(*existing, value);
    }
}

void Fail(ATTRIBUTES &data, std::string_view code, std::string_view details = {})
{
    data.SetAttribute("error", code);
    data.SetAttribute("errorDetails", details);
}

void ClearError(ATTRIBUTES &data)
{
    Fail(data, "", "");
}

std::string Hex(const std::array<uint8_t, 16> &bytes)
{
    std::string text;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text += '-';
        text += fmt::format("{:02x}", bytes[i]);
    }
    return text;
}

bool IsUuid(std::string_view text)
{
    if (text.size() != 36)
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash != (text[i] == '-') || (!dash && !std::isxdigit(static_cast<unsigned char>(text[i]))))
            return false;
    }
    return true;
}

// Euler angles in degrees and the scale of a locator matrix (rows: the axes, as the engine's matrices are).
void Decompose(const glm::mat4 &m, glm::vec3 &rotation, glm::vec3 &scale)
{
    const glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    scale = {glm::length(x), glm::length(y), glm::length(z)};
    const glm::vec3 nx = x / scale.x, ny = y / scale.y, nz = z / scale.z;
    rotation = glm::degrees(glm::vec3(std::atan2(-nz.y, std::sqrt(nx.y * nx.y + ny.y * ny.y)), std::atan2(nz.x, nz.z),
                                      std::atan2(nx.y, ny.y)));
}
} // namespace

std::string DynamicObjectId(std::string_view location, std::string_view container, std::string_view group,
                            std::string_view locator)
{
    uint64_t h1 = 0xcbf29ce484222325ull, h2 = 0x6c62272e07bb0142ull;
    const auto absorb = [&](uint8_t byte) {
        h1 = (h1 ^ byte) * 0x100000001b3ull;
        h2 = (h2 ^ byte) * 0xc2b2ae3d27d4eb4full;
    };
    for (const std::string_view part : {location, container, group, locator})
    {
        for (const char c : ResourceKey(std::string(part)))
            absorb(static_cast<uint8_t>(c));
        absorb(0xff);
    }
    std::array<uint8_t, 16> bytes{};
    for (int i = 0; i < 8; ++i)
    {
        bytes[i] = static_cast<uint8_t>(h1 >> (8 * i));
        bytes[8 + i] = static_cast<uint8_t>(h2 >> (8 * i));
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x80);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);
    return Hex(bytes);
}

void JsonToAttributes(const json &value, ATTRIBUTES &attributes)
{
    switch (value.type())
    {
    case json::value_t::object:
        for (const auto &[key, child] : value.items())
            JsonToAttributes(child, attributes.CreateAttribute(key));
        break;
    case json::value_t::array:
        for (size_t i = 0; i < value.size(); ++i)
            JsonToAttributes(value[i], attributes.CreateAttribute(std::to_string(i)));
        break;
    case json::value_t::string:
        attributes.SetValue(value.get_ref<const std::string &>());
        break;
    case json::value_t::boolean:
        attributes.SetValue(std::string_view(value.get<bool>() ? "1" : "0"));
        break;
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        attributes.SetValue(std::to_string(value.get<int64_t>()));
        break;
    case json::value_t::number_float:
        attributes.SetValue(fmt::format("{}", value.get<double>()));
        break;
    default:
        break;
    }
}

void ConfigureDynamicObjectComponents(ATTRIBUTES &instance, int32_t baseType, bool definition)
{
    ATTRIBUTES &components = Child(instance, "components");
    for (const auto &[type, mode] : kLifetimeModes)
        if (type == baseType)
            Child(components, "lifetime.config").SetAttribute("mode", std::string_view(mode));

    // The seat and item behaviours offer their actions; an action the scripts changed keeps its distance.
    const auto offer = [&](const char *behavior, std::initializer_list<const char *> actions, float distance,
                           bool targetRange) {
        ATTRIBUTES *component = components.GetAttributeClass(behavior);
        if (component == nullptr)
            return;
        distance = Number(component, "config.distance", distance);
        ATTRIBUTES &list = Child(components, "interaction.actions");
        for (const char *name : actions)
        {
            const std::string command = fmt::format("{}.{}", behavior, name);
            json defaults = {{"enabled", true}, {"command", command}};
            if (targetRange)
                defaults["targetRange"] = true;
            MergeMissing(list, json{{name, defaults}});
            ATTRIBUTES &action = *list.GetAttributeClass(name);
            if (Text(&action, "command") == command)
                action.SetAttributeUseFloat("distance", distance);
        }
    };
    offer("hasitems", {"place", "take"}, 0.0f, false);
    offer("hasseats", {"sit", "stand"}, 2.0f, true);

    for (const auto &type : ComponentTypes())
    {
        ATTRIBUTES *component = components.GetAttributeClass(type.key);
        if (component == nullptr)
            continue;
        json defaults = type.defaults;
        if (!definition)
            for (const auto &[field, policy] : type.fields)
            {
                if (policy == Field::State)
                    continue;
                defaults.erase(field);
                if (policy == Field::Definition)
                    DeleteChild(*component, field);
            }
        MergeMissing(*component, defaults);
    }
}

DynamicObjects::DynamicObjects(VSTRING_CODEC &codec, std::string location, const ObjectLibrary &library,
                               DynamicObjectsHost &host)
    : codec_(codec), location_(std::move(location)), library_(library), host_(host), records_(codec)
{
}

DynamicObjects::~DynamicObjects() = default;

const json *DynamicObjects::Prefab(const std::string &model)
{
    const std::string key = AsciiLower(model);
    auto found = prefabs_.find(key);
    if (found == prefabs_.end())
    {
        std::unique_ptr<json> parsed;
        if (const auto text = host_.ReadPrefab(model))
        {
            json value = json::parse(*text, nullptr, false);
            if (value.is_object())
            {
                // The engine reads a seat behaviour's settings from its prefab, with the defaults it does not set.
                const json::json_pointer config("/behavior/components/hasseats/config");
                if (value.contains(config) && value.at(config).is_object())
                    for (const auto &[key, fallback] : FindComponentType("hasseats")->defaults.at("config").items())
                        if (!value.at(config).contains(key))
                            value.at(config)[key] = fallback;
                parsed = std::make_unique<json>(std::move(value));
            }
            else
                spdlog::error("Dynamic objects: resource\\models\\object\\{}.json is not a JSON object", model);
        }
        found = prefabs_.emplace(key, std::move(parsed)).first;
    }
    return found->second.get();
}

void DynamicObjects::AddPlacement(std::string_view container, const ObjectInstance &placement, entid_t model)
{
    auto instance = std::make_unique<Instance>();
    instance->id = IsUuid(placement.locator)
                       ? AsciiLower(placement.locator)
                       : DynamicObjectId(location_, container, placement.group, placement.locator);
    if (FindById(instance->id) != nullptr)
    {
        spdlog::error("Dynamic objects: '{}:{}' of '{}' has the id {} of another object; left out", placement.group,
                      placement.locator, container, instance->id);
        return;
    }
    instance->prefab = placement.group;
    instance->model = placement.model;
    instance->entity = model;
    instance->transform = placement.transform;
    instance->position = glm::vec3(placement.transform[3]);
    Decompose(placement.transform, instance->rotation, instance->scale);
    for (const auto &locator : host_.ModelLocators(model))
        instance->locators.emplace(locator.name, locator.transform);
    instance->floor = host_.ModelFloor(model);

    ATTRIBUTES &record = records_.CreateAttribute(instance->id);
    SetText(record, "id", instance->id);
    SetText(record, "prefab", instance->prefab);
    SetVector(record, "position", instance->position);
    SetVector(record, "rotation", instance->rotation);
    SetVector(record, "scale", instance->scale);
    SetText(record, "model", instance->model);
    SetText(record, "location", location_);
    SetText(record, "runtimeOnlyPlacement", "1");
    SetText(record, "loaded", "1");
    SetText(record, "busy", "0");
    if (const json *prefab = Prefab(instance->model))
        if (const auto components = prefab->find("behavior"); components != prefab->end() && components->is_object())
            if (const auto found = components->find("components"); found != components->end())
                JsonToAttributes(*found, Child(record, "components"));
    ConfigureDynamicObjectComponents(record, -1);
    DeleteChild(Child(record, "components"), "lifetime"); // a placement lives as long as its container
    instance->record = &record;
    instances_.push_back(std::move(instance));
}

void DynamicObjects::RemoveModels(const std::vector<entid_t> &models)
{
    std::erase_if(instances_, [&](const std::unique_ptr<Instance> &instance) {
        if (std::ranges::find(models, instance->entity) == models.end())
            return false;
        records_.DeleteAttributeClassX(instance->record);
        return true;
    });
}

DynamicObjects::Instance *DynamicObjects::FindById(std::string_view id)
{
    for (const auto &instance : instances_)
        if (iEquals(instance->id, id))
            return instance.get();
    return nullptr;
}

DynamicObjects::Instance *DynamicObjects::Find(ATTRIBUTES *record, ATTRIBUTES &data)
{
    if (record != nullptr)
    {
        for (const auto &instance : instances_)
            if (instance->record == record)
                return instance.get();
        return nullptr;
    }
    return FindById(Text(&data, "id"));
}

void DynamicObjects::Describe(const Instance &instance, ATTRIBUTES &into) const
{
    SetText(into, "id", instance.id);
    SetText(into, "prefab", instance.prefab);
    SetVector(into, "position", instance.position);
    SetVector(into, "rotation", instance.rotation);
    SetVector(into, "scale", instance.scale);
}

ATTRIBUTES *DynamicObjects::Record(std::string_view id, ATTRIBUTES *data)
{
    if (data)
        ClearError(*data);
    if (Instance *instance = FindById(id))
        return instance->record;
    if (data)
        Fail(*data, "object_not_found", id);
    return nullptr;
}

std::optional<entid_t> DynamicObjects::Entity(std::string_view id, ATTRIBUTES *record, ATTRIBUTES *data)
{
    ATTRIBUTES scratch(codec_);
    ATTRIBUTES &errors = data ? *data : scratch;
    ClearError(errors);
    Instance *instance = nullptr;
    if (record)
        instance = Find(record, errors);
    else if (!id.empty())
        instance = FindById(id);
    else
    {
        Fail(errors, "invalid_request", "id");
        return std::nullopt;
    }
    if (instance && host_.ModelAlive(instance->entity))
        return instance->entity;
    Fail(errors, "object_not_found", id);
    return std::nullopt;
}

bool DynamicObjects::OfferCheck(Instance &owner, ATTRIBUTES &action, const DynamicObjectActor &actor,
                                ATTRIBUTES &result)
{
    const std::string command = Text(&action, "command");
    if (const auto dot = command.find('.'); dot != std::string::npos)
    {
        const std::string behavior = command.substr(0, dot);
        const ATTRIBUTES *components = owner.record->GetAttributeClass("components");
        const ComponentType *type = FindComponentType(behavior);
        const auto available = [&](const char *key) {
            const ATTRIBUTES *component = components ? components->GetAttributeClass(key) : nullptr;
            return component != nullptr && Flag(component, "enabled", true);
        };
        if (!available(behavior.c_str()) || (type && type->dependency && !available(type->dependency)))
        {
            Fail(result, "behavior_unavailable", behavior);
            return false;
        }
    }
    if (owner.record->GetAttributeClass("position") == nullptr)
    {
        Fail(result, "invalid_transform", "Missing vector: position");
        return false;
    }
    const glm::vec3 position(Number(owner.record, "position.x", 0.0f), Number(owner.record, "position.y", 0.0f),
                             Number(owner.record, "position.z", 0.0f));
    const float distance = Number(&action, "distance", 2.0f);
    const glm::vec3 offset = actor.position - position;
    if (!Flag(&action, "targetRange", false) && (distance < 0.0f || glm::dot(offset, offset) > distance * distance))
    {
        Fail(result, "out_of_range");
        return false;
    }
    const auto offered = host_.CanOfferInteractionAt(*owner.record, action.GetThisName(), actor, result);
    if (!offered)
    {
        Fail(result, "handler_missing", "DynamicObject_CanOfferInteractionAt");
        return false;
    }
    if (!*offered)
    {
        if (Text(&result, "error").empty())
            Fail(result, "condition_rejected");
        return false;
    }
    ClearError(result);
    return true;
}

const json *DynamicObjects::SeatConfig(const Instance &instance)
{
    const json *prefab = Prefab(instance.model);
    if (prefab == nullptr)
        return nullptr;
    const json::json_pointer path("/behavior/components/hasseats/config");
    return prefab->contains(path) && prefab->at(path).is_object() ? &prefab->at(path) : nullptr;
}

namespace
{
// The point `distance` in front of a seat (its +Z, level), on the floor under the furniture: where one walks to before
// sitting down, or where the sitting down starts.
std::optional<glm::vec3> InFrontOfSeat(const glm::mat4 &seat, float distance, float floor)
{
    const glm::vec2 facing(seat[2].x, seat[2].z);
    const float length = glm::length(facing);
    if (!std::isfinite(length) || length <= 1e-4f || !std::isfinite(distance) || distance < 0.0f)
        return std::nullopt;
    const glm::vec2 step = facing * (distance / length);
    const glm::vec3 point(seat[3].x + step.x, floor, seat[3].z + step.y);
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        return std::nullopt;
    return point;
}
} // namespace

bool DynamicObjects::SeatWithin(Instance &instance, const glm::vec3 &at, float radius)
{
    const json *config = SeatConfig(instance);
    if (config == nullptr || !config->contains("seats") || !config->at("seats").is_object())
        return false;
    for (const auto &[seat, description] : config->at("seats").items())
        if (const auto locator = instance.locators.find(seat); locator != instance.locators.end())
        {
            const glm::vec3 offset = glm::vec3((instance.transform * locator->second)[3]) - at;
            if (glm::dot(offset, offset) <= radius * radius)
                return true;
        }
    return false;
}

bool DynamicObjects::SeatCandidate(Instance &instance, const std::string &seat, const glm::mat4 &world,
                                   const DynamicObjectActor &actor, bool stand, ATTRIBUTES &data, ATTRIBUTES &chosen)
{
    // Nobody sits yet: a seat is free, and no one stands up from one.
    if (stand)
        return false;
    ATTRIBUTES context = data.Copy();
    SetText(context, "seatId", seat);
    glm::vec3 rotation, scale;
    Decompose(world, rotation, scale);
    SetVector(context, "position", glm::vec3(world[3]));
    SetVector(context, "rotation", rotation);
    const json &config = *SeatConfig(instance);
    const float floor = instance.floor.value_or(instance.position.y);
    auto approach = InFrontOfSeat(world, config.value("approachDistance", 0.8f), floor);
    if (!approach)
        return false;
    const bool initial = Flag(&data, "initialPlacement", false);
    const bool reachable = !initial && host_.Reachable(actor, *approach);
    if (!initial && !reachable)
        return false;
    ATTRIBUTES &way = Child(context, "approach");
    way.SetAttributeUseFloat("time", host_.Time());
    way.SetAttributeUseFloat("stopRadius", 0.3f);
    way.SetAttributeUseFloat("heightTolerance", kSeatHeightTolerance);
    SetText(way, "reachable", reachable ? "1" : "0");
    SetVector(way, "position", *approach);
    const auto start = InFrontOfSeat(world, config.value("sitStartDistance", 0.25f), floor);
    if (!start)
        return false;
    SetVector(Child(context, "sitStart"), "position", *start);
    if (const json::json_pointer name("/seats/" + seat + "/name"); config.contains(name))
        SetText(context, "seatName", config.at(name).get<std::string>());
    const auto accepted = host_.Check(stand ? "DynamicObject_HasSeatsCanStand" : "DynamicObject_HasSeatsCanSit",
                                      *instance.record, context);
    if (!accepted.value_or(false))
        return false;
    chosen = std::move(context);
    return true;
}

bool DynamicObjects::ChooseSeat(Instance &instance, ATTRIBUTES &data, bool stand)
{
    ClearError(data);
    const ATTRIBUTES *component = instance.record->FindAClass(instance.record, "components.hasseats");
    if (component == nullptr || !Flag(component, "enabled", true))
    {
        Fail(data, "behavior_unavailable", "hasseats");
        return false;
    }
    const ATTRIBUTES *actorAttribute = data.GetAttributeClass("actor");
    DATA *variable = actorAttribute ? actorAttribute->GetReference() : nullptr;
    const auto position = variable ? host_.ActorPosition(variable) : std::nullopt;
    if (!position)
    {
        Fail(data, "actor_unavailable");
        return false;
    }
    if (Flag(&data, "ambientRest", false))
    {
        ReportNotImplemented("ambient rest on dynamic object seats", kWithoutDynamicObjects);
        return false;
    }
    const json *config = SeatConfig(instance);
    if (config == nullptr || !config->contains("seats") || !config->at("seats").is_object())
    {
        Fail(data, "slot_not_found");
        return false;
    }
    const json &seats = config->at("seats");
    const std::string wanted = Text(&data, "seatId");
    if (!wanted.empty() && !seats.contains(wanted))
    {
        Fail(data, "slot_not_found", wanted);
        return false;
    }

    if (const json *animations = config->contains("animations") ? &config->at("animations") : nullptr)
        JsonToAttributes(*animations, Child(data, "animations"));
    for (const char *blend : {"enter", "exit"})
        if (config->contains(blend))
            JsonToAttributes(config->at(blend), Child(data, blend));
    for (const char *key : stand ? std::initializer_list<const char *>{"canStand", "stand"}
                                 : std::initializer_list<const char *>{"canSit", "sit"})
    {
        std::string value = Text(component, key);
        if (value.empty() && (key == std::string_view("sit") || key == std::string_view("stand")))
            value = stand ? "DynamicObject_HasSeats_DefaultStand" : "DynamicObject_HasSeats_DefaultSit";
        if (!value.empty())
            SetText(data, key, value);
    }

    const float radius = !stand && !Flag(&data, "initialPlacement", false) ? config->value("distance", 2.0f)
                                                                           : std::numeric_limits<float>::max();
    const DynamicObjectActor actor{variable, *position};
    std::optional<ATTRIBUTES> best;
    std::string bestSeat;
    float bestDistance = 0.0f;
    for (const auto &[seat, description] : seats.items())
    {
        if (!wanted.empty() && seat != wanted)
            continue;
        const auto locator = instance.locators.find(seat);
        if (locator == instance.locators.end())
            continue;
        if (instance.record->FindAClass(instance.record,
                                        fmt::format("components.hasseats.state.seats.{}.offset", seat).c_str()))
            ReportNotImplemented("seat offsets of dynamic objects", kWithoutDynamicObjects);
        const glm::mat4 world = instance.transform * locator->second;
        const glm::vec3 offset = glm::vec3(world[3]) - *position;
        const float distance = glm::dot(offset, offset);
        if (distance > radius * radius ||
            (best && (distance > bestDistance || (distance == bestDistance && seat > bestSeat))))
            continue;
        ATTRIBUTES chosen(codec_);
        if (!SeatCandidate(instance, seat, world, actor, stand, data, chosen))
            continue;
        best = std::move(chosen);
        bestSeat = seat;
        bestDistance = distance;
    }
    if (!best)
    {
        Fail(data, "no_suitable_slot");
        return false;
    }
    data = std::move(*best);
    ClearError(data);
    return true;
}

bool DynamicObjects::Interactions(Instance &owner, const DynamicObjectActor &actor, ATTRIBUTES &data)
{
    ClearError(data);
    SetText(data, "count", "0");
    SetText(data, "ownerId", owner.id);
    DeleteChild(data, "items");
    ATTRIBUTES &items = data.CreateAttribute("items");
    if (!host_.ModelAlive(owner.entity))
    {
        Fail(data, "object_not_found", owner.id);
        return false;
    }
    ATTRIBUTES *actions = owner.record->FindAClass(owner.record, "components.interaction.actions");
    size_t count = 0;
    for (size_t n = 0; actions && n < actions->GetAttributesNum(); ++n)
    {
        ATTRIBUTES &action = *actions->GetAttributeClass(static_cast<uint32_t>(n));
        const std::string command = Text(&action, "command");
        ATTRIBUTES check(codec_);
        ATTRIBUTES context(codec_);
        if (command == "hasseats.sit" || command == "hasseats.stand")
        {
            const json *config = SeatConfig(owner);
            const float radius = config ? config->value("distance", 2.0f) : 2.0f;
            if (command == "hasseats.sit" && !SeatWithin(owner, actor.position, radius))
                continue;
            if (!OfferCheck(owner, action, actor, check))
                continue;
            context.CreateAttribute("actor").SetReference(actor.variable);
            if (!ChooseSeat(owner, context, command == "hasseats.stand"))
                continue;
        }
        else if (command.starts_with("hasitems."))
        {
            ReportNotImplemented(fmt::format("dynamic object action '{}'", command), kWithoutDynamicObjects);
            continue;
        }
        else if (!OfferCheck(owner, action, actor, check))
            continue;
        ATTRIBUTES &item = items.CreateAttribute(fmt::format("item{}", count++));
        const auto dot = command.find('.');
        SetText(item, "ownerId", owner.id);
        SetText(item, "behavior", command.empty() ? "interaction" : command.substr(0, dot));
        SetText(item, "action", action.GetThisName());
        SetText(item, "command", command);
        if (action.GetAttributeClass("priority"))
            SetText(item, "priority", std::to_string(static_cast<int32_t>(Number(&action, "priority", 0.0f))));
        for (const char *field : {"slotId", "slotName", "objectId", "seatId", "seatName"})
            if (const ATTRIBUTES *value = context.GetAttributeClass(field); value && value->HasValue())
                SetText(item, field, value->GetValue());
    }
    SetText(data, "count", std::to_string(count));
    return true;
}

int32_t DynamicObjects::Nearest(ATTRIBUTES &data)
{
    SetText(data, "ownerId", "");
    ClearError(data);
    SetText(data, "count", "0");
    DeleteChild(data, "items");
    const auto actor = host_.Player();
    if (!actor)
    {
        Fail(data, "actor_unavailable");
        return 0;
    }
    std::optional<ATTRIBUTES> best;
    float bestDistance = 0.0f;
    for (const auto &instance : instances_)
    {
        const glm::vec3 offset = actor->position - instance->position;
        const float distance = glm::dot(offset, offset);
        if (best && distance >= bestDistance)
            continue;
        const ATTRIBUTES *actions = instance->record->FindAClass(instance->record, "components.interaction.actions");
        if (actions == nullptr || actions->GetAttributesNum() == 0)
            continue;
        ATTRIBUTES candidate(codec_);
        if (!Interactions(*instance, *actor, candidate) || Number(&candidate, "count", 0.0f) <= 0.0f)
            continue;
        best = candidate.Copy();
        bestDistance = distance;
    }
    if (!best)
        return 0;
    for (size_t n = 0; n < best->GetAttributesNum(); ++n)
    {
        const ATTRIBUTES *child = best->GetAttributeClass(static_cast<uint32_t>(n));
        DeleteChild(data, child->GetThisName());
        data.CreateAttribute(child->GetThisName()) = child->Copy();
    }
    return 1;
}

int32_t DynamicObjects::Request(std::string_view action, ATTRIBUTES &data, ATTRIBUTES *record)
{
    ClearError(data);
    if (action == "status")
    {
        SetText(data, "ready", "1");
        SetText(data, "blocked", "0");
        SetText(data, "busy", "0");
        SetText(data, "dirty", "0");
        SetText(data, "location", location_);
        SetText(data, "path", "");
        SetText(data, "status", "");
        return 1;
    }
    if (action == "library")
    {
        size_t n = 0;
        for (const auto &[id, model] : library_.entries)
        {
            ATTRIBUTES &item = data.CreateAttribute(fmt::format("item{}", n++));
            SetText(item, "id", id);
            SetText(item, "model", model);
        }
        SetText(data, "count", std::to_string(n));
        return 1;
    }
    if (action == "prefab")
    {
        const std::string id = Text(&data, "prefab");
        if (id.empty())
        {
            Fail(data, "invalid_request", "prefab");
            return 0;
        }
        const auto model = library_.models.find(AsciiLower(id));
        const json *prefab = model == library_.models.end() ? nullptr : Prefab(model->second);
        if (prefab == nullptr)
        {
            Fail(data, "prefab_not_found", id);
            return 0;
        }
        SetText(data, "id", id);
        SetText(data, "model", model->second);
        SetText(data, "behaviorDefinition", "1");
        if (const auto behavior = prefab->find("behavior"); behavior != prefab->end() && behavior->is_object())
            if (const auto found = behavior->find("components"); found != behavior->end())
                JsonToAttributes(*found, Child(data, "components"));
        ConfigureDynamicObjectComponents(data, -1, true);
        return 1;
    }
    if (action == "list_loaded")
    {
        size_t n = 0;
        for (const auto &instance : instances_)
            Describe(*instance, data.CreateAttribute(fmt::format("item{}", n++)));
        SetText(data, "count", std::to_string(n));
        return 1;
    }
    if (action == "interaction")
        return Nearest(data);
    if (action.starts_with("seats_"))
    {
        Instance *instance = Find(record, data);
        if (instance == nullptr || !host_.ModelAlive(instance->entity))
        {
            Fail(data, "object_not_found", Text(&data, "id"));
            return 0;
        }
        if (action == "seats_can_sit" || action == "seats_can_stand")
            return ChooseSeat(*instance, data, action == "seats_can_stand") ? 1 : 0;
        const json *config = SeatConfig(*instance);
        if (action == "seats_animations")
        {
            if (config == nullptr || !config->contains("animations"))
            {
                Fail(data, "behavior_unavailable", "hasseats");
                return 0;
            }
            JsonToAttributes(config->at("animations"), Child(data, "animations"));
            return 1;
        }
        if (action == "seats_describe")
        {
            if (config == nullptr || !config->contains("seats") || !config->at("seats").is_object())
            {
                Fail(data, "behavior_unavailable", "hasseats");
                return 0;
            }
            size_t n = 0;
            for (const auto &[seat, description] : config->at("seats").items())
            {
                const auto locator = instance->locators.find(seat);
                if (locator == instance->locators.end())
                    continue;
                const glm::mat4 world = instance->transform * locator->second;
                const auto approach = InFrontOfSeat(world, config->value("approachDistance", 0.8f),
                                                    instance->floor.value_or(instance->position.y));
                if (!approach)
                    continue;
                ATTRIBUTES &item = data.CreateAttribute(fmt::format("item{}", n++));
                SetText(item, "seatId", seat);
                SetText(item, "occupied", "0"); // nobody sits yet
                SetVector(item, "position", glm::vec3(world[3]));
                SetVector(item, "approach", *approach);
            }
            SetText(data, "count", std::to_string(n));
            return 1;
        }
    }
    if (action == "get" || action == "exists" || action == "interactions")
    {
        Instance *instance = Find(record, data);
        const bool alive = instance != nullptr && host_.ModelAlive(instance->entity);
        if (action == "exists")
            return alive ? 1 : 0;
        if (!alive)
        {
            Fail(data, "object_not_found", instance ? instance->id : Text(&data, "id"));
            return 0;
        }
        if (action == "get")
        {
            Describe(*instance, data);
            return 1;
        }
        ATTRIBUTES *actorAttribute = data.GetAttributeClass("actor");
        DATA *actor = actorAttribute ? actorAttribute->GetReference() : nullptr;
        if (actor == nullptr)
        {
            Fail(data, "actor_unavailable");
            return 0;
        }
        if (data.GetAttributeClass("actorPosition") == nullptr)
        {
            Fail(data, "actor_position_unavailable");
            return 0;
        }
        const DynamicObjectActor who{actor,
                                     {Number(&data, "actorPosition.x", 0.0f), Number(&data, "actorPosition.y", 0.0f),
                                      Number(&data, "actorPosition.z", 0.0f)}};
        return Interactions(*instance, who, data) ? 1 : 0;
    }
    ReportNotImplemented(fmt::format("dynamic object request '{}'", action), kWithoutDynamicObjects);
    Fail(data, "unknown_operation", action);
    return 0;
}

} // namespace storm::location
