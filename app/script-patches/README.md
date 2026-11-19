# Script patches

Paths and commands in this file are relative to `app/`.

Fixes to the player's own game scripts. The APK carries unified diffs, never game
files; the engine applies them at start-up to the files of the game version each
patch set was made for (engine: `engine/tools/script_patches/README.md` describes
the layer, the checks and what the player sees when a fix cannot be applied).

Shipped set: `sets/teho-1.7.3-x64` (Корсары: Каждому своё, ver. 1.7.3 (26.11.21) x64). Each fix's title,
licence, source, bug and save compatibility are in its `set.json`.

A game copy no set identifies runs unpatched. The launcher says so on the installed game and on each copy offered
for choice («Это не «ver. 1.7.3 (26.11.21) x64»: …», the version as the sets' `version` spells it, the game's own text), and the
diagnostics report («Поделиться логом») names the set or "no set matched". Both identify the copy with the
engine's own rule through the archive reader (engine `android/archive/README.md`), never a check of their own.

## What may be shipped

A diff of a script contains that script's text, so a fix may be shipped only when
the script's licence allows distributing modified copies. Every fix states its
`license` and `source`; packaging (`tools/android/script_patch_assets.py`) refuses
any licence outside `REDISTRIBUTABLE` (currently GPL-3.0-only and GPL-3.0-or-later, e.g. the published
TEHO scripts, storm-devs/sd-teho-public). A new licence there is a maintainers'
decision, made in its own pull request. Scripts of games whose licence is unknown
or proprietary must not get patch sets here.

The rules in short: GPL-covered scripts only; diffs, never game files; every fix
records its licence and source. The patches and `set.json` are GPL-3.0-only, like
the scripts they change.

## Adding a fix

1. Put the patch set in `sets/<set id>/`, one set per game version. `set.json`
   identifies the game by the sha256 of one or more of its files (a version
   header is a good choice) and lists targets and fixes; see the docstring of
   `tools/android/script_patch_assets.py` for the schema.
2. Fix a copy of the vanilla script, then write the patch and get its hashes:
   ```
   python3 tools/android/script_patch_assets.py diff --base <vanilla>/PROGRAM/dialog.c \
       --edited <fixed copy> --target PROGRAM/dialog.c --out script-patches/sets/<set>/dialog.c.patch
   ```
   Keep the vanilla and fixed copies of game scripts out of Git. Dialog texts are
   per language: each `PROGRAM/dialogs/<language>/...` file is its own target.
3. Fill `bug` (a neutral description of the defect and its symptom, with the upstream
   issue when there is one; the About / licences screen shows it to players), `title` (shown to the player when the fix cannot
   be applied) and `save_compatibility`: saves keep script variables by name and
   recompile code, so code fixes load into running games; say so explicitly when a
   fix renames or retypes globals, changes array sizes or writes new attributes
   into saved objects.
4. Check: `python3 tools/android/tests/script_patches/run.py`, and against the real
   game with `STORM_SCRIPT_PATCH_GAME=<vanilla TEHO 1.7.3 game dir>` (uses the engine's
   own applier, the same code the game runs; every target must reach its result sha256).

Packaging writes `assets/script-patches/` (`sets/<set>/index.txt` for the engine,
the patches, `SOURCE.json` with per-fix licence, source, bug and hashes, and
`manifest.json` for the installer) and `assets/licenses/script-patches.txt` for
the About / licences screen.
