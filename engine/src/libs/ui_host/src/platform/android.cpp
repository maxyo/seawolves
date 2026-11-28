// The touch interface on Android: the Java host's natives (org.stormengine.port.EngineUi) and the upcalls the bridge
// makes to GameActivity (engine_ui.hpp ui_platform).
#ifdef __ANDROID__
#include "../engine_ui.hpp"
#include "platform/jni_text.hpp"

#include "storm_ui/android_jni.h"
#include "storm_ui/asset_files.h"

#include <SDL2/SDL_system.h>
#include <jni.h>
#include <spdlog/sinks/android_sink.h>

namespace storm::android::ui
{
namespace
{
using storm::android::CallActivity;

// The local Java strings of one upcall's texts. ok() is false (logged) when one of them is not valid UTF-8 or the VM
// failed (its exception pending): the upcall is then not made.
class JavaStrings
{
  public:
    JavaStrings(JNIEnv *env, std::initializer_list<const std::string *> texts, const char *upcall) : env_(env)
    {
        for (const std::string *text : texts)
        {
            jstring value = nullptr;
            try
            {
                value = storm::jni_text::ToJava(env, *text);
            }
            catch (const std::exception &error)
            {
                Log().error("GameActivity.{} is not called: text {}: {}", upcall, values_.size(), error.what());
            }
            if (!value)
            {
                ok_ = false;
                return;
            }
            values_.push_back(value);
        }
    }
    JavaStrings(const JavaStrings &) = delete;
    JavaStrings &operator=(const JavaStrings &) = delete;
    ~JavaStrings()
    {
        for (jstring value : values_)
            env_->DeleteLocalRef(value);
    }
    bool ok() const
    {
        return ok_;
    }
    jstring operator[](size_t index) const
    {
        return values_[index];
    }

  private:
    JNIEnv *env_;
    std::vector<jstring> values_;
    bool ok_ = true;
};
} // namespace

namespace ui_platform
{
bool Ready()
{
    return SDL_AndroidGetJNIEnv() != nullptr;
}

// The UI pack in the activity's assets, through the AssetManager the layers read too.
std::unique_ptr<storm::ui::FileSource> OpenPack()
{
    return storm::ui::AssetFiles(storm::android::assets(), "ui");
}

void ShowTextEditor(const storm::ui::TextRequest &request)
{
    CallActivity(upcall::GameActivity::onUiTextRequest, [&](JNIEnv *env, jobject activity, jmethodID method) {
        const JavaStrings texts(env, {&request.token, &request.text, &request.title}, "onUiTextRequest");
        if (!texts.ok())
            return false;
        env->CallVoidMethod(activity, method, texts[0], texts[1], texts[2], jboolean(request.numeric),
                            jboolean(request.secret), jint(request.x), jint(request.y), jint(request.width),
                            jint(request.height));
        return true;
    });
}

void UiFailed(const std::string &reason)
{
    CallActivity(upcall::GameActivity::uiFailed, [&](JNIEnv *env, jobject activity, jmethodID method) {
        const JavaStrings text(env, {&reason}, "uiFailed");
        if (!text.ok())
            return false;
        env->CallVoidMethod(activity, method, text[0]);
        return true;
    });
}

void HudInput(const std::string &verb, const std::string &value)
{
    CallActivity(upcall::GameActivity::onUiHudInput, [&](JNIEnv *env, jobject activity, jmethodID method) {
        const JavaStrings texts(env, {&verb, &value}, "onUiHudInput");
        if (!texts.ok())
            return false;
        env->CallVoidMethod(activity, method, texts[0], texts[1]);
        return true;
    });
}

void Perform(const storm::ui::Action &action)
{
    CallActivity(upcall::GameActivity::onUiAction, [&](JNIEnv *env, jobject activity, jmethodID method) {
        const JavaStrings texts(env, {&action.node, &action.verb, &action.value}, "onUiAction");
        if (!texts.ok())
            return false;
        env->CallVoidMethod(activity, method, texts[0], texts[1], texts[2]);
        return true;
    });
}

std::filesystem::path ModelsFolder()
{
    const char *external = SDL_AndroidGetExternalStoragePath();
    return external ? std::filesystem::path(external) / "ui-models" : std::filesystem::path();
}

spdlog::sink_ptr LogSink(const char *tag)
{
    return std::make_shared<spdlog::sinks::android_sink_mt>(tag, true);
}
} // namespace ui_platform
} // namespace storm::android::ui

// ---- org.stormengine.port.EngineUi (platform threads) ----------------------------------
using storm::android::ui::EngineUi;
using storm::ui::jni::Bytes;
using storm::ui::jni::Text;

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
        auto name = Text(env, channel, "a state channel");
        if (!name)
            return;
        std::string text = Bytes(env, json);
        EngineUi::Get().PlatformState(std::move(*name), std::move(text));
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeToast(JNIEnv *env, jclass, jstring text,
                                                                          jstring tone)
    {
        auto line = Text(env, text, "a toast"), kind = Text(env, tone, "a toast tone");
        if (!line || !kind)
            return;
        EngineUi::Get().Post(
            [line = std::move(*line), kind = std::move(*kind)](storm::ui::Host &host) { host.Toast(line, kind); });
    }

    JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSubmitText(JNIEnv *env, jclass, jstring token,
                                                                               jstring text, jboolean accepted)
    {
        auto id = Text(env, token, "an editor token"), answer = Text(env, text, "an editor's text");
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
#endif
