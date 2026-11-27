// The lit and baked scene programs (shaders/ff_programs.cmake, effects::ChooseProgram) over every combination of a
// draw's facts: the program holds every block the draw reads and no narrower family of its entry does, it is embedded
// with its opaque twin, and every program of the generated table is chosen for some draw. A specialised program is
// chosen only for its setup and with the option on, of the draw's family or indoors of an interior kind within it. The
// matcher (FindSetup) compares every code of every stage, the stage count and the point sprite; a plain draw takes a
// specialised program of its entry, fs_ff or fs_ff_dyn, and of no other variant (ChoosePlainProgram). A game's own
// material takes a program of the pbr entry, which no other draw takes. No GPU.
#include "device.hpp"
#include "effects/lighting.hpp"

#include <catch2/catch.hpp>

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace storm;
using effects::FfBlocks;

namespace
{

bool holds(const FfBlocks &has, const FfBlocks &needs)
{
    return (has.lamps || !needs.lamps) && (has.ao || !needs.ao) && (has.town_wall || !needs.town_wall) &&
           (has.wet || !needs.wet);
}

const effects::FfProgramRow *row_of(const effects::FfProgram &program)
{
    for (const effects::FfProgramRow &row : effects::kFfProgramRows)
        if (row.program == program)
            return &row;
    return nullptr;
}

// Every specialised program and plain twin is embedded with its opaque twin.
bool embedded_with_twin(const char *name)
{
    const uint32_t id = shim::find_shader_by_name(name);
    return id != UINT32_MAX && shim::opaque_twin(id) != UINT32_MAX;
}

} // namespace

TEST_CASE("every draw's program is the narrowest embedded one, and every program is some draw's")
{
    std::set<std::string> chosen;
    // A game's own material: the pbr entry's narrowest family, with the highlights, the normal map or none, the sun
    // shadow or none.
    for (int bits = 0; bits < 4; ++bits)
        for (uint8_t relief = 0; relief <= 1; ++relief)
            for (const bool shadow : {false, true})
            {
                const FfBlocks needs{.lamps = (bits & 1) != 0, .ao = (bits & 2) != 0};
                const effects::FfChoice choice = effects::ChooseProgram(
                    {.lit = true, .pbr = true, .needs = needs, .specular = true, .relief = relief, .shadow = shadow});
                const effects::FfProgramRow *row = row_of(choice.program);
                INFO("material blocks " << bits << " relief " << int(relief) << " shadow " << shadow);
                REQUIRE(row != nullptr);
                INFO(row->name);
                const effects::FfFamilyRow &family = effects::kFfFamilyRows[size_t(choice.program.family)];
                CHECK(family.pbr);
                CHECK(holds(family.blocks, needs));
                for (const effects::FfFamilyRow &other : effects::kFfFamilyRows)
                    if (other.pbr && holds(other.blocks, needs))
                        CHECK_FALSE((holds(family.blocks, other.blocks) && !holds(other.blocks, family.blocks)));
                CHECK_FALSE(choice.specialised);
                CHECK(embedded_with_twin(row->name));
                chosen.insert(row->name);
            }
    std::vector<std::optional<uint8_t>> setups{std::nullopt};
    for (const effects::FfSetupRow &row : effects::kFfSetupRows)
        setups.push_back(row.id);
    for (const bool lit : {true, false})
        for (int bits = 0; bits < 16; ++bits)
            for (const bool specular : {false, true})
                for (uint8_t relief = 0; relief <= 2; ++relief)
                    for (const bool shadow : {false, true})
                        for (const bool interior : {false, true})
                            for (const std::optional<uint8_t> setup : setups)
                                for (const bool option : {false, true})
                                    for (const bool sun_shadow : {false, true})
                                    {
                                        const FfBlocks needs{.lamps = (bits & 1) != 0,
                                                             .ao = (bits & 2) != 0,
                                                             .town_wall = (bits & 4) != 0,
                                                             .wet = (bits & 8) != 0};
                                        if (lit && needs.town_wall)
                                            continue; // a lit draw is never a town wall (Lighting::prepare)
                                        const effects::DrawFacts facts{.lit = lit,
                                                                       .needs = needs,
                                                                       .specular = specular,
                                                                       .relief = relief,
                                                                       .shadow = shadow,
                                                                       .interior = interior,
                                                                       .sun_shadow = sun_shadow,
                                                                       .setup = setup,
                                                                       .specialised_on = option};
                                        const effects::FfChoice choice = effects::ChooseProgram(facts);
                                        const effects::FfProgram &program = choice.program;
                                        const effects::FfProgramRow *row = row_of(program);
                                        INFO((lit ? "lit" : "baked")
                                             << " blocks " << bits << " specular " << specular << " relief "
                                             << int(relief) << " shadow " << shadow << " interior " << interior
                                             << " setup " << int(setup.value_or(0)) << " option " << option
                                             << " sun shadow " << sun_shadow);
                                        REQUIRE(row != nullptr);
                                        INFO(row->name);
                                        const effects::FfFamilyRow &family =
                                            effects::kFfFamilyRows[size_t(program.family)];
                                        CHECK(family.lit == lit);
                                        CHECK_FALSE(family.pbr);
                                        CHECK(holds(family.blocks, needs));
                                        CHECK_FALSE(family.room);
                                        for (const effects::FfFamilyRow &other : effects::kFfFamilyRows)
                                            if (other.lit == lit && !other.pbr && !other.room &&
                                                holds(other.blocks, needs))
                                                CHECK_FALSE((holds(family.blocks, other.blocks) &&
                                                             !holds(other.blocks, family.blocks)));
                                        CHECK(program.specular == specular);
                                        CHECK(program.relief == relief);
                                        CHECK(program.shadow == shadow);
                                        CHECK(embedded_with_twin(row->name));
                                        chosen.insert(row->name);
                                        if (!choice.specialised)
                                            continue;
                                        REQUIRE(*choice.specialised < effects::kFfSpecialisedRows.size());
                                        const effects::FfSpecialisedRow &special =
                                            effects::kFfSpecialisedRows[*choice.specialised];
                                        INFO(special.name);
                                        CHECK(facts.specialised_on);
                                        REQUIRE(setup.has_value());
                                        CHECK(special.program.setup == *setup);
                                        CHECK(special.program.entry == effects::FfEntry::Family);
                                        CHECK(special.program.specular == specular);
                                        CHECK(special.program.relief == relief);
                                        const effects::FfFamilyRow &kind =
                                            effects::kFfFamilyRows[size_t(special.program.family)];
                                        CHECK(kind.lit == lit);
                                        CHECK(holds(kind.blocks, needs));
                                        if (kind.room)
                                        {
                                            // The kind leaves out the town wall, the rain and the sun shadow: a draw
                                            // that reads any of them takes its family's program.
                                            CHECK(interior);
                                            CHECK_FALSE(sun_shadow);
                                            CHECK_FALSE(needs.town_wall);
                                            CHECK_FALSE(needs.wet);
                                            CHECK(holds(family.blocks, kind.blocks));
                                            CHECK(special.program.shadow == 0);
                                        }
                                        else
                                        {
                                            CHECK(special.program.family == program.family);
                                            CHECK(special.program.shadow == shadow);
                                        }
                                        chosen.insert(special.name);
                                    }
    for (const effects::FfProgramRow &row : effects::kFfProgramRows)
    {
        INFO(row.name);
        CHECK(chosen.contains(row.name));
    }
    // The plain draws' rows over every plain draw's facts (ChoosePlainProgram, as Device::choose_program asks).
    for (const std::optional<uint8_t> setup : setups)
        for (const bool ground : {false, true})
            for (const bool option : {false, true})
                if (const std::optional<size_t> row =
                        effects::ChoosePlainProgram({.setup = setup, .ground = ground, .specialised_on = option}))
                    chosen.insert(effects::kFfSpecialisedRows[*row].name);
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        INFO(row.name);
        CHECK(embedded_with_twin(row.name));
        CHECK(chosen.contains(row.name));
    }
}

TEST_CASE("a draw's stages match a setup only when every compared code is the setup's")
{
    REQUIRE_FALSE(effects::kFfSetupRows.empty());
    for (const effects::FfSetupRow &row : effects::kFfSetupRows)
    {
        INFO("setup " << int(row.id));
        shim::FfStages stages;
        stages.count = row.count;
        stages.stage = row.stage;
        CHECK(effects::FindSetup(stages) == row.id);

        // Each code of each stage changed alone: another setup.
        for (uint8_t i = 0; i < row.count; ++i)
        {
            using Change = void (*)(shim::FfStageCodes &);
            const std::pair<const char *, Change> changes[] = {
                {"colour op",
                 [](shim::FfStageCodes &s) {
                     s.colour_op = s.colour_op == D3DTOP_MODULATE ? D3DTOP_ADD : D3DTOP_MODULATE;
                 }},
                {"colour arg1", [](shim::FfStageCodes &s) { s.colour_arg1 ^= 16; }},
                {"colour arg2", [](shim::FfStageCodes &s) { s.colour_arg2 ^= 32; }},
                {"colour arg0", [](shim::FfStageCodes &s) { s.colour_arg0 ^= 1; }},
                {"alpha op",
                 [](shim::FfStageCodes &s) {
                     s.alpha_op = s.alpha_op == D3DTOP_MODULATE ? D3DTOP_ADD : D3DTOP_MODULATE;
                 }},
                {"alpha arg1", [](shim::FfStageCodes &s) { s.alpha_arg1 ^= 16; }},
                {"alpha arg2", [](shim::FfStageCodes &s) { s.alpha_arg2 ^= 32; }},
                {"alpha arg0", [](shim::FfStageCodes &s) { s.alpha_arg0 ^= 1; }},
                {"temp", [](shim::FfStageCodes &s) { s.temp = !s.temp; }},
                {"projected", [](shim::FfStageCodes &s) { s.projected = !s.projected; }},
                {"bound", [](shim::FfStageCodes &s) { s.bound = !s.bound; }},
            };
            for (const auto &[name, change] : changes)
            {
                INFO("stage " << int(i) << " " << name);
                shim::FfStages other = stages;
                change(other.stage[i]);
                // Not this setup (the changed stages may be another listed one's).
                CHECK(effects::FindSetup(other) != row.id);
            }
        }
        // The setup's stages and one more (a lightmap stage after them): not the setup.
        if (row.count < shim::kFfStages)
        {
            shim::FfStages longer = stages;
            longer.stage[row.count] = {.colour_op = D3DTOP_MODULATE2X,
                                       .colour_arg1 = D3DTA_CURRENT,
                                       .colour_arg2 = D3DTA_TEXTURE,
                                       .colour_arg0 = D3DTA_CURRENT,
                                       .alpha_op = D3DTOP_DISABLE,
                                       .alpha_arg1 = D3DTA_CURRENT,
                                       .alpha_arg2 = D3DTA_CURRENT,
                                       .alpha_arg0 = D3DTA_CURRENT,
                                       .bound = true};
            longer.count = static_cast<uint8_t>(row.count + 1);
            CHECK_FALSE(effects::FindSetup(longer).has_value());
        }
        // A point sprite's coordinates are not a plain stage's.
        shim::FfStages sprite = stages;
        sprite.point_sprite = true;
        CHECK_FALSE(effects::FindSetup(sprite).has_value());
    }
}

TEST_CASE("a plain draw takes its setup's program of its entry (fs_ff or fs_ff_dyn), with the option on")
{
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        if (row.program.entry == effects::FfEntry::Family)
            continue;
        INFO(row.name);
        const bool ground = row.program.entry == effects::FfEntry::Ground;
        const effects::PlainFacts plain{.setup = row.program.setup, .ground = ground, .specialised_on = true};
        const std::optional<size_t> chosen = effects::ChoosePlainProgram(plain);
        REQUIRE(chosen.has_value());
        CHECK(std::string(effects::kFfSpecialisedRows[*chosen].name) == row.name);
        using Change = void (*)(effects::PlainFacts &);
        const std::pair<const char *, Change> excluded[] = {
            {"no setup", [](effects::PlainFacts &f) { f.setup.reset(); }},
            {"a cube stage", [](effects::PlainFacts &f) { f.fs_variant = 1; }},
            {"the slope bias", [](effects::PlainFacts &f) { f.fs_variant = 4; }},
            {"Lighting's program", [](effects::PlainFacts &f) { f.lit_variant = true; }},
            {"a pixel shader port", [](effects::PlainFacts &f) { f.ported = true; }},
            {"the option off", [](effects::PlainFacts &f) { f.specialised_on = false; }},
        };
        for (const auto &[name, change] : excluded)
        {
            INFO(name);
            effects::PlainFacts facts = plain;
            change(facts);
            CHECK_FALSE(effects::ChoosePlainProgram(facts).has_value());
        }
        // The other entry: its own row of the setup or none.
        effects::PlainFacts other = plain;
        other.ground = !other.ground;
        if (const std::optional<size_t> found = effects::ChoosePlainProgram(other))
        {
            CHECK(effects::kFfSpecialisedRows[*found].program.entry ==
                  (other.ground ? effects::FfEntry::Ground : effects::FfEntry::Plain));
            CHECK(effects::kFfSpecialisedRows[*found].program.setup == row.program.setup);
        }
    }
}
