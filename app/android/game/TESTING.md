# Testing a build on a device

The host checks (`python3 app/tools/android/run_host_tests.py`) prove that the code compiles and that its logic
holds on the host; they do not start the app. A CI build proves nothing about installation, import or gameplay.
This page is what to do on a phone or an emulator before a change that players see is merged, and before a
release. Commands run from the repository root.

## What you need

- An ARM64 Android device, API 24 or newer, with OpenGL ES 3.0 (the manifest's requirement), or an x86_64
  emulator with an x86_64 build (`STORM_ANDROID_ABI=x86_64`). An emulator exercises the Java side and the engine's
  logic, not the ARM64 code or a phone's GPU driver.
- Your own copy of the game: a game folder, a ZIP, 7z or RAR archive, or the Inno Setup installer (`setup.exe`
  plus all `setup-*.bin`). Game files are never uploaded, committed or sent to anyone.
- Storage on the device for the source package (installers and non-ZIP archives are copied into the app first)
  and the extracted game, about 15 GB with a full copy. A failed or cancelled import keeps the game that was
  installed before.
- `adb` for logs. The app needs no storage permission: import uses the system picker.

Where things are on the device: the game in `getExternalFilesDir()/game`; the download library of the github
flavor in `getExternalFilesDir()/game-downloads` (its own `:downloads` process); saves, logs and screenshots in
the app's internal `files/storm`. Uninstalling the app or installing a build signed with another key deletes all
of it: send the saves out first (About → «Отправить сохранение» → «Все сохранения»; «Восстановить сохранение»
brings the archive back).

## Acceptance sequence

1. **Install.** Install the APK and check that it is the one you built (`adb shell pm path`, then `sha256sum` of
   the base APK) and that its native libraries are there (play: `libstorm_archive.so` without
   `libstorm_torrent.so`). Without a game the launcher shows where to get the files from, and no Play button.
2. **Import.** Import the unpacked game folder and, separately, an archive; compare the imported file hashes
   with the input. Import the installer (`setup.exe` with all `setup-*.bin`) and a 7z archive. `setup.exe`
   without a volume, an extra unrelated file, an encrypted archive and an unsupported executable must each show
   their message before a large copy starts. A corrupt or truncated archive, an archive with unsafe paths, too
   little space and a cancellation (during copying and during extraction) must leave the previous game unchanged.
3. **Download library** (github flavor). Add several source sets; downloading one does not make it the active
   game. With a small public torrent that is not game data, check metadata-only operation, an invalid magnet
   link, a stall, the choice between found copies, pause, process death (`am force-stop`), resume with piece
   rechecking, completion and import; notifications with the permission granted and denied. Downloads run in
   `com.goodoldgames.seawolves:downloads` (`adb shell ps -A | grep seawolves`).
4. **Back.** Check Back on the game files screen and on About and its licence list
   (`adb shell input keyevent 4`).
5. **Start.** Start the game and confirm that the title screen draws; collect `adb logcat` and a report
   (About → «Поделиться отчётом»).
6. **Sound.** Effects, music changes, the volume categories, positional sounds, repeated scene changes and many
   sounds at once (`engine.ini` `[sound] physical_voices`, default 64, 2..128).
7. **Lifecycle.** Home and return, lock and unlock, a headset or Bluetooth route change during music. Sound
   stops in the background, no control stays held, rendering recovers, and an autosave is written when the app
   leaves the foreground.
8. **Controls.** The floating movement stick, camera swipes and the action buttons with several fingers at once.
   Context actions appear only when available, loading hides the controls, cutscenes keep only pause. Dialogue
   paging and reply scrolling, the inventory, equipment, maps, a larger interface size; at sea the helm, the
   sail lever, ammunition and fleet orders.
9. **Play.** Load a scene, move, interact, fight, save, restart the app and load the save.

```sh
adb logcat -c
adb shell am start -n com.goodoldgames.seawolves/org.stormengine.port.GameLauncherActivity
adb logcat -d > logcat.txt
adb shell run-as com.goodoldgames.seawolves ls files/storm     # debug builds
```

Known limits: Vorbis floor-0 and chained streams are not supported; resizing forced by a large-screen system
setting is not verified.

## Platform integration, and how to check it

- **Memory pressure**: `GameActivity.onTrimMemory` forwards `TRIM_MEMORY_BACKGROUND` and above to the engine as
  `SDL_APP_LOWMEMORY`.
- **Thermal**: a listener on API 29+; status changes are logged (tag `StormPerformance`).
- **ADPF**: a PerformanceHint session on API 31+ for the game thread and the render thread. The JNI entry points
  are static methods of `org.stormengine.port.GamePerformance`: `addThread(int tid)`, `updateTargetWork(long
  nanos)` (the frame pacing's interval), `reportFrameWork(long nanos)` (once per frame). A missing method logs
  `Frame work reporting disabled: <reason>` once. Check: `adb shell dumpsys performance_hint`.
- **Exit**: `GameActivity.stopEngine` sends the quit event and waits through `EngineExit` at most 3 s, or 4.5 s
  while a save is being written, staying below the 5 s ANR limit; then the process ends itself (saves are
  atomic). Logcat tag `StormExit`. Check:
  `adb shell dumpsys activity exit-info com.goodoldgames.seawolves`.
- **One engine run per process**: the `:game` process ends with its engine when `SDL_main` returns or the
  activity is destroyed. Check: exit through the pause menu, then «Играть» starts a new `:game` process
  (`adb shell pidof com.goodoldgames.seawolves:game`).
- **Exit from the pause menu**: «Выйти» runs the game's own exit; the launcher waits for the old process to end
  (at most 5 s) and the next start opens the main menu without the intro videos.
- **Crash diagnostics**: on API 30+ the launcher reads native crashes, ANRs, Java crashes and low-memory kills
  from `ApplicationExitInfo`, shows a notice and puts them into the report. Debug builds set
  `gwpAsanMode="always"`.
- **Native crash symbols**: `python3 app/tools/android/symbolize.py <report file>` resolves the native frames of
  a crash in a report by build id (debug builds: against the libraries you built; releases: against the run's
  `native-debug-symbols.zip`).

## Automated runs: the scenario driver and the boot smoke

Debug builds made with `build_apk.py --test-loop` contain a scenario driver
(`app/android/game/testloop/on/.../TestLoop.java`; its header lists every scenario). Every other build, and every
release, compiles the no-op `app/android/game/testloop/off`. A runner starts a scenario through the launcher's
Game Loop intent:

```sh
adb shell am start -a com.google.intent.action.TEST_LOOP \
    -n com.goodoldgames.seawolves/org.stormengine.port.GameLauncherActivity --ei scenario <n>
```

| Scenario | Needs | Does |
| --- | --- | --- |
| 1 launcher | nothing | the launcher screen, its game files screen, the system folder picker |
| 2 menu scene | a game subset | installs the subset, starts the game, measures the title scene under each graphics preset |
| 3 save scene | a subset with a save | as 2, then «Продолжить» loads the newest save and measures it under each preset |
| 4–8 | a subset with the matching save | the world map, the texture upscale, character relief, far LODs, preset steps |
| 9 boot smoke | a subset with a town save | title, automatic preset, «Продолжить», one screenshot of the town |

Each step has a hard limit; a missed one ends the run as `failed` with its reason. Results go to the app's
external files directory `testloop/`: screenshots, `events.log`, the game's log, a report and
`summary-<scenario>.json`.

**The game subset** is a part of your own copy of the game, packed as an OBB that the driver installs instead of
a full import, so a scenario does not need 13 GB on the device:

```sh
python3 app/tools/android/game_subset.py --game <game folder> --trace <strace log> [--save <save file>] \
    --out <dir outside the repository>/main.<versionCode>.com.goodoldgames.seawolves.obb
```

It packs what the scenario opens (traced with a host run of the engine, see the script's header) into `game/`
and the save into `storm/SaveData/player/`; `--base <earlier OBB>` writes only the difference as a `patch.` OBB.
The script refuses to write into the repository.

**The boot smoke** runs scenario 9 and judges it:

```sh
STORM_ANDROID_TEST_LOOP_SIBLING=1 bash app/tools/android/build_debug.sh
python3 app/tools/android/emu_smoke.py --apk app/build/apk/play-testloop/seawolves-play-arm64-debug.apk \
    --serial <adb serial> --obb <main.*.obb> --out <dir outside the repository>
```

It installs the APK, pushes the subset, starts at the main menu, loads the save and fails unless the driver
passed, the engine that ran is the APK's and the device log has no crash; about a minute on a warm emulator.
`--scenario <n>` runs another scenario the same way, `--same-libraries-as <APK>` also checks that another APK
packs the same native libraries.
