#pragma once
#include "app_ui_names.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>

// The player's display options (settings screen «Управление» → «Экран и касания»), app settings of the "appSettings"
// state channel: «Размер интерфейса» ui_scale, «Прозрачность кнопок» hud_transparency, «Под левую руку» left_handed
// (the HUD mirrored left to right) and «Вибрация» haptics (platform/android_haptics.hpp). Their steps and defaults
// are app::step (tools/android/app_contract.json); the engine's UI host (ui_host engine_ui.cpp) applies them
// (Host::SetDisplayOptions, android::hapticsOn).
namespace storm::android
{
inline bool OneOf(std::span<const int> steps, int value)
{
    return std::find(steps.begin(), steps.end(), value) != steps.end();
}

struct DisplayOptions
{
    int ui_scale = app::step::ui_scale.initial;
    int hud_transparency = app::step::hud_transparency.initial;
    int left_handed = app::step::left_handed.initial;
    int haptics = app::step::haptics.initial;
    std::string problems; // what was missing or invalid and replaced by its default (empty: none)
};

inline DisplayOptions ReadDisplayOptions(std::string_view app_settings)
{
    DisplayOptions options;
    const nlohmann::json settings = nlohmann::json::parse(app_settings, nullptr, false);
    if (!settings.is_object())
        options.problems = "the settings are not a JSON object";
    const auto take = [&](const char *key, const auto &steps, int &out) {
        const int fallback = steps.initial;
        const auto found = settings.is_object() ? settings.find(key) : settings.end();
        const bool integer = found != settings.end() && found->is_number_integer();
        const int64_t value = integer ? found->get<int64_t>() : 0;
        if (integer && value == static_cast<int>(value) && OneOf(steps.values, static_cast<int>(value)))
            out = static_cast<int>(value);
        else
        {
            out = fallback;
            options.problems +=
                std::string(options.problems.empty() ? "" : "; ") + key + " " +
                (found == settings.end() ? std::string("is missing") : found->dump() + " is not a step") +
                ", default " + std::to_string(fallback);
        }
    };
    take(app::setting::ui_scale, app::step::ui_scale, options.ui_scale);
    take(app::setting::hud_transparency, app::step::hud_transparency, options.hud_transparency);
    take(app::setting::left_handed, app::step::left_handed, options.left_handed);
    take(app::setting::haptics, app::step::haptics, options.haptics);
    return options;
}
} // namespace storm::android
