# Android native engine

ARM64 native engine, Android runtime/storage policy, audio backend and the pinned native dependency builds
(`archive/`, `audio/`, `torrent/`). Build instructions: [tools/android/README.md](../tools/android/README.md).
The Android application, launcher/importer, APK packaging and releases live in `app/`, next to `engine/`
(`app/android/`, `app/tools/android/`).

Android uses the platform C/C++ allocator, not mimalloc (TLS recursion and shared-libc++ heap ownership
mismatches).
