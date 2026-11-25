**English** | [Русский](README.ru.md)

# engine/

The native engine of [Seawolves](../README.md): a fork of [Storm Engine](https://github.com/storm-devs/storm-engine),
the engine of the Sea Dogs games, ported to Android. It builds `libengine.so`, which the application in
[`../app`](../app) packages into the APK, and a Linux host build of the same engine for development and checks.

What the port adds to upstream:

- **A bgfx renderer** (`src/libs/renderer_bgfx`): OpenGL ES 3.0 or Vulkan 1.1 through
  [bgfx](https://github.com/bkaradzic/bgfx) in place of DirectX 9. The original drawing code runs on a D3D9-shaped
  compatibility layer (`src/libs/renderer_bgfx/src/shim`); `src/libs/renderer_bgfx/fx` holds the lighting, shadows,
  relief, sea and post effects. Texture formats and effects follow the GPU's capabilities; whatever is turned off is
  logged.
- **A touch interface host** (`src/libs/ui`, `src/libs/ui_host`): the game's screens and HUD rebuilt on
  [RmlUi](https://github.com/mikke89/RmlUi), driven by the game's own interface scripts.
- **An Android platform layer** (`src/apps/engine`, `src/libs/util/include/platform`, `android/`): lifecycle and
  autosave, touch input, audio, archive import, downloads, diagnostics and logs.
- **Graphics and performance work for phones**: relief, normal and gloss maps generated from the game's textures
  (`src/libs/relief`) and a neural ×2 texture upscale, both when the player turns them on, far LODs, background
  resource preparation with a cache, thermal awareness, quality presets.

Only Sea Dogs: To Each His Own («Корсары: Каждому своё») is supported and tested. Other games on this engine may
need script and resource differences the port does not cover. No game files are included; the engine reads the
player's own installed copy.

**Names.** `storm-engine`, `StormEngine` and the `storm::` namespaces are upstream's names and are kept, so that
upstream changes still merge; the port's own build and tool variables follow upstream's `STORM_` prefix. Seawolves
is the name of the application and the repository.

## Directory map

| Path | What |
| --- | --- |
| `src/apps/engine` | the engine executable / `libengine.so` entry point |
| `src/libs/*` | engine libraries (upstream's and the port's) |
| `src/techniques` | the renderer techniques the game's scripts name |
| `android/` | Android-only native parts: archive reader, torrent session, audio backend |
| `cmake/` | build setup: host and Android toolchains, pinned dependencies, ccache |
| `tests/` | host checks, one directory each |
| `tools/` | build, CI and development tools ([tools/ci/README.md](tools/ci/README.md)) |
| `third_party/` | the third-party inventory ([third_party/README.md](third_party/README.md)); `licenses/` holds the licences and provenance of third-party code and models |

## Building

The Android library (from `engine/`; details in [tools/android/README.md](tools/android/README.md)):

```sh
bash tools/android/ci_dependencies.sh   # pinned NDK, SDK tools and dependencies into build/android-ci
bash tools/android/ci_engine.sh         # build/android-engine: libengine.so for arm64-v8a
```

The APK is built by the application: `bash app/tools/android/build_debug.sh` from the repository root.

The UI host (the interface library with its tests; no game, no GPU):

```sh
cmake -S src/libs/ui -B build/ui-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ui-host
xvfb-run -a ctest --test-dir build/ui-host --output-on-failure   # without xvfb-run when a display is there
```

The whole engine on the Linux host uses the same pinned sources as Android plus the distribution's SDL2 and zlib
(`cmake/host.cmake`). `tools/ci/boot_stage.py` configures and builds it (with sanitizers) in `build/boot-asan`.

## Checks

Before a pull request run `tools/ci/gate.py` ([tools/ci/README.md](tools/ci/README.md)): it builds and checks
what the change can affect. The checks live in five places:

| Where | What | How it runs |
| --- | --- | --- |
| `tests/<name>` | host checks, one CTest project (`tests/CMakeLists.txt`): each compiles the production code it guards against small stubs (no game, device or GPU unless it says so) | `python3 tools/ci/run_host_tests.py` (all, or `--only NAME`), or ctest in `build/host-ci` ([tools/ci/README.md](tools/ci/README.md)); CI on every push and pull request |
| `src/libs/ui/tests` | the interface: screens, HUD, input, layout, frame cost | ctest of the UI host build (above); CI |
| `src/libs/renderer_bgfx/fx/tests` | CPU-side checks of the effects package | host checks (`tests/renderer_bgfx`, label `renderer`) |
| `tools/game_checks/<name>` | checks on the real game data | `gate.py` (needs `STORM_GAME_DIR`), or `python3 tools/game_checks/<name>/run.py <game dir>` |
| `tools/scene_sweep` | the scene sweep: a host engine loads scenes of the game and checks every frame | on demand before releases ([tools/scene_sweep/README.md](tools/scene_sweep/README.md)); `tools/ci/boot_stage.py` runs three scenes of it |

## Environment variables

| Variable | Used by | What |
| --- | --- | --- |
| `STORM_GAME_DIR` | game checks, boot stage, scene sweep, `tests/video` (game videos) | an installed copy of the game |
| `STORM_BOOT_SAVE` | boot stage | a save whose hero stands in the town of Martinique |
| `STORM_VIRTUALGL` | boot stage, scene sweep, `tests/renderer_bgfx` (multisampled checks) | an unpacked VirtualGL, to render with the host GPU |
| `STORM_CI_WORK` | Android build, gate, boot stage | the dependency directory (default `build/android-ci`) |

Game data, saves and anything made from them stay outside the repository.

## Licence

GPL-3.0-only, as upstream released it ([LICENSE.txt](LICENSE.txt)). Every third-party library, tool and model, with
its version, licence and where its licence text ends up: [third_party/README.md](third_party/README.md). The fork
point and the licences of the rest of the repository: [../NOTICE](../NOTICE).

Thanks to the original Storm Engine authors for opening the engine, and to the
[storm-devs](https://github.com/storm-devs/storm-engine) community that maintains it.
