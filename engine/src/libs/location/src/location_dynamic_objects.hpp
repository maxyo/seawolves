// The location's side of its dynamic objects (dynamic_objects.hpp): the registry and what it asks of the location.
#pragma once

#include "dynamic_objects.hpp"
#include "location.h"

class Location::DynamicObjectsLink final : public storm::location::DynamicObjectsHost
{
  public:
    DynamicObjectsLink(Location &location, VSTRING_CODEC &codec, std::string id);

    std::optional<std::string> ReadPrefab(const std::string &model) override;
    bool ModelAlive(entid_t model) override;
    std::optional<storm::location::DynamicObjectActor> Player() override;
    std::optional<glm::vec3> ActorPosition(DATA *actor) override;
    std::vector<storm::location::ContainerLocator> ModelLocators(entid_t model) override;
    std::optional<float> ModelFloor(entid_t model) override;
    bool Reachable(const storm::location::DynamicObjectActor &actor, glm::vec3 &point) override;
    float Time() override;
    std::optional<bool> CanOfferInteractionAt(ATTRIBUTES &owner, const std::string &action,
                                              const storm::location::DynamicObjectActor &actor,
                                              ATTRIBUTES &result) override;
    std::optional<bool> Check(const char *function, ATTRIBUTES &owner, ATTRIBUTES &context) override;

    Location &location;
    storm::location::DynamicObjects registry;
    float time = 0.0f; // seconds the location has run
    float sinceUpdate = 0.0f;
    bool scripted = true; // false: the game's scripts have no dynamic objects
    bool announced = false;
};
