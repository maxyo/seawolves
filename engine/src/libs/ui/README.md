# Portable game UI runtime

`storm_ui` draws the game interface from a *UI pack* supplied by the application
(documents, styles, component specs, icons, fonts). It is built on RmlUi and renders only through
bgfx (`include/storm_ui/backend_bgfx.h`); the platform host owns the window, the frame loop and
touch delivery. The pack and the design rules live with the application
(`app/mobile-ui/design` in the repository). Game interfaces and the HUD: [src/screens/README.md](src/screens/README.md).

Build option `STORM_ENABLE_UI` (off in the main engine build; the directory is also a
stand-alone CMake project for host checks and captures). Dependencies are SHA256-pinned release
archives fetched at configure time: RmlUi 6.3 (MIT), FreeType 2.14.3 (FTL), nlohmann/json 3.12.0 (MIT), and
bimg's bundled stb_image (the pack's PNG and JPEG pictures, decoded when the pack loads). The stand-alone host build
also fetches lunasvg 3.5.0 with plutovg (MIT) for its tools: `storm_ui_icon_bake` rasterises the pack's icon atlas
on the developer's machine (the application's `mobile-ui/design/bake_icons.py`), `storm_ui_shot` writes PNGs; neither
is part of the runtime. Icons are drawn from that atlas, tinted (`src/icons.cpp`). Local fixes on top of the RmlUi pin live in `patches/rmlui-*.patch`.
The build pulls in `src/libs/renderer_bgfx` for bgfx (offline:
`-DFETCHCONTENT_SOURCE_DIR_STORM_{BGFX,BX,BIMG,BGFX_CMAKE}=<checkouts>`, bgfx patched).
No game data is involved in building or testing.

## Build and check on the host

```sh
cmake -S src/libs/ui -B build/ui-host -G Ninja
cmake --build build/ui-host
ctest --test-dir build/ui-host --output-on-failure
build/ui-host/storm_ui_shot --pack <pack dir> --doc gallery/button.rml --out shot.png
```

`ui_hud_land_game` compares the land HUD's place and talk icons with the scripts of an installed game: it runs
when `STORM_GAME_DIR` names one and is reported as not run otherwise.

Pack contract check: draws a real pack (the application's `mobile-ui/design/build.py --pack DIR`) with the
real screen and HUD adapters on every recorded model and state of `tests/` (cases and exemptions:
`tests/pack-contract.json`), without a GL context:
`build/ui-host/storm_ui_pack_check --pack DIR --fixtures src/libs/ui/tests`. A case fails on any error line
and any element marked `ui-error`; an application screen or a script screen with no case and no listed reason fails too.

Script screens are checked like the C++ ones: `tools/script_game.h` runs the application's interface scripts (its
mobile-ui layer as the APK ships it, `STORM_MOBILE_UI`) over the fixture game of the engine's `tests/ui_scripts/game`
on the engine's script VM (`tests/script_vm`, built into this project), and the pack check and `ui_modals` give the
screens it attaches to their hosts and routers. A screen that needs more of the game than the fixture game offers
gets it there (functions with fixed results, interface rows). It is the
ctest `ui_pack_contract` when configured with `-DSTORM_UI_PACK=<built pack>`, and the application runs it
in `tests/ui_pack_contract`. It also checks the pack's prewarm list (pack.json `prewarm`: the font uses
every state draws, which `Host::Prewarm` makes ahead of a screen's first opening, `src/prewarm.h`) against what the
states drew; `--prewarm FILE` writes the list for the application's `mobile-ui/design/prewarm.json`.

Every `models/*.json` interface model is a case; `pack-contract.json` adds the cases that need state channels or
the game's answer to what a screen asks for on opening (`answer`), and lists the screens without a case,
each with its reason (a listed screen that a case draws fails too: the entry is stale). A case feeds `states`
(channel=file, in order), then its `interface`, then `answer`; `covers` names the screens it draws that its interface
does not (a HUD scene of the scripts by its capability, `hud_world_map`); `taps` are element ids (or selectors) tapped after all that (a HUD state a tap opens, a sheet). A case is laid out on a 914 × 411 dp
phone, or at its `size` ("780x360", dp; such a case adds nothing to the prewarm list), and `language` (a game
language name) replaces the one its states name; `whole` lists selectors of what
the recorded state must show whole: every element found, scrolled into view, lies on the screen and inside every box
around it, and no text in it is cut (an ellipsis of a `<ui-clamp>`, a box that hides its overflow); `seen` asks
the same of the state as it opens, without scrolling; `shows` ({selector: text}) that what the selector finds, as
the state opens, lies whole in its boxes and draws that text (a text cut after it is allowed); `holds` ({selector:
text}) that a long press on what the selector finds opens the tooltip with that text, whole on the screen, sends
nothing, neither held nor released, and that a tap on it then sends; `align` ([edge,
selector, selector…], edge top, left or right) that what they find shares that edge. The crawl (`--crawl 1`, the
default) then makes every tap of the screen with a real touch, from the recorded state and from every state those
taps open without the game (tabs, sheets, modals, pickers), and fails the case on
- any host error in any of those states;
- a widget action that names a node the model does not have, or a verb the engine does not offer for it;
- an engine command `tools/android/app_contract.json` does not list (`--app-contract`);
- a tap that does nothing at all: no action, no change of what is drawn;
- a disabled control whose tap still sends or opens something;
- a tap whose frames show a transient state (the flicker check, `src/action_trace.h`); exemptions are
  `pack-contract.json` `flicker` [{case, tap, reason}]. `--flicker` writes every tap's trace for a ranked list.

`flows` replay taps the game answers in steps, with its real answers one game frame at a time: no transient frame is
allowed in them, a `predicted` tap must draw its result in its own frame, and what a flow lists as `steady` (selectors)
must keep its place and size from the tap until it settled. `--actions` writes every action the
taps sent, for the application's check that they end in the game's scripts and its Java
(`tests/ui_pack_contract/game_bindings.py`). A prewarm entry nothing draws fails a full run (no `--only`/`--shard`).

### storm_ui_shot

Renders off-screen through the bgfx backend (GLES 3.0 on headless Mesa llvmpipe, simulated clock).
Options: `--views N` (UI view range), `--game-root` (decodes game pictures itself), `--safe L,T,R,B`
(safe-area insets in dp, `safe.left` … `safe.bottom` in templates), `--interface <model.json>`, `--state <channel>=<file.json>`, `--size`, `--density`.
Component actions are printed as `action <id> <verb> <value>`.

`--script` replays touches and checks frames, e.g.
`--script "down 1 660 88; wait 0.5; shot held.png; up 1 660 88"`: `shot` draws a frame and saves it;
`seen <png>` saves what the last frame left on the screen; `made <max>` fails when the last frame created more textures; `mark` remembers that picture, `whole` fails
unless the last frame showed the marked picture or the new one complete, `arrived` fails while the marked
picture is still there; `bench 300 [<channel> <a.json> <b.json>]` measures frames as a platform host makes
them (with a channel the two states alternate every frame).

## Android

`tools/android/ci_engine.sh` builds `game/libstorm_ui.so` (target `storm_ui_runtime`) next to
`libengine.so` and `libbgfx.so`, and the licence texts of its dependencies in `<build>/licenses/`. The APK
ships the three from that one build. `libbgfx.so` is the only bgfx of a process; `libengine.so` and
`libstorm_ui.so` both link it. Application screens without the engine (launcher, downloads, gallery) load
`libstorm_ui.so` alone through `org.stormengine.port.StormUi` (`android/jni_host.cpp`) on their
GLSurfaceView's context. UI code is built with `-gline-tables-only` in builds with debug info.
Debuggable Android builds log frame costs every 5 s (logcat tag `StormUiFrames`).

## Rules for pack and screen authors

- Errors are loud: a template that does not compile (an unknown `<c-…>` tag or function, an attribute a
  component does not have, a raw `{{{ }}}` insertion) names the template and line and stops the host's start;
  an enum value outside its options, an expression that fails and an icon missing from the pack are logged with the
  element and, in debug builds, drawn as the striped `ui-error` marker or a crossed red icon (the pack styles
  `.ui-debug .ui-error`; `ui-debug` is the class of every pack document's root with `Config::show_errors`).
  Release builds only log them and draw nothing in their place. With
  `Config::strict_templates` (host checks, captures) a template path that names nothing is an error too;
  mark optional data in the template with `opt(r.reason)` or `or(r.id, r_i)` (`src/template.h`).
- A screen that throws closes its interface with an error line; a HUD scene that throws is off until the
  game mode changes. Error lines are toasts only with `Config::show_errors` (debug builds); release builds
  only log them.
- `icon-shadow: <colour> <x> <y> <sigma>` draws a drop shadow under an icon from the atlas's blurred coverage
  (the baked blur nearest to sigma at the icon's size); use it instead of `filter: drop-shadow` on icons.
- The host draws only when `Host::Prepare` reports the frame out of date; a frame is shown only when its
  textures are ready. `tests/frame_cost_check.cpp` holds the frame-cost rules.
- Nothing delays a present: the host never keeps a frame on the screen for the game's answer, and no timer
  stands between a tap and what it shows. A tap's result that needs no decision of the game is drawn in the frame
  of the tap (`screens::Bindings::Predict`, src/screens/README.md); the game's answer confirms it, or corrects it
  with an error line "UI prediction corrected: …". The engine answers a widget action in the frame it arrived in
  (xinterface runs the action's script events before that frame's model goes out). Flicker is prevented at its
  cause: a screen does not draw a state it knows to be a step of the game's answer (a model rule, never a time).
- Flicker check (`src/action_trace.h`, `Config::trace_actions`, on in debug builds and device crawls): every
  action's presented frames are recorded until they settle; a frame that differs from both the start and the
  settled state is logged "UI flicker: …". Every line says when the screen first changed ("first change in frame N,
  +X ms") and, for a predicted action, "predicted" or "LATE" (its result was not in the tap's own frame, a warning). The pack contract
  fails on a late predicted tap anywhere, on a transient frame in a tap that needs no game and in `flows` (`tests/pack-contract.json`: taps with the game's real
  answers frame by frame); `tools/ui_crawl` reports each tap's and ranks a crawl's (`ui_crawl.py flicker DIR`).
  An action open when the game stops being live (the app leaves the screen) ends there, marked "cut off": what it
  led to was not drawn, and the frames after the return answer nothing.

## Licence boundary

Game pictures are never bundled: `<ui-game-image>` reads
`<game root>/RESOURCE/Textures/<texture>.tga.tx` at run time. Tests use synthetic
texture data only.
