**English** | [Русский](README.ru.md)

# Seawolves

Seawolves is an Android app that runs Sea Dogs: To Each His Own («Корсары: Каждому своё») on a phone, from your
own copy of the game. It consists of an Android shell with a touch interface (`app/`) and a fork of
[Storm Engine](https://github.com/storm-devs/storm-engine), the game's open-source engine, ported to Android
(`engine/`).

It is an unofficial fan project, not affiliated with the game's rights holders. **The repository and the APK
contain no game files.** You need your own legally obtained copy of the game.

**Status: first public alpha.** The game starts, plays and saves on the phones listed under
[Requirements](#requirements); a full playthrough has not been verified, and devices outside that list are
untested. Expect bugs and please [report them](#reporting-a-bug).

## Screenshots

<p align="center">
  <img src="docs/screenshots/interior.jpg" width="800" alt="The captain in the Saint-Pierre tavern at night, candle and hearth light on the walls and floor, with the touch controls on screen"><br>
  <em>Indoors: the tavern of Saint-Pierre at night, lit by candles and the hearth.</em>
</p>
<p align="center">
  <img src="docs/screenshots/sea.jpg" width="800" alt="The player's corvette under full sail on the open sea in the morning, with the sea controls and the minimap"><br>
  <em>At sea: a corvette under full sail, third-person camera.</em>
</p>
<p align="center">
  <img src="docs/screenshots/hero.jpg" width="800" alt="The character window on its skills tab"><br>
  <em>The character window, skills tab.</em>
</p>

The screenshots show the game running in the app (Russian copy of the game). The game's graphics in them belong to
the game's rights holders, are shown for illustration only and are not covered by this repository's licence
([NOTICE](NOTICE)).

## What you need

- **The game**: Sea Dogs: To Each His Own, version `ver. 1.7.3 (26.11.21) x64`, installed on a computer or as
  its archive or installer. Other versions start too, but the fixes to the game's scripts are made for this exact
  version and are skipped on any other, and other versions are untested. The report file (see
  [Reporting a bug](#reporting-a-bug)) shows the version line of your copy.
- **A phone** that meets the requirements below, with about 15 GB free.

## Requirements

| | Minimum | Recommended |
|---|---|---|
| Android | 7.0 (API 24) | 12 or newer |
| Processor | 64-bit ARM (arm64-v8a) | 64-bit ARM (arm64-v8a) |
| RAM | 4 GB | 8 GB |
| Free storage | 15 GB | 20 GB |
| GPU | Vulkan 1.1 or OpenGL ES 3.2, a mid-range GPU of 2021 or newer | a flagship GPU of 2023 or newer |

| GPU family | Minimum | Recommended | Below the minimum |
|---|---|---|---|
| Adreno | 642L, 644, 660, 710, 720 | 740, 750, 830 | 610, 612, 618, 619 |
| Mali | G68 MC4, G610 MC6, G615, G715 | — | G52, G57 |
| Immortalis | — (all are above the minimum) | G715, G720, G925 | — |
| Xclipse | 530, 540, 920 | 940, 950 | — |
| PowerVR | D-Series DXT-48-1536 | — | GE8320, GE8322 |

The engine draws with Vulkan 1.1 where the phone has a working driver for it and with OpenGL ES otherwise. The
APK installs on any phone with OpenGL ES 3.0, which is all the engine needs to start; below the minimum above the
game is too slow to play. A phone that clearly misses the minimum (under 3 GB of RAM, neither Vulkan 1.1 nor
OpenGL ES 3.2, or the lowest graphics preset slower than 60 ms a frame on the title screen) gets one warning in the
launcher; the game still starts.

On the minimum the game runs at the «Низкая» (low) graphics preset; the recommended GPUs run «Средняя» or
«Высокая». The game takes 0.6–0.9 GB of RAM. Storage: 12.7 GB for the game and up to about 2 GB of caches the app
makes on the phone (upscaled textures, relief maps, shaders); the recommended figure leaves room for an archive
you import from.

**Played** on Galaxy S24 (Adreno 750), Xiaomi 15 Ultra (Adreno 830) and Pixel 10 Pro (PowerVR D-Series, on
OpenGL ES). The start, a loaded save and the graphics presets were also **measured** on Galaxy S23 (Adreno 740),
Galaxy S21 (Adreno 660), Galaxy A56 (Xclipse 540) and Pixel 8 (Mali-G715), on Android 12 and 16. Galaxy A12
(PowerVR GE8320) runs at 2–5 frames a second and is not supported. The other GPUs in the table, the RAM figures
and Android below 12 are **estimates** from public GPU benchmarks, not measurements.

## Installing

1. Download `seawolves-<version>-arm64.apk` and `SHA256SUMS` from
   [Releases](https://github.com/maxyo/seawolves/releases).
2. Optionally verify the download. The checksum:

   ```sh
   sha256sum --check SHA256SUMS
   ```

   and the signature (`apksigner` is part of the Android SDK build tools); every release is signed with one key,
   whose certificate has this SHA-256 digest:

   ```sh
   apksigner verify --print-certs seawolves-<version>-arm64.apk
   # Signer #1 certificate SHA-256 digest: 01096bac94d9135ed3e45ff23bbe78f002ca09fb5aa7e1a5f16e44810ab09749
   ```

3. Open the APK on the phone and allow your browser or file manager to install apps when Android asks
   (sideloading). Updates install over the previous version and keep the game and the saves.

## Importing the game

Start Seawolves and open **Game files**. Choose one of:

- **Game folder**: copy the folder of the installed game from your computer to the phone and pick it (a folder
  above it works too);
- **Archive or installer**: a ZIP, 7z or RAR archive of the game (split archives too), or the installer:
  `setup.exe` together with all its `setup-*.bin` files, selected at once.

The app copies the game into its own storage and checks it; then **Play** starts it. The **Download** screen
fetches a copy you are entitled to from a torrent file or magnet link you provide; the app itself contains no links
to the game.

## What it does

- Touch controls: a floating movement stick, camera swipes, action and ability buttons, a sea HUD with a floating
  helm and sail lever, ammunition and orders, haptic feedback, a left-handed layout.
- A redesigned interface for the menus, saves, settings, the hero, the ship, trade, the shipyard, the journal and
  dialogues, in Russian, English, German, Spanish, French and Italian.
- Graphics for phones: Vulkan or OpenGL ES, quality presets with an automatic one, relief and gloss maps and a ×2
  texture upscale generated on the phone from your game files.
- The game's videos, autosaves in three rotating records, sending and restoring saves, a report file for bug
  reports.
- Fixes to bugs in the game's own scripts, applied at start-up to your copy. One of them is a balance setting
  rather than a fix: random pirate encounters on the world map appear at 20 % of the game's frequency by default
  (Settings → Pirates on the world map; Often is the unchanged game).

Changes by version are in the [changelog](CHANGELOG.md).

## Plans

No dates and no promises; the order may change:

- **An iOS port.**
- **A mod manager**: install, enable and disable mods from the app.
- **Tidying the code**: the generic interface code into the engine, a better game script language, and the game's
  own interface moved from C++ into scripts.
- **Other games of the Sea Dogs series** on the same engine.
- **Caribbean Legend.**

## Reporting a bug

In the launcher open **About → Share a report** («О программе» → «Поделиться отчётом») and save the file: it holds
the app's logs, the phone model, the GPU and the version of your game copy. Attach it to a new issue in
[Issues](https://github.com/maxyo/seawolves/issues). If the bug depends on a save, add it with **Send a
save** on the same screen. Security problems: see [SECURITY.md](SECURITY.md).

## Building

On Linux with JDK 17, Python 3.12 or newer, CMake, Ninja, ccache, a C++ compiler, FFmpeg, curl, unzip, patch,
Perl and make:

```sh
git clone https://github.com/maxyo/seawolves.git
cd seawolves
bash app/tools/android/build_debug.sh
```

The first build downloads the pinned Android NDK and SDK and the open-source dependencies, builds the engine and
writes the debug APK to `app/build/dist/`. It needs no game files.
The checks that run without a phone:

```sh
python3 app/tools/android/run_host_tests.py          # the application
(cd engine && python3 tools/ci/run_host_tests.py)    # the engine
```

More in [CONTRIBUTING.md](CONTRIBUTING.md), the [application developer guide](app/tools/android/README.md) and
the [engine README](engine/README.md).

## Repository map

| Path | What |
|---|---|
| `app/android/` | the Android shell in Java: launcher, game import, the game activity, sharing |
| `app/mobile-ui/design/` | the touch interface pack: templates, styles, strings, icons, fonts |
| `app/mobile-ui/PROGRAM/` | interface scripts that run together with the game's scripts |
| `app/script-patches/` | fixes to the game's published scripts, as diffs |
| `app/tools/android/`, `app/tests/` | APK packaging, release tools, host checks |
| `engine/` | the native engine: C++, the bgfx renderer and its shaders, the UI runtime, its tests and tools |

## Licence

[GPL-3.0](LICENSE). The engine, the interface scripts and the script fixes are GPL-3.0-only, as Storm Engine and
the game's scripts were published; the rest of the application is GPL-3.0-or-later. [NOTICE](NOTICE) has the
details, the origin of every font, icon and texture, and the fork point of the engine. Third-party libraries
keep their licences (`engine/third_party/licenses`); every APK lists them on its About screen.

## Credits

The engine is a fork of [storm-devs/storm-engine](https://github.com/storm-devs/storm-engine) (branch `develop`
at commit `a68f43df`, 2023-11-25). Thanks to the authors of Storm Engine and of the game for opening their code,
and to the storm-devs community that maintains it and published the game's scripts in
[sd-teho-public](https://github.com/storm-devs/sd-teho-public).

Thanks also to Claude and Codex, the AI assistants that helped write this port.

## Disclaimer

Seawolves is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by or connected to
the developers, publishers or rights holders of «Корсары: Каждому своё» (Sea Dogs: To Each His Own) or of the
Sea Dogs series; their names and trademarks belong to their owners. The game's content (models, textures,
sounds, texts and everything else) is not part of this project and is not covered by its licence. The project
does not distribute the game and does not help to obtain it: use a copy you own. The software comes with no
warranty.
