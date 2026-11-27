# Engine checks and the pre-PR gate

Paths here are relative to `engine/`; run the tools from that directory.

| Tool | What | When |
| --- | --- | --- |
| `run_host_tests.py` | builds and runs the host checks, the CTest project `tests/CMakeLists.txt` (no NDK, device or game data) | CI on pushes and pull requests; locally `--only NAME ...` or `--changed-since REF` |
| `gate.py` | builds and checks what your change can affect, in parallel | before you open a pull request |
| `affected.py` | what a change can affect: host checks, game-data checks, the UI host and Android builds, the boot stage | used by the two above; `affected.py --base REF` prints the plan, its docstring has the rules |
| `boot_stage.py` | an ASan+UBSan host engine boots the game, loads a save and three scenes | `gate.py --boot auto` or `on`, or alone |
| `check_tmp.py` | the private `TMPDIR` of the gate's UI host and game-data stages | used by `gate.py` |

CI (`/.github/workflows/ci.yml`) runs `run_host_tests.py` and the UI host build with its tests on every
push and pull request: the host checks in three jobs (`--only renderer_bgfx ci_affected`; `--only archive torrent`;
all the others), with Clang and the packages of `requirements.txt`.

## Host checks (`run_host_tests.py`, `tests/CMakeLists.txt`)

The host checks are one CMake project, `tests/CMakeLists.txt`, run with CTest. The runner configures it in
`build/host-ci` once, builds it and runs ctest, printing one `PASS`, `FAIL` (with the output) or `NOT_RUN` line per
check and a summary:

```sh
python3 tools/ci/run_host_tests.py                       # all of them; headless: under xvfb-run -a -s -noreset
python3 tools/ci/run_host_tests.py --only utf8 renderer_bgfx  # checks or groups (the part of a name before the '.')
python3 tools/ci/run_host_tests.py --skip renderer_bgfx  # all but these; the renderer's then not even configured
python3 tools/ci/run_host_tests.py --changed-since origin/main
CXX=clang++-18 python3 tools/ci/run_host_tests.py --work build/host-ci-clang  # with CI's compiler, in a tree of its own
```

CMake keeps the compiler a build tree was first configured with, so the runner refuses a `--work` configured with
another compiler than `$CXX`.

The same with CTest directly, once `build/host-ci` is configured and built
(`cmake -S tests -B build/host-ci -G Ninja && cmake --build build/host-ci`):

```sh
xvfb-run -a -s -noreset ctest --test-dir build/host-ci -j8 --output-on-failure   # all
ctest --test-dir build/host-ci -L renderer                            # one label: renderer, gpu, vulkan, asan,
                                                                      # native, script, tools, format, game, display
ctest --test-dir build/host-ci -R '^utf8$' --output-on-failure        # one check
```

The configure step takes the pinned sources the checks compile against (`pins.json`: fmt, spdlog, fast_float,
mimalloc, Catch2, FFmpeg, miniaudio, GLM, SDL; nlohmann/json and bgfx) from the shared FetchContent copies
(`cmake/StormBuildCache.cmake`; network on the first run), linked under `build/host-ci/sources`. Every program
compiles through the ccache launcher (`tools/ccache`). A check is either a program whose exit code is the result
(`storm_host_check` in `tests/host_checks.cmake`, the renderer's `storm_renderer_check`) or, where it does more than
compile and run (it reads production code as text, builds pinned libraries, drives a tool), a Python script
(`storm_script_check`). Each check has its own `TMPDIR` (`build/host-ci/tmp/<name>`, emptied at the start of every
ctest run) and a deadlock guard (`TIMEOUT`). A check that cannot run on this machine exits with code 77 and says what
it lacks (`cxx_format` without the NDK's clang-format, a Vulkan check without a Vulkan device): CTest skips it and the
runner reports `NOT_RUN`, never a pass; the gate names it in its summary. The Python packages the scripts import are
in `requirements.txt` (`python3 -m pip install -r tools/ci/requirements.txt`).

### Display

The checks that draw open an SDL window on X. Headless, the X server is Xvfb started with `-noreset`
(`xvfb-run -a -s -noreset`): by default the X server resets whenever its last client disconnects, and a check that
connects during the reset (the next window of the same check, or another check starting) cannot open the display.
SDL then takes its offscreen video driver, whose window has no native handle, and the check fails.

## Before a pull request (`gate.py`)

```sh
git fetch origin
tools/ci/gate.py                                         # what the change since origin/main affects
tools/ci/gate.py --tests renderer_bgfx save_state   # add host checks by name
tools/ci/gate.py --skip android                          # leave out a stage you cannot run (say so in the PR)
tools/ci/gate.py --full --boot on                        # every stage, including the boot stage
```

It plans from `git diff origin/main...HEAD` plus uncommitted changes (`--base` to compare with another branch)
and prints one line per stage, `RUN` or `SKIP` with the reason (skipped host checks are listed in
`build/gate/plan.txt`). Stages run in parallel, each into `build/gate/<stage>.log`:

- **host_checks**: the affected host checks (`run_host_tests.py --only`), `--jobs` at once (default: the free cores,
  3 to 8);
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
| `STORM_BOOT_SAVE` (or `--save`) | a save whose hero stands in the town of Martinique (Saint-Pierre) |
| `STORM_VIRTUALGL` | an unpacked VirtualGL (`tools/scene_sweep/README.md`, Renderer) |
| `STORM_CI_WORK` | the dependency sources, default `build/android-ci` |

Log: `build/gate/boot.log`, the sweep's report in `build/gate/boot/`.

## Before a release build

`gate.py --full --boot on`, plus the on-demand scene sweep (`tools/scene_sweep`) for engine and renderer changes.
