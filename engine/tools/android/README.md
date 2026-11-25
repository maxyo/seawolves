# Android native build

These scripts build the native engine library (`libengine.so`) and its runtime dependencies. They never
build or publish APKs: packaging and releases are the application's (`app/tools/android/` in the repository).
Paths here are relative to `engine/`.

## CI scripts

```sh
bash tools/android/ci_dependencies.sh   # pinned NDK (checksum verified), tools, dependencies, regressions
bash tools/android/ci_engine.sh         # builds and verifies only libengine.so
```

Requires JDK 17, CMake, Ninja, a C++ compiler, a Python venv, FFmpeg, curl and unzip. `STORM_CI_WORK`
overrides the build workspace.

`ci_engine.sh` defaults to `Release`: `-O2 -g -DNDEBUG` with ThinLTO, hidden symbol visibility and section
garbage collection (`cmake/android.cmake`; `-DSTORM_ANDROID_RELEASE_LTO=OFF` disables only LTO). Only
`SDL_main` and the JNI entry points are exported. Debug info stays in the library; the application's
release packaging strips it and keeps the symbol file. `STORM_NATIVE_BUILD_TYPE=Debug` gives an
unoptimized build; `STORM_NATIVE_BUILD_TYPE` and `STORM_NATIVE_BUILD_DIR` together select an isolated
build directory (e.g. `RelWithDebInfo`). Package `game/libengine.so` from the build.

## Manual build

Every download the build makes is pinned in `pins.json` (version, URL and SHA-256, or a Git commit), read by CMake
(`storm_pin`, `storm_fetch_pin` in `cmake/StormBuildCache.cmake`) and by the scripts (`tools/pins.py`).
Dependencies: SDL (pin `sdl`) with `patches/sdl2-android-ndk30.patch`, fetched into the shared copies like the
engine's other sources (`storm_fetch_declare`, below); `--sdl-source` is where the script links the patched view
(`sdl_source.cmake`), for the application's packaging too:

```sh
python3 tools/android/build_dependencies.py \
  --ndk /path/to/android-ndk-r30 \
  --sdl-source /path/to/sources/SDL \
  --build-dir /path/to/android-dependency-build \
  --prefix /path/to/android-dependency-install \
  --jobs 4
```

The engine's other sources (fmt, spdlog, fast_float, Catch2, miniaudio with its patch, FFmpeg, GLM, ncnn, ...) are
fetched by the configure step into one shared read-only copy per pin (`storm_fetch_declare`, `~/.cache/storm-deps`).
The configure step also builds and caches the LGPL FFmpeg (`tools/ffmpeg/README.md`; package `runtime/ffmpeg/*.so`
and `configure.txt` with the engine) and the host bgfx shader compiler, and compiles through ccache when it is
installed (`tools/ccache/README.md`).

```sh
cmake -S . -B build/android-engine -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-arm64.cmake" \
  -DSTORM_ANDROID_NDK=/path/to/android-ndk-r30 \
  -DSTORM_ANDROID_DEPS=/path/to/dependency-prefix
cmake --build build/android-engine --target engine --parallel 2
python3 tools/android/verify_engine.py --library build/android-engine/game/libengine.so \
  --ndk /path/to/android-ndk-r30
```

`verify_engine.py` checks the library's ABI, exports, runtime libraries (one bgfx, no allocator of its own) and
that every `CREATE_*` registration compiled into the objects and archives its link consumed (Ninja's inputs of it
in the build directory above `--library`) is linked.

## ABIs: arm64-v8a releases, x86_64 emulator builds

Every release is arm64-v8a. x86_64 is only for debug builds that run natively on x86_64 emulators. The ABI
is a parameter of every Android build script, default `arm64-v8a`:

- `STORM_ANDROID_ABI=x86_64` for `ci_dependencies.sh` and `ci_engine.sh` (`abi.sh`); `--abi x86_64` for
  `build_dependencies.py`, `verify_engine.py`, `build_torrent.py` and `android/archive/build.py`
  (`android_abi.py`).
- CMake: `cmake/toolchains/android-x86_64.cmake` instead of `android-arm64.cmake`.
- Default directories of other ABIs carry a suffix (`$work/prefix-x86_64`, `$work/engine-x86_64`, ...);
  pass the archive build its own `--build-dir`.

## Fast emulator

Use an x86_64 build and let the emulator render on the host GPU through ANGLE in the guest (a plain
`-gpu host` under Xvfb gives software llvmpipe, 1-2 fps):

```sh
export DISPLAY=:71   # any X display (Xvfb is fine); without one the emulator falls back to SwiftShader
emulator -avd <avd> -port <port> -no-window -no-audio -no-boot-anim -no-snapshot \
  -gpu host -accel on -feature GuestAngle,VulkanNativeSwapchain
```

`adb shell dumpsys SurfaceFlinger | grep GLES:` must name ANGLE on the host GPU. Emulator present alone
takes ~50 ms, so performance numbers come from a real arm64 device.

## Runtime keys and diagnostics

- `render_metrics=1` in `engine.ini`: five-second logs of Present cadence, Present call time and draw
  count (not GPU timestamps). Off by default.
- `texture_anisotropy`: `0` keeps the game's techniques (default), `1` linear, `2`/`4`/`8`/`16`
  anisotropic, capped by the device. The application's graphics preferences override this key and
  `texture_quality`, `vsync`, `screen_x`, `screen_y`; changes need a restart.
- Frame pacing goes through Swappy (`src/libs/renderer_bgfx/src/shim/frame_pacing.hpp`) at exactly the player's
  frame-rate setting (Swappy's automatic swap interval is off: late frames take the next refresh, the rate is never
  lowered); there is no `max_fps` key. Its state is logged under tag `storm-bgfx`
  (`frame pacing: ...`, every 30 s with frame-time percentiles). `adb shell setprop swappy.disable 1`
  (root) switches to unpaced `eglSwapBuffers` for an A/B with the same APK.

## Compiled script cache

`[script] cache_mode` in `engine.ini`: compiled script cache in `fs::GetCachePath()/Cache`. `2` (default) checks
the key once per session, `1` on every segment load, `0` turns it off. The key covers the program directory and
the APK interface overlay (path, size, mtime), the engine build and the game directory; contents are not hashed,
so an in-place edit with unchanged size and mtime is not detected.

## Tracy profiler (development builds)

`-DSTORM_TRACY=ON` (`cmake/tracy.cmake`, Tracy 0.14.1) links the Tracy client into a development build; use
its own build directory (e.g. `build/android-tracy`). The application refuses such a `libengine.so` for
release APKs. The client records on demand and listens on 127.0.0.1:8086 only. The viewer must be Tracy
0.14.1:

```sh
adb -s <serial> forward tcp:8086 tcp:8086
tracy-capture -o game.tracy -a 127.0.0.1 -s 30   # or connect tracy-profiler to 127.0.0.1
```

A host build (no toolchain file) with `-DSTORM_TRACY=ON` is profiled the same way, without the
forward.

## Host checks

`python3 tools/ci/run_host_tests.py` builds and runs the host checks (the CTest project `tests/CMakeLists.txt`;
the pinned sources in `build/host-ci/sources`; under `xvfb-run -a` without a display). `/.github/workflows/ci.yml` runs it with the
host build on pushes and pull requests to `main` that change `engine/`.

## The application contract

`app_contract.json` lists what the engine and the application exchange by name: main-loop commands, HUD
input, UI host actions and state channels, settings, script events and the Java methods the engine looks up.
`cmake/StormAppContract.cmake` generates the engine's headers from it (`app_upcalls.hpp`: the Java methods the
engine calls, `storm_archive_status.h`: the archive reader's statuses, `app_script_events.hpp`: the events the engine
raises for the interface scripts, `app_ui_names.hpp`: HUD input, host actions, state channels, the settings, the
facts reported beside them and the debug overrides with their values), and `app/tools/android/app_contract.py` the
application's classes (`ArchiveStatus`, `AppNames`): code on both sides names these only through the generated ones.
Every APK build finds the upcalls in the compiled classes (javap). `tests/app_contract` checks the main-loop commands
and shortcuts on the engine's tables, `tests/renderer_bgfx/app_settings_check` that the effects take every `fx_*`
override and texture switch; the application's `tests/engine_contract` checks the interface scripts' event handlers.
The Java natives are not listed: every APK build matches them with the `Java_` functions the native libraries export
(`app/tools/android/build_apk.py`).

## Native crash reports

The engine links no crash-reporting SDK: debuggerd writes the tombstone and logs its frames with build
ids; the NDK's `ndk-stack -sym native-debug-symbols.zip` (the release's symbols, `app/tools/android/RELEASE.md`)
resolves them.
