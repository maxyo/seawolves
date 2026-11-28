#pragma once

// The game the engine runs, for the few things the game's own files cannot tell: values that two games give
// different meanings, built-ins whose signature differs, the script version the engine accepts. Everything a
// profile changes is a row of kGameProfiles below; code reads gameProfileFacts() and never compares profiles.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace storm
{
enum class GameProfile : uint8_t
{
    Legacy,          // To Each His Own and every game that is not detected as another profile
    CaribbeanLegend, // Caribbean Legend 2.0
};

enum class ScriptType : uint8_t
{
    Void,
    Int,
    Float,
    String,
};

// A built-in whose return type in this game is not the one of the engine's table.
struct BuiltinReturnType
{
    std::string_view name;
    ScriptType type;
};

// Where a colliding value is read.
enum class CollidingIdDomain : uint8_t
{
    Message,       // the first long of an entity message
    SoundPlayType, // the type argument of MSG_SOUND_PLAY
};

enum class CollidingIdMeaning : uint8_t
{
    None, // the value means the same in every game
    WhrLightningAdd,
    WhrUpdateState,
    SoundResume, // "lll": id, fade time
    SoundPause,  // "lll": id, paused
    SoundTypeMp3_3D,
    SoundTypeFmodEvent,
};

struct CollidingId
{
    CollidingIdDomain domain;
    int32_t value;
    CollidingIdMeaning meaning;
};

struct GameProfileFacts
{
    std::string_view name;
    // The profile's identity as the application's files name it (the touch interface's adapter set: scripts.json).
    std::string_view key;
    int32_t script_version; // the scripts' iScriptVersion the engine accepts, also GetEngineVersion()
    std::span<const BuiltinReturnType> builtin_returns;
    std::span<const CollidingId> colliding_ids; // every value whose meaning differs between the games
    // GetDLCenabled answers "owned" for every add-on: they were published as part of the game. Otherwise only for
    // the add-ons the player says he owns (platform/desktop_settings.hpp PlayerSettings).
    bool dlc_published_with_game;
    // The renderer makes normal, gloss and height maps and the neural x2 textures from the game's textures. A game that
    // ships its own maps gets none of them, not even for its models without maps, and no «Обработка текстур» settings.
    bool generates_texture_maps;
};

namespace profile_rows
{
inline constexpr CollidingId kLegacyIds[] = {
    {CollidingIdDomain::Message, 54000, CollidingIdMeaning::WhrLightningAdd},
    {CollidingIdDomain::Message, 77012, CollidingIdMeaning::SoundResume},
    {CollidingIdDomain::SoundPlayType, 3, CollidingIdMeaning::SoundTypeMp3_3D},
};
inline constexpr CollidingId kCaribbeanLegendIds[] = {
    {CollidingIdDomain::Message, 54000, CollidingIdMeaning::WhrUpdateState},
    {CollidingIdDomain::Message, 77012, CollidingIdMeaning::SoundPause},
    {CollidingIdDomain::SoundPlayType, 3, CollidingIdMeaning::SoundTypeFmodEvent},
};
// Caribbean Legend declares these built-ins with its own engine's types (Program/internal.c).
inline constexpr BuiltinReturnType kCaribbeanLegendReturns[] = {
    {"SaveEngineState", ScriptType::Int}, // whether the state was saved
    {"GetDeltaTime", ScriptType::Float},
    {"findSubStr", ScriptType::Int}, // the position, -1 when not found
};
} // namespace profile_rows

inline constexpr std::array<GameProfileFacts, 2> kGameProfiles = {{
    {"legacy (To Each His Own)", "legacy", 54128, {}, profile_rows::kLegacyIds, true, true},
    {"Caribbean Legend", "caribbean_legend", 2230981, profile_rows::kCaribbeanLegendReturns,
     profile_rows::kCaribbeanLegendIds, false, false},
}};

constexpr const GameProfileFacts &gameProfileFacts(GameProfile profile)
{
    return kGameProfiles[static_cast<size_t>(profile)];
}

constexpr CollidingIdMeaning collidingIdMeaning(GameProfile profile, CollidingIdDomain domain, int32_t value)
{
    for (const auto &id : gameProfileFacts(profile).colliding_ids)
        if (id.domain == domain && id.value == value)
            return id.meaning;
    return CollidingIdMeaning::None;
}

// Caribbean Legend when the game has both of its markers (resource/texture_library.json, resource/shaders), which no
// earlier game has; every other game is the legacy profile. `exists` answers for a game-relative path, any case.
// `log` receives the reason of a marker found alone.
inline GameProfile detectGameProfile(const std::function<bool(const std::string &)> &exists,
                                     const std::function<void(const std::string &)> &log)
{
    constexpr const char *library = "resource/texture_library.json";
    constexpr const char *shaders = "resource/shaders";
    const bool hasLibrary = exists(library), hasShaders = exists(shaders);
    if (hasLibrary && hasShaders)
        return GameProfile::CaribbeanLegend;
    if (hasLibrary || hasShaders)
        log(std::string("only ") + (hasLibrary ? library : shaders) + " of the Caribbean Legend markers is present");
    return GameProfile::Legacy;
}

// The running game's profile: detected from the game directory on first use and logged.
GameProfile gameProfile();
inline const GameProfileFacts &gameProfileFacts()
{
    return gameProfileFacts(gameProfile());
}

// For tools that compile scripts without a game directory (script_check test); logged.
void setGameProfile(GameProfile profile);
} // namespace storm
