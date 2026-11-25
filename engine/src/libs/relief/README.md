# Relief maps from PBRify

Normal maps, material textures (R height for parallax and cavity, G gloss), gloss maps and the neural ×2
textures are made from the player's own game files with PBRify's networks through ncnn (BSD-3-Clause,
`cmake/ncnn.cmake`), on the GPU (Vulkan compute) or the CPU: in the game its state decides each texture's device
(`device_choice.hpp`). Nothing derived is shipped. Which textures get relief: fx `ClassifyTextureName`; map formats and keys:
`fx/relief_model.hpp`, `fx/relief_store.hpp`.

In the game the maps are made lazily in the background (renderer_bgfx `src/effects/relief_generator.hpp`) and
kept as a cache in `fs::GetCachePath()/relief` (on Android in the app's cache dir: Clear cache removes them and
they are made again). A map's key holds the method and model digests, so a changed method never reuses old maps.

## The cache

The directory has no size limit: it holds at most one map per texture of the game the player has drawn with an
effect on, and the game bounds that. Its 5,823 textures give 2,252 relief maps (512 texels, with their material
texture: 1,275 MB) and 1,993 gloss maps (256 texels: 150 MB), 1.4 GB in all, and twice the relief maps' share for a
player who draws the whole game both with and without «Параллакс» (the maps without the height have keys of their
own). A session makes only the maps of the places it visits. The maps of another method or file layout are never
read again; `fx::SweepOutdatedMaps` deletes them when the game starts (one line in the log with their count) and
stamps the directory with the current version, so updates do not pile them up.

## Sizes

The networks run at the sizes of the maps they make: the relief map's (fx `kReliefMapSize`, 512) and the gloss's
(fx `kGlossMapSize`, 256), or the texture's own when it is smaller. Their input is box-filtered down to that size from
the texture's level of up to twice the map's size, or, for a texture up to the map's size, from its ×2 (fx
`ReliefMapsFromUpscale`: no DXT block steps in the height). Their cost grows with the texels they read (a 1024² input
takes four times a 512²'s), so they never run larger than the map. High uploads the maps whole, Medium from their
second mip.

## Memory on the CPU

The CPU networks of a session hold at most `kCpuNetworkBytes` (160 MB, `backend.hpp`) at once and return all of it
after each texture: one bounded allocator (`CpuMemory`, ncnn's allocator hook) instead of ncnn's own per-network pools,
which kept every block until the session ended (a 1024² ×2 took 440 MB, a relief texture 1.3..1.7 GB). Each network's
tiles are the largest that fit the bound by the bytes a texel it measured on this CPU (`cpu_tiles.hpp`; about 2.5 KB
with fp32 storage, half with ARMv8.2 fp16 storage); the upscaler's own tiles follow (`Network::TileCeiling`), so they are
not cut again. An allocation past the bound fails the run (ncnn's -100: a smaller tile, logged), and a texture that fits
no tile is not made (logged, unusable). The runtime's log line names the bound, each texture's line its peak. Check:
`storm_relief_check <models> --memory` (the host check `renderer_bgfx.relief_check.memory`).

## Host use

A host engine reads finished maps with engine.ini `relief_dir = <relief dir>` and makes maps itself only with
`relief_models = <model dir>`; a host build writes that directory into `<build>/relief_models_path.txt`
(target `storm_relief_model_files`; the scene sweep reads it).

## Models and Android

The ncnn graphs are made by the build, not kept in the repository: [model/README.md](model/README.md). The
Android build puts the graph files into `<build>/game/assets`; the app packages them as assets of the same names
and shows `PBRify-CC0-1.0.txt`, `PBRify-PROVENANCE.txt`, `ncnn-BSD-3-Clause.txt` and `glslang-LICENSE.txt`
(`third_party/licenses`) on its licences screen.
