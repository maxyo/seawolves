#pragma once

// Messages to the ship whose layout differs between the games' scripts (tests/ship_script_protocol checks them with
// the real MESSAGE). The events the ship raises are laid out by sea_script_protocol.hpp.

#include <cstdint>
#include <optional>

#include "message.h"

namespace storm::ship
{

// MSG_SHIP_LIGHTSRESET. Vanilla 1.7.3 (WhrWeather.c, on a day/night change at sea) sends only
// the message code, "l", after Ship_SetLightsAndFlares has stored the new state in the
// character's ship.lights. Later scripts (sd-teho-public, Caribbean Legend) append the light flag, "ll".
// Returns the flag when the message carries it, std::nullopt for the vanilla form; any other
// layout stays a MessageFormatError.
inline std::optional<bool> ReadLightsResetFlag(MESSAGE &message)
{
    if (message.GetFormat() == "l")
        return std::nullopt;
    return message.Long() != 0;
}

} // namespace storm::ship
