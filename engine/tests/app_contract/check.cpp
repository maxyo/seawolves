// The engine's side of the names the application shares with it (tools/android/app_contract.json) that live in engine
// code as data: the text commands the main loop takes, the shortcuts the native queue takes and the screens they open.
#include "platform/android_shortcuts.hpp"
#include "platform/android_ui_commands.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

int main(int argc, char **argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1]);
    const auto manifest = nlohmann::json::parse(file);
    using namespace storm::android;
    std::set<std::string> declared = manifest.at("hud_host_input").at("shortcut"), taken, screens;
    for (const auto &[name, shortcut] : kGameplayShortcuts)
    {
        taken.emplace(name);
        GameplayShortcuts queue;
        assert(queue.offer(name, 0) && queue.take(0, true) == shortcut);
        if (const char *screen = shortcutScreen(shortcut))
            screens.emplace(screen);
    }
    for (const auto &name : declared)
        if (!taken.contains(name))
            std::cerr << "shortcut '" << name << "' of app_contract.json: the native queue does not take it\n";
    for (const auto &name : taken)
        if (!declared.contains(name))
            std::cerr << "shortcut '" << name << "' the native queue takes: not in app_contract.json\n";
    const std::set<std::string> opened = manifest.at("script_events").at("AndroidOpenScreen");
    if (screens != opened)
        std::cerr << "AndroidOpenScreen: the shortcuts open other screens than app_contract.json lists\n";
    GameplayShortcuts queue;
    assert(!queue.offer("ChrAction", 0)); // never an arbitrary control or script name

    const std::set<std::string> commands = manifest.at("engine_commands");
    std::set<std::string> dispatched;
    for (const auto &entry : kUiCommands)
    {
        dispatched.emplace(entry.name);
        const std::string whole(entry.name);
        assert(parseUiCommand(whole + (entry.prefix ? "x" : ""))->command == entry.command);
        assert(entry.prefix || !parseUiCommand(whole + "x"));
    }
    if (dispatched != commands)
        std::cerr << "engine_commands of app_contract.json differ from the main loop's (android_ui_commands.hpp)\n";
    if (declared != taken || screens != opened || dispatched != commands)
        return 1;
    std::cout << "PASS app contract: " << dispatched.size() << " engine commands, " << taken.size() << " shortcuts and "
              << screens.size() << " screens agree with app_contract.json\n";
}
