#!/usr/bin/env bash
# Native dependency and regression pipeline; no APK packaging or game resources.
# STORM_ANDROID_ABI=x86_64 builds the emulator prefix ($work/prefix-x86_64); see abi.sh.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
source "$repo/tools/android/abi.sh"
mkdir -p "$work/downloads" "$work/sources" "$work/tools"
pins() { python3 "$repo/tools/pins.py" "$@"; } # engine/pins.json
ndk=$(pins ndk "$work")
if [[ ! -d "$ndk" ]]; then
    ndk_archive=$(pins fetch ndk "$work/downloads")
    mkdir -p "$work/tools/ndk"
    unzip -q "$ndk_archive" -d "$work/tools/ndk"
fi
python3 -m venv "$work/python"
"$work/python/bin/pip" install --disable-pip-version-check cmake==3.28.4 ninja==1.13.0
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then
    export JAVA_HOME="$JAVA_HOME_17_X64"
    export PATH="$JAVA_HOME/bin:$PATH"
fi
python3 "$repo/tools/android/build_dependencies.py" --ndk "$ndk" --sdl-source "$work/sources/SDL" --build-dir "$work/dependencies$sfx" --prefix "$work/prefix$sfx" --abi "$abi" --jobs "${STORM_BUILD_JOBS:-2}"
# The checks compiled for Android (their host runs are host checks: tests/CMakeLists.txt) compile through the Storm
# ccache launcher.
cxx="$repo/tools/ccache/cxx"
ndk_cxx="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/${triple}${api}-clang++"
python3 "$repo/tools/android/test_sdl_surface_lifecycle.py" --sdl-source "$work/sources/SDL"
python3 "$repo/tests/portability/check_toolchain.py"
CXX="$cxx" STORM_CCACHE_CXX="$ndk_cxx" python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL"
CXX="$cxx" STORM_CCACHE_CXX="$ndk_cxx" python3 "$repo/tests/host_hooks/run.py" --sdl-source "$work/sources/SDL"
