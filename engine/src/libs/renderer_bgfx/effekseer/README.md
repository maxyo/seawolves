# Effekseer on bgfx

Effects in the Effekseer format (`.efkefc` with its `.efkmodel` models, PNG and TGA textures and curves), played and
drawn by the renderer; the engine's side is `include/storm/render/effekseer.hpp` and `src/effekseer/`, the game's side
the `Effects` entity (`libs/effects`).

## Parts

| Target | What |
| --- | --- |
| `storm_effekseer_runtime` | Effekseer's core and `EffekseerRendererCommon` at pin `effekseer` (1.80h), without the editor's network server and the default texture loader; its PNG (stb_image) and TGA decoders are used |
| `storm_efkbgfx` | efkbgfx's renderer at pin `efkbgfx` with `patches/efkbgfx-*.patch` (the reasons head the patch), its shaders embedded for OpenGL ES and Vulkan by `storm_bgfx_embed_shaders`; `src/shaders.cpp` creates them |
| `src/effekseer/` in `storm_bgfx` | the manager, the loaders, the textures and the draw |

Only the runtime is built from the pinned trees; nothing is vendored. Licences: `third_party/licenses/Effekseer-MIT.txt`,
`efkbgfx-MIT.txt` (stb_image's text is `bimg-stb-MIT-or-Unlicense.txt`).

## Frame

- Lifetime: the manager and the renderer are made by the first `LoadEffect` (a game without effects pays nothing) and go
  in `Context::shutdown` before bgfx (`effekseer::ShutdownDevice`). Effects are loaded on demand, when a script plays
  them, and kept until unloaded; nothing is preloaded.
- `Update(seconds)`: Effekseer advances 60 frames a second of game time and the finished texture copies are swapped in.
- `Draw(view, projection)` (the `Effects` entity's realize, at the place of the game's own particles): a fresh view on
  the game's current render target and viewport (`Device::effects_view`), after the opaque scene and whatever the game
  drew before it, before the interface. The draws test against the scene's depth (less or equal) and write it only where
  the effect's node says so; they blend into the game picture and go through the post chain with the rest of the
  scene. The projection is the scene's D3D projection in the backend's clip space (`effects::BackendProjection`, the
  same as the fx world passes); a bottom-left-origin target flips Y, so culling swaps its winding (`mirror_winding`).
- Not drawn, reported by name per effect when it loads: distortion nodes (no copy of the scene behind them), material
  (`.efkmat`) nodes (efkbgfx has no material shader generator), GPU particle nodes; soft particles are drawn with hard
  edges (no copy of the scene depth) and sounds are not played. Caribbean Legend's effects use none of them except the
  ones the corpus check lists.

## Textures

An effect texture (PNG or TGA) is decoded once by Effekseer's decoders and made in the format of the GPU's tier for the
game's own DXT textures (`shim::compressed_2d_format`): BC1/BC3 on GPUs with S3TC, ETC2 RGB/RGBA elsewhere (alpha when
any texel is not opaque), with its whole mip chain (linear light), on a texture worker; until then it is drawn as RGBA8
without mips. The compressed copy is kept in the texture cache (key: the file's bytes and the tier), so a later load
reads it without decoding the image. On the RGBA8 tier (`texture_compression = rgba`, or a GPU with neither) the image
is uploaded with its chain at once. Effekseer shares a texture between the effects that name the same path; it goes
when the last of them does (a few updates after the unload, when the manager has removed their instances).
`GetStats()` counts the textures, their GPU bytes and where they came from.

## Checks

`tests/renderer_bgfx/effekseer_check.cpp` (on OpenGL ES and Vulkan) draws the samples of the pinned efkbgfx sources
through the shim; `renderer_bgfx.effekseer_check.corpus` loads, plays and draws every effect of the installed game in
`$STORM_GAME_DIR` and prints what each uses and its instances at its peak, the measurement behind the manager's budgets
(`src/effekseer/system.cpp`); not run without a game.
