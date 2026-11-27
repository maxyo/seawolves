# ui_host — the game's portable UI inside the engine (Android)

One `storm::ui::Host` lives in the engine process, on the game thread, and draws through the bgfx backend
(`storm_ui_bgfx`) in the frame owner's UI views, after the game picture, at native resolution. It receives state
channels, interface models (`xinterface`), toasts and presentation from the engine, touches and settings from
`GameActivity` through the `EngineUi` natives (applied by `ui::tick()` at the top of the next loop iteration), and
sends widget actions back to `xinterface` and host actions to the activity over JNI. A host that cannot start is an
error shown to the player (`uiFailed`).

`src/engine_ui.hpp` holds the host (`EngineUi`): `engine_ui.cpp` its game-thread side and the engine's calls,
`engine_ui_start.cpp` a new host, `engine_ui_platform.cpp` the activity's natives, `engine_ui_trace.cpp` its frame
clients (the input trace and the prewarm between frames), `dialogue_relay.hpp` the dialogue's states on their way
to the host (checked on the host by `tests/ui_host_dialogue`).

## The touch interface's scripts

`src/scripts/` is the VM side of the interface's scripts (application `app/mobile-ui/PROGRAM/interface/touch`), plain
engine code that the host checks build on Linux (`tests/ui_scripts`):

- `script_segments.*`: the pack's list `scripts.json` (segments in order: file, `core`, requirements, contracts
  provided or used, an adapter's bindings, the screens each draws) and its attacher (core `script_program.hpp`), which the activity sets before the game's program starts.
  After every `Main` or `OnLoad` it reads what the program offers (`GameSurface`: functions with their arguments,
  globals, event handlers, the `Interfaces[]` ini names, files), logs it with its cost, and attaches each segment on
  its own: one whose requirements (`fn:Name/n`, `var:`, `ev:`, `interface:`, `file:`, `node:interface#NODE`: a
  section of the interface's ini) the game does not meet is not compiled; one that fails is left out with its screens,
  an error line naming both; a `core` one that fails stops the start. Of the segments providing a contract the first
  attached is chosen and the later ones are skipped; one using a contract needs it provided; a screen without an
  interface takes it and its nodes from the binding of its capability by the attached adapter. Attached segments' globals never enter a save (core `COMPILER::SerializeState`).
- `script_port.*`: script screens (UI library `storm_ui/script_screens.h`). The admitted widgets go to the script's
  `UiModel`, the event `UiBuild(capability)` builds the view and `UiTap(capability, id, verb, value)` answers an event
  the script bound; the natives (declared in the scripts' `core/core.c`: the view, its bindings, widget actions on
  nodes and rows, the build's timing and the game's answers, the reading of the game's figures, the engine's amount
  drafts) act on the screen while its script runs (`storm_ui::ScriptCall`) and fail outside it. These events are raised only between scripts: one asked
  for while a script runs is refused with an error, and a script screen's model published from inside a script waits
  for the next `Tick`. Their cost per call is in the `script work:` line ("interface scripts").

Every screen switch logs one line (`StormUi` INFO): `UI switch <screen>: first frame N ms after the action …`.

## Debug logging

Set before the game starts (`adb shell setprop log.tag.<tag> DEBUG`):

- `StormUiInput`: per loop iteration every touch sample, action, model and frame with its CPU by stage;
- `StormUiState`: every state channel update (a screen channel's JSON, a HUD channel by name);
- `StormUiModels`: writes the newest model of each screen to `<external files>/ui-models/`;
- `StormUiModelCheck`: collects the interface model every frame and logs a model that went stale.
