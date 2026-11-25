// The display options read from the "appSettings" channel (platform/display_options.hpp).
#include <platform/display_options.hpp>

#include <cstdlib>

#include <catch2/catch.hpp>

TEST_CASE("display_options")
{
    using namespace storm::android;
    using storm::app::step::hud_transparency;
    using storm::app::step::ui_scale;
    CHECK(ReadDisplayOptions(R"({"a":1,"ui_scale": 115})").ui_scale == 115); // integer member
    CHECK(ReadDisplayOptions(R"({"ui_scale":115.0})").problems.find("ui_scale 115.0 is not a step") !=
          std::string::npos);                                                        // a fraction is not an integer
    CHECK(ReadDisplayOptions(R"({"ui_scale":"115"})").ui_scale == ui_scale.initial); // a string is not an integer
    CHECK(ReadDisplayOptions(R"({"nested":{"ui_scale":130}})").ui_scale ==
          ui_scale.initial); // a member of a nested object is not the setting
    CHECK(ReadDisplayOptions("{\"ui_scale\":").problems.find("not a JSON object") !=
          std::string::npos); // malformed settings are named
    const DisplayOptions good =
        ReadDisplayOptions(R"({"fx_preset":1,"ui_scale":130,"hud_transparency":60,"left_handed":1,"haptics":0})");
    CHECK((good.ui_scale == 130 && good.hud_transparency == 60 && good.left_handed == 1 && good.haptics == 0 &&
           good.problems.empty())); // valid steps are taken
    const DisplayOptions bad = ReadDisplayOptions(R"({"ui_scale":90,"hud_transparency":25,"left_handed":2})");
    CHECK((bad.ui_scale == ui_scale.initial && bad.hud_transparency == hud_transparency.initial &&
           bad.problems.find("ui_scale 90") != std::string::npos &&
           bad.problems.find("hud_transparency 25") != std::string::npos && bad.left_handed == 0 &&
           bad.problems.find("left_handed 2") !=
               std::string::npos)); // invalid values fall back to the defaults and are named
    const DisplayOptions missing = ReadDisplayOptions("{}");
    CHECK((missing.ui_scale == 100 && missing.hud_transparency == 0 &&
           missing.haptics == storm::app::step::haptics.initial &&
           missing.problems.find("is missing") != std::string::npos)); // missing values are named
}
