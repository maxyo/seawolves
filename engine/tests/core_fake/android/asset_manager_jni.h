#pragma once
// The NDK's asset manager of a Java AssetManager, for engine code built for a host check (asset_manager.h).
#include "asset_manager.h"
#include <jni.h>

inline AAssetManager *AAssetManager_fromJava(JNIEnv *, jobject)
{
    static AAssetManager manager;
    return &manager;
}
