# Engine checks and the pre-PR gate

Paths here are relative to `engine/`; run the tools from that directory.

| Tool | What | When |
| --- | --- | --- |
| `run_host_tests.py` | every `tests/*/run.py` (no NDK, device or game data) | CI on pushes and pull requests; locally `--only NAME ...` or `--changed-since REF` |
| `gate.py` | builds and checks what your change can affect, in parallel | before you open a pull request |
| `affected.py` | what a change can affect: host checks, game-data checks, the UI host and Android builds, the boot stage | used by the two above; `affected.py --base REF` prints the plan, its docstring has the rules |
| `boot_stage.py` | an ASan+UBSan host engine boots the game, loads a save and three scenes | `gate.py --boot auto` or `on`, or alone |
| `check_tmp.py` | the private `TMPDIR` of each check | used by the runners |

CI (`/.github/workflows/ci.yml`) runs `run_host_tests.py` and the UI host build with its tests on every
push and pull request.

## Host checks (`run_host_tests.py`)

```sh
python3 tools/ci/run_host_tests.py                       # all of them; headless: under xvfb-run -a
python3 tools/ci/run_host_tests.py --only glm_math torrent
python3 tools/ci/run_host_tests.py --changed-since origin/main
```

It fetches the pinned open-source sources the checks compile against into `build/host-ci/sources` (network on the
first run), then runs each `tests/<name>/run.py` with the options it declares. Each check gets its own `TMPDIR`
(`build/host-ci/tmp/<name>`), removed after a pass and kept after a failure.

## Before a pull request (`gate.py`)

```sh
git fetch origin
tools/ci/gate.py                                         # what the change since origin/main affects
tools/ci/gate.py --tests renderer_bgfx save_dangling_references   # add host checks by name
tools/ci/gate.py --skip android                          # leave out a stage you cannot run (say so in the PR)
tools/ci/gate.py --full --boot on                        # every stage, including the boot stage
```

It plans from `git diff origin/main...HEAD` plus uncommitted changes (`--base` to compare with another branch)
and prints one line per stage, `RUN` or `SKIP` with the reason (skipped host checks are listed in
`build/gate/plan.txt`). Stages run in parallel, each into `build/gate/<stage>.log`:

- **host_checks**: the affected `tests/*/run.py`, `--jobs` at once (default: the free cores, 3 to 8);
- **game_checks**: the affected `tools/game_checks/*` on an installed copy of the game (`$STORM_GAME_DIR`). A pass
  is cached in `build/game-checks/results/<name>.json` by the digest of its inputs and printed as `CACHED PASS`;
- **ui_host**: `build/ui-host` (src/libs/ui) build + ctest;
- **android**: `build/android-engine`, arm64 Release (LTO) `engine`, with the SDK, NDK and dependencies that
  `tools/android/ci_dependencies.sh` prepares in `$STORM_CI_WORK` (default `build/android-ci`);
- **boot** (`--boot auto|on|off`, off by default): `boot_stage.py`.

A selected stage whose input is missing stops the gate and names the variable to set; `--skip STAGE` leaves it out
explicitly. Nothing is skipped silently.

## The engine boot stage (`boot_stage.py`)

`build/boot-asan` (RelWithDebInfo, `-fsanitize=address,undefined`, `-DSTORM_SCENE_SWEEP=ON`) runs the scene sweep
with one engine on OpenGL ES through VirtualGL: main menu → the save → `sea:Martinique` → the town →
`loc:FortFrance_tavern`. It fails on a crash, ASan or UBSan report, fatal engine error, script errors, missing
resources, a black or HUD-less frame, or the time budget (`--budget`, `$STORM_BOOT_BUDGET`, default 300 s). Known
game-data defects (`tools/scene_sweep/known_defects.json`) stay KNOWN; UBSan suppressions with their causes are in
`ubsan.supp`.

Needs, else it fails loudly:

| Variable | What |
| --- | --- |
| `STORM_GAME_DIR` (or `--game`) | an installed copy of the game |
| `STORM_BOOT_SAVE` (or `--save`) | a save whose hero stands in the town of Martinique (Сен-Пьер) |
| `STORM_VIRTUALGL` | an unpacked VirtualGL (`tools/scene_sweep/README.md`, Renderer) |
| `STORM_CI_WORK` | the dependency sources, default `build/android-ci` |

Log: `build/gate/boot.log`, the sweep's report in `build/gate/boot/`.

## Before a release build

`gate.py --full --boot on`, plus the on-demand scene sweep (`tools/scene_sweep`) for engine and renderer changes.
