#include "storm_archive.h"
#include "app_upcalls.hpp"
#include "platform/jni_text.hpp"
#include "save_file.hpp"
#include <jni.h>
#include <string>
#include <memory>
#include <stdexcept>
namespace
{
using storm::jni_text::ToUtf8;
// The reader's text quotes names inside the player's archive, which need not be UTF-8 (a ZIP without the UTF-8 flag).
jstring java_string(JNIEnv *env, const char *data)
{
    return storm::jni_text::ToJavaReporting(env, data, "StormArchive", "archive");
}
// A path or password from Java (null: none).
struct Text
{
    std::string value;
    Text(JNIEnv *env, jstring s)
    {
        if (s)
            value = ToUtf8(env, s);
    }
    const char *data() const
    {
        return value.c_str();
    }
};
// [status, error, json] (json: the result document, or empty), or [status, error] without one.
jobjectArray result(JNIEnv *e, int code, const char *error, const char *json = nullptr)
{
    auto cls = e->FindClass("java/lang/String");
    if (!cls)
        return nullptr;
    const int count = json ? 3 : 2;
    auto array = e->NewObjectArray(count, cls, nullptr);
    e->DeleteLocalRef(cls);
    if (!array)
        return nullptr;
    const std::string number = std::to_string(code);
    const char *values[] = {number.c_str(), error, json};
    for (int i = 0; i < count; ++i)
    {
        auto s = java_string(e, values[i]);
        if (!s)
            return nullptr;
        e->SetObjectArrayElement(array, i, s);
        e->DeleteLocalRef(s);
    }
    return array;
}
struct Callback
{
    JNIEnv *env;
    jobject target;
    jmethodID method;
};
int progress(void *data, int stage, const char *path, uint64_t bytes, uint64_t total, uint64_t files)
{
    auto &c = *static_cast<Callback *>(data);
    if (!c.target)
        return 0;
    if (c.env->ExceptionCheck())
        return 1;
    auto text = java_string(c.env, path);
    if (!text)
        return 1;
    bool cancel =
        c.env->CallBooleanMethod(c.target, c.method, static_cast<jint>(stage), text, static_cast<jlong>(bytes),
                                 static_cast<jlong>(total), static_cast<jlong>(files));
    bool exception = c.env->ExceptionCheck();
    c.env->DeleteLocalRef(text);
    return cancel || exception;
}
// [totalBytes,fileBytes,entries,seconds,scratchBytes,depth,ratio,reserveBytes]
bool source_limits(JNIEnv *env, jlongArray bounds, storm_archive_source_limits &limits)
{
    if (env->GetArrayLength(bounds) != 8)
        return false;
    jlong v[8];
    env->GetLongArrayRegion(bounds, 0, 8, v);
    if (env->ExceptionCheck())
        return false;
    for (auto x : v)
        if (x <= 0)
            return false;
    limits = {uint64_t(v[0]), uint64_t(v[1]), uint64_t(v[2]), uint64_t(v[3]),
              uint64_t(v[4]), uint64_t(v[5]), uint64_t(v[6]), uint64_t(v[7])};
    return true;
}
bool callback(JNIEnv *env, jobject cb, Callback &out)
{
    constexpr auto progress = storm::android::upcall::NativeArchives_Progress::onProgress;
    out = {env, cb, nullptr};
    if (!cb)
        return true;
    auto cls = env->GetObjectClass(cb);
    if (!cls)
        return false;
    out.method = env->GetMethodID(cls, progress.name, progress.signature);
    env->DeleteLocalRef(cls);
    return out.method != nullptr;
}
template <class F> jobjectArray boundary(JNIEnv *env, F f)
{
    try
    {
        return f();
    }
    catch (const std::bad_alloc &)
    {
        storm::jni_text::Throw(env, "java/lang/OutOfMemoryError", "Native archive metadata memory limit exceeded");
    }
    catch (const std::exception &x)
    {
        storm::jni_text::Throw(env, "java/lang/IllegalArgumentException", x.what());
    }
    catch (...)
    {
        storm::jni_text::Throw(env, "java/lang/IllegalStateException", "Native archive failure");
    }
    return nullptr;
}
} // namespace
// Results are [status, error, json]; json is the discovery document or a
// failure description (encrypted, no space, missing volumes), or empty.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_stormengine_port_NativeArchives_discover(JNIEnv *env, jclass, jstring source, jstring scratch,
                                                  jstring password, jstring layer, jlongArray bounds, jobject cb)
{
    return boundary(env, [&]() -> jobjectArray {
        Text src(env, source), tmp(env, scratch), secret(env, password), patches(env, layer);
        storm_archive_source_limits limits{};
        if (!bounds || !source_limits(env, bounds, limits))
            return env->ExceptionCheck()
                       ? nullptr
                       : result(env, STORM_ARCHIVE_LIMIT, "Expected eight positive source limits", "");
        Callback progress_callback;
        if (!callback(env, cb, progress_callback))
            return nullptr;
        char error[2048]{};
        char *raw = nullptr;
        int code = storm_archive_discover(src.data(), tmp.data(), password ? secret.data() : nullptr,
                                          layer ? patches.data() : nullptr, &limits, progress, &progress_callback, &raw,
                                          error, sizeof(error));
        std::unique_ptr<char, decltype(&storm_archive_free)> json(raw, storm_archive_free);
        if (env->ExceptionCheck())
            return nullptr;
        return result(env, code, error, json ? json.get() : "");
    });
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_NativeArchives_extractRoot(
    JNIEnv *env, jclass, jstring source, jstring scratch, jstring password, jstring id, jstring staging,
    jlongArray bounds, jobject cb)
{
    return boundary(env, [&]() -> jobjectArray {
        Text src(env, source), tmp(env, scratch), root(env, id), out(env, staging), secret(env, password);
        storm_archive_source_limits limits{};
        if (!bounds || !source_limits(env, bounds, limits))
            return env->ExceptionCheck()
                       ? nullptr
                       : result(env, STORM_ARCHIVE_LIMIT, "Expected eight positive source limits", "");
        Callback progress_callback;
        if (!callback(env, cb, progress_callback))
            return nullptr;
        char error[2048]{};
        char *raw = nullptr;
        int code =
            storm_archive_extract_root(src.data(), tmp.data(), password ? secret.data() : nullptr, root.data(),
                                       out.data(), &limits, progress, &progress_callback, &raw, error, sizeof(error));
        std::unique_ptr<char, decltype(&storm_archive_free)> json(raw, storm_archive_free);
        if (env->ExceptionCheck())
            return nullptr;
        return result(env, code, error, json ? json.get() : "");
    });
}
// [status, error, json]: the sets of a script patch layer (storm_archive_patch_layer).
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_NativeArchives_patchLayer(JNIEnv *env, jclass,
                                                                                              jstring layer)
{
    return boundary(env, [&]() -> jobjectArray {
        Text path(env, layer);
        char error[2048]{};
        char *raw = nullptr;
        int code = storm_archive_patch_layer(layer ? path.data() : nullptr, &raw, error, sizeof(error));
        std::unique_ptr<char, decltype(&storm_archive_free)> json(raw, storm_archive_free);
        return result(env, code, error, json ? json.get() : "");
    });
}
// [status, error]: removes a tree by the bytes of its names (storm_archive_remove_tree).
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_NativeArchives_removeTree(JNIEnv *env, jclass,
                                                                                              jstring path)
{
    return boundary(env, [&]() -> jobjectArray {
        Text target(env, path);
        char error[2048]{};
        int code = storm_archive_remove_tree(path ? target.data() : nullptr, error, sizeof(error));
        return result(env, code, error);
    });
}
// null when the engine would load the save at `path`, else why not (the field at fault). Reads only.
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeArchives_checkSave(JNIEnv *env, jclass,
                                                                                        jstring path)
{
    try
    {
        std::string error;
        if (storm::save::checkFile(Text(env, path).value, error))
            return nullptr;
        return java_string(env, error.c_str());
    }
    catch (const std::bad_alloc &)
    {
        storm::jni_text::Throw(env, "java/lang/OutOfMemoryError", "Native save check memory limit exceeded");
    }
    catch (const std::exception &x)
    {
        storm::jni_text::Throw(env, "java/lang/IllegalArgumentException", x.what());
    }
    return nullptr;
}
