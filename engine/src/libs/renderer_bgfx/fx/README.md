# Graphics effects package for the bgfx renderer

Post-processing (tone curve, colour grades, bloom, sun shafts, lens flare, dither, SMAA, the
FSR 1 upscale and sharpening), sea improvements and per-pixel lighting for the fixed-function emulator (sun, point
and spot lights, sun shadows, interior ambient-occlusion volume, relief from derived normal maps, aerial
perspective, wet surfaces). The package depends on bgfx and GLM (the engine's `storm_glm.h`, vector maths) and takes plain structs; `src/effects`
(`effects::Host`) owns the settings, feeds per-frame inputs and calls it. Licences: [THIRD_PARTY.md](THIRD_PARTY.md).

## Presets and options

The player chooses Low / Medium / High (`Settings::FromPreset`, `include/storm/fx/settings.hpp`). Every
preset renders the 3D scene at an internal height (900 / 1000 / 1000 lines, `fx_render_height`, never below half
the display) on taller displays and upscales it with FSR 1; interfaces are drawn at the display size. Low is
the legacy image with the upscale only; Medium and High add the effects listed in `Settings::FromPreset`.
What the phone makes from the game's textures is the player's apart from the preset (Texture processing,
`TextureProcessing`, `ApplyTextureProcessing`): the normal maps, the height maps (parallax, cavity; made with the
normal maps and drawn through them) and the neural ×2 upscale, engine.ini or app keys `texture_normal_maps`,
`texture_height_maps`, `texture_upscale` (a key left out is off); the preset decides only how an effect that is on
looks. Texture quality
(`TextureQuality`, `texture_quality`) is the resolution alone.
Where MSAA is unavailable `ConstrainSettings` replaces it with SMAA 1x (logged).

Single options are `fx_*` keys of engine.ini or `STORM_FX_OPTIONS` (parsed in `src/effects/effects.cpp`), for
development. Useful ones:

- `fx_gpu_profile=1`: the `effects profile:` line every 30 frames: GPU time of every frame, per view from the frames the `gpu passes:` line samples with detailed diagnostics (`../README.md`), and the effects' draw counts; off by default;
- `fx_wet_force`: forces a rain wetness for review;
- `fx_character_relief` (0/1/2), `fx_character_relief_strength`, `fx_character_parallax_depth`,
  `fx_character_skin_colour`: relief review (debuggable builds);
- `fx_light_linear`: linear-light sums (in no preset: the game's lights were tuned in gamma space).

Parallax exclusions: the shipped list is `src/parallax_exclusions.inc` (`exclude <name>` / `include <name>` lines).
The log line `effects parallax: texture ...` (detailed diagnostics) says why a texture has no parallax.

## Build

Built with the renderer. Shaders are compiled offline with the pinned shaderc into embedded blobs for OpenGL ES
(`STORM_FX_SHADER_PROFILE`, default `300_es`) and Vulkan (SPIR-V); `shaders/shaders.cmake` lists every variant. Unit
checks of the CPU side (`storm_fx_core_check` and the `fx_*` ctests) are host checks (`tests/renderer_bgfx`, label `renderer`).

## Rules

- No silent fallbacks: `ConstrainSettings()` is the only place a request is lowered, before rendering, and every
  change comes back as a line of text. A call that still cannot be honoured is refused and reported once as an error
  of the renderer's log (`storm/render/log.hpp`).
- Stage 15 belongs to the shadow map, stage 14 to the derived normal map (and `SeaDepth::Texture()` in the sea
  shader), stage 7 to the gloss / relief material.
- Normal, height and gloss maps are made on the device from the player's own textures (src/libs/relief); never ship
  or commit them.

## Design notes

### Post chain (`post_chain.hpp`)

```
scene ─► [wobble] ─┬─────────────────────────────► composite ─► [SMAA] ─► [EASU] ─► [RCAS] ─► target
                   └► bright pass ► down… ► up… ──┘ (bloom,     edges,     (scene     (target
                                                     + shafts)  weights,    → target   size)
                                                                blend       size)
```

Everything before EASU runs at the scene's internal size; EASU runs when the scene is smaller than the target
(`Settings::render_height`), RCAS and the lens flare at the target size. With `Settings::PostIsIdentity()` the chain is
a single copy; passes that are off cost nothing. Everything runs in gamma-encoded RGBA8 targets.

### Sky (`sky.hpp`, `sky_model.hpp`)

The scene environment (`src/libs/scene_environment`) is turned into a sky state: a 64 x 32 panorama of single
scattering (Rayleigh and Mie) around the sun, its horizon and dome averages, rebuilt on a thread of its own when the
sun, the light or the weather move. The panorama drives the scene grade, the hemisphere ambient and the aerial fog;
the environment's sky radiance scales it, so a game's data can make it brighter, warmer or darker. For a game
without sky textures (`render::DrawSky`) it is also the visible sky: one triangle at the far plane behind what the
scene has drawn, the panorama by direction, the sun's and the moon's discs, stars on a hashed grid of cells by night,
the clouds and a haze band at the horizon; in the scene before its geometry and in the sea's reflection.

The clouds need no texture of the game's: a layer at 1500 m seen from below, a fractal sum of one tiling octave of
gradient noise (`CloudNoise`, 128² R8 with mips, 21 KB) turned and doubled per octave, kept where it rises above a
threshold the environment's cover lowers (the coverage remap of Schneider 2015). The rain clouds grow over the same
noise from its high values down as their cover rises, grey and even when it is full. The wind's drift moves the layer.
The sky's panorama above and at the horizon lights them (its colours carry the twilight's tint), and the direct light
(the sun, or the moon at night) where the layer thins towards it: a second lookup a step towards the light, a
directional derivative (Quílez), with a silver lining round the light. `Settings::cloud_octaves`: «Низкая» 2 octaves
and no lookup towards the light, «Средняя» 3, «Высокая» 4. A fog with a height
layer of its own reaches the draws at the eye's density and the aerial fog lays it out with height
(`ComputeAerialFogHaze`).

### Light falloff (`light_falloff.hpp`)

Every light of the per-pixel lighting (lamps, torches, candles, lanterns, the game's D3D point lights) has one falloff.
The game describes a light by D3D9 attenuation constants tuned by eye in gamma space; in linear light most of them burn
white next to the flame and are gone a few metres away. Each is turned once into a windowed inverse square with a soft
core:

    linear light at distance d = colour² · intensity · window(d / radius) / (d² + core²),
    window(x) = (1 - x⁴)² for x < 1, else 0

`radius` is `reach` times the distance at which the game's light falls to `kGameLightFloor`; `intensity` makes the
light equal `level` times the game's at `kMatchShare` of that distance; `core` grows until the light next to the flame
is at most `level` times the game's there. Without `FX_LIGHT_LINEAR` the shaders take the square root, so a light looks
the same either way. `fxLightFalloff` (`fx_lighting.sh`) mirrors it.

### Place probes and visibility (`place_probes.hpp`, `visibility.hpp`)

One set of rays per place gives everything its still geometry contributes that does not depend on the hour. From the
centre of every cell of one grid, rays run to the nearest thing the place draws (`fx::Occluders`, its textures' holes
cut): `kProbeRays` over the sphere where occlusion is asked for (interiors), `kSkyRays` over the sky elsewhere. From
them: the visibility volume, the near sky the game's town lightmaps were painted with (`TexelSky`), and the
ambient-occlusion lobes of interiors. The cache is one file per place named by the key of everything the rays depend
on (`PlaceKey`); its sections carry their own keys (the probes, the town light's sets).

The visibility volume says how open to the sky every point is: per cell the cosine-weighted share of the upper
hemisphere that reaches past the geometry (rays to the place's bounds, so a high hall reads closed), and the share of
directions more than 15° up that reach out (whether a sun can get there at all). A cell inside the geometry takes the
most closed of its open neighbours, so a wall does not carry the outside into the room.

### Ray bakes (`bakes.hpp`)

The place's probes, the town light and the far LODs bake on threads of their own, apart from the texture workers, so a
long or paused bake never holds up a transcode; cache reads run first on a load thread, so a cached result never waits
behind a running bake. A bake is cancelled when its owner drops its ticket and by `ShutdownBakes` (a running one stops
at its next piece). Every piece takes a permit of the shared CPU admission (`cpu_admission.hpp`): behind a loading
picture all of them, in play at most two threads, kept off the fastest cores.

### Town light (`town_light.hpp`, `sun_relight.hpp`, `sun_fit.hpp`)

The game's lightmaps were baked by hand for a near-noon sun whatever the hour. Fitted on FortFrance and SentJons
ground, their luminance is about 0.04 + 0.14 sun + 0.26 sky (R² 0.46; sun alone 0.38), the rest texture-like paint.
The town light rasterizes each lightmap's texels (with a gutter for bilinear filtering), reads their sky from the
place's probes and the bake's sun by rays, fits how the lightmap was lit (`CalibrateTownLight`), and composes the
ratio for the hour's sun on the GPU.

`SunRelight` does that compose: no rays, no CPU compose and no copy of the game's lightmap. The place's sun map is one
D16 map of the still geometry along the sun. The bake's sun is read once per lightmap through the same map and filter,
so what the map cannot resolve (a railing thinner than its texel) is missed by both suns and its painted shadow stays.
Each compose pass writes, per ratio texel, (a + b sun + c sky) now over the same with the bake's sun, log-encoded: a
texel e (0..255) holds 3^((e - 127) / 127), 127 exactly 1, steps of 0.9 %, filtered in the log. The ratio map is far
coarser than the paint (about 0.2 m a texel, under 1 MB for a town). What does not depend on the hour (positions,
normals, sky) is packed once per lightmap into 10 bytes a texel on the CPU and uploaded only for a pass.

### Parallax trust (`parallax_trust.hpp`)

PBRify's height is right for plain wood and stone and wrong where the picture is a window, a door's glass or a sign:
the parallax shifts a pane against its frame. A per-texel mask made with the relief takes the parallax away only there
(normals stay everywhere). Two classical detectors on the colour and the height at the map's size, both on blobs
rather than thin joints: a recess (deeper than its surroundings and darker than its neighbourhood, compact enough to be
a window) and glass (dark and uniform over a blob). What they find is grown and feathered so the parallax fades out
over a few texels. The mask is part of the relief method (`kHeightMapVersion`) and stored in the material's height
(where the trust is t the height is 1 - t (1 - h)); the file also keeps the share of trusted texels, by which a texture
with almost none gets no parallax (`kParallaxMinCoverage`).

### Gloss maps (`gloss_map.hpp`)

The gloss is PBRify's roughness network's, run with the normal and height networks and corrected for this game
(`GlossOfRoughness`): the GGX perceptual roughness becomes a Blinn-Phong power 2^(3 + 4.5 g); texels below their
surroundings in the height keep a quarter of it; dark texels lose it except on metal (the model reads baked dirt as
smooth); metal texels by colour (`GlossMetalMask`) keep at least half and tint their highlight; matte classes are
capped. Which textures get a gloss map and which are metal is the path's class (`ClassifyGlossName`). The maps are
cached on the device, never shipped.

### Ship wakes (`sea_wakes.hpp`, `wake_trail.hpp`)

`Settings::sea_wakes` (Medium and High). What a hull leaves is churned water, drawn as foam:

- **The trail astern.** `WakeTrails` keeps where each stern has been: a point every `kSpacing` metres of its way,
  with the game time it was laid and how hard the hull churned the water there (`Strength`: nothing under
  `kSlowest`, drifting or going astern; whitest from `kFull`). Each frame the track becomes a ribbon, two vertices a
  point, as wide as the band has spread by its age. The pixel shader (`fs_fx_wake.sc`, `fx_wake_trail.sh`) turns age
  into foam (dense for the first seconds, then `kFoamLife` seconds to 1/e, gathering along the band's edges and
  breaking into patches) and into the calmed band that outlasts it (`kLife`). Everything is a function of where the
  water is and how old it is, so a hull that slows lays less, a hull at rest lays nothing, and what was laid fades
  where it lies; nothing follows the hull.
- **The bow wave.** A second ribbon around the waterline (the hull's stations) from ahead of the stem to astern of
  the stern, the water at each section as old as the distance from the stem over the hull's speed: white where the
  stem cuts the water, a streak along the hull, by the hull's speed and length (`BowStrength`).

No band begins or ends at an edge: each fades in and out over `kRamp` metres along its length and over metres across,
more than two texels of the target, so its front moves with the hull and not texel by texel. The waterline the bands
are drawn around follows the reported one within `kSettle` seconds: the game solves it ten times a second, and drawn
as reported the ends of the bands moved in those steps with the hull's pitch.

Both go into one RGBA8 target of `kSize` texels over `kExtent` metres around the camera (R foam, G calm), cleared and
redrawn every frame and blended as the maximum, so crossing wakes do not add up. It exists only once a hull makes a
wake in its reach. The Sea2 pixel shader reads it once (`sea_wakes.sh`): the calm fades the bump detail, the foam
becomes pale aerated water under a lace of the game's foam texture read at two scales (the second a whole multiple of
the first: `seaFoamFine`), lit by the sky above and by the weather's sun (its colour by its height, nothing while clouds hide it or at
night), times an albedo (`seaFoamLight`: dark at night, never brighter than an overcast sky). Towards the target's border the wake fades out by the distance
from the camera (`seaWakeReach`), so a trail enters and leaves unseen. The foam's patches come from a pattern fixed in the water (`fxWakeClump`,
periodic over `kPatternTile`). Ages run on the sea's game time: a pause holds them, time acceleration speeds them.
The trail of a hull that is no longer reported ends where it was last laid and ages out; a jump of more than `kJump`
metres starts a trail anew; after frames without the sea, trails of hulls that are not in the first frame are dropped
(another scene). The game's own foam ribbons (`ShipTracks`) are drawn over the sea as before.
