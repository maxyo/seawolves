# The Android shell

The Java side of the application: the launcher, the game import and downloads, the game activity that hosts the
engine, the settings the engine reads, saves and the diagnostics report. Building and packaging:
[tools/android/README.md](../../tools/android/README.md); checking a build on a device: [TESTING.md](TESTING.md).

## Settings (`GraphicsSettings`)

The settings screen of the portable UI shows the app's preferences on the state channel `appSettings` and sends
them back with «Готово» (UI action `app-settings`); the engine reads them at start and re-applies the graphics
effects at once (the mailbox's graphics reload). They never change the player's game installation.

- `fx_preset`: the graphics preset, 0 «Низкая», 1 «Средняя», 2 «Высокая». On the first start the engine measures
  the title scene and picks one (`autoPresetChosen`), again after an engine with a newer measurement version, never
  over the player's own choice (`PresetSource`). The engine reads `fx_preset_auto` (-1 the player's choice, else
  the stored measurement's version, 0 none); the settings screen gets it as a flag and does not send it back. The
  measurement may choose «Низкая» at a lower scene height (`fx_auto_render_height`).
- `texture_quality`: «Качество текстур» 0 Низкое, 1 Среднее, 2 Высокое, applied at the next start; until the
  player chooses, the level of the phone's memory class (`TextureMemoryClass`).
- `texture_normal_maps`, `texture_height_maps`, `texture_upscale`: «Обработка текстур», what the phone makes from
  the game's textures in the background, applied at once, off until the player turns them on
  (`TextureProcessing`).
- `frame_rate`: «Частота кадров», among `FrameRates.offered` for the display (reported as `frame_rate_options`),
  60 by default.
- `pirate_frequency`: «Частота пиратов на карте», the percentage of the game's random world-map pirate events kept
  (20 by default); the script patch of `encounters_map.c` asks the engine for it at every event.
- `haptics` («Вибрация»), `left_handed` («Под левую руку»), `ui_scale` («Размер интерфейса»),
  `hud_transparency`.
- Debuggable builds only: single `fx_*` and `render_*` overrides (`AppNames.DEBUG_KEYS`, each kept only at a value
  the settings screen offers, -1 = the preset's value) for bisecting rendering problems on a phone.

The graphics API the engine's device runs and why is recorded for «О программе» (`renderBackend`), not sent to the
settings screen. `engine/tools/android/app_contract.json` lists every key, fact and override; both sides name them
only through the classes generated from it (`AppNames`, the engine's `app_ui_names.hpp`).

## Saves (`SaveArchive`, `SaveExport`, `SaveRestore`)

The engine keeps saves as `<filesDir>/storm/SaveData/<profile>/<file>` and compares names ASCII-case-insensitively;
`netsave` is not a profile, and the suffixes of its atomic save machinery (`.storm-tmp`, `.storm-bak`,
`.storm-deleted`) are never saves.

«Отправить сохранение» / «Отправить все сохранения» write a ZIP whose every entry is one save file named
`<profile>/<file>`, with the save's modification time as the entry time: no folders, no game data, no manifest.

«Восстановить сохранение» takes such an archive or one bare save file. It first stages every save in the cache and
checks it as the engine would load it, so nothing reaches `SaveData` before the whole file is known to be good; then
it writes each save as the engine does (a hidden `.storm-tmp`, synced, renamed over the name, the folder synced). A
save goes to the profile its archive entry records, else to the profile of the last new game, else to `Player`.
Profiles and names are matched the engine's way, so an existing `player` folder takes the saves of `Player`, and a
save whose name folds like an existing one is a conflict the player answers.

## The engine's mailbox (`MobileMailbox`)

The engine thread takes the activity's messages once per frame (`GameActivity.deliverMobileActions`: one typed
native per message). Every message is either STATE (the latest value of a key the engine must end up with: audio
focus, graphics reload, the UI's HUD declarations; a newer one replaces the queued one and a purge never removes it)
or COMMAND (input for the screen it was made on: a tap, a camera step, Back; a purge, when the game loses or changes
its screen, drops it).

## The diagnostics report (`DiagnosticsReport`, `DiagnosticsExport`)

«Поделиться отчётом» writes one text file: build, game, device, GPU (an EGL probe), graphics settings; the
renderer facts the engine logged (backend and why, caps, effects support); the system's exit history of the app's
processes (reason, status, importance, pss/rss, description; an ANR's trace head; for a native crash a line that
the system's trace is not included); the tails of the engine's log files of the latest game process and the ones
before it, each labelled with its start, pid and how it ended, preceded by a digest of the rest (periodic
summaries, stall reports, errors) when a file is longer than its tail; and the app's own logcat lines.

«Подробная диагностика» on the About screen (off by default; kept in `diagnostics-detailed.txt` because the game's
process reads it when it comes back to the foreground) turns on the engine's costly diagnostics for a report (engine
renderer_bgfx/README.md, «Diagnostics»); the report's Build section says whether it is on.
