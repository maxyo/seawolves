#ifdef __ANDROID__
// The natives GameActivity.deliverMobileActions calls on the engine thread, one per message of its mailbox.
#include "platform/android_mailbox.hpp"
#include "platform/jni_text.hpp"

#include <jni.h>
#include <spdlog/spdlog.h>

using namespace storm::android;

namespace
{
void deliver(mailbox::Message message)
{
    if (!mailbox::delivering)
    {
        spdlog::error("mailbox: a message (kind {}) arrived outside GameActivity.deliverMobileActions; not applied",
                      message.index());
        return;
    }
    mailbox::delivering->push_back(std::move(message));
}
} // namespace

extern "C"
{
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeAudioFocus(JNIEnv *, jclass, jboolean audible,
                                                                                   jboolean ducked)
    {
        deliver(mailbox::AudioFocus{audible == JNI_TRUE, ducked == JNI_TRUE});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeGraphicsReload(JNIEnv *, jclass)
    {
        deliver(mailbox::GraphicsReload{});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeHudOwnerLost(JNIEnv *, jclass)
    {
        deliver(mailbox::HudOwnerLost{});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeMailboxBack(JNIEnv *, jclass)
    {
        deliver(mailbox::Back{});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLocationLoadCancel(JNIEnv *, jclass,
                                                                                           jlong generation)
    {
        deliver(mailbox::LocationLoadCancel{static_cast<uint64_t>(generation)});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeCameraZoom(JNIEnv *, jclass, jfloat scale)
    {
        deliver(mailbox::CameraZoom{scale});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLookDown(JNIEnv *, jclass, jfloat x, jfloat y,
                                                                                 jlong time, jfloat width,
                                                                                 jfloat height, jfloat density)
    {
        deliver(LookEvent{LookDown{x, y, static_cast<double>(time), width, height, density}});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLookMove(JNIEnv *, jclass, jfloat x, jfloat y,
                                                                                 jlong time)
    {
        deliver(LookEvent{LookMove{x, y, static_cast<double>(time)}});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLookUp(JNIEnv *, jclass, jlong time)
    {
        deliver(LookEvent{LookUp{static_cast<double>(time)}});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLookCancel(JNIEnv *, jclass)
    {
        deliver(LookEvent{LookCancel{}});
    }
    JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeShortcut(JNIEnv *env, jclass, jstring name)
    {
        std::string error;
        if (auto text = storm::jni_text::TryToUtf8(env, name, error))
            deliver(mailbox::Shortcut{std::move(*text)});
        else
            spdlog::error("mailbox: a shortcut name that is not text ({}); not applied", error);
    }
}
#endif
