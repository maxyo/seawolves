# renderer

The engine's `VRenderer` service (`src/renderer.cpp`) over the bgfx renderer (`../renderer_bgfx`), with the project's
additions: the automatic graphics preset, the startup render resolution, the preset's scene detail, the game pictures
and the mesh helpers the sky and skinned models draw through (`include/`).

## Automatic graphics preset (`src/auto_preset.hpp`)

On Android, on the first start (and again when `kAutoPresetVersion` is newer than the stored automatic choice) the
title scene is measured on the rungs of `kAutoPresetRungs` and the highest rung that holds `kAutoPresetTargetFps`
with headroom is kept. The player's own choice of «Графика» is never measured over (the app stores its source).

- Rungs: «Низкая» at a lower internal height (`kAutoPresetLowestHeight`, chosen only here), «Низкая», «Средняя»,
  «Высокая». The measurement starts on «Низкая» and goes up while rungs pass; when «Низкая» fails, the rung below it
  is measured and kept.
- Headroom: the title (110-200 draws) is one of the cheapest scenes; a port town draws 400-490. On an Adreno 830 towns
  cost up to 1.5x the title's GPU time. Where the GPU has timer queries a rung passes when its mean GPU frame time
  times `kAutoPresetHeadroom` fits the target frame time and the 95th percentile of its present intervals is at most
  `AutoPresetLimitMs()`; without them, when the 95th percentile of the present intervals fits the target divided by
  the headroom. The measurement paces at `kAutoPresetPacingFps` whatever «Частота кадров» says, so the margin means
  the same for every player.
- Conditions: relief generation and ray bakes are held while the measurement waits and runs. It starts after
  `kAutoPresetTitleSeconds` of the title with settled effects and no background work in progress. Each rung waits
  until its switch is active (programs prepared and self-tested, which can take a minute without a program cache),
  then settles `kAutoPresetSettleSeconds` and `kAutoPresetSettleFrames` from that moment (activation rebuilds render
  targets, and its first frame can be very slow), and is measured `kAutoPresetMeasureSeconds`, at least
  `kAutoPresetMinFrames` frames. Leaving the title, the app going to the background or new settings from the player
  abort it; nothing is stored and the next start measures again.

The header is free of the engine, so `tests/android_auto_preset` runs it as it is.

## Graphics options of the game's scripts (`src/script_graphics.hpp`, `src/script_graphics.cpp`)

Caribbean Legend's options screen reads and sets its graphics through the `Graphics_*` natives. What this renderer has
is taken as effects options over the preset, under the developer's `fx_*` keys of engine.ini, and only once the scripts
set it (a game that never calls them keeps its preset). On desktop such a game draws over «Высокая» unless engine.ini
or `STORM_FX_OPTIONS` names a preset: what its options leave out is drawn in full, as its own engine does. On Android
the app's graphics settings decide and the scripts' choices are not applied (logged once).

- Taken: sun shadows (off and two qualities for the game's three levels), lamp shadows and occlusion off,
  anti-aliasing (`ScriptAntiAliasingOf`: its FXAA as SMAA, its supersampling as MSAA 2x/4x with alpha to coverage, its
  TAA and DLAA, the game's default, as MSAA 4x with alpha to coverage, shown in its options as supersampling «High»),
  anisotropic filtering (previewed live; kept in engine.ini `anisotropic_filtering`, which applies at the start), warmth
  and saturation over the colour grade, gamma, contrast and brightness on the window's colour ramp (brightness as a
  gain), vsync (live, kept in engine.ini `vsync`), the window's size and full screen (kept in engine.ini, taken at the
  next start), the display's modes, the GPU's memory (half the system's where the backend cannot tell).
- Not available, answered as off and logged once when asked for: a TAA or DLSS of their own (the getters never say they
  run), frame generation, other occlusion modes, turning sea reflections off, sea qualities other than the one our sea
  stands for (refused, so the screen shows that one), a lamp shadow size or a separate switch for characters' shadows.

## Bench (`src/bench.hpp`)

A debuggable build measures the GPU cost of graphics variants where the player stands, for comparisons that do not
depend on the GPU clock: «Отладка» → «Бенчмарк» (полный / быстрый) → «Готово» asks for one run (the app does not store
the request), which starts once the menus are closed. Opening a menu, new settings or the background end it; the
player's settings come back at its end.

- Presentation: while it runs frames go to the display without Swappy at its fastest mode, so they queue back to back
  and the GPU stays busy (`storm/render/frame_rate.hpp` `SetBenchPresentation`, debuggable builds only: the one
  exception to the paced presentation). The game time stands still, as under the pause menu.
- Slots: the reference (the player's settings), then each variant followed by the reference again. A slot is measured
  only when its settings draw, no relief map, ×2 texture or bake is in progress, and it then settled for the
  automatic preset's settle time and frames without a frame that left a draw out; it is measured for the automatic
  preset's measuring time and frames, from the frame timer query alone. A switch, background work or an incomplete
  frame during the measurement throws it away and the slot waits again; GPU times of frames presented before the
  measurement began never count.
- Variants (`BenchVariants`): quick, the scene heights 1200 and 1400; full, also 1000, MSAA 1/2/4, sun shadows, lamp
  shadows, point lights, specular and gloss off one at a time, anisotropy 1/8, each texture switch the other way,
  alpha to coverage off, the depth pre-pass off and on (alone, with the occlusion volume off, with plain shading,
  with the specialised programs off, at the heights 1200 and 1400, and at 1400 with plain shading and with MSAA 1), the three presets, and with the relief switches on: the
  occlusion parallax with its shadow, the characters' parallax and two lamps per pixel with one filtered.
- Lines: `bench: <variant> | <options> | gpu_ms mean, p95, frames, gpu share | reference before and after, delta |
  scene target and scale | relief maps resident | thermal | refresh and presentation | debuggable, GWP-ASan`. The GPU
  share is the part of the measured time the GPU worked (`BenchSample::gpu_share`). A variant light enough for the
  display's refresh or the CPU to hold its frames leaves the GPU idle between them, and its `gpu_ms` then follows the
  GPU clock, not the work: such a slot shows a lower share than the reference and a frame count on the refresh grid
  (360 or 180 frames in 3 s at 120 Hz), and its delta is not a cost. Compare it at a larger scene height. The build is
  debuggable with GWP-ASan, so only the delta against the reference beside it counts, not the absolute time. It does
  not start with detailed diagnostics or `fx_gpu_profile` on (their queries cost GPU time) or without GPU timer queries.

The header is free of the engine, so `tests/android_bench` runs it as it is.
