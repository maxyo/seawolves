#!/usr/bin/env bash
set -euo pipefail
work=${1:?Native build workspace required}
fetch_sdk() {
  local archive=$1 sha=$2 dest=$3
  mkdir -p "$work/downloads"
  if [[ ! -f "$work/downloads/$archive" ]]; then
    curl -fL --retry 3 "https://dl.google.com/android/repository/$archive" -o "$work/downloads/$archive"
  fi
  printf '%s  %s\n' "$sha" "$work/downloads/$archive" | sha256sum -c -
  if [[ ! -d "$dest" ]]; then mkdir -p "$dest"; unzip -q "$work/downloads/$archive" -d "$dest"; fi
}
# Google Play requires targetSdk 36 (Android 16) for new apps and updates since 2026-08-31.
# Versioned destinations: an existing API 35 extraction must not satisfy the check.
fetch_sdk platform-36_r02.zip 37607369a28c5b640b3a7998868d45898ebcb777565a0e85f9acf36f29631d2e "$work/tools/platform-36"
fetch_sdk build-tools_r36_linux.zip 5d9ac77fb6ff43d9da518a337b4fcf8f9097113df531d99ccefe80ef7ce8250b "$work/tools/buildtools-36"
