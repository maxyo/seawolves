# Screens: game interfaces in the portable UI

The engine describes every open game interface as an `InterfaceModel`
(`include/storm_ui/interface_model.h`): the interface id and its widgets in reading order.
`Host::SetInterface` routes the model to a *screen*: C++ code here that turns the widgets
into the props of a pack screen (`<s-…>`, the application's
`mobile-ui/design/screens/<name>/`) and component events back into engine actions.
Interfaces without a ported screen (`kPorted` in `registry.cpp`) get the generic layout (`generic.cpp`).

A *script screen* (`script_screen.h`) is the same thing written in the game's script language by the touch
interface's scripts (`Host::SetScriptScreens`, preferred to a ported screen of the same interface): the router admits
its nodes as for any screen, its script builds the view through the engine's VM side (`storm_ui/script_screens.h`,
engine `src/libs/ui_host/README.md`) and binds the events it answers; `script_props.h` converts the view to the pack
screen's props by their types and, for a `json` prop, by its `shape` in the spec.

```text
engine ──InterfaceModel──▶ Router ──admission──▶ ported Screen ─┐
                                   └─(unknown id)─▶ generic layout ─┤─▶ props ─▶ <s-…> in the pack
engine ◀──────Action {screen, opening, node, index, verb, value}──┘◀─ events (id, verb, value)
```

## Adding a screen

1. **Pack.** `mobile-ui/design/screens/<name>/spec.json` (tag `s-<name>`, one prop per
   part of the view, plus `"extras": {"type": "json"}`), `template.rml` built only from
   shared `<c-…>` components and the screen parts (`s-extras`, `s-note`, `s-control`,
   `s-block`, `s-actions`), `style.rcss` with layout only (flex, sizes in dp or %, colours
   as tokens). The root is `<c-screen id="header" key="header" … actions="{{ json(extras.actions) }}">`
   and ends with `<s-extras id="extras" extras="{{ json(extras) }}"/>`; put
   `<s-note id="extras-note" text="{{extras.note}}"/>` under the main block. Nothing may
   assume a fixed size: the viewport is the real dp size of the display.
2. **C++.** A class derived from `Screen` (`screen.h`) in `src/screens/<zone>_<name>.cpp`:
   - `Component()` → `"s-<name>"`;
   - `Nodes()` → every node the design draws with the type it expects (`"a|b"` alternatives,
     `NAME*` prefix); `required = true` marks REQUIRED. REQUIRED names only what the game always
     sends: a table with no rows comes as one `table` widget, so a table is required as
     `"choice|table"` and its rows are read with `admitted.Rows(node)`;
   - `Build(admitted, bindings)` → the props (JSON). Read widgets with `admitted.One/All/Rows/Label`;
     bind every interactive element by its id: `bindings.Forward(id, widget, "press")`,
     `bindings.On(id, verb, handler)` and inside a handler `bindings.Send`, `SendLatest` (drags),
     `Back(exit)`, `EditText` / `AskText`, `Read`, `Rebuild()`, `SendHost(node, value)` (a setting of
     the application). Declare every modal the view shows with `bindings.Modal(id, cancel)`, topmost
     last; `cancel` must do what the modal's ✕ / Cancel does, `nullptr` makes it mandatory (scrim
     tap and back key are refused and logged). Never bind `dismiss` by hand. Something else the back
     key closes first returns true from `OwnsBack()` and closes it in `Back()`. Mark expected events
     that need nothing with `bindings.Ignore(id, verb)`.
   Add the file to `CMakeLists.txt`.
3. **Registry.** Add `{"<interface id>", Create…}` to `kPorted` in `registry.cpp`.
4. **Tests.** Add a synthetic model to `tests/models/`, extend `tests/screens_check.cpp`
   (admission, the props that matter, every action mapping) and add every modal of the screen
   to the walk in `tests/modals_check.cpp`.

Element ids: an event carries the id of the nearest ancestor that has one. Components
inside lists report under the list's id with their own verb (`goods row 3`,
`goods change 150`); header parts report under `header` (`section ship`, `subject-next`),
its buttons as `header-back`, `header-close`; the subject sheet's rows report as `header-subject-<id>` (the row's id, its place without one).
Debug and contract hosts (`Config::show_errors`, `strict_templates`) check every tap the drawn
screen offers (`Behavior::Taps`: each press part's `data-action`, `click` for the component) against
the bindings when it is drawn: one that maps to nothing is an error then, not first when tapped
(`tests/taps_check.cpp`; the pack contract reports it for every recorded model). The pack contract also crawls:
it makes every tap with a real touch in every state taps reach without the game and fails on a tap that does
nothing at all, a widget action to a node or verb the model does not offer, and a disabled control that still sends
or opens something (`tools/pack_check.cpp`); the application follows the actions into the game's scripts
(its `tests/ui_pack_contract`). A row that only shows something is drawn `inert` (c-list-row, its action and menu
parts still take taps), `rowTap="0"` (c-trade-list) or with `pointer-events: none`, never bound to `Ignore`.
A disabled control's tap reports `refused`; only a handler bound to that verb gets it (one bound to every verb of
the id is its action and does not run), and unbound it needs nothing: the control shook.

A widget action reaches the engine as the `Action` struct itself (`storm_ui/action.h`): `xinterface` refuses one
made on another opening of the interface once, with the reason, and its nodes read the fields. Every one carries a
number of the request ledger (`src/requests.h`): the engine answers each, applied or refused, and the model after an
applied answer shows it. Logs and checks print one line (`to_string`, `widget:<screen>\t<node>\t<index>\t…`):

| verb | value | printed |
|---|---|---|
| `press`, `left`, `right`, any other | empty | the verb |
| `select`, `activate`, `open`, … | row identity | `verb@<opening>:<row id>` |
| `toggle` | `1` / `0` | the value |
| `edit` | the text | the value |
| `set` | slider position | `set@<opening>:<position>` |
| any, with `guard` | — | `guard@<opening>:<guard row>\|…` |
| `back` without a node | — | `back` (an engine command) |

## What the router does for every screen

- **Admission.** All REQUIRED nodes present → the ported screen. A model missing one is held
  hidden for the first second of the opening; after that it is an error (missing nodes logged,
  a toast in debug builds) and the interface closes through the engine's back. Mark nodes
  REQUIRED only when every way the game opens the interface has them.
- **Extras.** Widgets a ported screen does not know become rows of the Other sheet (header «⋯»);
  texts become one note.
- **Busy until answered.** An event that sent something marks its element
  (`bindings.Pending(id)`) until the engine answers; no answer in 5 s is an error.
- **Incremental update.** Every revision patches the mounted screen in place; scroll offsets and
  pressed parts survive.
- **Nothing is dropped quietly.** Malformed models, a missing pack screen, an event without a
  binding and a state channel without a consumer are error lines and, with `Config::show_errors`,
  toasts.

## Answering a tap at once

Nothing in the UI waits before it draws: no frame is held for the game, and no timer stands between a tap and its
result. Three rules keep that free of flicker:

- **Predict what needs no decision of the game.** `bindings.Predict(id, verb, [](const Event &, Json &view) {…})`
  next to the `On` of the same event: the function changes the view to what the tap leads to (the tapped row
  selected, the switch moved; `Bindings::Patch`). The router keeps what that changes as fields addressed by name and
  by a list row's `id` (a row's identity; only lists whose items carry no id, the screen's own sections, are stepped
  by place) and lays them over every view built until the ledger says the requests the handler sent are shown (and
  the screen no longer `Await`s) or refused. Shown, they are compared with the game's view: a difference is the
  error line "UI prediction corrected: …"; refused, they end at once; a row absent from a build is not marked, and
  one log line says so when the request ends. Capture values only (the function outlives the model it was declared
  for), and predict only what the game cannot quietly decide otherwise. An event without `Predict` is not
  predicted. A handler that alone knows the result says `bindings.PredictThis(view)`: a script screen's `UiPredict`
  gives the view the tap leads to. Predicted today: the toggles and segments of `ControlRows` (the generic
  layout, Other).
- **Do not draw a step.** A model the screen knows to be a step of the game's answer (the store's row selected
  before its amount window, a confirmed deal closed before it reopens) is not drawn: the build returns the view
  it had and says `bindings.Await()` (a script screen: `UiAwait()`). A rule about models, never about time; a
  timeout there only reports a game that never completed the step.
- **The busy mark is feedback.** `bindings.Pending(id)` marks the pressed control busy in the tap's frame.

The flicker check (`../action_trace.h`) judges every tap; its lines carry the tap's latency, and a predicted tap
whose result is not in the tap's own frame is reported ("LATE"), in the pack contract a failure. The tap's own
frame of a predicted tap is its feedback and is not judged as a transient state; the frames after it are.

## UI strings

Screens never contain player text of their own. Texts the game sends pass through; everything
else is a key of the pack's string table (`mobile-ui/design/strings/<lang>.json`):

- C++: `bindings.Text("trade.price")`; a key kept in a table goes through
  `StringKey("trade.store")` so the pack build still sees it;
- templates: `{{ tr('common.next') }}`.

Add a key to every `strings/<lang>.json` (the design build fails otherwise) with its zone prefix
(`trade.`, `crew.`, `common.`). The word lists in `controls.cpp` (`kExitLabels`, `kConfirmLabels`,
`kDangerWords`) recognise button roles in all six game languages; add a language's words there with
its string table, and a case to `Roles` in `tests/screens_check.cpp`.

## Game pictures in rows

`PictureOf(image)` (controls.h) turns a `pictures.ini` reference into a row image
(`atlas:GROUP/Picture`); `TextureOf(image)` gives `{texture, uv}` for
`<ui-game-image texture uv [fit="contain|cover"]>`, `MapViewOf` the props of `c-map-view`.
Pictures are resolved from the game root at run time; nothing of the game is bundled.

## State channels

`Host::SetState(channel, json)` carries game state outside interfaces. Implement
`StateConsumer` (`Channel()`, `OnState(json, router)`) and register it in
`CreateStateConsumers()`. The HUD's channels are typed instead: the engine hands over the structs of
`storm_ui/hud_state.h` (`Host::SetState(const HudState &)`); their JSON is the fixture format of tools and
checks, read in `../hud/states.cpp`. So is the dialogue: `storm_ui/dialogue_state.h` (`Host::SetDialogue`), its
fixture JSON read in `dialogue.cpp`. Channel names are listed in the engine's `tools/android/app_contract.json`.

## The HUD

`src/hud` draws the in-game HUD from state channels:

```text
engine ──HudState (hud_state.h)──▶ Hud::Set ──States──▶ Scene::Build ──View──▶ mount (host_hud.cpp)
engine ◀──"<verb>:<value>" (Action, screen "hud")── Hud::Send ◀── Input handlers ◀── component events
```

A scene builds a `View`: the layout scene of the pack's `hud-layout.json`, the props of its
elements by id (an element left out is not mounted), an optional sheet and elements to dim.
`Input::On(id, verb, handler)` binds component events. A layout element may add
`"behavior": "hold"` (reports `down` / `up`) or `"swipe"` (`data` → `data-swipe-*`);
`"toastsBelow": true` keeps toasts below a top status.

A scene is a script of the touch interface (`app/mobile-ui/PROGRAM/interface/touch/hud/`, its README "HUD
scenes"); `kScenes` in `hud.cpp` names the gameplay modes each one draws and the capability of its script, and
`script_scene.cpp` runs it. The sea scene is still C++ (`sea.cpp`) until it moves. What stays native, because it
runs every frame or every touch move without a script, or is the engine's protocol: the readings laid into a view
(`script_states.cpp`: one table of readings and their formats), the continuous controls (`HoldSet`: helm, held
buttons, the sail lever; the walk stick; the sword wheel; the picker `Fan` with its fold time), an element that
lingers, the geometry the engine is told (the world map's label area), a tapped choice drawn until the game answers
its command (the request ledger), the land menu's select-then-activate and the wait for the scene change a travel
place starts, and the revisions and generations every command carries.

A new channel for scenes: its struct in `hud_state.h` (and `HudState`), its field in `States`, its fixture reader in
`states.cpp`, and its writer for scripts (without its readings) in `script_states.cpp`.

Tests: synthetic states in `tests/states`, a scene check like `tests/hud_worldmap_check.cpp` or
`tests/hud_land_check.cpp` (the script scene on the fixture game's program with the application's pack,
`tests/hud_scripts.h`; `AttachScripts` gives a whole runtime the scripts).

```sh
build/ui-host/storm_ui_shot --pack <app>/build/ui-pack --size 915x412 --density 2 --out land.png \
    --state gameplay=tests/states/gameplay-explore.json --state combat=tests/states/combat-explore.json \
    --state vitals=tests/states/vitals.json --script "wants 96 329; down 1 96 329; move 1 96 280; wait 0.3"
```

## Trying a screen on the build host

```sh
python3 <app>/mobile-ui/design/build.py                       # the pack
cmake --build build/ui-host && ctest --test-dir build/ui-host --output-on-failure
build/ui-host/storm_ui_shot --pack <app>/build/ui-pack \
    --interface src/libs/ui/tests/models/store-deal.json --size 915x412 --density 2 \
    --out store.png --script "down 1 600 216; up 1 600 216; wait 0.3; shot row.png"
```

`--state dialogue=file.json` feeds a state channel instead; script commands
`interface <json>` (the next revision), `text <answer>` (accept the editor request).
Actions are printed as `command widget:store<TAB>TABLE_LIST<TAB>1<TAB>activate@7:…` (`to_string`).
Render at 780×360, 824×412, 961×412, 1024×768 and 1280×800 (`--density 1`) before
calling a layout done. Models in `tests/models` are synthetic; never commit a model
recorded from a game with its texts unless it holds nothing but interface strings.
