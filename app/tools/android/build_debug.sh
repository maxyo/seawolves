#!/usr/bin/env bash
# Build the native engine (engine/) and package the APK into app/build/. Despite its name the
# script makes every build type; the developer guide (tools/android/README.md) lists the environment it reads.
#
# STORM_ANDROID_BUILD_TYPE=debug (default): debuggable APKs signed with a debug key generated on the first build
#   ($apk_root/debug.keystore), versionName <VERSION>-dev.
# STORM_ANDROID_BUILD_TYPE=release: the engine's Release (LTO) build of the release tag STORM_RELEASE_TAG, checked
#   before anything is built (tools/android/RELEASE.md). The APK is left unsigned and the STORM_ANDROID_* signing
#   variables are refused: no key is present during a release build (tools/android/sign_release.py signs afterwards).
# STORM_ANDROID_ABI=x86_64 (debug only): APKs that run natively on x86_64 emulators, from the engine's x86_64
#   build directories (engine/tools/android/README.md, ABIs); not distributable, so no build/dist manifest.
# STORM_ANDROID_APPLICATION_ID / STORM_ANDROID_APP_LABEL (debug only): another application id and launcher label, so the
#   APK installs beside the game as a separate app with its own data (the renderer bench's sibling:
#   com.dustoff.seawolves.bench, «Seawolves bench»).
# STORM_ANDROID_TEST_LOOP_SIBLING=1 (arm64 debug only): also packages the --test-loop APK from the same native
#   libraries into $apk_root/testloop, for the boot smoke on an emulator (tools/android/emu_smoke.py).
set -euo pipefail
app=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# The engine is the sibling of app/ in the repository.
engine=$(cd "$app/../engine" && pwd)
work=${STORM_CI_WORK:-"$engine/build/android-ci"}
export STORM_ANDROID_ABI=${STORM_ANDROID_ABI:-arm64-v8a}
source "$engine/tools/android/abi.sh"
build_type=${STORM_ANDROID_BUILD_TYPE:-debug}
case "$build_type" in
  debug) native_type=${STORM_NATIVE_BUILD_TYPE:-RelWithDebInfo} ;;
  release)
    native_type=${STORM_NATIVE_BUILD_TYPE:-Release}
    if [[ $native_type != Release ]]; then
      echo "Release APKs package only the engine's Release (LTO) build, not $native_type" >&2
      exit 2
    fi ;;
  *) echo "Unsupported STORM_ANDROID_BUILD_TYPE: $build_type (debug or release)" >&2; exit 2 ;;
esac
identity=()
if [[ -n ${STORM_ANDROID_APPLICATION_ID:-}${STORM_ANDROID_APP_LABEL:-} ]]; then
  if [[ $build_type != debug || -z ${STORM_ANDROID_APPLICATION_ID:-} || -z ${STORM_ANDROID_APP_LABEL:-} ]]; then
    echo "STORM_ANDROID_APPLICATION_ID and STORM_ANDROID_APP_LABEL go together, in debug builds only" >&2
    exit 2
  fi
  identity=(--application-id "$STORM_ANDROID_APPLICATION_ID" --app-label "$STORM_ANDROID_APP_LABEL")
fi
if [[ $build_type == release && $abi != arm64-v8a ]]; then
  echo "Release APKs are arm64-v8a only, not $abi" >&2
  exit 2
fi
# A release is built only from its tag v<VERSION> on a clean tree (tools/android/release_version.py, RELEASE.md):
# STORM_RELEASE_TAG names it; STORM_RELEASE_REHEARSAL=1 is the workflow's dry run for a tag not created yet.
# The tag gives the versionName and the versionCode; nothing else may set them for a release.
if [[ $build_type == release ]]; then
  if [[ -z ${STORM_RELEASE_TAG:-} ]]; then
    echo "Release APKs are built only from a release tag: set STORM_RELEASE_TAG=v<VERSION> (tools/android/RELEASE.md)" >&2
    exit 2
  fi
  if [[ -n ${STORM_ANDROID_KEYSTORE:-}${STORM_ANDROID_KEY_ALIAS:-}${STORM_ANDROID_STORE_PASSWORD:-}${STORM_ANDROID_KEY_PASSWORD:-} ]]; then
    echo "A release build signs nothing: unset the STORM_ANDROID_* signing variables (tools/android/RELEASE.md)" >&2
    exit 2
  fi
  rehearse=(); [[ ${STORM_RELEASE_REHEARSAL:-0} == 1 ]] && rehearse=(--rehearse)
  release_info=$(python3 "$app/tools/android/release_version.py" check --tag "$STORM_RELEASE_TAG" "${rehearse[@]}")
  STORM_ANDROID_VERSION_NAME=$(sed -n 's/^version_name=//p' <<<"$release_info")
  STORM_ANDROID_VERSION_CODE=$(sed -n 's/^version_code=//p' <<<"$release_info")
  export STORM_ANDROID_VERSION_NAME STORM_ANDROID_VERSION_CODE
  echo "Release $STORM_RELEASE_TAG: versionName $STORM_ANDROID_VERSION_NAME, versionCode $STORM_ANDROID_VERSION_CODE"
elif [[ -n ${STORM_ANDROID_VERSION_NAME:-}${STORM_ANDROID_VERSION_CODE:-} ]]; then
  echo "STORM_ANDROID_VERSION_NAME and STORM_ANDROID_VERSION_CODE are set only by a release build from its tag" >&2
  exit 2
fi
native_build=${STORM_NATIVE_BUILD_DIR:-"$work/engine-${native_type,,}$sfx"}
archive_build=${STORM_ARCHIVE_BUILD_DIR:-"$work/archive-reader$sfx"}
torrent_build=${STORM_TORRENT_BUILD_DIR:-"$work/torrent-reader"}
archive_library=${STORM_ARCHIVE_LIBRARY:-"$archive_build/output/libstorm_archive.so"}
torrent_library=${STORM_TORRENT_LIBRARY:-"$torrent_build/android$sfx/libstorm_torrent.so"}
ndk=$(python3 "$engine/tools/pins.py" ndk "$work") # the pinned NDK (engine/pins.json)
export STORM_NATIVE_BUILD_TYPE="$native_type"
export STORM_CI_WORK="$work"
export STORM_NATIVE_BUILD_DIR="$native_build"
if [[ ${STORM_SKIP_NATIVE_BUILD:-0} != 1 ]]; then
  bash "$engine/tools/android/ci_dependencies.sh"
  bash "$engine/tools/android/ci_engine.sh"
  python3 "$engine/android/archive/build.py" --build-dir "$archive_build" --ndk "$ndk" --abi "$abi" --jobs "${STORM_BUILD_JOBS:-2}"
  python3 "$engine/tools/android/build_torrent.py" --work "$torrent_build" --ndk "$ndk" --abi "$abi" --jobs "${STORM_BUILD_JOBS:-2}"
fi
bash "$app/tools/android/prepare_sdk.sh" "$work" --lint
python3 "$engine/tools/android/verify_engine.py" --library "$native_build/game/libengine.so" --ndk "$ndk" --abi "$abi"
apk_root=${STORM_APK_BUILD_DIR:-"$app/build/apk$sfx"}
# The component gallery is a developer tool: only in the debug package.
gallery=(); [[ $build_type == debug ]] && gallery=(--ui-gallery)
python3 "$app/tools/android/build_apk.py" --build-type "$build_type" --android-jar "$work/tools/platform-36/android-36/android.jar" \
  --build-tools "$work/tools/buildtools-36/android-16" --lint "$work/tools/cmdline-tools-23/cmdline-tools/bin/lint" --sdl-source "$work/sources/SDL" --deps-prefix "$work/prefix$sfx" --abi "$abi" \
  --engine-library "$native_build/game/libengine.so" --ndk "$ndk" \
  --build-dir "$apk_root" --default-keystore "$apk_root/debug.keystore" --engine-root "$engine" --engine-licenses "$native_build/engine-licenses" \
  --ffmpeg-libraries "$native_build/runtime/ffmpeg" --archive-library "$archive_library" --torrent-library "$torrent_library" \
  --ui-library "$native_build/game/libstorm_ui.so" --bgfx-library "$native_build/game/libbgfx.so" --ui-licenses "$native_build/licenses" "${gallery[@]}" "${identity[@]}"
if [[ $abi == arm64-v8a ]]; then
  # build/dist is what the release publication reads.
  python3 "$app/tools/android/write_manifest.py" --app "$app" --build-type "$build_type" --apk-root "$apk_root" \
    --output "${STORM_DIST_DIR:-"$app/build/dist"}" \
    --aapt2 "$work/tools/buildtools-36/android-16/aapt2"
else
  echo "$abi APK: $apk_root (emulator debug build, no dist manifest)"
fi
if [[ ${STORM_ANDROID_TEST_LOOP_SIBLING:-0} == 1 ]]; then
  if [[ $build_type != debug || $abi != arm64-v8a ]]; then
    echo "The test-loop sibling is an arm64 debug build (STORM_ANDROID_BUILD_TYPE=debug)" >&2
    exit 2
  fi
  env -u STORM_ANDROID_KEYSTORE -u STORM_ANDROID_KEY_ALIAS -u STORM_ANDROID_STORE_PASSWORD -u STORM_ANDROID_KEY_PASSWORD \
    python3 "$app/tools/android/build_apk.py" --test-loop --build-type debug --android-jar "$work/tools/platform-36/android-36/android.jar" \
    --build-tools "$work/tools/buildtools-36/android-16" --lint "$work/tools/cmdline-tools-23/cmdline-tools/bin/lint" --sdl-source "$work/sources/SDL" --deps-prefix "$work/prefix$sfx" --abi "$abi" \
    --engine-library "$native_build/game/libengine.so" --ndk "$ndk" \
    --build-dir "$apk_root/testloop" --default-keystore "$apk_root/debug.keystore" --engine-root "$engine" --engine-licenses "$native_build/engine-licenses" \
    --ffmpeg-libraries "$native_build/runtime/ffmpeg" --archive-library "$archive_library" --torrent-library "$torrent_library" \
    --ui-library "$native_build/game/libstorm_ui.so" --bgfx-library "$native_build/game/libbgfx.so" --ui-licenses "$native_build/licenses"
fi
