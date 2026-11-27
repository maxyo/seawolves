// Land messages of both games read with the real MESSAGE (location/src/land_messages.hpp), and the share of a push
// between two characters (push_share.hpp).
#include "land_messages.hpp"
#include "push_share.hpp"

#include "shared/messages.h"

#include <catch2/catch.hpp>

using namespace storm::location;

TEST_CASE("TELEPORT_TO_LOCATOR forces the place unless the message says otherwise")
{
    MESSAGE legacy;
    legacy.Reset("lss", int32_t{MSG_CHARACTER_TELEPORT_TO_LOCATOR}, std::string("goto"), std::string("g1"));
    legacy.Long();
    legacy.String();
    legacy.String();
    CHECK(ReadOptionalLong(legacy, 3).value_or(1) == 1);

    MESSAGE refused;
    refused.Reset("lssl", int32_t{MSG_CHARACTER_TELEPORT_TO_LOCATOR}, std::string("goto"), std::string("g1"), 0);
    refused.Long();
    refused.String();
    refused.String();
    CHECK(ReadOptionalLong(refused, 3) == std::optional<int32_t>(0));

    MESSAGE wrong;
    wrong.Reset("lssf", int32_t{MSG_CHARACTER_TELEPORT_TO_LOCATOR}, std::string("goto"), std::string("g1"), 0.0f);
    wrong.Long();
    wrong.String();
    wrong.String();
    CHECK_THROWS_AS(ReadOptionalLong(wrong, 3), MessageFormatError);
}

TEST_CASE("VIEWSTRPOS: the second offset is depth, the third height; the colour is clamped")
{
    MESSAGE message;
    message.Reset("lfffsfff", int32_t{MSG_CHARACTER_VIEWSTRPOS}, 1.0f, 2.0f, 3.0f, std::string("text"), 300.0f, 16.0f,
                  -5.0f);
    message.Long();
    const auto view = ReadViewText(message);
    CHECK(view.offset == glm::vec3(1.0f, 3.0f, 2.0f));
    CHECK(view.text == "text");
    CHECK(view.color == 0xff1000u);
}

TEST_CASE("SetSex: a man is 1 in both games")
{
    const auto read = [](int32_t value) {
        MESSAGE message;
        message.Reset("l", value);
        return ReadIsMale(message);
    };
    CHECK(read(1));
    CHECK_FALSE(read(0)); // To Each His Own: a woman
    CHECK_FALSE(read(2)); // Caribbean Legend: a woman
    CHECK_FALSE(read(3)); // Caribbean Legend: a skeleton
}

TEST_CASE("a push: movers take it from standers, equals by mass, the unpushable stay")
{
    const PushParty walking{true, 1.0f, false}, standing{false, 1.0f, false};
    CHECK(PushShares(false, walking, standing) == std::pair{0.9f, 0.1f});
    CHECK(PushShares(true, standing, walking) == std::pair{0.001f, 0.999f});
    CHECK(PushShares(false, standing, standing) == std::pair{0.5f, 0.5f});
    const PushParty heavy{false, 3.0f, false};
    const auto [light, heavyShare] = PushShares(false, standing, heavy);
    CHECK(light == 0.75f);
    CHECK(heavyShare == 0.25f);
    const PushParty wall{true, 1.0f, true};
    CHECK(PushShares(true, wall, standing) == std::pair{0.0f, 1.0f});
    CHECK(PushShares(false, walking, wall) == std::pair{1.0f, 0.0f});
}
