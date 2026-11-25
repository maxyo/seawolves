#!/usr/bin/env bash
# The pinned Android SDK parts (engine/pins.json) unpacked into <workspace>/tools.
#   prepare_sdk.sh <workspace>          platform and build tools: packaging and signing
#   prepare_sdk.sh <workspace> --lint   also the command-line tools: Android Lint over the app's Java (build_apk.py)
set -euo pipefail
work=${1:?Native build workspace required}
lint=${2:-}
[[ -z $lint || $lint == --lint ]] || { echo "prepare_sdk.sh: unknown option $lint" >&2; exit 2; }
pins=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../engine/tools" && pwd)/pins.py
fetch_sdk() {
  local pin=$1 dest=$2 archive
  archive=$(python3 "$pins" fetch "$pin" "$work/downloads")
  if [[ ! -d "$dest" ]]; then mkdir -p "$dest"; unzip -q "$archive" -d "$dest"; fi
}
# Google Play requires targetSdk 36 (Android 16) for new apps and updates since 2026-08-31.
# Versioned destinations: an existing API 35 extraction must not satisfy the check.
fetch_sdk android-platform "$work/tools/platform-36"
fetch_sdk android-build-tools "$work/tools/buildtools-36"
if [[ -n $lint ]]; then
  fetch_sdk android-cmdline-tools "$work/tools/cmdline-tools-23"
fi
