#include "script_segments.hpp"

#include "core.h"
#include "istring.hpp"
#include "v_data.h"
#include "v_file_service.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <stdexcept>

namespace storm::ui_scripts
{
namespace
{
using Json = nlohmann::json;

Requirement ParseRequirement(const std::string &text, const std::string &where)
{
    const auto colon = text.find(':');
    const std::string kind = text.substr(0, colon == std::string::npos ? 0 : colon);
    std::string name = colon == std::string::npos ? std::string() : text.substr(colon + 1);
    if (name.empty())
        throw std::runtime_error(where + ": requirement '" + text + "' is not <kind>:<name>");
    Requirement requirement;
    requirement.text = text;
    if (kind == "fn")
    {
        const auto slash = name.rfind('/');
        const char *first = name.data() + (slash == std::string::npos ? name.size() : slash + 1);
        const char *last = name.data() + name.size();
        const auto [end, error] = std::from_chars(first, last, requirement.arguments);
        if (slash == 0 || error != std::errc() || end != last)
            throw std::runtime_error(where + ": requirement '" + text + "' is not fn:<name>/<arguments>");
        requirement.kind = Requirement::Kind::Function;
        name.resize(slash);
    }
    else if (kind == "var")
        requirement.kind = Requirement::Kind::Global;
    else if (kind == "ev")
        requirement.kind = Requirement::Kind::Event;
    else if (kind == "interface")
        requirement.kind = Requirement::Kind::Interface;
    else if (kind == "file")
        requirement.kind = Requirement::Kind::File;
    else if (kind == "node")
    {
        const auto hash = name.find('#');
        if (hash == 0 || hash == std::string::npos || hash + 1 == name.size())
            throw std::runtime_error(where + ": requirement '" + text + "' is not node:<interface>#<node>");
        requirement.kind = Requirement::Kind::Node;
        requirement.node = name.substr(hash + 1);
        name.resize(hash);
    }
    else
        throw std::runtime_error(where + ": requirement '" + text + "' has an unknown kind");
    requirement.name = std::move(name);
    return requirement;
}

std::vector<storm::ui::ScriptNode> ParseNodes(const Json &nodes, const std::string &where)
{
    std::vector<storm::ui::ScriptNode> parsed;
    for (const Json &node : nodes)
    {
        if (!node.is_array() || node.size() < 2 || node.size() > 3)
            throw std::runtime_error(where + ": node " + node.dump() +
                                     " is not [node, type] or [node, type, required]");
        parsed.push_back(
            {node[0].get<std::string>(), node[1].get<std::string>(), node.size() == 3 && node[2].get<bool>()});
    }
    return parsed;
}

// A screen entry: capability and component, with the interface and nodes when the segment is its own adapter.
storm::ui::ScriptScreen ParseScreen(const Json &entry, const std::string &where)
{
    storm::ui::ScriptScreen screen;
    screen.capability = entry.at("capability").get<std::string>();
    screen.component = entry.at("component").get<std::string>();
    if (entry.contains("interface") != entry.contains("nodes"))
        throw std::runtime_error(where + " screen " + screen.capability + ": interface and nodes come together");
    if (entry.contains("interface"))
    {
        screen.interface = entry.at("interface").get<std::string>();
        screen.nodes = ParseNodes(entry.at("nodes"), where + " screen " + screen.capability);
    }
    return screen;
}

storm::ui::ScriptScreen ParseBinding(const Json &entry, const std::string &where)
{
    storm::ui::ScriptScreen binding;
    binding.capability = entry.at("capability").get<std::string>();
    binding.interface = entry.at("interface").get<std::string>();
    binding.nodes = ParseNodes(entry.at("nodes"), where + " binding " + binding.capability);
    return binding;
}

std::vector<std::string> ParseContracts(const Json &entry, const char *key, const std::string &where)
{
    std::vector<std::string> contracts = entry.value(key, std::vector<std::string>());
    for (const std::string &contract : contracts)
        if (!contract.starts_with("adapter:") || contract.size() == 8)
            throw std::runtime_error(where + ": " + key + " '" + contract + "' is not adapter:<family>");
    return contracts;
}
} // namespace

std::vector<Segment> ParseSegments(std::string_view text)
{
    std::vector<Segment> segments;
    const Json list = Json::parse(text);
    for (const Json &entry : list.at("segments"))
    {
        Segment segment;
        segment.file = entry.at("file").get<std::string>();
        const std::string where = "scripts.json segment " + segment.file;
        try
        {
            segment.core = entry.value("core", false);
            for (const Json &requirement : entry.value("requires", Json::array()))
                segment.needs.push_back(ParseRequirement(requirement.get<std::string>(), where));
            segment.provides = ParseContracts(entry, "provides", where);
            segment.uses = ParseContracts(entry, "uses", where);
            for (const Json &binding : entry.value("binds", Json::array()))
                segment.binds.push_back(ParseBinding(binding, where));
            for (const Json &screen : entry.value("screens", Json::array()))
                segment.screens.push_back(ParseScreen(screen, where));
        }
        catch (const Json::exception &error)
        {
            throw std::runtime_error(where + ": " + error.what());
        }
        if (segment.core && (!segment.needs.empty() || !segment.uses.empty() || !segment.provides.empty()))
            throw std::runtime_error(where + ": a core segment cannot have requirements or contracts");
        segments.push_back(std::move(segment));
    }
    return segments;
}

GameSurface Probe(const storm::script::Program &program, std::function<bool(const std::string &)> file_exists)
{
    GameSurface surface;
    surface.file_exists = std::move(file_exists);
    program.ForEachFunction([&](const storm::script::Function &function) {
        surface.functions[AsciiLower(std::string(function.name))] = function.arguments;
    });
    program.ForEachGlobal([&](std::string_view name) { surface.globals.insert(AsciiLower(std::string(name))); });
    program.ForEachEvent([&](std::string_view name) { surface.events.insert(AsciiLower(std::string(name))); });
    // The interface table of the game's xinterface scripts: an interface's id is its ini file's name.
    if (auto *table = static_cast<VDATA *>(core.GetScriptVariable("Interfaces")); table && table->IsArray())
        for (uint32_t n = 0; n < table->GetElementsNum(); n++)
        {
            const char *ini = nullptr;
            if (!table->Get("IniFile", ini, n) || ini == nullptr)
                continue;
            std::string_view stem(ini);
            stem = stem.substr(stem.find_last_of("\\/") == std::string_view::npos ? 0 : stem.find_last_of("\\/") + 1);
            surface.interfaces.emplace(AsciiLower(std::string(stem.substr(0, stem.rfind('.')))), ini);
        }
    return surface;
}

std::string Unmet(const GameSurface &surface, const Requirement &requirement)
{
    using Kind = Requirement::Kind;
    switch (requirement.kind)
    {
    case Kind::Function: {
        const auto found = surface.functions.find(AsciiLower(requirement.name));
        if (found == surface.functions.end())
            return requirement.name + " is not a function of the game";
        if (found->second != requirement.arguments)
            return requirement.name + " takes " + std::to_string(found->second) + " arguments, not " +
                   std::to_string(requirement.arguments);
        return {};
    }
    case Kind::Global:
        return surface.globals.contains(AsciiLower(requirement.name))
                   ? std::string()
                   : requirement.name + " is not a global of the game";
    case Kind::Event:
        return surface.events.contains(AsciiLower(requirement.name)) ? std::string()
                                                                     : requirement.name + " has no handler in the game";
    case Kind::Interface:
        return surface.interfaces.contains(AsciiLower(requirement.name))
                   ? std::string()
                   : requirement.name + " is not an interface of the game";
    case Kind::Node: {
        const auto found = surface.interfaces.find(AsciiLower(requirement.name));
        if (found == surface.interfaces.end())
            return requirement.name + " is not an interface of the game";
        const auto ini = fio->OpenIniFile(found->second.c_str());
        if (!ini)
            return found->second + " cannot be read";
        return ini->TestSection(requirement.node.c_str()) ? std::string()
                                                          : found->second + " has no node " + requirement.node;
    }
    case Kind::File:
        return surface.file_exists(requirement.name) ? std::string() : requirement.name + " is not in the game";
    }
    return "unknown requirement";
}

void Attacher::Attach(storm::script::Program &program)
{
    if (!listed_)
    {
        segments_ = ParseSegments(hooks_.read_list());
        listed_ = true;
    }
    program_ = &program;
    const auto started = std::chrono::steady_clock::now();
    const GameSurface surface = Probe(program, hooks_.file_exists);
    spdlog::info("UI scripts: the game offers {} functions, {} globals, {} events, {} interfaces (probed in {:.2f} ms)",
                 surface.functions.size(), surface.globals.size(), surface.events.size(), surface.interfaces.size(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());

    std::vector<storm::ui::ScriptScreen> screens;
    std::string offered, missing;
    std::unordered_map<std::string, std::string> providers;            // contract -> the segment chosen for it
    std::unordered_map<std::string, storm::ui::ScriptScreen> bindings; // capability -> its adapter's binding
    const auto off = [&](const Segment &segment, const std::string &reason) {
        std::string names;
        for (const auto &screen : segment.screens)
        {
            names += (names.empty() ? "" : ", ") + screen.capability;
            missing += (missing.empty() ? "" : ", ") + screen.capability;
        }
        spdlog::error("UI script {} is off: {}; its screens ({}) are not drawn by scripts", segment.file, reason,
                      names.empty() ? "none" : names);
    };
    for (const Segment &segment : segments_)
    {
        // Every provider of a contract defines the same functions: only the first that attaches is chosen.
        const auto chosen =
            std::ranges::find_if(segment.provides, [&](const auto &c) { return providers.contains(c); });
        if (chosen != segment.provides.end())
        {
            spdlog::info("UI scripts: {} by {}; {} not needed", *chosen, providers.at(*chosen), segment.file);
            continue;
        }
        std::string reason;
        for (const Requirement &requirement : segment.needs)
            if (const std::string unmet = Unmet(surface, requirement); !unmet.empty())
                reason += (reason.empty() ? "" : "; ") + requirement.text + ": " + unmet;
        for (const std::string &contract : segment.uses)
            if (!providers.contains(contract))
                reason += (reason.empty() ? "" : "; ") + contract + ": no attached script provides it";
        if (reason.empty() && !program.Attach(segment.file.c_str()))
            reason = "does not compile (see the compile log)";
        if (!reason.empty() && segment.core)
            throw std::runtime_error("The touch interface's script " + segment.file + " " + reason +
                                     ". Reinstall the app.");
        if (!reason.empty())
        {
            off(segment, reason);
            continue;
        }
        for (const std::string &contract : segment.provides)
            providers.emplace(contract, segment.file);
        for (const auto &binding : segment.binds)
            bindings.emplace(binding.capability, binding);
        for (storm::ui::ScriptScreen screen : segment.screens)
        {
            if (screen.interface.empty())
            {
                const auto bound = bindings.find(screen.capability);
                if (bound == bindings.end())
                {
                    spdlog::error("UI script {}: screen {} has no binding from an attached adapter; not drawn by "
                                  "scripts",
                                  segment.file, screen.capability);
                    missing += (missing.empty() ? "" : ", ") + screen.capability;
                    continue;
                }
                screen.interface = bound->second.interface;
                screen.nodes = bound->second.nodes;
            }
            offered += (offered.empty() ? "" : ", ") + screen.capability;
            screens.push_back(std::move(screen));
        }
    }
    spdlog::info("UI scripts: capabilities offered: {}; missing: {}", offered.empty() ? "none" : offered,
                 missing.empty() ? "none" : missing);
    hooks_.attached(screens);
}

void Attacher::Released()
{
    program_ = nullptr;
    hooks_.released();
}
} // namespace storm::ui_scripts
