# Contributing

Bug reports, fixes, translations and device results are all welcome, in Russian or in English. This page says
how the repository is laid out, how to build and check a change, and what a pull request should look like.

Reporting a bug needs no build: see [Reporting a bug](README.md#reporting-a-bug).

## Layout

The repository has two parts, and the rule between them is strict:

- **`engine/`** holds all native code: the C++ engine, the renderer and its shaders, the UI runtime, every native
  library and its build, the engine's tests and tools. It is a fork of
  [Storm Engine](https://github.com/storm-devs/storm-engine) and is GPL-3.0-only. Start with
  [engine/README.md](engine/README.md).
- **`app/`** holds the Android shell and everything the APK adds to the engine: the Java sources
  (`app/android/game`), the interface pack (`app/mobile-ui/design`: templates, styles, strings, icons), the
  interface scripts (`app/mobile-ui/PROGRAM`), the fixes to the game's scripts (`app/script-patches`), the
  packaging and release tools (`app/tools/android`) and the host checks (`app/tests`). Start with the
  [developer guide](app/tools/android/README.md).

No C or C++ source, shader or native library goes under `app/`, and no Android packaging goes under `engine/`;
`python3 app/tools/android/check_project_boundary.py` (part of the host checks) enforces it. A new third-party
library belongs to the engine, with its licence text in `engine/third_party/licenses` (the APK build fails
when a notice there is not packaged).

**No game content, ever.** Do not add game files, saves, or text, images and sounds taken from the game, to the
repository, to an issue or to a pull request. Tests that need the game read your own copy from `$STORM_GAME_DIR`.
The one exception is the three README screenshots in `docs/screenshots`, labelled as such in [NOTICE](NOTICE).

## Building

Linux, with JDK 17, Python 3.12 or newer, CMake, Ninja, ccache, a C++ compiler (Clang for the sanitizer checks),
FFmpeg, curl, unzip, patch, Perl and make.

```sh
bash app/tools/android/build_debug.sh                           # arm64 debug APK into app/build/dist/
STORM_ANDROID_ABI=x86_64 bash app/tools/android/build_debug.sh  # APK for an x86_64 emulator, app/build/apk-x86_64/
```

The first build downloads the pinned NDK, SDK and dependencies and compiles the engine; later builds are
incremental. Debug APKs are signed with a key generated on your machine (`app/build/apk*/debug.keystore`): keep
it to update the app in place on your device, and note that it cannot update an APK from Releases, which is
signed with the release key.

## Checks

Run these before opening a pull request; CI runs the same commands.

```sh
python3 app/tools/android/run_host_tests.py           # the application: about two minutes
(cd engine && python3 tools/ci/run_host_tests.py)     # the engine
python3 app/mobile-ui/design/build.py --check         # the interface pack, after editing app/mobile-ui/design
python3 app/tools/android/java_format.py              # formats the Java sources (the check is in the host tests)
```

With your own copy of the game, `STORM_GAME_DIR=<game folder> python3 app/tools/android/run_host_tests.py --local`
also runs the checks that read the game's scripts and draw the interface in software. None of the checks needs a
phone, and none of them proves that a change works on one: try anything a player sees on a device or an emulator
([app/android/game/TESTING.md](app/android/game/TESTING.md)) and say in the pull request what you tried it on.

## Changes to the game's scripts

The fixes in `app/script-patches` are unified diffs against the scripts the game's developers published under
GPL-3.0 in [sd-teho-public](https://github.com/storm-devs/sd-teho-public). The rules
([app/script-patches/README.md](app/script-patches/README.md) has the format):

- only scripts covered by that GPL publication, as diffs; never a whole game file, and never a resource that
  is not a script;
- every fix records its licence, its source and a plain description of the defect and its symptom: players read
  that text on the About screen;
- a fix is made for one exact game version, identified by file hashes, and must not break existing saves; say
  how saves are affected;
- a change of balance or presentation is not a bug fix: it must say so, and it needs a setting or a line in
  the changelog.

## Pull requests

- One topic per pull request, with a description of what changes for a player or a developer and how you
  checked it. Small pull requests are reviewed sooner.
- Commit subjects say what the commit does in plain words (English); no ticket numbers or prefixes are required.
- Comments describe the code as it is: what a thing does and why it must be so, not the history of how it was
  found. The same goes for documentation.
- No dead code: remove what a change replaces. No silent fallbacks: an unexpected state is reported, not hidden.
- Interface text goes into all six string tables (`app/mobile-ui/design/strings`, `app/android/game/assets/locales`);
  a machine translation is acceptable if you say so, and a native speaker's correction is always welcome.
- A change players will notice gets a line in [CHANGELOG.md](CHANGELOG.md), in Russian and in English.
- By contributing you agree that your work is licensed as the part of the tree it goes into: GPL-3.0-only under
  `engine/`, `app/mobile-ui/PROGRAM/` and `app/script-patches/`, GPL-3.0-or-later elsewhere ([NOTICE](NOTICE)).

The [code of conduct](CODE_OF_CONDUCT.md) applies to all project spaces. Security problems are reported
privately: [SECURITY.md](SECURITY.md).
