# ui_host — the game's portable UI inside the engine (touch interface builds: Android, the desktop's development build)

One `storm::ui::Host` lives in the engine process, on the game thread, and draws through the bgfx backend
(`storm_ui_bgfx`) in the frame owner's UI views, after the game picture, at native resolution. It receives state
channels, interface models (`xinterface`), toasts and presentation from the engine, touches and settings from
`GameActivity` through the `EngineUi` natives (applied by `ui::tick()` at the top of the next loop iteration), and
sends widget actions back to `xinterface` and host actions to the activity over JNI. A host that cannot start is an
error shown to the player (`uiFailed`). What it asks of the platform (`ui_platform` in `engine_ui.hpp`: the pack, the
text editor, the activity's actions, the log) is `src/platform/android.cpp`, with the activity's natives, or
`src/platform/desktop.cpp` (mouse as one finger; `../../README.md`, "Building").

`src/engine_ui.hpp` holds the host (`EngineUi`): `engine_ui.cpp` its game-thread side and the engine's calls,
`engine_ui_start.cpp` a new host, `engine_ui_platform.cpp` the platform's state channels, `engine_ui_trace.cpp` its frame
clients (the input trace and the prewarm behind a loading screen), `dialogue_relay.hpp` the dialogue's states on their way
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
  attached is chosen and the later ones are skipped; one using a contract needs it provided. A binding (an adapter's
  interface, nodes — lists or named `nodeSets` — and `data`) whose interface or required nodes the game lacks is off
  alone, its provider and other bindings kept; a screen without an interface takes the interface, nodes and data
  of its capability's attached binding. Attached segments' globals never enter a save (core `COMPILER::SerializeState`).
- `script_port.*`: script screens (UI library `storm_ui/script_screens.h`). Each capability has its object
  `UiScreens.<capability>` (`capability`, `screen`: the screen file's stem, `look`: the screen entry's presentation,
  `data`: the binding's, `model`: the admitted widgets of its last build, a node written again only when its widgets
  changed, so a build without a new model writes nothing); the event `UiBuild(ui)` builds the view and
  `UiTap(ui, id, verb, value)` answers an event the script bound, each with that object; the natives (declared in the scripts' `core/core.c`: the view, its bindings, widget actions on
  nodes and rows, waiting for the game's answer (`UiHold`, `UiAnswer`, `UiAwait`), the reading of the game's figures,
  the deal of a quantity control (`UiDeal`), an order of a list (`UiOrder`), a whole number of the game's globals
  (`UiGameInt`), the layout of a tree (`UiTreeLayout`)) act on the screen while its script runs
  (`storm_ui::ScriptCall`) and fail outside it, as does one given an argument of the wrong type or an object with an
  unknown or missing field. These events are raised only between scripts: one asked
  for while a script runs is refused with an error, and a script screen's model published from inside a script waits
  for the next `Tick`. Their cost per call is in the `script work:` line ("interface scripts").

A tap's answer is drawn in the tap's frame. The touches are applied at the top of the loop, so the widget actions a
tap sends are applied by the interface's `Execute` (`XINTERFACE::MobileApplyActions`: each action's script events run
before the next action); its `Realize` collects the model and hands it to the screen, whose build may send the next
step of the game's answer (a row's window after its selection, a confirmed deal opened again). Those are applied and
the model collected again before the frame is drawn (`MobileSettle`, `storm::mobile::SettleActions`, at most
`kSettleRounds` rounds, an error naming the screen past them; never from inside a script). Steps the game makes in its
own per-frame handlers (`ProcessFrame` on the `frame` event: TEHO's store binds a switched ship there) still take its
next frame. An event of a script screen that holds (`UiHold`) is built once, with the model of its answer
(`Bindings::BuildLater`), not before and again after it.

Every screen switch logs one line (`StormUi` INFO): `UI switch <screen>: first frame N ms after the action …`.

## Debug logging

Set before the game starts (`adb shell setprop log.tag.<tag> DEBUG`):

- `StormUiInput`: per loop iteration every touch sample, action, model and frame with its CPU by stage;
- `StormUiState`: every state channel update (a screen channel's JSON, a HUD channel by name);
- `StormUiModels`: writes the newest model of each screen to `<external files>/ui-models/`;
- `StormUiModelCheck`: collects the interface model every frame and logs a model that went stale.
