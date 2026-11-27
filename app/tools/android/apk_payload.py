"""What the APK ships besides the engine and the UI layers: one definition for packaging and the APK check."""

# The relief generation's ncnn models (engine src/libs/relief/README.md), put next to libengine.so by the engine build.
RELIEF_MODELS = ('pbrify-normal.ncnn.param', 'pbrify-normal.ncnn.bin', 'pbrify-height.ncnn.param',
                 'pbrify-height.ncnn.bin', 'pbrify-roughness.ncnn.param', 'pbrify-roughness.ncnn.bin',
                 'pbrify-upscale.ncnn.param', 'pbrify-upscale.ncnn.bin')
# Native libraries every APK packages; libfmt and ncnn are linked into libengine.so.
REQUIRED_LIBRARIES = ('engine', 'SDL2', 'c++_shared', 'storm_ui', 'bgfx', 'storm_archive', 'storm_torrent')
# Notices of the torrent client's own dependencies (the archive reader ships its own set, with the Boost both use).
TORRENT_LICENSES = ('OpenSSL-Apache-2.0.txt', 'libtorrent-BSD-3-Clause.txt', 'try_signal-license.txt')
# Notices of the encoders and decoders bimg links into libbgfx.so besides bimg's and etcpak's own (engine
# third_party/README.md), kept in engine/third_party/licenses as bimg ships them at the pinned commit.
BIMG_LICENSES = ('bimg-astc-encoder-Apache-2.0.txt', 'bimg-etc1-Apache-2.0.txt', 'bimg-libsquish-MIT.txt',
                 'bimg-nvtt-MIT.txt', 'bimg-pvrtc-BSD-3-Clause.txt', 'bimg-edtaa3-MIT.txt', 'bimg-iqa-BSD-3-Clause.txt',
                 'bimg-stb-MIT-or-Unlicense.txt')
# Notices of the third-party code SDL compiles into libSDL2.so on Android, copied from the fetched SDL:
# APK name -> path in the SDL source. HIDAPI's API (its Android backend is SDL's own) under its BSD option.
SDL_LICENSES = {'SDL-hidapi-LICENSE.txt': 'src/hidapi/LICENSE.txt',
                'SDL-hidapi-LICENSE-bsd.txt': 'src/hidapi/LICENSE-bsd.txt',
                'SDL-yuv2rgb-LICENSE.txt': 'src/video/yuv2rgb/LICENSE'}


def apk_name(arch, build_type):
    """The packaged APK's file name. A release is packaged unsigned: sign_release.py signs it."""
    return f'seawolves-{arch}-{build_type}{"-unsigned" if build_type == "release" else ""}.apk'


def app_java_sources(root, ui_gallery=False, test_loop=False):
    """The application's Java sources for one build: the single list javac sees in packaging and checks."""
    game = root / 'android/game'
    sources = sorted((game / 'src').rglob('*.java'))
    if ui_gallery:
        sources += sorted((game / 'debug').rglob('*.java'))
    sources += sorted((game / 'testloop' / ('on' if test_loop else 'off')).rglob('*.java'))
    return sources


def apk_problems(names):
    """Missing native libraries and third-party notices of a packaged APK, given its entry names."""
    names = set(names)
    problems = [f'missing lib{library}.so' for library in REQUIRED_LIBRARIES
                if f'lib/arm64-v8a/lib{library}.so' not in names]
    for name in (*TORRENT_LICENSES, *BIMG_LICENSES, *SDL_LICENSES):
        if 'assets/licenses/' + name not in names:
            problems.append(f'missing bundled third-party notice {name}')
    return problems
