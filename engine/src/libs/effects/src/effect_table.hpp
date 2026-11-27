// The effect ids a game names in Resource/Effects/effects.json (README.md): parsed once, read by the natives and the
// Effects entity. Pure code, checked by tests/effects.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace storm::effects
{

inline constexpr std::string_view kEffectsDirectory = "resource/effects/";
inline constexpr std::string_view kEffectTableFile = "resource/effects/effects.json";

struct EffectEntry
{
    enum class Kind
    {
        Effekseer,      // .efkefc, played by the Effects entity
        ParticleSystem, // .xps, created by the "particles" entity
    };
    Kind kind = Kind::Effekseer;
    std::string file; // as the table names it: "CLTorchFire.efkefc", "Fire.xps"
    float speed = 1.0f;

    // The game path of an Effekseer effect.
    std::string EffectPath() const
    {
        return std::string(kEffectsDirectory) + file;
    }
};

struct EffectTable
{
    std::map<std::string, EffectEntry> entries; // by the id folded to lower case
    std::vector<std::string> errors;            // entries left out, and why

    // The entry of an id (case-insensitive), or nullptr.
    const EffectEntry *Find(std::string_view id) const;
};

// The table of a whole effects.json. Keys with an empty string value are section titles; an entry that is not
// {"file": <.efkefc or .xps>, "speed": <number>} is left out with an error.
EffectTable ParseEffectTable(std::string_view json);

} // namespace storm::effects
