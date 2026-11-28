// Caribbean Legend's dynamic objects: the props a location's object containers place, with the behaviour of their
// prefab (resource/models/object/<model>.json, behavior.components), as the game's scripts reach them through
// MSG_LOCATION_DYNAMIC_OBJECT. Each placement is an instance with a record (an attribute tree the scripts read and
// write); the scripts hold the policy (conditions, callbacks, AI), the engine the instances, their places and the
// nearest target.
#pragma once

#include "attributes.h"
#include "entity.h"
#include "object_library.hpp"
#include "storm_glm.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::location
{

// The id of a placement whose locator is not named by a UUID: a version-8 UUID of the location, the container (models
// path + name, without ".gm"), the group and the locator, the same in every session (records and saves name it).
std::string DynamicObjectId(std::string_view location, std::string_view container, std::string_view group,
                            std::string_view locator);

// JSON into attributes: objects and arrays (children "0", "1", ...) become children, values text; null has no value.
void JsonToAttributes(const nlohmann::json &json, ATTRIBUTES &attributes);

// DynamicObject_ConfigureComponentsNative: the lifetime mode of the base type (PERMANENT 0, DAYS 1, VISIT 2,
// UNTIL_LOAD 4; any other keeps it), the interaction actions of the seat and item behaviours, and the missing
// defaults of each component. A behaviour definition (a prefab) keeps its definition fields; an instance loses them
// and reads them from its prefab.
void ConfigureDynamicObjectComponents(ATTRIBUTES &instance, int32_t baseType, bool definition = false);

// A character the scripts name: its script variable (an element of Characters) and where it stands.
struct DynamicObjectActor
{
    DATA *variable = nullptr;
    glm::vec3 position{};
};

// What the registry needs from the location and the scripts.
class DynamicObjectsHost
{
  public:
    virtual ~DynamicObjectsHost() = default;
    // The text of resource/models/object/<model>.json; nullopt when there is none.
    virtual std::optional<std::string> ReadPrefab(const std::string &model) = 0;
    virtual bool ModelAlive(entid_t model) = 0;
    virtual std::optional<DynamicObjectActor> Player() = 0;
    // Where a character the scripts name stands, when it is in this location.
    virtual std::optional<glm::vec3> ActorPosition(DATA *actor) = 0;
    // The locators of a prop's model (a seat is the locator named by its id), in the model's own space.
    virtual std::vector<ContainerLocator> ModelLocators(entid_t model) = 0;
    // The lowest world height of the prop's bounding box: the floor under the furniture.
    virtual std::optional<float> ModelFloor(entid_t model) = 0;
    // Whether the actor can walk to `point` over the walking patch; `point.y` becomes the patch height there.
    virtual bool Reachable(const DynamicObjectActor &actor, glm::vec3 &point) = 0;
    virtual float Time() = 0;
    // DynamicObject_CanOfferInteractionAt(owner, action, actor, x, y, z, result); nullopt: the scripts have none.
    virtual std::optional<bool> CanOfferInteractionAt(ATTRIBUTES &owner, const std::string &action,
                                                      const DynamicObjectActor &actor, ATTRIBUTES &result) = 0;
    // A script check of an owner and a context (DynamicObject_HasSeatsCanSit ...); nullopt: the scripts have none.
    virtual std::optional<bool> Check(const char *function, ATTRIBUTES &owner, ATTRIBUTES &context) = 0;
};

class DynamicObjects
{
  public:
    DynamicObjects(VSTRING_CODEC &codec, std::string location, const ObjectLibrary &library, DynamicObjectsHost &host);
    ~DynamicObjects();

    // A prop an object container placed; `container` is the folded models path + container name.
    void AddPlacement(std::string_view container, const ObjectInstance &placement, entid_t model);
    // The instances of a container's props, when the container goes.
    void RemoveModels(const std::vector<entid_t> &models);

    // MSG_LOCATION_DYNAMIC_OBJECT "lsa" / "lsaa": the request's answer (0 or 1); data.error says why not.
    int32_t Request(std::string_view action, ATTRIBUTES &data, ATTRIBUTES *instance);
    // "lssea" record: the record of the instance with this id, nullptr (and data.error) when there is none.
    ATTRIBUTES *Record(std::string_view id, ATTRIBUTES *data);
    // "lsseaa" entity: the model of the instance (by record when given, else by id).
    std::optional<entid_t> Entity(std::string_view id, ATTRIBUTES *instance, ATTRIBUTES *data);

    [[nodiscard]] size_t Count() const
    {
        return instances_.size();
    }

  private:
    struct Instance
    {
        std::string id, prefab, model;
        glm::vec3 position{}, rotation{}, scale{1.0f};
        entid_t entity{};
        glm::mat4 transform{1.0f};
        std::unordered_map<std::string, glm::mat4> locators; // by name, in the model's space
        std::optional<float> floor;                          // see DynamicObjectsHost::ModelFloor
        ATTRIBUTES *record = nullptr;
    };

    const nlohmann::json *Prefab(const std::string &model);
    Instance *Find(ATTRIBUTES *instance, ATTRIBUTES &data);
    Instance *FindById(std::string_view id);
    void Describe(const Instance &instance, ATTRIBUTES &into) const;
    bool Interactions(Instance &owner, const DynamicObjectActor &actor, ATTRIBUTES &data);
    bool OfferCheck(Instance &owner, ATTRIBUTES &action, const DynamicObjectActor &actor, ATTRIBUTES &result);
    int32_t Nearest(ATTRIBUTES &data);
    const nlohmann::json *SeatConfig(const Instance &instance);
    bool SeatWithin(Instance &instance, const glm::vec3 &at, float radius);
    bool ChooseSeat(Instance &instance, ATTRIBUTES &data, bool stand);
    bool SeatCandidate(Instance &instance, const std::string &seat, const glm::mat4 &world,
                       const DynamicObjectActor &actor, bool stand, ATTRIBUTES &data, ATTRIBUTES &chosen);

    VSTRING_CODEC &codec_;
    std::string location_;
    const ObjectLibrary &library_;
    DynamicObjectsHost &host_;
    ATTRIBUTES records_; // the records of the placed instances: the engine's, not a script variable's
    std::vector<std::unique_ptr<Instance>> instances_;
    std::unordered_map<std::string, std::unique_ptr<nlohmann::json>> prefabs_; // by folded model; null: none
};

} // namespace storm::location
