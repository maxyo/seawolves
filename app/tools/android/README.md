# Seawolves: application developer guide

How the Android application is built, packaged, checked and released. The overview for players is the
[README](../../../README.md) ([по-русски](../../../README.ru.md)); the rules for contributions are in
[CONTRIBUTING.md](../../../CONTRIBUTING.md); the engine has its own guide,
[engine/README.md](../../../engine/README.md). Commands here run from the repository root.

The application id is `com.dustoff.seawolves`; the Java package is `org.stormengine.port`. **No game
resources and no links to the game are bundled**: players supply their own installation, archive, torrent file
or magnet link.

## What is where

| Path | What |
|---|---|
| `app/android/game/src` | the Java sources: launcher, import, the game activity, the bridge to the engine |
| `app/android/game/debug` | the component gallery, a developer screen of debug builds |
| `app/android/game/testloop` | the scenario driver for automated runs on a device (`on`) and its no-op twin (`off`) |
| `app/android/game/res`, `icon` | the platform resources and the launcher icon ([PLATFORM_ICONS.md](../../android/game/PLATFORM_ICONS.md)) |
| `app/android/game/assets/locales` | the texts of the Android-side dialogs, six languages |
| `app/mobile-ui/design` | the interface pack the engine's UI runtime draws ([its README](../../mobile-ui/design/README.md)) |
| `app/mobile-ui/PROGRAM` | interface scripts that run with the game's scripts |
| `app/script-patches` | fixes to the game's scripts ([its README](../../script-patches/README.md)) |
| `app/tools/android` | the build, packaging and release scripts described here |
| `app/tests`, `app/tools/android/tests` | the host checks |

## Building

```sh
bash app/tools/android/build_debug.sh
```

Needs Linux with JDK 17, Python 3.12+ with venv, a host C++ compiler (Clang for sanitizer checks), CMake, Ninja,
ccache, FFmpeg, curl, unzip, patch, Perl and make. The script fetches the SHA256-pinned NDK r30, SDK Platform 36
and Build Tools 36.0.0 and the pinned open-source dependencies into the engine's build workspace
(`engine/build/android-ci`), compiles the engine from `engine/` and packages the APK. It needs no game
files. Output: `app/build/dist/seawolves-arm64-debug.apk` with `SHA256SUMS` and `build-manifest.json`.

Environment of `build_debug.sh`:

| Variable | Meaning |
|---|---|
| `STORM_ANDROID_BUILD_TYPE` | `debug` (default) or `release`; a release needs the signing variables and `STORM_RELEASE_TAG` ([RELEASE.md](RELEASE.md)) |
| `STORM_ANDROID_ABI` | `arm64-v8a` (default) or `x86_64`: a debug APK for x86_64 emulators in `app/build/apk-x86_64/`, not distributable |
| `STORM_APK_BUILD_DIR`, `STORM_DIST_DIR` | where the packaging work and the distributable copies go |
| `STORM_CI_WORK` | the native build workspace (default `engine/build/android-ci`) |
| `STORM_NATIVE_BUILD_TYPE` | the engine's CMake build type: `RelWithDebInfo` for debug APKs (default), `Debug` unoptimized; a release is always `Release` (LTO) |
| `STORM_NATIVE_BUILD_DIR`, `STORM_SKIP_NATIVE_BUILD=1` | package an existing engine build; its build stamp must still match the checkout |
| `STORM_ARCHIVE_LIBRARY`, `STORM_TORRENT_LIBRARY` | reader libraries built elsewhere |
| `STORM_ANDROID_KEYSTORE` (+ `_KEY_ALIAS`, `_STORE_PASSWORD`, `_KEY_PASSWORD`) | sign with an existing key; a missing keystore fails instead of generating one |
| `STORM_ANDROID_TEST_LOOP_SIBLING=1` | also package the `--test-loop` APK for the boot smoke (below) |
| `STORM_BUILD_JOBS` | parallel jobs of the native builds |

**Versions.** A debug build is `<VERSION>-dev` with the versionCode of `VERSION`; a release takes both from its
tag ([RELEASE.md](RELEASE.md)).

**Signing.** A debug build is signed with a key generated on the first build, `app/build/apk*/debug.keystore`;
keep the file to update the app in place on your device. APKs signed with different keys cannot update each
other: uninstall the other build first (this deletes its game copy and saves; send the saves to yourself from
the About screen before that).

## What the APK ships

One APK for every distribution. Players bring their own copy of the game in two ways:

- the import through the system picker (folder, archive, installer) with the engine's archive reader
  `libstorm_archive.so`;
- the download library, for the player's own torrent file or magnet link of a copy they are entitled to: a
  torrent client (`libstorm_torrent.so`) in a separate `:downloads` process with a foreground service of type
  `dataSync`. The app carries no links to the game.

`apk_payload.py` defines this payload; `write_manifest.py` checks the packaged APK against it
(`app/tests/android_apk_payload`).

`--application-id` and `--app-label` override the defaults (`com.dustoff.seawolves`, «Seawolves»). The
About screen lists every bundled notice and links the exact source commit of the build, which is the
corresponding source the GPL requires. `build_apk.py` fails when a notice in `engine/third_party/licenses` is
neither packaged nor listed as debug-only, so a new engine dependency cannot ship without its licence.

## Release packaging

`build_apk.py --build-type release` (`build_debug.sh` with `STORM_ANDROID_BUILD_TYPE=release`) packages a
non-debuggable APK:

- `android:debuggable` is absent (checked with `aapt2 dump badging`); `gwpAsanMode` is set only in debug builds;
- every native library is stripped with `llvm-strip --strip-unneeded` (exports and GNU build IDs unchanged); the
  unstripped ones are kept in `<build dir>/symbols/arm64-v8a/` and `<build dir>/native-debug-symbols.zip`. Keep
  them with the exact APK: `python3 app/tools/android/symbolize.py <report file>` resolves the native frames of a
  crash in a player's report by build id;
- every ELF `LOAD` segment must be 16 KiB aligned and the APK is `zipalign -P 16` (both build types);
- a signing key is mandatory; there is no generated release key.

How a release is tagged, built by CI and published: [RELEASE.md](RELEASE.md).

## The interface pack and its gallery

The interface is drawn by the engine's UI runtime from [`app/mobile-ui/design`](../../mobile-ui/design/README.md);
`python3 app/mobile-ui/design/build.py --check` validates it. The component gallery (every shared component in
every state) is a debug screen, packaged only with `build_apk.py --ui-gallery` (`build_debug.sh` passes it for
the github debug APK):

```sh
adb shell am start -n com.dustoff.seawolves/org.stormengine.port.UiGalleryActivity \
    --es doc gallery/button.rml     # optional; --ez bare true hides the page switcher
```

## Checks

```sh
python3 app/tools/android/run_host_tests.py [--local]
```

runs every host check: `app/tests/*/run.py`, `app/tools/android/tests/*/run.py`, the project boundary, the
translation catalogs, the launcher icon and the Java formatting. It needs JDK 17, Python 3.12+, CMake, ccache and
a C++ compiler (it builds the engine's UI library from `engine/`). CI runs it on pull requests, on `main` and
before release packaging.

- The Java checks compile the production classes with small stand-ins for the Android types they touch and run
  them on the host JVM; several splice production method bodies out of the sources, so they follow the code
  without a device.
- Three checks need your copy of the game or a software-GL build of the engine's `storm_ui_shot` and run when
  `STORM_GAME_DIR` names the game folder; `--local` turns their missing inputs into failures. Without them they
  are reported `NOT_RUN`, which is never counted as a pass.
- The platform icon check needs the SDK of the build workspace and is `NOT_RUN` before the first APK build.
- `python3 app/tools/android/java_format.py` formats the Java sources (google-java-format, AOSP style).

Testing on a device, the scenario driver and the boot smoke: [android/game/TESTING.md](../../android/game/TESTING.md).
An emulator run of an x86_64 build exercises the Java side and the engine's logic, not the ARM64 code or a phone's
GPU driver; CI runs no emulator.
