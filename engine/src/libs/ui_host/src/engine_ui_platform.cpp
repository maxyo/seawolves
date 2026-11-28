#include "engine_ui.hpp"

namespace storm::android::ui
{
void EngineUi::PlatformState(std::string channel, std::string json)
{
    if (channel == "presentation")
    {
        // Android's part of the presentation: {"live": true|false}. The rest is the engine's
        // (Presentation): it reaches the UI in the frame it changes, not through this thread.
        const bool on = json == R"({"live":true})";
        if (!on && json != R"({"live":false})")
        {
            Log().error("presentation from the activity is not {{\"live\":…}}: {}", json);
            return;
        }
        const std::lock_guard lock(inbox_mutex_);
        live_ = on;
        return;
    }
    {
        const std::lock_guard lock(inbox_mutex_);
        platform_states_[channel] = json;
    }
    if (channel == "appSettings")
        DisplayOptionsFrom(json);
    Post([channel = std::move(channel), json = std::move(json)](storm::ui::Host &host) {
        host.SetState(channel, json);
    });
}

void EngineUi::DisplayOptionsFrom(const std::string &app_settings)
{
    const storm::android::DisplayOptions options = storm::android::ReadDisplayOptions(app_settings);
    if (!options.problems.empty())
        Log().error("display options: {}", options.problems);
    const bool left_handed = options.left_handed != 0;
    if (shown_ui_scale_ != options.ui_scale || shown_hud_transparency_ != options.hud_transparency ||
        shown_left_handed_ != options.left_handed || shown_haptics_ != options.haptics)
        Log().info("display options: UI size {}%, HUD buttons {}% see-through, {}, haptics {}", options.ui_scale,
                   options.hud_transparency, left_handed ? "left-handed HUD" : "right-handed HUD",
                   options.haptics ? "on" : "off");
    shown_ui_scale_ = options.ui_scale;
    shown_hud_transparency_ = options.hud_transparency;
    shown_left_handed_ = options.left_handed;
    shown_haptics_ = options.haptics;
    storm::android::hapticsOn.store(options.haptics != 0, std::memory_order_relaxed);
    Post([options](storm::ui::Host &host) { ApplyDisplayOptions(host, options); });
}
} // namespace storm::android::ui
