# The touch interface's scripts

The logic of the touch interface's screens, in the game's script language: one shared screen per notion (rest, trade,
…) and, per family of games, an adapter that reads the game's interface and performs its actions. The engine keeps the
runtime (admission, drawing, waiting for the game, the amount control); the pack (`../../../design`) keeps the
components and texts. The scripts sit at `interface/touch/` below the program directory, attached by the engine after
the game's own (`engine/src/libs/ui_host`, `script_segments` and `script_port`).

**The game decides.** Prices, sums, limits, weights, what an action leads to and whether it is possible are the
game's scripts' to compute; no screen, adapter or engine code computes them again, not even "the same way". A screen
sends the action to the game (the adapter performs it on the game's own window) and shows the game's answer: the
amount its window clamped the offer to, the money and weight it writes, the figures it shows after it. Reading what
the game displays and interpreting it (a cell's text, a row's colour as its trade type, a dash for "not traded so")
is reading; working out what the game would compute is a copy, and is not written. Where the answer is too slow for
the interface, the engine's path from the action to the game's script and back is made faster (the answer is drawn in
the tap's frame: `engine/src/libs/ui_host/README.md`), never the calculation moved here. Nothing checks this rule
mechanically: a reviewer does.

## Layers

| Directory | What | Calls |
|---|---|---|
| `core/` | the natives, the capability ids (`capabilities.h`, generated from `design/capabilities.json`) | — |
| `lib/` | game-free helpers: text and figures, list entries, list heads, facts, buttons | core |
| `contracts/<family>.c` | the adapter contract: `extern` declarations with the shape of every argument | — |
| `lineages/<lineage>/<family>.c` | a family's adapter for a lineage of games (`legacy`: the games of this engine) | game, lib, core |
| `lineages/<lineage>/lib.c` | what that lineage's adapters share (table cells, the amount window, tabs) | game, lib, core |
| `screens/<family>/<screen>.c`, `screens/<screen>.c` | the shared screens | contracts, lib, core |

`build.py --check` enforces the layers: a shared layer calls natives, `TouchLib_*`, the contracts it uses and a short
list of built-ins; it reads no model (`.model`), names no game and writes into no object that is not its own. Not made
yet: `hud/` and `games/<game>/` (a game's own screens), the `cl` lineage.

## Names

| Thing | Prefix | Example |
|---|---|---|
| screen | `Touch<Stem>_`, its state object `Touch<Stem>` | `TouchTrade_Build`, `TouchTrade` |
| contract | `TouchAdp<Family>_` | `TouchAdpTrade_Deal` |
| adapter, lineage lib | `TouchLeg<Family>_`, `TouchLegLib_` | `TouchLegTrade_Reach` |
| lib | `TouchLib_` | `TouchLib_Add` |
| natives, engine events | `Ui` | `UiShow`, `UiBuild` |
| capability ids | `TOUCH_CAP_<ID>` | `TOUCH_CAP_TRADE_STORE` |

A file's stem is its screen's or family's one name: the file `screens/trade/trade.c`, its functions `TouchTrade_*`,
the contract `contracts/trade.c`, the adapter `lineages/legacy/trade.c`, the pack screen `s-trade`. Names compare
without case in the VM and share one namespace with the game's thousands, hence the prefixes.

## The list: `design/scripts.json`

Segments in layer order. A segment has `file`, `core` (core and lib only: a failure stops the start), `requires`
(`fn:Name/n`, `var:`, `ev:`, `interface:`, `file:`, `node:interface#NODE`), `provides`/`uses` (`adapter:<family>`:
of the providers the first that attaches is chosen), and:
- an adapter's `binds`: `{capability, interface, nodes, data}`; `nodes` are `[node, type, required]` or `"@set"` from
  the top-level `nodeSets`; `data` is what the adapter reads per capability (the trade windows' columns). A binding
  whose interface or required nodes the game lacks is off alone, the provider and its other bindings stay.
- a screen's `screens`: `{capability, component, look}`; `look` is the screen's presentation per capability (title
  key, icons, captions). A screen takes the interface, nodes and data of its capability's attached binding.

A required node is looked up in the interface's ini file: a node the engine adds to the model (`ANDROID_*`) is listed
without `true`, and the adapter that needs it says so with `UiError` when it is absent.

The node types of `binds` are the model's widget types (`engine/src/libs/ui/include/storm_ui/interface_model.h`):
`choice` (a list's rows, a scroll of ships or characters), `table` (a list with no rows), `edit`, `slider`, `check`,
`button`, `text`, `heading`, `image`; `a|b` takes either. A node name ending in `*` takes every node it starts
(`TABBTN_*`, `QTY_*`), and `"@set"` a named set of `nodeSets`.

The pack screen a capability fills (`component`) is `../../../design/screens/<name>/` (its `spec.json` names the
props a view may give and their shapes: a view with anything else fails); the texts a screen shows that the game does
not send are string keys (`UiText`) of `../../../design/strings/<lang>.json`, every key in all six languages.

## The runtime

Each capability has its object `ui` = `UiScreens.<capability>`: `capability`, `screen` (the screen file's stem), `look`,
`data`, `model` (the admitted widgets of its last build: `<NODE>.{type, label, value, min, max, reason, offers.<verb>,
context, headers, images (a node without rows), rows.r<index>.{index, id, label, value, reason, offers, images,
cells.c<n>.{text, color, images}}}`; an image is `i<n>.{kind, group, image, uv}`, copied whole into a view's
`picture` or `texture`). The
events are `UiBuild(ui)` and `UiTap(ui, id, verb, value)`, raised to every screen: a screen's handler starts with
`aref ui = GetEventData(); if (ui.screen != "<stem>") return;`. Only adapters read `ui.model`; the screen hands `ui` to
every contract call.

The natives (`core/core.c`):
- the view: `UiShow`, `UiPredict` (in a tap: the view the tap leads to), `UiOn`, `UiModal`, `UiBackTo` (in a build:
  the back key raises that tap), `UiPending`, `UiText`;
- the game: `UiSend(node, verb, value)`, `UiSendTo({node, verb, value, row, guard, lead})`, `UiBack`, `UiOffers`;
- waiting for the game: after a move, `UiHold(seconds)`; the event is then built once, with the model of the game's
  answer (in the tap's frame). A build from an answer the game completes in a later frame says `UiAwait()` and is not
  drawn (what the kept view shows of a closed window takes no input). `UiAnswer()` in a build tells what became of the
  hold: `""`, `"waiting"`, `"applied"`, `"refused"` (drawn at once), `"late"` (past `seconds`: an error line unless the
  game had answered and nothing more is awaited). A hold still waiting keeps its first action when another move holds.
- the amount control: `UiDeal({key, element, origin, other, low, high, answer, cutReason, more, less, disabled,
  reason, commit, offer}, &out)` in a build. The game decides the amount: each move of the control raises
  `UiTap(ui, offer, "", change asked)`, whose handler offers it to the game's window (and holds); the control then
  shows `answer` (our side as the game's window holds it now) and says `cutReason` when the game cut what was asked.
  `out.value`, `out.change` and `out.button` (absent while unchanged); the button's press raises
  `UiTap(ui, commit, "", change)`. The deal owns the screen's `primary`: a screen whose primary is something else, or
  with several amounts (the self repair's parts), binds its controls' `change`/`commit` itself and asks the game;
- `UiOrder(list, field, descending)`: a stable order by a whole number; `UiRebuild`, `UiFirst`;
- `UiSubject(screen, out)` in a build: the subject another part of the interface asked that screen to show (the
  HUD's tap on a quest record: `out.quest`; a character, a ship: `out.character`, `out.position`; `out.tab`), taken
  once; 0 without one;
- texts: `UiAmount`, `UiNumber` (fail naming the text without a number), `UiHasAmount`, `UiDecimal`, `UiOneLine`, `UiFill`;
- the game's state that no model shows: `UiGameInt(path)`, a whole number of a global ("iHourWait") or of an
  attribute of a global object or reference ("refStore.Goods.Sugar.TradeType"); it reaches the globals of the game's
  open interface, which a segment attached before that interface cannot name. Absent or not a whole number, it fails.
  The checks give such globals per recorded state (a case's `game`);
- `UiToast`, `UiRead`, `UiError(text)`.

Failures are loud. `UiError`, a native given an argument of the wrong type or an object with a missing or unknown
field, and a runtime error of the script (the interpreter logs it and goes on) fail the build or tap: the router closes
the interface with an error line. There is no default for what the game should have given: an adapter that cannot
read what its contract promises calls `UiError`.

## The steps of a move

A move that the game answers in more than one step (TEHO's store: a confirmed deal closes the window, and the row is
opened again; a side switched is bound in the game's next frame) is the adapter's. `State` may advance its pending
step: it keeps the step in its own object (`TouchLegTrade`), sends the next action from the build and says
`UiAwait()` while the answer is not complete; the engine applies what a build sends in the same frame. The step lives
only while `UiAnswer()` is `"waiting"`, `"applied"` or `"late"`. The screen holds (`UiHold`) after every move that
waits for the game, and not after one that only changes its own state (a filter, a sort: `UiRebuild()`). When a
second adapter needs the same step record, it moves to `lineages/legacy/lib.c`.

## Rules no check enforces

- No game logic of our own (above).
- No work per frame: a screen builds when its model, its answers or its own state change, never on a timer of its own.
- `GetEventData` only at the start of an event handler.
- Every timeout, limit or retry has a measurement or a named race behind it, written beside it.
- A tap's script (its handler and the build it causes) stays under 2 ms on the phone; the `script work:` line has the
  cost per call of `UiBuild` and `UiTap`.

## The language, as found

- No escapes in strings: a line break is `TouchLib_Newline()`.
- `sort` is a built-in: a variable so named does not compile. Check names against `internal_functions.cpp`.
- A member of a call's result (`f().x`) is not an expression: take the result in a local first.
- `&&` and `||` are not relied on to short-circuit: test presence with `if` before reading an attribute.
- Reading an absent attribute is a runtime error (it fails the screen): test it with `CheckAttribute` where it may
  be absent.
- Object literals work in declarations and assigned to attributes; `CopyAttributes(at, &literal)` copies one into an
  `aref`. A `#define` is the program's from its first segment: only the core includes `capabilities.h`.
- A segment that includes its `extern` declarations cannot define them: a contract is its own segment, listed before
  its providers, and neither providers nor screens include it.
- Script globals are the program's: a screen's or adapter's state object serves one opening at a time (`UiFirst()`).

## Adding a screen

A shared screen with an adapter, the files in order (the rest screen is the smallest example, the trade screen the
full one):
1. `design/capabilities.json`: the capability id and a line on what it is.
2. The pack screen in `design/screens/<name>/` from the shared components, and its strings in every language.
3. `contracts/<family>.c`: `State(ui, out)` with the shape of `out` written once, and one verb per move of the notion
   (not of one game's window). Limits, reasons and types come from the adapter as data, not as a closed list.
4. `lineages/legacy/<family>.c`: reads `ui.model` and `ui.data` (helpers in `lineages/legacy/lib.c`), performs the
   moves on the game's own window and reads its answer, hides its game's frame order behind them (`UiAwait`). Nothing
   the game computes is computed here.
5. `screens/<family>/<name>.c`: builds the view from `State`, binds its elements, answers taps with contract moves and
   `UiHold` where the game answers in frames; lists and their heads with `lib/list.c`.
6. `design/scripts.json`: the contract, the adapter with its `binds` (shared nodes as `nodeSets`), the screen with its
   `screens` and `look`.
7. Checks, below.

## Tests

- `engine/tests/ui_scripts/cases/<capability>.json` (copy `cases/trade_store.json`, the reference screen's; a screen
  of several capabilities has one file, `cases/<screen>.json`, each case naming its `capability`): recorded
  models of the game's interface, the props the screen's view gives the pack screen for each, and the calls its build
  and a list of events make (`check.cpp` runs them on the production VM). The fixture game under it
  (`engine/tests/ui_scripts/game/PROGRAM/seadogs.c`) gives only what an adapter calls of the game (its functions and
  globals, by name), never a copy of a game rule.
- `engine/src/libs/ui/tests`: the interface models a game publishes (`models/<interface>.json`, and `flows/` for the
  models of a flow's frames), recorded or derived from recorded ones; they are game states, so a model must show what
  the game shows in that state (a fresh amount window holds nothing). Router behaviour of the screen with a game that
  answers (`screens_check.cpp`: `GameAnswers` sets what the game's window answers to an offer), the pack contract's
  cases and flows (`pack-contract.json`), and the recorded parity of the trade family (`trade-parity.json`).
- Commands (from the repository root; `XDG_STATE_HOME` to a private directory when run by hand):
  `python3 engine/tools/ci/run_host_tests.py --only ui_scripts` (with `STORM_GAME_DIR` set, `ui_scripts.game` too),
  `ctest --test-dir engine/build/ui-host -R 'ui_screens|ui_script_screen|ui_trade_parity'`,
  `python3 app/mobile-ui/design/build.py --check`, `python3 app/tests/ui_pack_contract/run.py`. Both test trees take
  the scripts as a copy made when they build: rebuild (`ui_scripts_check`, the UI host) after editing a script.

## Retiring a C++ screen

The script screen, its pack screen and checks land in the same commit that deletes the C++ screen's `Layout` and its
registration (`engine/src/libs/ui/src/screens/registry.cpp`). Its recorded parity (`trade-parity.json`) is kept until
the family's C++ is gone; a difference in it is re-recorded only when it is the game's answer shown where the C++
screen computed its own (named in the commit), never to make a regression pass. A retired screen whose recorded runs
are mostly the C++ screen's own arithmetic loses its parity case instead (named in the commit): its
`cases/<capability>.json` and the host checks on the game's answers are its record then.

## Not done

- Variants of a shared screen per game (parts and slots): there is none yet; the first real variant brings them.
- The trade family's last interface, the ship exchange, is still C++
  (`engine/src/libs/ui/src/screens/trade_commerce.cpp`, `extra_transfer.cpp`); its recorded parity
  (`trade-parity.json`) goes with it.
- The game hooks before this structure (`interface/android_*.c`, `android_ui_bootstrap.c`) are not moved yet; the
  crew hire's and the self repair's are gone (their adapters read the game themselves).
- The shipyard and the hero's ship screen are still C++ (`trade_shipyard.cpp`, `hero_ship.cpp`).
