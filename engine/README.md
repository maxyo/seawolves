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
xvfb-run -a -s -noreset ctest --test-dir build/ui-host --output-on-failure   # without xvfb-run when a display is there
```

The whole engine on the Linux host uses the same pinned sources as Android plus the distribution's SDL2 and zlib
(`cmake/host.cmake`). `tools/ci/boot_stage.py` configures and builds it (with sanitizers) in `build/boot-asan`. The
build installs the layers the engine lays over the game beside the executable (below); `-DSTORM_SCRIPT_PATCHES=DIR`
names the application's `script-patches` folder (default: the repository's `app/script-patches`). Run it with
`<build>/engine --game <game folder>`.

**Touch interface on the desktop — development only.** `-DSTORM_TOUCH_UI=ON` (in its own build directory) builds
the touch interface in, as on Android: the game's interfaces go to the script screens, the touch HUD is drawn, the
left mouse button is one finger, the wheel a short drag, Escape is Back. The build also installs `layers/mobile-ui`
(the interface scripts from `app/mobile-ui/PROGRAM`, and in `ui/` the pack `app/mobile-ui/design/build.py` makes).
Not on the desktop: the system text editor (a text field keeps its text, logged), the application's own actions
(settings, About, exit to the menu; logged), haptics. Players' desktop builds never have it: they keep the game's own
interface. The touch traces switched on by `adb shell setprop log.tag.<name> DEBUG` on Android are the environment
variables `<name>=DEBUG` here (`platform/debug_switch.hpp`).

## Where files live

The game folder is the player's and is only read. The engine writes into the player's own folders, chosen once at
start-up (`src/libs/util/include/platform/storage.hpp`); the file service refuses a write anywhere else.

| | Android | Linux |
| --- | --- | --- |
| the game | the folder chosen in the app | `--game <folder>`, remembered in `$XDG_CONFIG_HOME/seawolves/settings.json` |
| saves (the game's `SAVE\…`) | `<files>/storm/SaveData` | `$XDG_DATA_HOME/seawolves/<game>/saves` |
| the game's settings files, first copied from the game | `options` in `<files>/storm` | `options` and `engine.ini` in `$XDG_CONFIG_HOME/seawolves/<game>` |
| caches | the app's cache root | `$XDG_CACHE_HOME/seawolves/<build>` |
| logs | `<files>/storm/Logs` | `$XDG_STATE_HOME/seawolves/logs` |
| layers over the game | installed by the app from the APK into its cache: `mobile-ui`, `engine-shaders`, `script-patches` | installed by the build beside the executable in `layers/`: `engine-shaders` (`src/techniques`), `script-patches` (the application's desktop set); `mobile-ui` in a touch interface build |

`<files>` is the app's internal storage. `<game>` keys the game whatever its version: a hash of the title its
scripts show (`VERSION_NUMBER1` in `PROGRAM/_LSC_on_off.h`), so an update or a script mod keeps the saves and two
games of the series never share them; whether an old save still loads is the game scripts' business. `<build>` keys
one exact build, a hash of its whole PROGRAM folder, so caches are never shared between builds. Scripts that name no
title get `<build>` for their saves too, with a warning in the log (`platform/desktop_folders.hpp`). A game moved
elsewhere keeps both. The XDG variables default to `~/.config`, `~/.local/share`, `~/.cache` and `~/.local/state`.
`engine --game <folder> --folders` prints the folders and exits.

On Linux `settings.json` may also say what the game's store checks would: `"player_name": "<name>"` (the name some
games give a character after the player) and `"owned_apps": [<app id>, ...]` (the add-ons and games the player owns,
by their store app ids). Nothing is owned unless listed there, except where a game published its add-ons with it.

## Graphics options

On desktop every renderer feature that can be set is an option of its own, kept per game in that game's `engine.ini` of
the player's settings folder (above) under the option's name. A fresh start draws the defaults below: the full quality,
the scene at the window's own size; the texture generation (normal maps, parallax, ×2) is off, as on phones. The game's
scripts read, set and list them by name (`Graphics_GetOption(name)`, `Graphics_SetOption(name, value)` answering 1 taken
or 0 refused, and `Graphics_ListOptions(&object)` with each option's kind, range, values, default, the value that runs
and whether it is taken at the next start; script library `GraphicsOptionsScriptLib`); an unknown name reads as -1; it
and a value the option does not have are refused (0), each an error in the log. What a getter answers is what draws: an
option the device or another option keeps from running reads as the value that does. Caribbean Legend's own `Graphics_*`
natives set the same options (`src/libs/renderer/README.md`). On Android the app's presets decide and these natives do
not exist.

The table is `src/libs/renderer/src/graphics_options.cpp`; `tests/script_graphics` fails when this copy differs and
prints the current one.

| option | values | desktop default | applies |
| --- | --- | --- | --- |
| `render_scale` | the 3D scene's size, % of the window's height (FSR 1 upscale below 100) | 100 | at once |
| `full_screen` | 0 a window, 1 full screen | 1 | next start |
| `vsync` | 0 off, 1 in step with the display | 1 | at once |
| `anti_aliasing` | 0 off, 1 SMAA, 2 MSAA 2x, 3 MSAA 4x (MSAA with alpha to coverage for leaves and rigging) | 3 | at once |
| `anisotropic_filtering` | 1 off, 2, 4, 8, 16: anisotropic filtering of the world's textures | 16 | at once |
| `texture_quality` | 0 low, 1 medium, 2 high: the textures from their second mip, their first, their full size | 2 | next start |
| `sun_shadows` | 0 off, 1 low, 2 soft | 2 | at once |
| `lamp_shadows` | 0, 8, 16: lamps around the camera that light with their shadows | 16 | at once |
| `lamps_per_pixel` | 1..8: the most lamps that light one pixel with their shadows | 3 | at once |
| `ambient_occlusion` | interiors: 0 off, 1 as the game's data, 2 moderate, 3 strong | 2 | at once |
| `point_lights` | 0 the game's own, 4, 8: lights per draw lit per pixel | 8 | at once |
| `flash_lights` | 0..8: shots and explosions that light the scene at once | 8 | at once |
| `specular` | 0 off, 1 highlights of the sun and the lamps (shaped by gloss maps with the normal maps) | 1 | at once |
| `town_light` | 0 the game's town lightmaps, 1 towns relit for the live sun | 1 | at once |
| `aerial_perspective` | 0 the game's fog, 1 aerial perspective and height fog | 1 | at once |
| `bloom` | 0 off, 1 on | 1 | at once |
| `sun_shafts` | 0 off, 1 on (part of the bloom) | 1 | at once |
| `lens_flare` | 0 off, 1 the sun's glare | 1 | at once |
| `tone_mapping` | 0 off, 1 a tone curve | 1 | at once |
| `colour_grade` | 0 the original, 1 Caribbean (restrained), 2 overcast | 1 | at once |
| `colour_temperature` | 0..200: 100 the grade's warmth, 0 the coolest, 200 the warmest | 100 | at once |
| `colour_saturation` | 0..200 % of the grade's saturation | 100 | at once |
| `sharpen` | 0 off, 1 FSR 1 sharpening after the upscale (render scale below 100) | 1 | at once |
| `sea_detail` | 0 the game's sea, 1 no shimmer at the horizon and the sun's glint, 2 also soft shores | 2 | at once |
| `sea_wakes` | 0 off, 1 foam astern and at the bow | 1 | at once |
| `far_lod` | 0 every model whole, 1..4 pixels a far model's simplified level may differ by | 1 | at once |
| `wet_surfaces` | 0 off, 1 wet in the rain, 2 also puddles | 2 | at once |
| `vegetation_sway` | 0 off, 1 plants of a game without wind marks bend in the wind by their height (a guess from their textures) (old games only) | 0 | at once |
| `texture_normal_maps` | 0 off, 1 relief from normal maps made of the game's textures (old games only) | 0 | at once |
| `texture_height_maps` | 0 off, 1 parallax from height maps made with the normal maps (drawn through them) (old games only) | 0 | at once |
| `texture_upscale` | 0 off, 1 neural x2 textures near the camera (old games only) | 0 | at once |

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
