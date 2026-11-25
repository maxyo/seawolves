// The game's dialogue as the engine fills it: Host::SetDialogue takes the struct. Its JSON (Host::SetState
// "dialogue") is the fixture format of tools and checks, read into this struct (src/screens/dialogue.cpp).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace storm::ui
{

struct Dialogue
{
    // A reply the player can choose; `index` is its link in the dialogue node. An editable one is answered with typed
    // text of at most `max_code_points`.
    struct Choice
    {
        int index = 0;
        std::string text;
        bool editable = false;
        int max_code_points = 256;
        bool operator==(const Choice &) const = default;
    };
    // The engine refused a typed answer to the reply `index`: its code (too_long, invalid_text, unavailable).
    struct Refusal
    {
        int index = -1;
        std::string error;
        bool operator==(const Refusal &) const = default;
    };
    bool active = false; // false: no dialogue now (the other fields are empty)
    uint64_t generation = 0;
    std::string language, speaker, text; // the game's texts as its scripts hold them
    std::vector<Choice> choices;
    std::optional<long long> money; // the hero's money, the most a typed amount offers; none when unknown
    std::optional<Refusal> refusal;
    bool operator==(const Dialogue &) const = default;
};

} // namespace storm::ui
