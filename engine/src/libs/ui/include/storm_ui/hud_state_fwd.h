// The HUD's state channels by name only (their structs: hud_state.h), for headers that pass a HudState by reference.
#pragma once

#include <variant>

namespace storm::ui::hud
{
struct Gameplay;
struct Combat;
struct Vitals;
struct WorldInfo;
struct ContextAction;
struct Door;
struct LandCommands;
struct Ship;
struct SeaOrders;
struct SeaQuick;
struct WorldMap;
struct Presentation;
struct Spyglass;
} // namespace storm::ui::hud

namespace storm::ui
{
// One state of a HUD channel; the channel is the type.
using HudState = std::variant<hud::Gameplay, hud::Combat, hud::Vitals, hud::WorldInfo, hud::ContextAction, hud::Door,
                              hud::LandCommands, hud::Ship, hud::SeaOrders, hud::SeaQuick, hud::WorldMap,
                              hud::Presentation, hud::Spyglass>;
} // namespace storm::ui
