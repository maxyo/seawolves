// The texture name rules of the derived maps: one table gives a normalized texture path (NormalizeTexturePath) its
// relief class (normal_map.cpp ClassifyTextureName) and its gloss class (gloss_map.cpp ClassifyGlossName). Internal to
// fx.
#pragma once

#include "storm/fx/gloss_map.hpp"
#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace storm::fx::rules
{

enum class Match : uint8_t
{
    Folder,   // the path starts with the pattern (a folder, "ships/"); the name is not looked at
    Prefix,   // the file name starts with the pattern
    Contains, // the file name contains the pattern
    Exact,    // the file name equals the pattern
};

// `folder`: only paths in this folder ("" = any).
inline bool Matches(std::string_view path, const char *folder, Match match, const char *pattern)
{
    if (path.compare(0, std::strlen(folder), folder) != 0)
        return false;
    const size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    switch (match)
    {
    case Match::Exact:
        return name == pattern;
    case Match::Folder:
        return true;
    case Match::Prefix:
        return name.compare(0, std::strlen(pattern), pattern) == 0;
    case Match::Contains:
        return name.find(pattern) != std::string_view::npos;
    }
    return false;
}

// A rule gives a class to the relief, the gloss or both; a classification takes the first matching rule that gives
// its class.
struct Rule
{
    const char *folder; // only paths in this folder ("" = any)
    Match match;
    const char *pattern;
    std::optional<ReliefClass> relief;
    std::optional<GlossClass> gloss;
    const char *text;                           // the rule as logged
    bool (*accept)(std::string_view) = nullptr; // optional additional name constraint
};

inline bool ResidenceInterior(std::string_view path)
{
    const size_t slash = path.rfind('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (!name.starts_with("residence"))
        return false;
    name.remove_prefix(9);
    while (!name.empty() && ((name.front() >= '0' && name.front() <= '9') || name.front() == '_'))
        name.remove_prefix(1);
    return name == "ins" || name == "inside" || name == "tex";
}

// The classes by their letters; x: the rule gives none of that kind.
inline constexpr std::optional<ReliefClass> R_x,
    R_N = ReliefClass::None, R_S = ReliefClass::Stone, R_W = ReliefClass::Wood, R_B = ReliefClass::Building,
    R_P = ReliefClass::Plaster, R_M = ReliefClass::Metal, R_O = ReliefClass::Prop, R_C = ReliefClass::Character;
inline constexpr std::optional<GlossClass> G_x,
    G_N = GlossClass::None, G_M = GlossClass::Matte, G_D = GlossClass::DryWood, G_H = GlossClass::PaintedWood,
    G_L = GlossClass::Lacquered, G_S = GlossClass::Stone, G_Q = GlossClass::WetStone, G_B = GlossClass::Building,
    G_I = GlossClass::Interior, G_T = GlossClass::Tile, G_K = GlossClass::Leather, G_X = GlossClass::Metal,
    G_G = GlossClass::Glass, G_C = GlossClass::Character;
#define STORM_RULE(folder, match, pattern, relief, gloss)                                                              \
    {folder, Match::match, pattern, R_##relief, G_##gloss, folder pattern}
// TEHO's RESOURCE/Textures, first match wins. Location textures lie in the root folder under descriptive names
// (English, Russian transliterations, a few French); ships in Ships/<type>/Hull<n>/. Written from the texture list of
// the game. Every surface gets relief (the parallax wherever it can work); the rules with no relief name what it
// cannot work on or would be wrong for (their text is the reason in the logs and the census). A texture no rule names
// is a prop for the relief (the last relief rule) and gets no gloss map.
inline constexpr Rule kRules[] = {
    // Grey "bump" textures are heights (the relief derives its own), not colour.
    STORM_RULE("", Contains, "bump", x, N),

    // The characters' atlases (one per model: clothes, armour, skin and hair, the skin faded out per vertex); their
    // gloss
    // is a low highlight on buttons, buckles, leather and armour.
    STORM_RULE("characters/", Folder, "", C, C),

    // Folders of things without relief or highlights: UI, sky and weather, light maps, effects, the grass cards,
    // animals, flags; the islands seen from the sea have no gloss.
    STORM_RULE("interfaces/", Folder, "", N, N),
    STORM_RULE("battle_interface/", Folder, "", N, N),
    STORM_RULE("mainmenu/", Folder, "", N, N),
    STORM_RULE("loading/", Folder, "", N, N),
    STORM_RULE("fonts/", Folder, "", N, N),
    STORM_RULE("dialog/", Folder, "", N, N),
    STORM_RULE("crosshair/", Folder, "", N, N),
    STORM_RULE("tips/", Folder, "", N, N),
    STORM_RULE("quest_signs/", Folder, "", N, N),
    STORM_RULE("weather/", Folder, "", N, N),
    STORM_RULE("lighting/", Folder, "", N, N), // the locations' baked light maps
    STORM_RULE("worldmap/", Folder, "", N, N),
    STORM_RULE("islands/", Folder, "", x, N),
    STORM_RULE("grass/", Folder, "", N, N),
    STORM_RULE("animals/", Folder, "", N, N),
    STORM_RULE("flags/", Folder, "", N, N),
    STORM_RULE("shipsflares/", Folder, "", N, N),
    STORM_RULE("locefx/", Folder, "", N, N),
    STORM_RULE("tornado/", Folder, "", N, N),
    STORM_RULE("texturesequence/", Folder, "", N, N),

    // Weapons and items lying about.
    STORM_RULE("ammo/", Folder, "", M, X),
    STORM_RULE("items/", Folder, "", x, N),

    // Ships: sails and ropes matte, wakes and alpha-tested rigging none, metal fittings and guns metal; hulls (painted
    // planks) and decks are wood.
    STORM_RULE("ships/", Contains, "parus", N, M),
    STORM_RULE("ships/", Contains, "sail", x, M),
    STORM_RULE("ships/", Contains, "trail", N, N),
    STORM_RULE("ships/", Contains, "alpha", N, N),
    STORM_RULE("ships/", Contains, "col", N, N),
    STORM_RULE("ships/", Contains, "rope", x, M),
    STORM_RULE("ships/", Contains, "glass", x, G),
    STORM_RULE("ships/", Contains, "deck", x, D),
    STORM_RULE("ships/", Contains, "metal", M, X),
    STORM_RULE("ships/", Contains, "cannon", M, X),
    STORM_RULE("ships/", Contains, "gun", M, X),
    STORM_RULE("ships/", Folder, "", W, H),

    // Names. Building atlases whose names start like an excluded word come first (masonry, plaster, wood and window
    // patches, not window overlays or wooden props).
    STORM_RULE("", Exact, "foudation_win_door", B, x),
    STORM_RULE("", Exact, "foudation_win2door", B, x),
    STORM_RULE("", Exact, "hut1_walls", B, x),
    STORM_RULE("", Exact, "hut2_walls", B, x),
    STORM_RULE("", Exact, "hut3_walls", B, x),
    STORM_RULE("", Exact, "hut4_walls", B, x),
    STORM_RULE("", Exact, "barn2_wall", B, x),
    STORM_RULE("", Exact, "hut1", B, x),
    STORM_RULE("", Exact, "hut2", B, x),
    STORM_RULE("", Exact, "hut3", B, x),
    STORM_RULE("", Exact, "hut4", B, x),
    STORM_RULE("", Exact, "barn2", B, x),
    STORM_RULE("", Contains, "windmill", B, B),
    STORM_RULE("", Contains, "fonar", M, X), // lanterns
    STORM_RULE("", Contains, "lighthouse", S, x),

    // Not surfaces with relief: occlusion / reflection / mask maps, windows and glass (glossy), light, fire, smoke,
    // water, foliage cards, cloth, bones, signs, UI and effect sheets.
    STORM_RULE("", Prefix, "occl", N, N),
    STORM_RULE("", Prefix, "refl", N, N),
    STORM_RULE("", Contains, "mask", N, N),
    STORM_RULE("", Prefix, "wind", N, G), // windows lit at night (windTavern1, windBigHouse01...)
    STORM_RULE("", Contains, "window", N, x),
    STORM_RULE("", Contains, "glass", N, G),
    STORM_RULE("", Contains, "win_", N, x),
    STORM_RULE("", Contains, "_win", N, x),
    STORM_RULE("", Contains, "cannonflash", x, N),
    STORM_RULE("", Contains, "alpha", N, N),
    STORM_RULE("", Contains, "alfa", N, N),
    STORM_RULE("", Contains, "light", N, x),
    STORM_RULE("", Contains, "lamp", N, X),
    STORM_RULE("", Contains, "candle", N, X), // candlesticks
    STORM_RULE("", Contains, "corona", N, N),
    STORM_RULE("", Contains, "fire", N, N),
    STORM_RULE("", Contains, "bonfire", N, x),
    STORM_RULE("", Contains, "flash", N, N),
    STORM_RULE("", Contains, "spark", N, N),
    STORM_RULE("", Contains, "smoke", N, N),
    STORM_RULE("", Contains, "shadow", N, N),
    STORM_RULE("", Contains, "sky", N, N),
    STORM_RULE("", Prefix, "sea", N, N),
    STORM_RULE("", Contains, "water", N, N),
    STORM_RULE("", Contains, "under", N, N), // underwater
    STORM_RULE("", Contains, "caust", N, N),
    STORM_RULE("", Contains, "foam", N, N),
    STORM_RULE("", Contains, "splash", N, N),
    STORM_RULE("", Contains, "fall", N, N), // waterfalls
    STORM_RULE("", Contains, "river", N, N),
    STORM_RULE("", Contains, "lava", N, N),
    STORM_RULE("", Contains, "reef", N, x),
    STORM_RULE("", Contains, "coral", N, x),
    STORM_RULE("", Contains, "algae", N, x),
    STORM_RULE("", Contains, "leaf", N, N),
    STORM_RULE("", Contains, "leaves", N, N),
    STORM_RULE("", Contains, "palm", N, N),
    STORM_RULE("", Contains, "tree", N, N),
    STORM_RULE("", Contains, "plant", N, N), // also Plantation: its walls are atlases with foliage
    STORM_RULE("", Contains, "flower", N, N),
    STORM_RULE("", Contains, "flora", N, N),
    STORM_RULE("", Contains, "fern", N, N),
    STORM_RULE("", Contains, "grass", P, x), // grass ground (the grass cards: grass/)
    STORM_RULE("", Contains, "back", N, N),  // background cards (back_jungle, back_mountains)
    STORM_RULE("", Contains, "sail", N, M),
    STORM_RULE("", Contains, "flag", N, N),
    STORM_RULE("", Contains, "nets", N, x),
    STORM_RULE("", Contains, "web", N, N),
    STORM_RULE("", Contains, "chain", M, X),
    STORM_RULE("", Contains, "canopy", N, M),
    STORM_RULE("", Contains, "vant", N, M), // shrouds
    STORM_RULE("", Contains, "metal", M, X),
    STORM_RULE("", Contains, "iron", M, X),
    STORM_RULE("", Contains, "gold", M, X),
    STORM_RULE("", Contains, "gundeck", W, D), // the gun deck's planks
    STORM_RULE("", Contains, "cannon", M, X),
    STORM_RULE("", Contains, "skin", N, N),
    STORM_RULE("", Contains, "telo", N, N),   // body
    STORM_RULE("", Contains, "golova", N, N), // head
    STORM_RULE("", Prefix, "eye", N, N),
    STORM_RULE("", Contains, "sign", N, x),
    STORM_RULE("", Contains, "painting", N, N),
    STORM_RULE("", Contains, "freska", N, N),
    STORM_RULE("", Contains, "map", N, N),
    STORM_RULE("", Contains, "menu", N, N),
    STORM_RULE("", Contains, "particles", N, N),
    STORM_RULE("", Contains, "blood", N, N),
    STORM_RULE("", Contains, "bird", N, N),
    STORM_RULE("", Contains, "seagull", N, N),
    STORM_RULE("", Contains, "fish", N, N),

    // Floors and woven surfaces stay shallow. These stock interior atlases mix floors and rugs with furniture and wall
    // patches: the whole atlas shallow; exterior Residence* masonry remains a building.
    STORM_RULE("", Contains, "floor", P, x),
    STORM_RULE("", Contains, "parquet", P, x),
    STORM_RULE("", Contains, "cloth", P, M),
    STORM_RULE("", Contains, "carpet", P, M),
    STORM_RULE("", Contains, "fabric", P, M),
    {"", Match::Prefix, "residence", R_P, G_x, "residence + interior suffix", ResidenceInterior},
    STORM_RULE("", Exact, "smallresidence", P, x),
    STORM_RULE("", Exact, "pirate_residence", P, x),

    // Glass: windows and bottles.
    STORM_RULE("", Contains, "window", x, G),
    STORM_RULE("", Contains, "steklo", x, G),
    STORM_RULE("", Contains, "bottle", x, G),
    STORM_RULE("", Contains, "butyl", x, G),

    // Weapons, tools and metal (before the wood: "rapier" holds "pier").
    STORM_RULE("", Contains, "gun", M, X),
    STORM_RULE("", Contains, "pistol", M, X),
    STORM_RULE("", Contains, "blade", M, X),
    STORM_RULE("", Contains, "sabre", M, x),
    STORM_RULE("", Contains, "rapier", M, x),
    STORM_RULE("", Contains, "sword", M, X),
    STORM_RULE("", Contains, "katana", M, x),
    STORM_RULE("", Contains, "anchor", M, X),
    STORM_RULE("", Contains, "yakor", x, X), // anchor
    STORM_RULE("", Contains, "bell", x, X),
    STORM_RULE("", Contains, "kolokol", x, X), // bell
    STORM_RULE("", Contains, "treasure", x, X),
    STORM_RULE("", Contains, "copper", x, X),
    STORM_RULE("", Contains, "bronze", x, X),
    STORM_RULE("", Contains, "brass", x, X),
    STORM_RULE("", Contains, "lantern", x, X),

    // Leather.
    STORM_RULE("", Contains, "leather", x, K),
    STORM_RULE("", Contains, "kozh", x, K),
    STORM_RULE("", Prefix, "bag", x, K),
    STORM_RULE("", Contains, "saddle", x, K),
    STORM_RULE("", Contains, "book", x, K),

    // Rope.
    STORM_RULE("", Contains, "rope", N, M),
    STORM_RULE("", Contains, "nets", x, M),

    // Stone; caves, grottos, crypts, dungeons and wells are wet stone, tiles and marble glazed.
    STORM_RULE("", Contains, "cave", S, Q),
    STORM_RULE("", Contains, "grot", S, Q),
    STORM_RULE("", Contains, "crypt", S, Q),
    STORM_RULE("", Contains, "dungeon", x, Q),
    STORM_RULE("", Contains, "prison", S, Q),
    STORM_RULE("", Contains, "stal", S, Q), // stalactites, stalagmites
    STORM_RULE("", Contains, "mine", x, Q),
    STORM_RULE("", Contains, "well", x, Q),
    STORM_RULE("", Contains, "tile", S, T),
    STORM_RULE("", Contains, "marble", x, T),
    STORM_RULE("", Contains, "mramor", x, T),

    // Furniture, cabins, chests and doors: lacquered wood.
    STORM_RULE("", Contains, "furniture", W, L),
    STORM_RULE("", Contains, "mebel", x, L),
    STORM_RULE("", Contains, "chest", W, L),
    STORM_RULE("", Contains, "cabin", W, L),
    STORM_RULE("", Prefix, "cap", x, L), // captain's cabins and decks
    STORM_RULE("", Contains, "door", W, L),
    STORM_RULE("", Contains, "table", x, L),
    STORM_RULE("", Contains, "shkaf", x, L), // cupboard

    // Stone and brick.
    STORM_RULE("", Contains, "stone", S, S),
    STORM_RULE("", Contains, "brick", S, S),
    STORM_RULE("", Contains, "rock", S, S),
    STORM_RULE("", Contains, "skala", S, S), // rock
    STORM_RULE("", Contains, "pierre", S, S),
    STORM_RULE("", Contains, "cobble", S, S),
    STORM_RULE("", Contains, "town_cob", S, x),
    STORM_RULE("", Contains, "beton", S, S),
    STORM_RULE("", Contains, "fort", S, x),
    STORM_RULE("", Contains, "castle", S, B),
    STORM_RULE("", Contains, "temple", S, S),
    STORM_RULE("", Contains, "pyramid", S, S),
    STORM_RULE("", Contains, "piramid", S, S),
    STORM_RULE("", Contains, "incas", S, S),
    STORM_RULE("", Contains, "tenochtitlan", S, x),
    STORM_RULE("", Contains, "border", S, S),
    STORM_RULE("", Contains, "stair", S, S),
    STORM_RULE("", Contains, "steps", S, S),
    STORM_RULE("", Contains, "arch", S, S),
    STORM_RULE("", Contains, "ruin", S, S),
    STORM_RULE("", Contains, "grave", S, S),
    STORM_RULE("", Contains, "cemetery", S, S),
    STORM_RULE("", Contains, "fontain", S, S),
    STORM_RULE("", Contains, "mayak", S, S), // lighthouse
    STORM_RULE("", Contains, "mine", S, x),
    STORM_RULE("", Contains, "kamin", S, S), // fireplace

    // Wood and planks.
    STORM_RULE("", Contains, "wood", W, D),
    STORM_RULE("", Contains, "plank", W, D),
    STORM_RULE("", Contains, "deck", W, D),
    STORM_RULE("", Contains, "board", W, D),
    STORM_RULE("", Contains, "balk", W, D), // beams
    STORM_RULE("", Contains, "derevo", W, D),
    STORM_RULE("", Contains, "gate", W, D),
    STORM_RULE("", Contains, "pier", W, D),
    STORM_RULE("", Contains, "prichal", W, D), // pier
    STORM_RULE("", Contains, "hold", W, D),
    STORM_RULE("", Contains, "boat", W, D),
    STORM_RULE("", Prefix, "dm_", W, D), // deck models of the ships
    STORM_RULE("", Prefix, "cap", W, x), // captain's cabins and decks
    STORM_RULE("", Contains, "barrel", W, D),
    STORM_RULE("", Contains, "ladder", W, D),
    STORM_RULE("", Contains, "grating", W, D),
    STORM_RULE("", Contains, "lafet", W, D), // gun carriages
    STORM_RULE("", Contains, "roof", W, D),
    STORM_RULE("", Contains, "hut", W, D),
    STORM_RULE("", Contains, "barn", W, D),
    STORM_RULE("", Contains, "objects_tex", W, D), // town props: crates, barrels, carts

    // Interiors: the atlases of a room ("tavern1ins", "store3_inside", "tavern_room_tex").
    STORM_RULE("", Contains, "ins", x, I),
    STORM_RULE("", Contains, "room", x, I),

    // Buildings: one atlas per house, tavern or church with its stone, plaster, beams and doors.
    STORM_RULE("", Prefix, "flh", B, B), // Flemish houses
    STORM_RULE("", Prefix, "sph", B, B), // Spanish houses
    STORM_RULE("", Contains, "shipyard", B, B),
    STORM_RULE("", Contains, "house", B, B),
    STORM_RULE("", Contains, "residence", B, B),
    STORM_RULE("", Contains, "tavern", B, B),
    STORM_RULE("", Contains, "store", B, B),
    STORM_RULE("", Contains, "bank", B, B),
    STORM_RULE("", Contains, "church", B, B),
    STORM_RULE("", Contains, "barracks", B, B),
    STORM_RULE("", Contains, "brothel", B, B),
    STORM_RULE("", Contains, "usurer", B, B),
    STORM_RULE("", Contains, "estate", B, B),
    STORM_RULE("", Contains, "arsenal", B, B),
    STORM_RULE("", Contains, "college", B, B),
    STORM_RULE("", Contains, "office", B, B),
    STORM_RULE("", Contains, "home", B, B),
    STORM_RULE("", Contains, "village", B, B),
    STORM_RULE("", Contains, "quarter", B, B),
    STORM_RULE("", Contains, "town", B, B),
    STORM_RULE("", Contains, "lighthouse", x, B),
    STORM_RULE("", Contains, "wall", B, B),

    // Plaster and ground.
    STORM_RULE("", Contains, "celling", P, x), // ceilings
    STORM_RULE("", Contains, "potolok", P, x), // ceiling
    STORM_RULE("", Contains, "grnd", P, x),
    STORM_RULE("", Contains, "land", P, x),
    STORM_RULE("", Contains, "fort", x, B),
    STORM_RULE("", Contains, "sand", P, M),
    STORM_RULE("", Contains, "pesok", P, x), // sand
    STORM_RULE("", Contains, "beach", P, x),
    STORM_RULE("", Contains, "shore", P, x),
    STORM_RULE("", Contains, "ground", P, M),
    STORM_RULE("", Contains, "grass", x, M), // grass ground (the grass cards: grass/)
    STORM_RULE("", Contains, "earth", x, M),
    STORM_RULE("", Contains, "dirt", P, M),
    STORM_RULE("", Contains, "path", P, x),
    STORM_RULE("", Contains, "tropa", P, x), // path
    STORM_RULE("", Contains, "jungle", P, x),

    // Everything else is a prop of the locations or the ships, an item, an island texture (no gloss).
    {"", Match::Prefix, "", R_O, G_x, "(no other rule: a prop)"},
    STORM_RULE("", Contains, "road", x, M),
    STORM_RULE("", Contains, "plaster", x, M),
    STORM_RULE("", Contains, "shtukatur", x, M),
};
#undef STORM_RULE

} // namespace storm::fx::rules
