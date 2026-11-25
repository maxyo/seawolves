#include "platform/android_spyglass.hpp"
#include <string>

#include <catch2/catch.hpp>

TEST_CASE("android_spyglass")
{
    using namespace storm::android;
    REQUIRE(spyglassCaptainLine("24 фунт.       Мигель де Сандоваль") ==
            std::make_pair(std::string("24 фунт."), std::string("Мигель де Сандоваль")));
    REQUIRE(spyglassCaptainLine("Кулеврины  Мушкетный залп   Ян ван Дейк").second == "Ян ван Дейк");
    REQUIRE((spyglassCaptainLine("       Хуан").first.empty() && spyglassCaptainLine("       Хуан").second == "Хуан"));
    REQUIRE(spyglassCaptainLine("   ").second.empty());

    SpyglassState s;
    REQUIRE((s.hud().known && !s.hud().on && !s.hud().target));
    s.turn(true);
    REQUIRE((s.on() && s.hud().on && !s.hud().target));
    storm::ui::hud::Spyglass t;
    t.kind = "ship";
    t.name = "Санта \"Каталина\"";
    t.ship_class = 2;
    t.crew = -1;
    t.speed = 11.04;
    t.distance = 420.0;
    t.skills[0] = 7;
    s.target(t);
    const auto &shown = s.hud();
    REQUIRE((shown.target && shown.kind == "ship" && shown.name == "Санта \"Каталина\"" && shown.crew == -1));
    REQUIRE((shown.speed == 11.0 && shown.distance == 420.0 && shown.skills[0] == 7)); // to a tenth, as printed
    t.speed = 2e6;
    s.target(t);
    REQUIRE(s.hud().speed == -1.0); // not a speed the game shows
    s.clear();
    REQUIRE((s.hud().on && !s.hud().target));
    s.target(t);
    s.turn(false);
    REQUIRE((!s.hud().on && !s.hud().target && s.hud().name.empty())); // lowering drops the target
    s.target(t);
    REQUIRE(!s.hud().target); // no target while the telescope is down
}
