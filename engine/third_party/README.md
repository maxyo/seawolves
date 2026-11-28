# Third-party code, models and tools

Every piece of third-party code, model or tool the repository vendors or the build fetches, with its pin and
licence. Licence ids are SPDX. "Fetched" means the build downloads it at the pinned version (the file named in
Where; paths are relative to `engine/` unless they start with `app/`); nothing fetched is stored in this
repository. "pin `<name>`": the URL and SHA-256 or the Git commit are in [pins.json](../pins.json). The licence texts of what ships in the APK are packaged under
`assets/licenses/` by `app/tools/android/build_apk.py`, which fails when a file of `licenses/` here is not packaged.
The About screen of every APK shows them.

Texts kept in this repository: [licenses/](licenses) and [../android/archive/licenses](../android/archive/licenses)
(plus the ones named in the tables). Texts the build copies from the fetched sources are named by their APK path.

## In the APK

| Name | Pin | Licence | Where | Licence text in the APK |
| --- | --- | --- | --- | --- |
| SDL | 2.0.18, pin `sdl` | Zlib | fetched: `tools/android/ci_dependencies.sh`; `libSDL2.so` and its Java sources | `SDL.txt` (from the fetched `LICENSE.txt`) |
| HIDAPI's API (`src/hidapi/hidapi/hidapi.h`), inside SDL; on Android its backend is SDL's own `src/hidapi/android/hid.cpp` (Zlib) | as SDL | BSD-3-Clause OR GPL-3.0-only OR the original HIDAPI licence (used under BSD-3-Clause) | in `libSDL2.so` | `SDL-hidapi-LICENSE.txt`, `SDL-hidapi-LICENSE-bsd.txt` (from the fetched `src/hidapi/LICENSE.txt`, `LICENSE-bsd.txt`) |
| yuv2rgb, inside SDL | as SDL | BSD-3-Clause | in `libSDL2.so` (`src/video/yuv2rgb`) | `SDL-yuv2rgb-LICENSE.txt` (from the fetched `src/video/yuv2rgb/LICENSE`) |
| Android NDK C++ runtime (`libc++_shared.so`) and toolchain runtime code | NDK r30, pin `ndk` | Apache-2.0 WITH LLVM-exception, libc++ also NCSA OR MIT | fetched: `tools/android/ci_dependencies.sh` | `NDK-toolchain-NOTICE.txt` (the NDK's `NOTICE.toolchain`) |
| Android NDK cpufeatures, compiled by SDL's build | NDK r30 | BSD-2-Clause | not in `libSDL2.so`: SDL reads the CPU features with `getauxval`, and the linker drops the unused object (`--gc-sections`) on arm64 and x86_64 | not needed |
| fmt | 12.1.0, pin `fmt` | MIT | fetched: `cmake/engine_dependencies.cmake` | `fmt-LICENSE` |
| spdlog | 1.17.0, pin `spdlog` | MIT | fetched: `cmake/engine_dependencies.cmake` | `spdlog-LICENSE` |
| fast_float | 3.4.0, pin `fast_float` | MIT OR Apache-2.0 | fetched: `cmake/engine_dependencies.cmake` | `fast_float-LICENSE-MIT`, `fast_float-LICENSE-APACHE` |
| portable-snippets debug-trap | — | CC0-1.0 | vendored: `src/libs/util/include/debug-trap.h` (licence in its header) | none needed |
| FFmpeg (libavcodec, libavformat, libavutil, libswresample, libswscale; shared, unmodified) | n8.1.3, pin `ffmpeg` | LGPL-2.1-or-later | fetched: `cmake/ffmpeg.cmake`, built by `tools/ffmpeg` | `FFmpeg-LGPL-2.1.txt`, `FFmpeg-configure.txt` |
| miniaudio, with this port's patch | 0.11.23, pin `miniaudio` | Unlicense OR MIT-0 | fetched: `cmake/engine_dependencies.cmake`; patch `tools/android/patches/miniaudio-0.11.23-error-cleanup.patch` | `miniaudio.txt`, `miniaudio-storm-patch.txt` |
| stb_vorbis | the copy in miniaudio's `extras/` | MIT OR Unlicense | fetched with miniaudio | `stb_vorbis.txt` |
| GLM | 1.0.3, pin `glm` | MIT | fetched: `cmake/glm.cmake` | `GLM-MIT.txt` |
| bgfx | pin `bgfx`, with the patches in `src/libs/renderer_bgfx/patches` | BSD-2-Clause | fetched: `src/libs/renderer_bgfx/cmake/bgfx_fetch.cmake`; `libbgfx.so` | `bgfx-BSD-2-Clause.txt` |
| bx | pin `bx` | BSD-2-Clause | fetched: `src/libs/renderer_bgfx/cmake/bgfx_fetch.cmake`; `libbgfx.so` | `bx-BSD-2-Clause.txt` |
| bimg | pin `bimg` | BSD-2-Clause | fetched: `src/libs/renderer_bgfx/cmake/bgfx_fetch.cmake`; `libbgfx.so` | `bimg-BSD-2-Clause.txt` |
| etcpak, inside bimg | as bimg | BSD-3-Clause | `libbgfx.so` (the ETC2/EAC encoder) | `etcpak-BSD-3-Clause.txt` |
| astc-encoder, etc1, libsquish, NVIDIA Texture Tools, PVRTC encoder, edtaa3, IQA, stb_image_resize2, inside bimg | as bimg | Apache-2.0 (astc-encoder, etc1), MIT (libsquish, nvtt, edtaa3), BSD-3-Clause (PVRTC encoder, IQA), MIT OR Unlicense (stb_image_resize2) | `libbgfx.so`: the encoder entry point the engine calls (`bimg::imageEncodeFromRgba8`) and bimg's ASTC decoder reach them; bimg's other bundled code (tinyexr, miniz, lodepng and the decoders of `bimg_decode`) is not linked | `bimg-astc-encoder-Apache-2.0.txt`, `bimg-etc1-Apache-2.0.txt`, `bimg-libsquish-MIT.txt`, `bimg-nvtt-MIT.txt`, `bimg-pvrtc-BSD-3-Clause.txt`, `bimg-edtaa3-MIT.txt`, `bimg-iqa-BSD-3-Clause.txt`, `bimg-stb-MIT-or-Unlicense.txt` (bimg's own texts at the pin) |
| Android Frame Pacing (Swappy) | pin `swappy` | Apache-2.0 | fetched: `src/libs/renderer_bgfx/cmake/swappy.cmake`; static in `libengine.so` | `Swappy-Apache-2.0.txt` |
| meshoptimizer | pin `meshoptimizer` | MIT | fetched: `src/libs/geometry/far_lod/CMakeLists.txt` | `meshoptimizer-MIT.txt` |
| bvh (madmann91) | pin `bvh` | MIT | fetched: `src/libs/renderer_bgfx/fx/CMakeLists.txt` | `bvh-MIT.txt` |
| xxHash | 0.8.3 | BSD-2-Clause | fetched: `src/libs/renderer_bgfx/cmake/xxhash.cmake`; header-only, in `libengine.so` | `xxHash-BSD-2-Clause.txt` |
| SMAA | pin `smaa` | MIT | fetched: `src/libs/renderer_bgfx/fx/CMakeLists.txt` | `SMAA-MIT.txt` |
| AMD FidelityFX FSR 1 | pin `fidelityfx-fsr` | MIT | fetched: `src/libs/renderer_bgfx/fx/CMakeLists.txt` | `FidelityFX-FSR-MIT.txt` |
| Effekseer (runtime: core and EffekseerRendererCommon, without the editor's network server) | 1.80h, pin `effekseer` | MIT | fetched: `src/libs/renderer_bgfx/effekseer/CMakeLists.txt`; static in `libengine.so` | `Effekseer-MIT.txt` |
| stb_image, inside Effekseer (its PNG decoder) | as Effekseer | MIT OR Unlicense | static in `libengine.so` | `bimg-stb-MIT-or-Unlicense.txt` (the same text) |
| efkbgfx (Effekseer's bgfx renderer), with this port's patch | pin `efkbgfx`, with `src/libs/renderer_bgfx/effekseer/patches` | MIT | fetched: `src/libs/renderer_bgfx/effekseer/CMakeLists.txt`; static in `libengine.so` | `efkbgfx-MIT.txt` |
| three.js AgX tone curve | r160 | MIT | vendored, adapted: `src/libs/renderer_bgfx/fx` ([THIRD_PARTY.md](../src/libs/renderer_bgfx/fx/THIRD_PARTY.md)) | `threejs-MIT.txt` |
| MJP Shadows PCF | — | MIT | vendored, adapted: `src/libs/renderer_bgfx/shaders/include/ff_lamps.sh` | `MJP-Shadows-MIT.txt` |
| ncnn | 20261020, pin `ncnn` | BSD-3-Clause | fetched: `cmake/ncnn.cmake`; static in `libengine.so` | `ncnn-BSD-3-Clause.txt` |
| glslang, inside ncnn | as ncnn | BSD-3-Clause AND BSD-2-Clause AND MIT AND Apache-2.0 | static in `libengine.so` | `glslang-LICENSE.txt` |
| PBRify normal, height and roughness models | QtMeshEditor-models revision `b35b7f4c9249` | CC0-1.0 | fetched: `src/libs/relief/CMakeLists.txt`, converted to ncnn at build time | `PBRify-CC0-1.0.txt`, `PBRify-PROVENANCE.txt` |
| PBRify 4x upscaler model | PBRify_Remix v1.7.2 | CC0-1.0 | vendored, converted: `src/libs/relief/model/pbrify-upscale.ncnn.{param,bin}` | `PBRify-CC0-1.0.txt`, `PBRify-PROVENANCE.txt` |
| RmlUi | 6.3, pin `rmlui` | MIT | fetched: `src/libs/ui/CMakeLists.txt`; `libstorm_ui.so` | `ui-RmlUi-MIT.txt` |
| FreeType | 2.14.3, pin `freetype` | FTL | fetched: `src/libs/ui/CMakeLists.txt`; `libstorm_ui.so` | `ui-FreeType-FTL.txt` |
| stb_image, inside bimg (its bundled copy) | as bimg | MIT OR Unlicense | `libstorm_ui.so`: decodes the UI pack's PNG and JPEG pictures | `bimg-stb-MIT-or-Unlicense.txt` |
| nlohmann/json | 3.12.0, pin `nlohmann-json` | MIT | fetched: `src/libs/ui/CMakeLists.txt` | `ui-nlohmann-json-MIT.txt` |
| libtorrent | 2.0.15, pin `libtorrent` | BSD-3-Clause | fetched: `android/torrent/sources/CMakeLists.txt` | `libtorrent-BSD-3-Clause.txt` |
| try_signal | pin `try_signal` | BSD-3-Clause | fetched: `android/torrent/sources/CMakeLists.txt` | `try_signal-license.txt` |
| OpenSSL | 3.5.9, pin `openssl` | Apache-2.0 | fetched: `android/torrent/sources/CMakeLists.txt` | `OpenSSL-Apache-2.0.txt` |
| Boost | 1.90.0, pin `boost` | BSL-1.0 | fetched: `android/archive/sources/CMakeLists.txt`, `android/torrent/sources/CMakeLists.txt` | `archive-boost_1_90_0.txt` (the same text as `licenses/Boost-BSL-1.0.txt`) |
| libarchive | 3.8.10, pin `libarchive` | BSD-2-Clause | fetched: `android/archive/sources/CMakeLists.txt` | `archive-libarchive-3.8.10.txt` |
| xz (liblzma) | 5.8.4, pin `xz` | 0BSD | fetched: `android/archive/sources/CMakeLists.txt` | `archive-xz-5.8.4.txt` |
| bzip2 | 1.0.8, pin `bzip2` | bzip2-1.0.6 | fetched: `android/archive/sources/CMakeLists.txt` | `archive-bzip2-1.0.8.txt` |
| zlib | 1.3.2, pin `zlib` | Zlib | fetched: `android/archive/sources/CMakeLists.txt` (the archive reader; the engine itself uses Android's system zlib) | `archive-zlib-1.3.2.txt` |
| innoextract | 1.9, pin `innoextract` | Zlib | fetched: `android/archive/sources/CMakeLists.txt` | `archive-innoextract-1.9.txt` |

The fonts, icons and pictures of the application are listed in [../../NOTICE](../../NOTICE).

FreeType: portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights
reserved.

## Development and test builds only

| Name | Pin | Licence | Where | Use |
| --- | --- | --- | --- | --- |
| Tracy profiler client | 0.14.1, pin `tracy` | BSD-3-Clause | fetched: `cmake/tracy.cmake` | `-DSTORM_TRACY=ON` development builds; a debug APK built with it packages `Tracy-BSD-3-Clause.txt`, release APKs never contain it |
| mimalloc | 2.0.3, pin `mimalloc` | MIT | fetched: `cmake/host.cmake` | the Linux host engine only |
| Catch2 | 2.13.7, pin `catch2` | BSL-1.0 | fetched: `cmake/engine_dependencies.cmake` | host unit tests |
| bgfx.cmake | pin `bgfx-cmake` | BSD-2-Clause | fetched: `src/libs/renderer_bgfx/cmake/bgfx_fetch.cmake` | the CMake build of bgfx, bx and bimg; bgfx's shader compiler (`shaderc`, with its bundled glslang, SPIRV-Tools and SPIRV-Cross) runs on the build host |
| pnnx, PyTorch, spandrel, ONNX and their Python dependencies | `src/libs/relief/model/requirements-ncnn.txt`, `upscale-requirements.txt` | their own (BSD-3-Clause, MIT, Apache-2.0) | fetched with pip | converting the PBRify models on the build host |
| Android NDK and SDK tools | pins `ndk`, `android-platform`, `android-build-tools`, `android-cmdline-tools` (fetched by `tools/android/ci_dependencies.sh`, `app/tools/android/prepare_sdk.sh`) | Apache-2.0 and others | fetched | building, signing and Android Lint |
| CMake, Ninja | 3.28.4, 1.13.0 (pip) | BSD-3-Clause, Apache-2.0 | fetched: `tools/android/ci_dependencies.sh` | building |
| lunasvg, with plutovg inside | 3.5.0, pin `lunasvg` | MIT | fetched: `src/libs/ui/CMakeLists.txt` (the stand-alone host build) | rasterising the UI pack's icon atlas (`storm_ui_icon_bake`) and writing captures' PNGs (`storm_ui_shot`) |
| google-java-format | 1.28.0 | Apache-2.0 | fetched: `app/tools/android/java_format.py` | formatting the Java sources |
| AOSP org.json (`com.vaadin.external.google:android-json`) | 0.0.20131108.vaadin1 | Apache-2.0 | fetched: `app/tests/java_check.py` | Android's own `org.json` on the classpath of the app's Java host checks; never packaged |
| the host's SDL2 and zlib | the distribution's | Zlib | system packages (`cmake/host.cmake`) | the Linux host engine; zlib also the audio import tool |
| libogg | 1.3.6, pin `libogg` | BSD-3-Clause | fetched: `tools/audio-import/cmake/xiph.cmake` | the audio import tool (`tools/audio-import`) and its checks; never packaged |
| libvorbis (with vorbisenc) | 1.3.7, pin `libvorbis` | BSD-3-Clause | fetched: `tools/audio-import/cmake/xiph.cmake` | the audio import tool and its checks; never packaged |
