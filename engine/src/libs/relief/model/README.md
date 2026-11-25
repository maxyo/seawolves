# The PBRify models of the relief maps

Not in the repository: the build makes them. The game runs ncnn graphs (fp16 weights) of
Kim2091/PBRify_Remix's SPAN models (CC0-1.0), the same files on the GPU and on the CPU (`../backend.hpp`):
`pbrify-normal`, `pbrify-height` and `pbrify-roughness` (made of 1x-PBRify_NormalV3, 1x-PBRify_Height and
1x-PBRify_RoughnessV2) and `pbrify-upscale` (4x-PBRify_UpscalerSPANV4), each a `.ncnn.param` and a `.ncnn.bin`. Every
pin (the float models, the graphs, the upscaler's release) is a `pbrify-*` entry of `engine/pins.json`.

They stay CC0-1.0 (`third_party/licenses/PBRify-CC0-1.0.txt`; provenance in `PBRify-PROVENANCE.txt`). Input 1x3xHxW
(blob `in0`), output 1x3xHxW (the upscaler 1x3x4Hx4W; blob `out0`), 0..1, any size. The first 8 bytes of each `.bin`
digest are `fx::kRelief*ModelDigest` / `fx::kUpscaleModelDigest`, generated from the pins into
`storm/fx/model_digests.hpp` (`digests.cmake`): part of the maps' and the ×2's methods, so a new graph
makes them again.

## How the build makes them

`../CMakeLists.txt` fetches the three relief networks as the ONNX exports by fernandotonon/QtMeshEditor-models
(CC0-1.0) by URL and sha256 (`storm_fetch_declare`: one shared copy per machine in
`STORM_DEPS_CACHE`; offline, `FETCHCONTENT_SOURCE_DIR_STORM_PBRIFY_{NORMAL,HEIGHT,ROUGHNESS}` name folders with the
files), and the PBRify_Remix release v1.7.2 for the upscaler's `.pth`. `storm_derived_files`
(`cmake/StormDerivedFiles.cmake`) makes the graphs: a venv with exactly the pinned requirements (pip, binary wheels, no
dependency resolution; offline, `STORM_PIP_WHEELHOUSE` names a folder of those wheels), then

- `make_ncnn_models.py` (`requirements-ncnn.txt`: pnnx, ncnn's converter, its standalone binary): each ONNX graph into
  ncnn's;
- `make_upscale_model.py` (`upscale-requirements.txt`: torch CPU and spandrel for the export, pnnx): the `.pth`
  exported to ONNX (opset 17) with spandrel, then converted by pnnx;

every file checked against its pinned sha256 (another toolchain that makes other bytes fails the build), then kept in
`STORM_DEPS_CACHE/derived/<name>-<digest>` for every build directory. The Android build copies them into
`<build>/game/assets`; host builds write the folder into `<build>/relief_models_path.txt`.

To change a model or the tools: edit the script or its requirements, make the files once by hand (a venv with the
requirements, then `<script> --sources <dir> --output <dir>` with the arguments `../CMakeLists.txt` passes), and update
the pins in `engine/pins.json`.

## The upscaler

`pbrify-upscale.ncnn.param` and `pbrify-upscale.ncnn.bin` are made by `make_upscale_model.py` from Kim2091/PBRify_Remix
release v1.7.2's `4x-PBRify_UpscalerSPANV4.pth` (CC0-1.0; the release pinned by sha256): exported with spandrel,
converted by pnnx (fp16 weights). Input 1x3xHxW, output 1x3x4Hx4W, 0..1.

Unlike the relief models, these bytes depend on the CPU: GitHub's AVX2 runners make another ncnn `.bin` than the
AVX-512 reference machine that made the pins. So the build does not make them: the reference machine's two files
are kept in this directory (`pbrify-upscale.ncnn.param`, `pbrify-upscale.ncnn.bin`, CC0-1.0) and the build checks each
by sha256. The host target `storm_upscale_model_check` makes them again with the script, as a
`storm_derived_files` (`STORM_DEPS_CACHE/derived/storm_upscale_model-<digest>`; delete that folder to make them again
for the same inputs), and fails unless every file matches its pin. To change the model: edit the script or its
requirements, build `storm_upscale_model_check` on the reference machine with the new pins, replace the two files in this
directory and change the pins in `engine/pins.json`. `fx::kUpscaleModelDigest` is part of the ×2 textures' method and of
the relief maps' (the maps of small textures are made from the ×2).
