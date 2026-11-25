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

// ---- org.stormengine.port.EngineUi (platform threads) ----------------------------------
using storm::android::ui::EngineUi;

extern "C"
{

    JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeWantsTouch(JNIEnv *, jclass, jfloat x, jfloat y)
    {
        return EngineUi::Get().WantsTouch(x, y) ? JNI_TRUE : JNI_FALSE;
    }

    // phase: 0 down, 1 move, 2 up, 3 cancel; physical pixels of the surface; time: the sample's
    // MotionEvent time (SystemClock.uptimeMillis base)
    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeTouch(JNIEnv *, jclass, jint phase, jint pointer,
                                                                          jfloat x, jfloat y, jlong time)
    {
        if (storm::ui::jni::Phase(phase))
            EngineUi::Get().Touch(phase, pointer, x, y, static_cast<double>(time));
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeCancelInput(JNIEnv *, jclass)
    {
        EngineUi::Get().Post([](storm::ui::Host &host) { host.CancelInput(); });
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeInputLive(JNIEnv *, jclass, jboolean live)
    {
        storm::android::inputEpoch.set(live == JNI_TRUE);
    }

    JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeWantsBack(JNIEnv *, jclass)
    {
        return EngineUi::Get().WantsBack() ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeBack(JNIEnv *, jclass)
    {
        EngineUi::Get().Post([](storm::ui::Host &host) { host.Back(); });
    }

    JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeInterfaceActive(JNIEnv *, jclass)
    {
        return EngineUi::Get().InterfaceActive() ? JNI_TRUE : JNI_FALSE;
    }

    // The channels the platform writes itself: "presentation", "appSettings".
    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSetState(JNIEnv *env, jclass, jstring channel,
                                                                             jbyteArray json)
    {
        auto name = storm::android::ui::Text(env, channel, "a state channel");
        if (!name)
            return;
        std::string text = storm::android::ui::Bytes(env, json);
        EngineUi::Get().PlatformState(std::move(*name), std::move(text));
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeToast(JNIEnv *env, jclass, jstring text,
                                                                          jstring tone)
    {
        auto line = storm::android::ui::Text(env, text, "a toast"),
             kind = storm::android::ui::Text(env, tone, "a toast tone");
        if (!line || !kind)
            return;
        EngineUi::Get().Post(
            [line = std::move(*line), kind = std::move(*kind)](storm::ui::Host &host) { host.Toast(line, kind); });
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSubmitText(JNIEnv *env, jclass, jstring token,
                                                                               jstring text, jboolean accepted)
    {
        auto id = storm::android::ui::Text(env, token, "an editor token"),
             answer = storm::android::ui::Text(env, text, "an editor's text");
        if (!id || !answer)
            return;
        EngineUi::Get().Post([id = std::move(*id), answer = std::move(*answer), accepted](storm::ui::Host &host) {
            host.SubmitText(id, answer, accepted == JNI_TRUE);
        });
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSetSafeArea(JNIEnv *, jclass, jfloat left,
                                                                                jfloat top, jfloat right, jfloat bottom)
    {
        EngineUi::Get().SafeArea(left, top, right, bottom);
    }

} // extern "C"
