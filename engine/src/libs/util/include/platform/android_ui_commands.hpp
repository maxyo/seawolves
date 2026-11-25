#pragma once
// The text commands of the UI library's game-interface screens (storm::ui::EngineCommand) the engine's main loop
// takes: one table, read by the dispatcher and by tests/app_contract against tools/android/app_contract.json.
#include <array>
#include <optional>
#include <string_view>

namespace storm::android
{
enum class UiCommand
{
    Back,        // the interface's back
    PressSound,  // a press started on a game interface: the scripts' click
    Title,       // the title screen's choice
    Quick,       // the pause menu's quick save or load, after the menu closed
    Dialogue,    // a dialogue's choice or text
    GameOverEnd, // the death screen's choice: end the death picture
};

struct UiCommandName
{
    UiCommand command;
    std::string_view name; // the whole command, or its start when `prefix`
    bool prefix;
};

inline constexpr std::array<UiCommandName, 6> kUiCommands{{
    {UiCommand::Back, "back", false},
    {UiCommand::PressSound, "uisound:press", false},
    {UiCommand::Title, "main:", true},
    {UiCommand::Quick, "quick:", true},
    {UiCommand::Dialogue, "dialogue:", true},
    {UiCommand::GameOverEnd, "widget:game_over\tEND\t", true},
}};

struct ParsedUiCommand
{
    UiCommand command;
    std::string_view argument; // what follows a prefix
};

inline std::optional<ParsedUiCommand> parseUiCommand(std::string_view text)
{
    for (const auto &entry : kUiCommands)
        if (entry.prefix ? text.starts_with(entry.name) : text == entry.name)
            return ParsedUiCommand{entry.command, text.substr(entry.name.size())};
    return std::nullopt;
}
} // namespace storm::android
