#include "torrent_session.hpp"
#include "platform/jni_text.hpp"
#include <jni.h>
#include <map>
#include <mutex>
#include <stdexcept>

namespace
{
std::mutex lock;
std::map<jlong, std::unique_ptr<storm::torrent::Session>> sessions;
jlong next_id = 1;
using storm::jni_text::ToUtf8;
// The session's JSON quotes names from the torrent file, whose "path" (without ".utf-8") need not be UTF-8.
jstring result(JNIEnv *env, std::string const &s)
{
    if (env->ExceptionCheck())
        return nullptr;
    return storm::jni_text::ToJavaReporting(env, s, "StormTorrent", "torrent");
}
void fail(JNIEnv *env, std::exception const &e) noexcept
{
    storm::jni_text::Throw(env, "java/io/IOException", e.what());
}
storm::torrent::Session &get(jlong id)
{
    auto it = sessions.find(id);
    if (it == sessions.end())
        throw std::runtime_error("Torrent session is closed");
    return *it->second;
}
} // namespace
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeTorrents_inspect(JNIEnv *env, jclass, jstring path)
{
    try
    {
        return result(env, storm::torrent::inspect_file(ToUtf8(env, path)));
    }
    catch (std::exception const &e)
    {
        fail(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT jlong JNICALL Java_org_stormengine_port_NativeTorrents_open(JNIEnv *env, jclass, jstring source,
                                                                                 jstring staging)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        auto s = std::make_unique<storm::torrent::Session>(ToUtf8(env, source), ToUtf8(env, staging));
        auto id = next_id++;
        sessions.emplace(id, std::move(s));
        return id;
    }
    catch (std::exception const &e)
    {
        fail(env, e);
        return 0;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeTorrents_poll(JNIEnv *env, jclass, jlong id)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        return result(env, get(id).poll());
    }
    catch (std::exception const &e)
    {
        fail(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeTorrents_metadata(JNIEnv *env, jclass, jlong id)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        return result(env, get(id).metadata());
    }
    catch (std::exception const &e)
    {
        fail(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_start(JNIEnv *env, jclass, jlong id,
                                                                                 jintArray files)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        if (!files)
            throw std::runtime_error("Missing file selection");
        auto size = env->GetArrayLength(files);
        // The session caps files at 100000; never size a native copy by an unchecked length.
        if (size < 1 || size > 100000)
            throw std::runtime_error("Select between 1 and 100000 files");
        std::vector<int> ids(size);
        env->GetIntArrayRegion(files, 0, size, ids.data());
        if (!env->ExceptionCheck())
            get(id).start(ids);
    }
    catch (std::exception const &e)
    {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_pause(JNIEnv *env, jclass, jlong id)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        get(id).pause();
    }
    catch (std::exception const &e)
    {
        fail(env, e);
    }
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_stormengine_port_NativeTorrents_awaitCheckpoint(JNIEnv *env, jclass,
                                                                                               jlong id, jint millis)
{
    std::lock_guard<std::mutex> guard(lock);
    try
    {
        if (millis < 0)
            throw std::runtime_error("Negative checkpoint wait");
        return get(id).await_checkpoint(std::chrono::milliseconds(millis)) ? JNI_TRUE : JNI_FALSE;
    }
    catch (std::exception const &e)
    {
        fail(env, e);
        return JNI_FALSE;
    }
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_close(JNIEnv *, jclass, jlong id)
{
    std::lock_guard<std::mutex> guard(lock);
    sessions.erase(id);
}
