#include "object_library.hpp"

#include "istring.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
#include <utility>

namespace storm::location
{

ObjectLibrary ParseObjectLibrary(std::string_view json)
{
    ObjectLibrary library;
    // nlohmann keeps the last of two equal keys: the callback sees every key of the top object first.
    std::set<std::string> seen, duplicates;
    const auto spotDuplicates = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &parsed) {
        if (depth == 1 && event == nlohmann::json::parse_event_t::key)
            if (const auto key = AsciiLower(parsed.get<std::string>()); !seen.insert(key).second)
                duplicates.insert(key);
        return true;
    };
    const auto root = nlohmann::json::parse(json, spotDuplicates, false);
    if (!root.is_object())
    {
        library.errors.push_back(root.is_discarded() ? "invalid object library JSON"
                                                     : "the object library is not an object");
        return library;
    }
    library.valid = true;
    for (const auto &[group, model] : root.items())
    {
        const auto key = AsciiLower(group);
        if (duplicates.contains(key))
        {
            library.errors.push_back(fmt::format("duplicate object group '{}'", group));
            continue;
        }
        if (!model.is_string())
        {
            library.errors.push_back(fmt::format("model for group '{}' must be a string", group));
            continue;
        }
        if (group.empty() || model.get_ref<const std::string &>().empty())
        {
            library.errors.push_back(fmt::format("empty group or model name ('{}')", group));
            continue;
        }
        library.models.emplace(key, model.get<std::string>());
    }
    return library;
}

ContainerExpansion ExpandContainer(std::span<const ContainerLocator> locators, const ObjectLibrary &library)
{
    ContainerExpansion result;
    std::set<std::string> missingGroups, placed;
    for (const auto &locator : locators)
    {
        const auto found = library.models.find(AsciiLower(locator.group));
        if (found == library.models.end())
        {
            if (missingGroups.insert(locator.group).second)
                result.problems.push_back(fmt::format("group '{}' is not in location_objects.json", locator.group));
            continue;
        }
        if (!placed.insert(AsciiLower(locator.group) + ':' + AsciiLower(locator.name)).second)
        {
            result.problems.push_back(fmt::format("duplicate locator '{}:{}'", locator.group, locator.name));
            continue;
        }
        bool finite = true;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                finite = finite && std::isfinite(locator.transform[column][row]);
        if (!finite)
        {
            result.problems.push_back(fmt::format("invalid transform of locator '{}:{}'", locator.group, locator.name));
            continue;
        }
        result.instances.push_back({found->second, locator.group, locator.name, locator.transform});
    }
    return result;
}

} // namespace storm::location
