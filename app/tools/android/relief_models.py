"""The relief generation's model files (engine src/libs/relief/README.md, relief.hpp kModelFiles): ncnn graphs of
PBRify's normal, height and roughness models and of its 4x upscaler (the neural x2 textures, engine fx/upscale.hpp), run
by ncnn (linked into libengine.so) on the GPU or the CPU. The engine build puts them in assets/ next to libengine.so;
build_apk.py packages them as assets/<name> and write_manifest.py checks them byte for byte against that directory."""
RELIEF_MODELS = ('pbrify-normal.ncnn.param', 'pbrify-normal.ncnn.bin', 'pbrify-height.ncnn.param',
                 'pbrify-height.ncnn.bin', 'pbrify-roughness.ncnn.param', 'pbrify-roughness.ncnn.bin',
                 'pbrify-upscale.ncnn.param', 'pbrify-upscale.ncnn.bin')
