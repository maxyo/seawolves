# renderer_bgfx

The engine's only renderer: [bgfx](https://github.com/bkaradzic/bgfx), on OpenGL ES 3.0 or on Vulkan,
on Android and on the Linux development host alike.

- `cmake/bgfx_fetch.cmake` — bgfx, bx, bimg and bgfx.cmake at their pins (`engine/pins.json`); `cmake/bgfx_upstream.cmake`
  adds those targets with `patches/` applied (each patch starts with its reason; the configure fails when a
  patch does not apply or changes nothing). Everything that uses bgfx, bx or bimg links `storm_bgfx_runtime`
  (or `storm_bgfx`), never those targets, and never fetches bgfx itself. On Android `storm_bgfx_runtime` is
  `libbgfx.so`, shared by `libengine.so` and `libstorm_ui.so` so a process has exactly one bgfx
  (`tools/android/verify_engine.py` refuses a `libengine.so` with its own). Offline builds pass
  `-DFETCHCONTENT_SOURCE_DIR_STORM_{BGFX,BX,BIMG,BGFX_CMAKE}=<pinned checkout>` (bgfx patched).
  Licences: `third_party/licenses/{bgfx,bx,bimg}-BSD-2-Clause.txt`.
- `src/shim/` — the D3D9-shaped device behind `VRenderer` (src/libs/renderer/include/renderer.h); an
  unimplemented path logs a `[storm-bgfx] TRAP`.
- `shaders/` — fixed-function emulation, the blit and `include/storm_common.sh`; `shaders/ports/` +
  `src/shim/ports.cpp` — hand ports of the vs_1_1/ps_1_x shaders used by `src/techniques`, looked up by
  `<fx base name>_<shader name>` (unknown keys are logged and their draws skipped).
- `include/storm/render/frame_owner.hpp` — the frame owner contract (the header is the documentation).
- `src/texture/` — GPU copies of the game's DXT textures with rebuilt mip chains, made in the background
  with an on-device cache; the tier (bc / etc2 / rgba8) is chosen per GPU and logged. The textures come from
  `.tx` files or, told apart by their first bytes, DDS (BC1-3, read by bimg) and TXX files
  (`src/texture/core/texture_file.hpp`); other DDS formats are refused with the file named.
- `fx/` — optional effects package ([fx/README.md](fx/README.md)), driven by `src/effects/`.
- `effekseer/` — Effekseer effects at their pins, drawn into the game's scene after its opaque geometry
  ([effekseer/README.md](effekseer/README.md)), driven by `src/effekseer/`.
- `host-tools/` — builds only the host `shaderc` from the same pin.

## Backends

`render_backend = auto | gles | vulkan` in engine.ini (debuggable Android builds: the graphics setting,
0 / 1 / 2). `auto` (default) takes Vulkan when the device has Vulkan 1.1 with `VK_KHR_swapchain` and
`VK_KHR_maintenance1`, a hardware GPU, ETC2 or BC textures, is not on `VulkanDenied`
(`src/shim/backend_select.cpp`) and has no recorded Vulkan crash, and when `bgfx::init` and the backend
self-test pass. Otherwise the device runs OpenGL ES; every start logs one line
`render backend: ...: <reason>` that the diagnostics export and the graphics settings show.

A process that died during the Vulkan start leaves `<cache>/RenderBackend/vulkan-running.txt`; the next start
records it (`vulkan-crashed.txt`) and that device and driver run OpenGL ES until the driver changes or
Clear cache. A bgfx FATAL on Vulkan later in the run (`ProgramCache::fatal`: an error line in error.log, then
the abort) is recorded the same way (`Context::vulkan_failed`).

`VulkanDenied` lists drivers that pass every check above and still break the game, by driver build, each with the
evidence it logs (`kDeniedDrivers` in `src/shim/backend_select.cpp`): every Imagination (PowerVR) driver for now.
`auto` runs OpenGL ES there and logs the reason as a warning; `render_backend = vulkan` still takes Vulkan.

Developer keys in engine.ini: `render_backend_self_test = fail` (tests the fallback),
`missing_texture_look = neutral | loud` (stand-in for a failed texture; loud is the default on the host and in
debuggable builds), `vulkan_async_pipelines = 0` (create every Vulkan pipeline on the render thread),
`texture_compression`, `texture_cache_mb`.

Every depth + stencil target (the scene's depth, the sea's backdrop, the effects' self-test, the interface layers) has
the format of `render::DepthStencilFormat()` (`include/storm/render/depth_stencil.hpp`): D24S8, or D32FS8 on a Vulkan
device that renders to no D24S8 (Xclipse). The `bgfx renderer=` line logs it (`depth_stencil=`); the host checks
`renderer_bgfx.*.no_d24s8` run as such a device.

On Vulkan a multisampled target is resolved by its own render pass (`patches/bgfx-vulkan-resolve-in-pass.patch`),
the depth of the scaled scene too where the device has `VK_KHR_depth_stencil_resolve` or Vulkan 1.2 (render pass 2,
sample 0); otherwise bgfx resolves that depth in a pass of its own when the scene's frame buffer is left. The first
scaled scene logs which (`effects: the scaled scene's depth is resolved ...`, a warning for the pass of its own);
`STORM_VK_DEPTH_RESOLVE_IN_PASS=0` in the environment at start takes the pass of its own (its host check). OpenGL ES
blits the multisampled depth into the texture when the frame buffer is left.

The host checks `tests/renderer_bgfx` run every check that draws on OpenGL ES and again on Vulkan
(`renderer_bgfx.<check>.vulkan`, `STORM_CHECK_BACKEND=vulkan`; not run where the host has no Vulkan device).

## Threads

bgfx runs multithreaded: the game thread records a frame, `bgfx::frame()` (only in `Context::run_frame`)
hands it to the render thread, which executes the backend and presents after `bgfx::frame()` returned.

- No GL or EGL call outside bgfx; driver facts come from `shim::Context::gl_info()`.
- `bgfx::makeRef` without a release function only for data that lives as long as the program; otherwise
  `bgfx::copy` or a release callback (runs on the render thread). A pointer given to `bgfx::read` stays valid
  until `bgfx::frame()` has returned the frame number `bgfx::read` gave.
- bgfx's callbacks and the present function run on the render thread.
- `render_thread = 0` keeps bgfx on the game thread, `render_overlap = 0 / 1` forces the pacing choice
  (engine.ini or the debug graphics settings; for measurements).

## Building

The configure step builds the pin's host `shaderc` once per machine into `~/.cache/storm-deps/shaderc-<key>`
(`-DSTORM_SHADERC=<binary>` overrides it). Shader compiles go through `cmake/shaderc_cache.py`, a
content-addressed cache shared by all build directories (`~/.cache/storm-deps/shader-cache`;
`STORM_SHADER_CACHE` moves it, `STORM_NO_SHADER_CACHE="<reason>"` turns it off; statistics in
`<build dir>/shader-cache-stats.log`). Host:

```sh
cmake -S . -B build/host -G Ninja
ninja -C build/host engine
```

Android (`tools/android/ci_engine.sh` does this):

```sh
cmake -S . -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/android-arm64.cmake \
  -DSTORM_ANDROID_NDK=... -DSTORM_ANDROID_DEPS=...
ninja -C build/android engine
```

Texture tests: `-DSTORM_BGFX_BUILD_TESTS=ON` (`storm_texture_tests`, `storm_eac_encoder_tests`,
`storm_texture_file_tests`; the last also parses every DDS and TXX file of an installed game named by
`$STORM_GAME_DIR`, `storm_texture_file_tests --corpus`, and is reported as not run without one).

## Writing shaders

`storm_bgfx_embed_shaders` (`cmake/storm_shaders.cmake`) compiles every `shaders/*.sc` and `shaders/ports/*.sc`
at build time twice, for OpenGL ES (`300_es`) and Vulkan (`spirv`), and embeds the blobs (`<name>`,
`<name>_spv`); nothing is compiled at run time and no shader files ship in the APK.

- The SPIR-V build compiles the sources as HLSL: stage inputs and outputs (`v_*`, `a_*`, `gl_Position`, ...)
  exist in `main()` only, and globals cannot be written. Pass what a helper needs as parameters or use a
  macro expanded in `main()`.
- Everything must be highp: `cmake/shader_precision_check.py` fails the build otherwise.
- Matrices are D3D row-vector matrices uploaded as they are: multiply `vector * matrix`. vs_1_1 ports output
  D3D clip space and apply the shim's clip-space fix-up matrix themselves.
- `D3DCOLOR` vertex colours arrive as B,G,R,A bytes: swizzle with `D3DCOLOR_TO_RGBA`.

## Known limits

- 5 fixed-function texture stages (`kFfStages`).
- 1 user clip plane, emulated with `discard` in the fragment shader.
- `D3DTEXF_NONE` as a mip filter is not emulated (sampled like `D3DTEXF_POINT`).
- The game may use views `0..223` per frame (`kGameViewEnd`); later passes reuse the last view
  and the overflow is logged once.

## Effects (`src/effects`)

`effects::Host` owns the settings, feeds the `fx/` passes their per-frame inputs and answers the shim's questions
per draw. The notes below are the design of each part; the headers keep what each class owns.

### Lighting (`lighting.hpp`)

The shim asks `Lighting::prepare()` which program variant a fixed-function draw uses and which of its vertex-lighting
terms move to the fragment shader (`fx_lighting.sh`, `fx_coverage.sh`), then calls `submit()` before `bgfx::submit`.

- Lit draws (D3DRS_LIGHTING on, vertex normals, not pre-transformed) take `vs_ff_lit` and a lit family. The first
  enabled directional light is the sun (per pixel with `kLightingPerPixel`), the global ambient becomes the hemisphere
  (`kLightingHemisphere`), and the strongest point and spot lights up to the budget (4 / 8) are evaluated per pixel
  (`kLightingPoint`); the rest stay in the vertex sum, so no light is lost.
- Baked draws (lighting off, vertex normals: the location's static models with baked colours) get the dynamic lights
  the engine reports (carried lanterns) and a baked family; without any of these effects the legacy program draws
  them.
- Program families: each lit or baked program compiles only the blocks of `ff_fragment.sh` its family names (lamps, AO
  volume, town wall, rain). One table, `shaders/ff_programs.cmake`, defines the families and the specular, relief and
  sun shadow axes; CMake compiles the programs from it and generates their C++ table (`effects/ff_programs.hpp` in the
  build tree, `cmake/ff_program_table.cmake`). `ChooseProgram` gives a draw the program of the narrowest family that
  holds all it receives.
- Specialised programs: the same table lists stage setups (the D3D stage state the shim reads into `FfStages`) and
  programs compiled for one of them, the general program's source with the setup's operations and arguments as
  constants (`ff_stages.sh` `FF_SETUP`), so the stage interpreter folds away; indoors a baked one may be of an interior
  kind without the sun shadow, town wall and rain blocks. A draw whose stages equal a setup's takes its program (plain
  draws in `Device::choose_program`, lit and baked ones through `ChooseProgram`). `fx_ff_specialised=0` turns them all
  off.
- Flash lights of shots and explosions (`fx::FlashLights`) are per pixel only and cast no shadow; lit draws give them
  up to half of their point budget.
- Lamps (`lamp_shadows.hpp`, below) relight lit and baked draws into the game picture; a lit draw's D3D point lights
  that are such lamps leave its sums. Lit draws of an interior take their ambient from the room light.
- A game's own material takes its own programs (Materials, below).
- The material comes from the technique the renderer executes: `Animation*` is a character, `ShipSail*` / `ShipFlag`
  a sail (two-sided, translucent), `DLightModel` stone; lit draws without a technique are hulls, masts and deck
  objects.
- Relief (`kLightingNormalMap`): a draw whose stage 1 grey height texture, or else its stage 0 colour texture, has a
  derived normal map uses `FX_LIGHT_NORMALMAP` with the map at stage 14; lit draws perturb the normal, baked ones scale
  their colour. Characters take it with `Settings::character_relief`, faded out on skin and hair by the vertices' skin
  weight; their parallax is shallow and has no self-shadow. A relief map that follows stage 0 and has a height takes
  `FX_LIGHT_PARALLAX` with the height at stage 7.
- Sun shadows (`shadows.hpp`): lit draws into the game picture receive both layers; surfaces with their own static
  light (lightmapped and baked ground, decks) take a moving caster's shadow from the dynamic layer alone
  (`FfDraw::ground`). What casts is decided by the published shadow volumes (`shadow_casters.hpp`), not here.
- Alpha to coverage: alpha-tested, non-blended draws into a multisampled target use
  `BGFX_STATE_BLEND_ALPHA_TO_COVERAGE` and `fxCoverageAlpha()` instead of the discard.
- An alpha test that cannot fail is no alpha test (`Device::alpha_test_discards`): the game draws all location models
  with it, most with fully opaque textures and vertex alpha. When every stage alpha is 1 (opaque textures, checked
  when the game file is loaded; vertex, material and texture-factor alpha) the draw takes the opaque twin, without
  the discard or alpha to coverage, so early depth and a tile GPU's hidden-surface removal stay on.
- Wet surfaces (`fx/wetness.hpp`, `ff_wet.sh`): in the rain, outdoor lit and baked draws (not characters or sails) get
  wet where the visibility volume shows the sky above them; the wet gloss drives the highlights, so wet baked draws
  take the specular variant.
- Aerial perspective (`kLightingAerial`): every fixed-function draw whose fog is the game's exp fog takes fog mode 5
  (`fx_aerial.sh`); draws with a ported vertex shader keep the fixed-function fog (reported once), and without weather
  (interiors) the game's fog stays.
- Cube-mapped draws (`fs_ff_cube*`) have no lighting or coverage variant: they keep the legacy path, reported once.

### Materials (`render::EffectsSurfaceMaterial`, `shaders/fs_ff_pbr.sc`)

A game that ships its own texture maps (Caribbean Legend; core's game profile) draws every object of its models with a
material: its entry of the material file beside the model (`<model>.json`, `scene_data::ModelSidecar`, read by the
geometry), or the default one, matte and without metal, as the game's own engine draws an object without an entry. The
geometry sets it before the object's draws (`GEOM_SERVICE::SetSurface`) and clears it after the model's; keys the
material programs do not draw are named once a session (`materials: <key> is not drawn`), maps that cannot be loaded
once each.

- Programs: the families `pbr` and `pbr_lamps` of `shaders/ff_programs.cmake`, on every preset (their own entry, never
  chosen for another draw). They take the lit programs' light: the sun with its shadow, the point lights per pixel (at
  least four on «Низкая», where no vertex sum keeps the others), the place's lamps with theirs, the hemisphere (flat on
  «Низкая») and the AO and visibility volumes. Lambert diffuse and Cook-Torrance highlights (GGX, Smith-Schlick,
  Schlick's Fresnel at N.V; Karis 2013) fading out towards roughness 1 as the materials were authored; the ambient's
  reflection is the hemisphere along the reflected ray through Karis's analytic split-sum fit (2014). With
  `Settings::material_maps` («Средняя», «Высокая») the normal map leans the normal (`fxPerturbNormal`, a frame from the
  screen derivatives: the models have no tangents); without it, colour, metal, occlusion and emission only.
- Units: 0 the albedo (stage 0), 1 the occlusion on the second texture coordinates (stage 1: twice its texel, the
  modulate-2x stage the models were made for), 2 the emission (`fx::kEmissionStage`: stage 2's unit, which a material's
  draw does not read), 7 the rma map (R roughness, G metal, B occlusion of the ambient) and 14 the normal map: the units
  of the generated material and relief maps, which a game that ships its own never has. The technique's stage
  operations do not apply.
- Colour and data: the albedo and the emission are colour (squared: the targets' gamma 2.0); normal and rma maps are
  numbers (`render::DeclareMaterialMap`): their rebuilt mips are plain averages (`texture::TexelSpace::Data`, its own
  cache key), whatever the file's header says; the GPU format is the transcode tier's as for every texture.
- Light and exposure: the sun's and the ambient's colours of every lit draw, as linear light, times the scene light's
  scales (`render::EffectsSceneLight`, from the scene-light driver; 1 before it), plus the emission times the hour's
  factor; the sum times the exposure is compressed into the 8-bit target by the inverse of the composite's expansion
  (`fx_tonemap.sh` `fxCompressHdr`): up to a luminance of 3 the post chain gives it back, and on «Низкая» (no post
  chain) the compression is the tone curve.

### Lamps (`lamp_shadows.hpp`)

The location's lamps (the static lights of `lights.ini` at its locators, with their flicker) relight it per pixel,
each with its own shadow maps. Candles of one fixture are merged; every lamp lights within its shape's radius
(`fx/light_falloff.hpp`), and its own fixture (`fx::FindFixtures`) casts no shadow of it. The lamps nearest to the
camera, up to `Settings::lamp_shadows`, take a slot:

- a row of six cube faces in two depth atlases (D24, else D32F) holding the hardware depth of each face's perspective
  projection. The static atlas holds the still geometry around the lamp, drawn once when the lamp takes the slot (a
  few slots a frame); the dynamic one what moves (characters and other lit draws within the lamp's reach, on screen or
  not), drawn only in frames that have such a caster. The faces that hold a caster this frame are a frame uniform
  (`u_fxLampFaces`): receivers read the dynamic atlas only there;
- with `Settings::lamp_edges`, its edge maps: a filtered lookup read 9 (5x5) or 4 (3x3) taps of the still face
  although most of them answer "every texel passes" or "none does". The maps are an array of the faces' format, 6 x
  128 by rows x 256 texels, one layer per filter of the preset (the 5x5's with `lamp_soft`, then the 3x3's). A
  slot's row holds faces 0..2 over the first half of its height and 3..5 over the second; per 2 x 2 texels of a
  face (a cell) a pair of texels side by side: the nearest and the farthest depth of the texels the layer's filter
  reads for a point whose nearest texel corner is in the cell (the cell's own and three more on every side for the
  5x5, two for the 3x3), each two steps of a 24-bit depth on the safe side (a depth read as a float and stored
  again may round by one each way). One linearly filtered compare between the pair tells a receiver that every
  texel of its filter passes (1: the result is 1), none (0) or some (0.5: the filter runs); a filter whose taps all
  pass returns exactly 1 as well, so the frames are those without the maps. The maps are drawn from the still atlas
  (`fs_ff_lamp_edges.sc`) after a slot's faces, at most as many slots a frame as faces are drawn; a light that
  moves gets its when it stands still; until then `u_fxLampFaces[slot].y` is 0 and the filters run. They have no
  texture unit of their own (the lit programs use all sixteen): they take the sun map's in the draws that do not
  read the sun map, the others filter. A device is asked once whether a compare between two texels is their mean
  (`check_pair_read`): where it is not, or it has no such array, no maps are drawn and the log says so;
- its number in the lamp grid, a small 3D texture of which slots reach which cell of the place.

A lamp counts only as far as its light can show (`fx::LampReach`, `fx::kLampNegligible`). The strongest lamp of a
pixel takes the 5x5 or 3x3 filter, up to `Settings::lamp_filtered` lamps take a filter, the rest one hardware-filtered
tap; at most `Settings::lamp_per_pixel` grid lamps light one pixel. A lamp fades in once its faces are drawn and out
before its slot goes to another. The last `Settings::lamp_moving` slots are for lights that move, which only add light
(no bake holds theirs): carried lanterns and torches, whose still faces are redrawn when they move, and the ships'
lanterns, whose faces are in their ship's space. A lamp without a slot lights nothing per pixel: no light without its
shadow. Interiors also get the room light (`fx::RoomLight`, a 3D texture of the baked colours around every point) as
the ambient of the lit models. Positions are absolute world. The textures are always bound (1x1 open ones when there
is nothing), since GL refuses a draw that leaves a shadow or 3D sampler on a unit of another type.

### Sun shadows (`shadows.hpp`)

Two layers on one projection around the camera's focus, both drawn in pre-game views so receivers of the same frame
read them. The static layer holds the place's still geometry in tiles with the holes its textures' alpha cuts; it is
redrawn when the shadowed region leaves its guard band, the sun moves by `fx::kSunStepDegrees`, or the geometry, a
cut-out texture, the quality or the device changes, and costs nothing otherwise. The dynamic layer holds the moving
casters in every frame that has any; it is published as a shadow volume and the place's own draws are left out of it.
Lit draws and the towns' relit walls read both layers; surfaces that hold their own static light read only the
dynamic one, and only where a moving caster's shadow can reach (`shaded()`). The map is an array at stage 15. A sun at
or below the horizon gives no shadows; indoors no sun map is made.

### Shadow casters (`shadow_casters.hpp`, `include/storm/render/shadow_casters.hpp`)

What casts is decided by the lights, not by the camera. The lights publish the frame's shadow volumes (the sun's two
layers, the faces of the lamps that hold a slot): where a caster must be to reach the light's map, and how it is drawn
into it. The shim's caster entry (`Device::draw_caster`) offers every candidate draw into the game picture, drawn by
the camera or rejected by it (`render::ShadowCasterOnly`), and `submit()` puts it into each volume it meets, once. No
light keeps a caster list of its own.

- Static and dynamic: a volume names the owner whose still geometry its light keeps in a static layer. The static
  layer takes only that owner's still draws, the dynamic layer the lit draws but those, so the two never hold the same
  triangle.
- Spaces: a volume is in the absolute world minus its `origin`. The sun uses the device's camera-relative world, so its
  casters keep the game's world matrix; the lamps are absolute.
- With the light given, a caster is taken only when its shadow can cross the camera's view.
- The game's traversal adds what the shim cannot see: geometry the camera culls is offered through
  `ShadowCasterReach*` and drawn inside a `ShadowCasterOnly` scope (same draws, shadow volumes only); skinned nodes give
  their sphere (`ShadowCasterBounds`); still geometry is marked (`ShadowCasterStill`). With no shadow volume in the
  frame the queries answer false and cost nothing.

### Casters (`casters.hpp`)

One rule for every pass that renders depth instead of colour (the sun layers, the lamp atlases, the AO bake): what the
colour pass's alpha test discards is cut out of the caster too (the product of the texture alphas the material's alpha
chain multiplies, against the draw's alpha reference; vertex, material and texture-factor alpha are not part of the
cut, so a fading character casts until it is gone); a blended material that does not write depth (glass, water,
particles) casts nothing; a blended material that writes depth (sails) casts like any surface. The caster programs
(`vs_ff_caster*`, `fs_ff_caster*`, `fs_ff_lamp_caster*`) live there too; none writes its depth, so early depth testing
stays on.

### Town light (`town_light.hpp`, `fx/town_light.hpp`, `fx/sun_relight.hpp`)

On Средняя and Высокая the hour's sun relights the towns' lightmaps as a light ratio map per lightmap, which the
lightmapped draws multiply the game's own lightmap by (`ff_stages.sh ffTownRatio`, one more texture fetch): the
paint, its painted shadows and grain stay the game's at full resolution. Once per lightmap set the game shows (it swaps
sets by the hour) the direction the set was baked with is fitted (`fx::FitBakedSun`), then the set's tone
(`fx::CalibrateTownLight`) and the walls' (`fx::CalibrateTownWalls`), read from the place's cache file or made on the
bake thread. A set whose sun the fit cannot tell is not relit (dividing by a wrong sun's light would double the
shadows); nothing is fitted at night. The place's still geometry is drawn into the place's sun map along the bake's
direction once and again whenever the hour's sun moves by `fx::kSunStepDegrees`; each ratio map is then composed again
on the GPU, one a frame. The ratio's texels are about 0.2 m apart (`fx::SunRelight::RatioSize`), under 1 MB for a
town. The ratio fades in over `shim::kTextureFadeMs` and out when the town light is switched off; until it is there,
and on Низкая, the game's lightmaps are drawn as they are.

### Relief maps (`normal_maps.hpp`, `relief_generator.hpp`)

Each loaded texture reports its name and game file (`SetEffectsTextureSource`); `fx::ReliefClassifier` decides from
the path (and, for "bump" names, the pixels) whether it gets relief. Colour textures of the relief classes get their
maps made by PBRify's models in the game process the first time a draw that reads the map draws them; grey height
("bump") textures get none. Gloss maps follow the same path for gloss classes whose relief class has no height. Maps
are EAC RG11 with mips, read on a texture worker and uploaded when a draw first needs them (a smaller preset drops top
mips); with the parallax, cavity or gloss on, the material texture (R height, G gloss) is uploaded beside them. The
relief fades in over `kReliefFade`. A new scene, or settings that change which draws read the maps, forget which
textures were drawn and drop the generator's queue; one line per scene says what was looked up.

None of this, nor the neural ×2 textures, exists for a game that ships its own texture maps (`render::EffectsGame`, from
core's game profile): its «Обработка текстур» options are left off, and the renderer reports `texture_processing:
false` in the interface's "renderCaps" channel so that the settings screen does not offer them.

The generator (`ReliefGenerator`) runs one worker thread at nice 10. Only a texture the current scene draws with a draw
that reads its map is requested; requests are reselected by recent player distance before each job, alternating with
×2 upscales. The device follows the game: the CPU while the player sees the scene, the GPU only while no scene is drawn
or in a place's loading window (a GPU job stops at its next submission when the scene shows; the GPU networks are loaded
one by one only then too). CPU work takes `fx::CpuAdmission`'s share, asked again for every tile: all cores behind a
loading picture, in play a quarter of them (at most two, one when the device warms), so a texture begun behind the
picture goes on with the gameplay share once it lifts. The renderer's explicit hold and a device about to throttle pause the work. The models are loaded at the
first job and released after 10 s idle. A map is made once per game, method and map size.

### Neural ×2 textures (`upscale.hpp`)

Every texture `fx::ClassifyUpscale` gives a target gets an entry; its ×2 costs four times its memory, so only textures
drawn near the camera are resident at ×2. Each draw reports its distance; twice a second `fx::PlanUpscaleResidency`
picks, nearest first, those within `fx::kUpscaleNear` (a resident one until `fx::kUpscaleFar`) while they fit the
budget (`fx::UpscaleBudgetBytes` of the device's RAM). A chosen ×2 is read from the upscale directory on a texture
worker, or requested from the relief generator. Draws bind the ×2 in place of the texture; nothing else changes.

### Scene reveal (`scene_reveal.hpp`)

A location, the sea, the world map and the title load behind the loading picture in the stock loading transaction
(core `PrepareLoadingScene`); before each of its frames `render::HoldSceneReveal` asks whether the picture may lift.
`SceneReveal` holds one load: `Preparing` while a scene texture has no GPU copy, a first shadow pass is due, a
settings switch or the scene's Vulkan pipelines are in progress, or fewer than three full frames followed;
`Failed` while a mandatory resource failed (a location then stays behind the picture, with Cancel); `Ready` reveals.
Under a presented opaque cover a frame with something pending draws its scene for its resources only (the draws are
discarded); a full frame is drawn into the target it began with and presented. Once the mandatory part is ready the
quality products in progress (relief and gloss maps, ×2, the place's probes, the town light) hold it at most
`kSceneProductBudgetMs`; the rest fades in after the reveal. The title holds at most `kTitleRevealBudgetMs` from its
first loading frame and then `Expired` reveals it with what it has. A texture upload without progress for
`kTextureStallMs` stops holding (an ERROR names it); a load without any change for `kSlowMs` is an ERROR naming what
is pending. Time while the transaction is suspended (the app in the background) counts towards none of these.
`scene_reveal.cpp` samples the producers and forwards the shim's frame events.

## Diagnostics

A player who reports nothing pays for nothing measurable. Always on: the summaries every 30 s (`log.hpp`
`kSummaryPeriod`): `frame pacing:`, `frame work:`, `memory:`, `gpu passes:` with the frame's GPU time and
`effects: thermal status`, and, when it changed, the state of the background texture work (`effects relief:`,
`effects gloss:`, `effects upscale:`, `texture fades:`, `texture realize (play):`); events (a location's readiness, a
load, a setting), errors and the stall watchdog; once a session, when the scene after the title is shown, `memory at
start` (anonymous and file pages at each phase of the start and the peaks up to it: where a start's peak happened).

Detailed diagnostics (`log.hpp` `DetailedDiagnostics`) are the About screen's «Подробная диагностика», off by default,
read at the start and at each return to the foreground (`renderer.cpp`; engine.ini `diagnostics_detailed` off
Android); `diagnostics: detailed on|off` logs each change. They add the per-pass GPU and render-thread times of
`gpu passes:` (sampled frames); the `render passes:` line (Vulkan); the parallax census (`effects parallax:` per frame and per texture, its coverage table)
and its work on every textured draw; and a texture's own lines (its relief map or request, a grey bump texture, a map
without height, its ×2 request, a character's skin colour), whose counts per scene the always-on
`effects: relief generation in` and `effects upscale:` lines keep; and a model's far LOD line, whose sums the always-on
`far LOD: N models` line keeps once the place's LODs are all installed.

## Shim (`src/shim`)

### Frame pacing (`frame_pacing.hpp`)

Android Frame Pacing (Swappy) presents each frame at a vsync on either backend: Swappy GL in place of
`eglSwapBuffers` (`patches/bgfx-swap-present-hook.patch`), Swappy Vk in place of `vkQueuePresentKHR`
(`patches/bgfx-vk-present-hook.patch`). The rate is exactly the «Частота кадров» setting at a whole number of display
refreshes (`swap_interval.hpp`); Swappy's auto swap interval and auto pipeline are off, so late frames or heat never
make it pace slower, and a frame that misses its slot is presented at the next refresh. About a second after a choice
the display's refresh rate is compared with the chosen mode and a new choice is made if the display stayed in another
mode (logged as an error). The ADPF hint session targets the swap interval with the game thread's CPU time per frame;
Swappy's CPU affinity is off. Without Swappy an error says why and the present is unpaced at the display's rate. Every
30 s a `frame pacing:` line logs the frame intervals (percentiles, a histogram in refreshes) and the mean present wait. The renderer
bench of a debuggable build (renderer/README.md, "Bench") is the one exception: while it runs, Swappy is set to the
display's fastest mode and the presents bypass it (logged), and each frame's GPU time is kept for it.

### GPU passes (`gpu_passes.hpp`)

Every 30 s, after `frame work:`, a `gpu passes:` line gives the frame's GPU time (bgfx's frame query, every frame,
each result once) as its mean and maximum, on any backend with timer queries (none: no line). With detailed
diagnostics it also says which pass of the frame takes the GPU's and the render thread's time: per pass (a view name;
views of one name added up) the GPU time and the render thread's submit time are a mean and maximum over sampled
frames, a pass absent from one counting 0 there. bgfx's per-view queries end the render pass at every view, which on a
tile GPU stores and reloads the targets (up to 2.4 ms more on an Adreno 830 frame), so they run in one presented frame
of 30 (`kSampleFrames`) and only with detailed diagnostics, and the line gives those frames' own GPU time beside all
frames' (the difference is the sampling's cost). A view's GPU time is the span between two bottom-of-pipe timestamps
around its commands: the GPU's work on it after everything before it finished, including what it waits for inside the
frame.

With detailed diagnostics on Vulkan a `render passes:` line follows every 30 s (`PassTraffic`,
`patches/bgfx-vulkan-traffic-counters.patch`): per frame the render passes, those of the scene and the views they run
through, and the megabytes (10^6 bytes) their load ops read, their store ops write and their resolves write, colour and
depth apart; per pass name (the view that began it; bgfx's depth resolve and resolve copies by their size) the same
with its draws (opaque / alpha-tested / blended / depth only); and every cut of the scene's pass with its cause
(another target, another rect, a blit, the profiler) and the pass that came next. With `STORM_VK_PIPELINE_STATISTICS=1` in the
environment at start (host measurements; not on the phones) each pass name also gets its fragment shader invocations
per pixel of its render area, by the same classes. `tests/renderer_bgfx/scene_passes_check.cpp` keeps a game
frame at a render scale in one scene pass with the traffic worked out by hand.

### Texture realize budget (`realize_budget.hpp`)

A texture's first draw makes its GPU copy on the game thread (cache key, file read, `bgfx::createTexture`), up to tens
of milliseconds on a phone. Behind a loading screen nothing is budgeted. In play the 3D scene draws make textures only
while the frame has spent less than `kFrameMs` on them, and always at least one: a draw past the budget whose texture
has no copy yet is left out of the frame (the frame counts as incomplete), and a finished transcode past the budget is
swapped in later. Everything is counted (`tex_realizes`, `tex_realize_ms`, `tex_realize_waits`) and logged every 30 s
(`kSummaryPeriod`) while it happens (`texture realize (play): ...`).

### Texture fades (`texture_fade.hpp`)

A texture swapped in front of the player (a finished transcode for its interim copy, the neural ×2 and back) fades over
`kTextureFadeMs` instead of popping. Each store remembers the handle its draws bound last; when it changes while the
store was on screen a moment before, the previous handle is bound beside the new one and the fixed-function fragment
shader mixes the two by the stage's share: no extra pass, one more fetch while it fades. The previous handle goes to a
unit the draw leaves free; a draw with none free, a shader port and cube or volume stages swap at once (counted). A
replaced handle is retired, not destroyed, until no fade can read it (`kTextureRetireMs`).

### View schedule (`view_schedule.hpp`)

The shim opens a view whenever the game changes its render target. A pass into another target in the middle of the
scene (the sea's cube faces, the hulls' wakes, the sea's depth capture) would cut the scene's pass in two, and on
a tile GPU every cut stores and reloads the multisampled scene. `ViewSchedule` records what every view writes and
reads and its lane (Target, Scene, Post, Picture) and orders the views by lane where no dependency forbids it
(`bgfx::setViewOrder`; only the execution order changes). A view of another lane that still runs between two scene
views cuts the scene and is logged once per pair.

### Depth pre-pass (`fx_depth_prepass`)

With `Settings::depth_prepass` (on «Средняя» and «Высокая»; the bench's «pre-pass 0» and «pre-pass 1» rows measure it) the opaque 3D draws
of the scene lay their depth before any colour is drawn, so the lit shader runs once a pixel instead of once for every
layer the camera looks through. `Device::submit_draw` submits the colour draw, then from its bindings a copy into the
view's twin: a view on the same frame buffer that runs right before it (`ViewSchedule::twin`), opened at the view's
first copy, and that takes the view's clear. The copy has the colour draw's vertex shader and a fragment shader that
writes nothing (`fs_ff_depth*`, one per vertex output set), its state without the colour writes; the colour draw keeps
its own (less-or-equal, depth write) and passes only where its depth is the nearest one.

- Copied: perspective draws into the scene before its boundary, with an opaque twin (nothing discards: no alpha test
  that can fail, no clip plane), no blending, all colour channels, depth test less-or-equal with depth write, no
  stencil, no depth or slope bias, no vertex shader port, a vertex shader with a depth-only partner (not the skinned
  ones), triangles. Every other draw draws as without the pre-pass against a depth that already holds the nearest
  copied surface. That gives the same picture when nearer-or-equal wins and the draw either replaces the colour or
  writes no depth. A draw with a stencil or another depth test would see the later depth: the draws after it go to a
  new pair of views. A draw that writes depth without replacing the colour (blended, channels masked; source-alpha
  blending with an alpha of 1 everywhere replaces it) shows what was drawn before it, which copies of later draws
  behind it would already have hidden: it starts a pair of its own. The game's skies, sun, sails, flags, ropes and
  grass are such draws, and the draws before each of them do not see the depth of those after it.
- Equal depth: the two programs share their vertex shader, whose position is decorated Invariant
  (`patches/bgfx-shaderc-invariant-position.patch`).
- Uniforms: the copy carries only the transform and `u_ffProj` / `u_ffVs`, all its position reads, sent by the twin's
  own draws; the colour draw's uniforms stay with it, so the twin never changes a value the view's draws rely on.
- Vulkan: a copy carries `STORM_STATE_PIPELINE_PAIRED` and is left out while the pipeline of its colour draw is being
  created (`patches/bgfx-vulkan-pipeline-cache.patch`): a copy alone would lay depth where no colour follows.
- Cost: the opaque geometry is drawn twice (vertex work, a second submission per draw) for the fragments it saves.
  The `render passes:` line counts the copies as depth-only draws.

### Vulkan pipeline sets (`pipeline_sets.hpp`)

bgfx reports every pipeline it creates with its recipe (`patches/bgfx-vulkan-pipeline-cache.patch`). Recipes are
recorded per scene in one file next to the device's pipeline cache, for one build of the engine (a recipe names its
render pass, which another build may never make: a file of another build is removed and the scenes record again);
when a scene begins its recipes are prewarmed on the pipeline threads while it loads, and the reveal waits (`HoldSceneReveal`: with the scene's other resources, or at
most `kRevealBound` for a scene the reveal does not know) until nothing is pending. Only 3D scene draws may wait for
a pipeline; interface, text, fader and pre-transformed draws take theirs at once. A scene draw left out for an
unpredicted pipeline is logged and learned for the next time; the first `kMeasureWindow` of play after a reveal is
measured and logged (a lost surface ends it: time in the background is no frame). A lost surface also starts the
reveal's wait again on return: a stay in the background is no wait for pipelines.
