// The ship's messages and events in the layout the vanilla 1.7.3 scripts use, built and read
// with the real MESSAGE.
#include "ship_script_protocol.hpp"

#include <cstdlib>
#include <iostream>

#include "shared/messages.h"

#include <catch2/catch.hpp>

namespace
{
// SHIP::ProcessMessage reads the message code before dispatching.
MESSAGE lightsReset(MESSAGE message)
{
    message.Move2Start();
    REQUIRE(message.Long() == MSG_SHIP_LIGHTSRESET); // message code
    return message;
}

void lightsResetForms()
{
    // WhrWeather.c (vanilla 1.7.3): SendMessage(&characters[i], "l", MSG_SHIP_LIGHTSRESET)
    MESSAGE vanilla;
    vanilla.Reset("l", int32_t{MSG_SHIP_LIGHTSRESET});
    auto message = lightsReset(vanilla);
    REQUIRE(!storm::ship::ReadLightsResetFlag(message).has_value()); // vanilla form carries no flag

    for (const int32_t flag : {0, 1})
    {
        MESSAGE explicitFlag;
        explicitFlag.Reset("ll", int32_t{MSG_SHIP_LIGHTSRESET}, flag);
        message = lightsReset(explicitFlag);
        const auto read = storm::ship::ReadLightsResetFlag(message);
        REQUIRE((read.has_value() && *read == (flag != 0))); // explicit light flag
    }

    MESSAGE wrong;
    wrong.Reset("lf", int32_t{MSG_SHIP_LIGHTSRESET}, 1.0f);
    message = lightsReset(wrong);
    bool rejected = false;
    try
    {
        storm::ship::ReadLightsResetFlag(message);
    }
    catch (const MessageFormatError &)
    {
        rejected = true;
    }
    REQUIRE(rejected); // an unknown layout stays a format error
}
} // namespace

TEST_CASE("ship_script_protocol")
{
    lightsResetForms();
    std::cout << "PASS ship script protocol: MSG_SHIP_LIGHTSRESET vanilla and explicit forms\n";
}
