// Object containers: a model whose locators place props. Each locator group of the container names a prop of the
// game's object library (resource/ini/location_objects.json: {"<group>": "<model>"}); each locator of the group is
// one instance of that prop, the locator's matrix its transform.
#pragma once

#include "storm_glm.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::location
{

struct ObjectLibrary
{
    std::unordered_map<std::string, std::string> models; // folded group name -> model name
    std::vector<std::string> errors;                     // one line per entry left out; empty when all were read
    bool valid = false;                                  // false: the file is not a JSON object
};

// Group names compare in any case: the containers were authored on a case-insensitive system.
ObjectLibrary ParseObjectLibrary(std::string_view json);

struct ContainerLocator
{
    std::string group, name;
    glm::mat4 transform;
};

struct ObjectInstance
{
    std::string model, group, locator;
    glm::mat4 transform;
};

struct ContainerExpansion
{
    std::vector<ObjectInstance> instances;
    std::vector<std::string> problems; // locators or groups left out, one line each
};

ContainerExpansion ExpandContainer(std::span<const ContainerLocator> locators, const ObjectLibrary &library);

} // namespace storm::location
