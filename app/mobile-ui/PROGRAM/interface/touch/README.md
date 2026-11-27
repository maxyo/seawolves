# The touch interface's scripts

The logic of the touch interface's screens, in the game's script language: one shared screen per notion (rest, trade,
…) and, per family of games, an adapter that reads the game's interface and performs its actions. The engine keeps the
runtime (admission, drawing, waiting for the game, the amount control); the pack (`../../../design`) keeps the
components and texts. The scripts sit at `interface/touch/` below the program directory, attached by the engine after
the game's own (`engine/src/libs/ui_host`, `script_segments` and `script_port`).

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

## The runtime

Each capability has its object `ui` = `UiScreens.<capability>`: `capability`, `screen` (the screen file's stem), `look`,
`data`, `model` (the admitted widgets of its last build: `<NODE>.{type, label, value, min, max, reason, offers.<verb>,
context, headers, rows.r<index>.{index, id, label, value, reason, offers, cells.c<n>.{text, color, images}}}`). The
events are `UiBuild(ui)` and `UiTap(ui, id, verb, value)`, raised to every screen: a screen's handler starts with
`aref ui = GetEventData(); if (ui.screen != "<stem>") return;`. Only adapters read `ui.model`; the screen hands `ui` to
every contract call.

The natives (`core/core.c`):
- the view: `UiShow`, `UiPredict` (in a tap: the view the tap leads to), `UiOn`, `UiModal`, `UiPending`, `UiText`;
- the game: `UiSend(node, verb, value)`, `UiSendTo({node, verb, value, row, guard, lead})`, `UiBack`, `UiOffers`;
- waiting for the game: after a move, `UiHold(seconds)`. The views built before the answer are drawn; a build from an
  answer the game completes in later frames says `UiAwait()` and is not drawn (what the kept view shows of a closed
  window takes no input). `UiAnswer()` in a build tells what became of the hold: `""`, `"waiting"`, `"applied"`,
  `"refused"` (drawn at once), `"late"` (past `seconds`: an error line unless the game had answered and nothing more is
  awaited). An adapter keeps the step its game is at and awaits while it is not done; its steps live only while
  `UiAnswer()` is `"waiting"`, `"applied"` or `"late"`.
- the amount control: `UiDeal({key, element, origin, other, low, high, moreReason, lessReason, more, less, disabled,
  reason, commit}, &out)` in a build: the engine keeps the player's draft, binds the control, and gives `out.value`,
  `out.change` and `out.button` (absent while unchanged); the button's press raises `UiTap(ui, commit, "", change)`;
- `UiOrder(list, field, descending)`: a stable order by a whole number; `UiRebuild`, `UiFirst`;
- texts: `UiAmount`, `UiNumber` (fail naming the text without a number), `UiHasAmount`, `UiDecimal`, `UiOneLine`, `UiFill`;
- `UiToast`, `UiRead`, `UiError(text)`.

Failures are loud. `UiError`, a native given an argument of the wrong type or an object with a missing or unknown
field, and a runtime error of the script (the interpreter logs it and goes on) fail the build or tap: the router closes
the interface with an error line. There is no default for what the game should have given: an adapter that cannot
read what its contract promises calls `UiError`.

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
   moves, hides its game's frame order behind them (`UiAwait`); rule copies say `// rule copy: <game file> <function>`.
5. `screens/<family>/<name>.c`: builds the view from `State`, binds its elements, answers taps with contract moves and
   `UiHold` where the game answers in frames; lists and their heads with `lib/list.c`.
6. `design/scripts.json`: the contract, the adapter with its `binds` (shared nodes as `nodeSets`), the screen with its
   `screens` and `look`.
7. Checks: the attach and props cases in `engine/tests/ui_scripts` (`cases/<capability>.json` as `cases/rest.json`),
   router behaviour in `engine/src/libs/ui/tests` (`screens_check.cpp`), the pack contract's cases
   (`engine/src/libs/ui/tests/pack-contract.json`) when the screen needs states or taps.

## Not done

- Variants of a shared screen per game (parts and slots): there is none yet; the first real variant brings them.
- Comparison checks of the rule copies against the game's own computation (`STORM_GAME_DIR`).
- The trade family's other interfaces (contraband, cannons, items trade, colony capture, the ship exchange) are still
  C++ (`engine/src/libs/ui/src/screens/trade_commerce.cpp`); their recorded parity (`trade-parity.json`) goes with them.
- The game hooks before this structure (`interface/android_*.c`, `android_ui_bootstrap.c`) are not moved yet.
