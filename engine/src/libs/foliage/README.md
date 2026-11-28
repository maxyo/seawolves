# Foliage

Caribbean Legend 2.0 places trees on its islands and outdoor locations with files of its own. This library reads them
and decides which trees a density draws; it has no engine dependencies but GLM, nlohmann/json and zlib
(`util/include/zip_archive.hpp` reads the asset archives).

## Files

- `Resource/INI/foliage.json`: flat settings, land keys and `island_` twins (`Settings` keeps those this engine draws
  with; the other keys are not read).
- `<folder>.foliage`, one per location or island model folder, named after the folder: JSON `version` 1,
  `coordinates` `ENGINE_Y_UP` and `instances[]` of `id`, `resource` (`foliage/<name>.bmfoliage`) and a column-major
  4×4 `transform` in the world frame.
- `Models/Foliage/<name>.bmfoliage`, a zip: `manifest.json` (materials, the meshes of each level, the textures, the
  attribute channel names), `meshes/N.bin` (`BFLM0001`: vertex count, index count, stride 40; position, normal, uv,
  occlusion, RGBA8; u32 triangle indices) and `attributes/N.bin` (`BFLA0001`: vertex count, 36 floats per vertex, two
  per channel), and the DDS textures. The last level is the billboard: an atlas of views drawn with billboard
  materials, which no other level uses.

Each reader checks its whole input and refuses it with the first problem named.

## Selection

A tree's rank is the top 32 bits of its id's FNV-1a. A share `s` of the trees draws those ranked below `s · 2³²`: the
same trees every time, and a larger share only adds trees. The trees are kept on a square grid in xz with each cell's in
rank order, so the trees a share draws in a cell are a prefix of it.

## Checks

`python3 tools/ci/run_host_tests.py --only foliage`: the readers over small invented files and the selection;
`foliage.corpus` reads every foliage file of an installed game (`$STORM_GAME_DIR`; not run without one).
