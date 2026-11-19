#!/usr/bin/env python3
"""Package the full engine as a signed APK.

--build-type debug (default): debuggable, native libraries packaged as built, signed with a locally generated
debug key unless STORM_ANDROID_KEYSTORE names another one; versionName <VERSION>-dev with the versionCode of VERSION.
--build-type release: not debuggable, native libraries stripped, unstripped copies kept beside the APK for crash
symbolication (symbols/ and native-debug-symbols.zip), the release signing key required (STORM_ANDROID_*), version
from the release tag (build_debug.sh).
Both types require 16 KiB ELF LOAD alignment and 16 KiB zip alignment.

The inputs are the SDK, NDK and SDL sources the engine's build workspace already holds and the engine's build
output; this script downloads nothing and packages no game resources. build_debug.sh passes all of them.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from mobile_ui_assets import collect as collect_mobile_ui, collect_shaders, collect_ui_pack
from script_patch_assets import collect as collect_script_patches
from menu_locales import collect as collect_menu_locales
from platform_resources import resource_directory
from relief_models import RELIEF_MODELS
import flavors
import release_version

ROOT = Path(__file__).resolve().parents[2]  # app/
REPO = ROOT.parent  # the repository: app/ and engine/ side by side, the licences, NOTICE and VERSION
ANDROID_NAMESPACE = 'http://schemas.android.com/apk/res/android'
ANDROID = '{' + ANDROID_NAMESPACE + '}'
ET.register_namespace('android', ANDROID_NAMESPACE)
# APK ABI: (short name in the APK file name, NDK sysroot triple, ELF e_machine).
ABIS = {'arm64-v8a': ('arm64', 'aarch64-linux-android', 183), 'x86_64': ('x86_64', 'x86_64-linux-android', 62)}
TARGET_SDK = '36'  # the API level Google Play requires of new apps and updates

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

p = argparse.ArgumentParser(description=__doc__)
for name in ('android-jar', 'build-tools', 'sdl-source', 'deps-prefix', 'ndk', 'build-dir'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--engine-library', type=Path, required=True)
p.add_argument('--engine-root', type=Path, required=True)
p.add_argument('--engine-sources', type=Path)
p.add_argument('--audio-sources', type=Path)
# FFmpeg shared libraries for in-game video (engine build: runtime/ffmpeg, tools/ffmpeg/README.md).
p.add_argument('--ffmpeg-libraries', type=Path)
p.add_argument('--archive-library', type=Path)
p.add_argument('--torrent-library', type=Path)
# From the same engine build as libengine.so (its game/ directory), which links both: the UI
# runtime (src/libs/ui) and bgfx/bx/bimg as one shared library (one bgfx per process).
p.add_argument('--ui-library', type=Path, required=True)
p.add_argument('--bgfx-library', type=Path, required=True)
p.add_argument('--ui-licenses', type=Path, required=True)
# Developer tool: the component gallery activity (android/game/debug). Off by default, so
# play and release packages never contain it; orthogonal to the flavor and the build type.
p.add_argument('--ui-gallery', action='store_true')
# Developer tool: the scenario driver for automated runs on a device (android/game/testloop/on, started by
# tools/android/emu_smoke.py) and the launcher's TEST_LOOP intent filter. Debug builds only; every other build gets
# testloop/off (no-ops).
p.add_argument('--test-loop', action='store_true')
# Distribution flavor is orthogonal to the debug/release build type.
p.add_argument('--flavor', choices=flavors.FLAVORS, default='github',
               help='github: SAF import plus the download library incl. torrents; play: SAF import (folder, ZIP, '
                    'archives, installers) without download library code, torrent library or network permission')
p.add_argument('--application-id', default='com.goodoldgames.seawolves')
p.add_argument('--app-label', default='Seawolves')
p.add_argument('--default-keystore', type=Path, help='Debug key used/generated when STORM_ANDROID_KEYSTORE is unset')
p.add_argument('--build-type', choices=('debug', 'release'), default='debug')
# One ABI per APK, matching the native libraries given above (engine tools/android/README.md, ABIs).
# Releases are arm64-v8a only; x86_64 is a debug build that runs natively on x86_64 emulators.
p.add_argument('--abi', choices=sorted(ABIS), default='arm64-v8a')
a = p.parse_args()
release = a.build_type == 'release'
if release and a.test_loop:
    p.error('--test-loop is a debug-build developer tool; release APKs never contain the scenario driver')
triple, elf_machine = ABIS[a.abi][1:]
if release and a.abi != 'arm64-v8a':
    p.error('Release APKs are arm64-v8a only; --abi ' + a.abi + ' is for emulator debug builds')
SIGNING = ('STORM_ANDROID_KEYSTORE', 'STORM_ANDROID_KEY_ALIAS', 'STORM_ANDROID_STORE_PASSWORD', 'STORM_ANDROID_KEY_PASSWORD')
if release:
    # A release key never falls back to a generated key, the debug alias or passwords.
    missing = [name for name in SIGNING if not os.environ.get(name)]
    if missing:
        p.error('Release packaging needs ' + ', '.join(missing) + ' (the release key; tools/android/RELEASE.md)')
library = flavors.has_library(a.flavor)
if not all((a.engine_sources, a.audio_sources, a.ffmpeg_libraries)):
    p.error('Engine APK requires engine/audio sources and FFmpeg libraries')
FFMPEG_LIBRARIES = ('libavcodec.so', 'libavformat.so', 'libavutil.so', 'libswresample.so', 'libswscale.so')
missing_ffmpeg = [name for name in FFMPEG_LIBRARIES if not (a.ffmpeg_libraries / name).is_file()]
if missing_ffmpeg or not (a.ffmpeg_libraries / 'configure.txt').is_file():
    p.error(f'--ffmpeg-libraries {a.ffmpeg_libraries} lacks ' + ', '.join(missing_ffmpeg or ['configure.txt']))
if not a.archive_library:
    p.error('Engine APKs require the archive library (SAF archive and installer import)')
if library and not a.torrent_library:
    p.error('The github flavor requires the torrent library')
# A development engine build with the Tracy profiler client (engine cmake/tracy.cmake) never goes into a release
# APK. In a debug APK the profiler's loopback socket needs the INTERNET permission, which the play flavor drops.
tracy = b'storm-profiler: Tracy' in a.engine_library.read_bytes()
if tracy and release:
    p.error(f'{a.engine_library} has the Tracy profiler (engine built with -DSTORM_TRACY=ON): not for release APKs')
if tracy:
    print('Debug APK with the Tracy profiler client: INTERNET permission kept for its loopback socket, licence added')
b = a.build_dir.resolve()
b.mkdir(parents=True, exist_ok=True)
classes = b / 'classes'  # rebuilt below unless the Java stamp says its inputs did not change
manifest = ET.parse(ROOT / 'android/game/AndroidManifest.xml')
manifest.getroot().set('package', a.application_id)
application = manifest.getroot().find('application')
for provider in application.findall('provider'):
    # Authorities are device-wide: each application id (play/github flavours side by side) needs its own.
    provider.set(ANDROID + 'authorities', a.application_id + provider.get(ANDROID + 'authorities'))
if release:
    # Absent means false; never ship a debuggable (run-as/JDWP-attachable) build.
    application.attrib.pop(ANDROID + 'debuggable', None)
    application.attrib.pop(ANDROID + 'gwpAsanMode', None)
else:
    application.set(ANDROID + 'debuggable', 'true')
if not release:
    # Debug builds sample heap allocations with GWP-ASan (the engine allocates with Bionic too).
    application.set(ANDROID + 'gwpAsanMode', 'always')
application.set(ANDROID + 'label', a.app_label)
if not library:
    for permission in list(manifest.getroot().findall('uses-permission')):
        if permission.get(ANDROID + 'name') in flavors.LIBRARY_PERMISSIONS and not (
                tracy and permission.get(ANDROID + 'name') == 'android.permission.INTERNET'):
            manifest.getroot().remove(permission)
    for component in list(application):
        if component.get(ANDROID + 'name') in flavors.LIBRARY_COMPONENTS:
            application.remove(component)
if not a.ui_gallery:
    for component in list(application):
        if component.get(ANDROID + 'name') == 'org.stormengine.port.UiGalleryActivity':
            application.remove(component)
TEST_LOOP = 'com.google.intent.action.TEST_LOOP'
if a.test_loop:
    # The scenario driver is started through the launcher's own filter for the Game Loop intent (action, mime type).
    launcher = next(c for c in application if c.get(ANDROID + 'name') == 'org.stormengine.port.GameLauncherActivity')
    loop = ET.SubElement(launcher, 'intent-filter')
    ET.SubElement(loop, 'action').set(ANDROID + 'name', TEST_LOOP)
    ET.SubElement(loop, 'category').set(ANDROID + 'name', 'android.intent.category.DEFAULT')
    ET.SubElement(loop, 'data').set(ANDROID + 'mimeType', 'application/javascript')
if any(action.get(ANDROID + 'name') == TEST_LOOP for action in manifest.getroot().iter('action')) != a.test_loop:
    raise RuntimeError('TEST_LOOP intent filter must be present exactly in --test-loop builds')
if not library and any(component.get(ANDROID + 'process') == ':downloads' for component in application):
    raise RuntimeError('Play flavor must not declare the download process')
manifest.write(b / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)
unsigned = b / 'unsigned.apk'
# A release takes its versionName and versionCode from its tag (tools/android/release_version.py check, run by
# build_debug.sh, which exports both). Any other build is <VERSION>-dev with the versionCode of VERSION.
declared_version = release_version.declared()
if release:
    if 'STORM_ANDROID_VERSION_NAME' not in os.environ or 'STORM_ANDROID_VERSION_CODE' not in os.environ:
        raise RuntimeError('Release APKs take their version from a release tag: build them with build_debug.sh')
    version_name = os.environ['STORM_ANDROID_VERSION_NAME']
    version_code = int(os.environ['STORM_ANDROID_VERSION_CODE'])
    if (version_name, version_code) != (declared_version.text, declared_version.code):
        raise RuntimeError(f'Release version {version_name} ({version_code}) is not VERSION {declared_version.text}')
else:
    version_name = declared_version.text + '-dev'
    version_code = declared_version.code
resource_args = []
generated = b / 'generated-resources'
if generated.exists():
    shutil.rmtree(generated)
generated.mkdir()
compiled = b / 'platform-resources.zip'
if compiled.exists():
    compiled.unlink()
run(a.build_tools / 'aapt2', 'compile', '--dir', resource_directory(ROOT), '-o', compiled)
resource_args = ['--custom-package', 'org.stormengine.port', '--java', generated, compiled]
run(a.build_tools / 'aapt2', 'link', '-I', a.android_jar, '--manifest', b / 'AndroidManifest.xml',
    '--min-sdk-version', '24', '--target-sdk-version', TARGET_SDK, '--version-code', str(version_code),
    '--version-name', version_name, '-o', unsigned, *resource_args)
sources = sorted((a.sdl_source / 'android-project/app/src/main/java').rglob('*.java'))
app_sources = flavors.app_java_sources(ROOT, a.flavor, a.ui_gallery, a.test_loop)
sources += app_sources
sources += sorted(generated.rglob('*.java'))
if not sources:
    raise RuntimeError('No Activity/SDL Java sources')
# javac and d8 take several seconds and depend only on these inputs: a content stamp of all of them (sources,
# android.jar, the javac/d8 tools, the flags below) reuses classes/ and dex/ when nothing changed.
dex = b / 'dex'
stamp_file = b / 'java-stamp.txt'


def java_stamp():
    h = hashlib.sha256(b'java-stamp 2 javac -source 8 -target 8; d8 --min-api 24')
    tools = [shutil.which('javac'), a.build_tools / 'd8', a.build_tools / 'lib/d8.jar', a.android_jar]
    for tool in tools:
        if tool is None or not Path(tool).exists():
            continue
        path = Path(os.path.realpath(tool))
        h.update(str(path).encode() + b'\0')
        h.update(path.read_bytes())
        modules = path.parent.parent / 'lib/modules'  # the JDK's class library: an in-place JDK update changes it
        if modules.is_file():
            h.update(str(modules.stat().st_size).encode() + str(modules.stat().st_mtime_ns).encode())
    for file in sources:
        h.update(b'\0' + str(file).encode() + b'\0' + Path(file).read_bytes())
    return h.hexdigest()


stamp = java_stamp()
if stamp_file.exists() and stamp_file.read_text() == stamp and classes.is_dir() and any(dex.glob('*.dex')):
    print(f'Java inputs unchanged (stamp {stamp[:12]}): classes and dex of the last build reused', flush=True)
    java_rebuilt = False
else:
    stamp_file.unlink(missing_ok=True)
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir()
    java_rebuilt = True
if java_rebuilt:
    run('javac', '-source', '8', '-target', '8', '-classpath', a.android_jar, '-d', classes, *sources)
# Every manifest component must exist; the play flavor must contain no download library classes.
for component in application:
    name = component.get(ANDROID + 'name')
    if component.tag != 'meta-data' and name and not (classes / (name.replace('.', '/') + '.class')).is_file():
        raise RuntimeError('Manifest component is not compiled: ' + name)
leaked = [name for name in flavors.LIBRARY_SOURCES if (classes / 'org/stormengine/port' / (name + '.class')).exists()]
if not library and leaked:
    raise RuntimeError('Play flavor contains download library classes: ' + ', '.join(sorted(leaked)))
if java_rebuilt:
    jar = b / 'classes.jar'
    with zipfile.ZipFile(jar, 'w') as z:
        for file in classes.rglob('*.class'):
            z.write(file, file.relative_to(classes))
    dex.mkdir(exist_ok=True)
    run(a.build_tools / 'd8', '--lib', a.android_jar, '--min-api', '24', '--output', dex, jar)
    stamp_file.write_text(stamp)
libraries = [a.engine_library, a.deps_prefix / 'lib/libSDL2.so',
             a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib' / triple / 'libc++_shared.so']
libraries += [a.ffmpeg_libraries / name for name in FFMPEG_LIBRARIES]
libraries.append(a.archive_library)
if library:
    libraries.append(a.torrent_library)
libraries += [a.ui_library, a.bgfx_library]
# The relief generation in libengine.so (ncnn is linked in): the models (relief_models.py), put next to libengine.so
# by the engine build.
relief_dir = a.engine_library.parent
relief_models = [relief_dir / 'assets' / name for name in RELIEF_MODELS]
for file in relief_models:
    if not file.is_file():
        raise SystemExit(f'{file} is missing: build the engine target (it puts it there) in {relief_dir.parent}')


def engine_stamp(root):
    """The stamp engine/cmake/build_stamp.cmake gives a library built from this checkout."""
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
    return commit + ('+dirty' if dirty else '')


def check_stamp(library, expected):
    """A native library from another commit silently lacks newer engine code: refuse it."""
    found = re.findall(rb'storm-build-stamp:([0-9a-f]{40}(?:\+dirty)?)', Path(library).read_bytes())
    if len(set(found)) != 1:
        raise SystemExit(f'{library}: no engine build stamp (not built by engine/)')
    if found[0].decode() != expected:
        raise SystemExit(f'{library} was built from commit {found[0].decode()}, but the checkout is at {expected}: rebuild it')


stamp = engine_stamp(a.engine_root)
check_stamp(a.engine_library, stamp)
check_stamp(a.ui_library, stamp)
check_stamp(a.bgfx_library, stamp)
symbols = b / 'symbols' / a.abi
stripped = b / 'stripped'
native_sizes = {}
if release:
    # Ship stripped libraries; keep the exact unstripped inputs, under their
    # packaged names, for ndk-stack / llvm-symbolizer / Play's symbol upload.
    tools = a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
    for directory in (symbols, stripped):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    packaged = {}
    for file in libraries:
        name = file.name
        shutil.copyfile(file, symbols / name)
        # --strip-unneeded keeps .dynsym/.dynamic (JNI and inter-library exports) and the
        # GNU build-id that ties a crash frame to its symbol file.
        run(tools / 'llvm-strip', '--strip-unneeded', '-o', stripped / name, file)
        build_id = subprocess.check_output([str(tools / 'llvm-readelf'), '-n', str(stripped / name)], text=True)
        full_id = subprocess.check_output([str(tools / 'llvm-readelf'), '-n', str(symbols / name)], text=True)
        ids = [[line.split()[-1] for line in text.splitlines() if 'Build ID' in line] for text in (build_id, full_id)]
        if ids[0] != ids[1]:
            raise RuntimeError(f'Build ID changed while stripping {name}')
        native_sizes[name] = (file.stat().st_size, (stripped / name).stat().st_size, ids[0][0] if ids[0] else '')
        packaged[file] = stripped / name
    with zipfile.ZipFile(b / 'native-debug-symbols.zip', 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for file in sorted(symbols.iterdir()):
            z.write(file, a.abi + '/' + file.name)
else:
    packaged = {file: file for file in libraries}
# The build id -> symbols index of this package's native libraries, kept beside the APK (never packaged): a native
# crash's backtrace in the diagnostics export names each frame's build id, tools/android/symbolize.py finds the
# unstripped library by it (release: the copies in symbols/; debug: the libraries as built, which the APK carries).
readelf = a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf'
build_ids, without_id = {}, {}
for file in libraries:
    name = file.name
    source = symbols / name if release else file.resolve()
    notes = subprocess.check_output([str(readelf), '-n', str(source)], text=True)
    found = [line.split()[-1] for line in notes.splitlines() if 'Build ID' in line]
    if found:
        build_ids[found[0]] = {'library': name, 'symbols': str(source)}
    else:
        without_id[name] = str(source)  # its frames are matched by name (FFmpeg's build links without --build-id)
if without_id:
    print('Native libraries without a GNU build id (crash frames matched by library name only): ' + ', '.join(sorted(without_id)))
(b / 'build-ids.json').write_text(json.dumps({'abi': a.abi, 'libraries': build_ids, 'without_build_id': without_id},
                                             indent=1, sort_keys=True) + '\n')
# Native libraries must resolve inside the APK, checked here at build time, not on a device: a library the Java
# side loads but the package lacks leaves the engine unloaded and every start throws UnsatisfiedLinkError.
# 1. Every library the app's Java loads (SDL's getLibraries() list, System.loadLibrary("...")) in the sources this
#    flavor compiles is packaged.
# 2. Every DT_NEEDED of every packaged library is packaged or one of Android's public NDK system libraries.
packaged_names = {source.name for source in libraries}
loaded = set()
for source in app_sources:
    text = source.read_text(errors='replace')
    listed = re.search(r'getLibraries\(\)\s*\{\s*return new String\[\]\s*\{([^}]*)\}', text)
    if listed:
        loaded.update(re.findall(r'"([^"]+)"', listed.group(1)))
    loaded.update(re.findall(r'System\.loadLibrary\("([^"]+)"\)', text))
if not any('getLibraries' in source.read_text(errors='replace') for source in app_sources):
    raise SystemExit('No getLibraries() in the app sources: the packaged-library check cannot run')
missing = sorted(name for name in loaded if 'lib' + name + '.so' not in packaged_names)
if missing:
    raise SystemExit('The app loads native libraries the APK does not package: ' + ', '.join(missing))
ANDROID_SYSTEM_LIBRARIES = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so', 'libEGL.so', 'libGLESv1_CM.so',
                            'libGLESv2.so', 'libGLESv3.so', 'libvulkan.so', 'libOpenSLES.so', 'libaaudio.so', 'libamidi.so',
                            'libmediandk.so', 'libnativewindow.so', 'libz.so', 'libjnigraphics.so', 'libsync.so',
                            'libbinder_ndk.so', 'libneuralnetworks.so', 'libcamera2ndk.so', 'libicu.so'}
for source in libraries:
    dynamic = subprocess.check_output([str(readelf), '-d', str(packaged[source])], text=True)
    for needed in re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', dynamic):
        if needed not in packaged_names and needed not in ANDROID_SYSTEM_LIBRARIES:
            raise SystemExit(f'{source.name} needs {needed}, which is neither packaged nor an Android system library')
with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as z:
    for file in dex.glob('*.dex'):
        z.write(file, file.name)
    for source in libraries:
        file = packaged[source]
        data = file.read_bytes()
        header = data[:20]
        if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != elf_machine:
            raise RuntimeError(f'Not {a.abi} ELF: {file}')
        offset = struct.unpack_from('<Q', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 54)
        if size < 56 or offset + size * count > len(data):
            raise RuntimeError(f'Invalid ELF program headers: {file}')
        loads = [struct.unpack_from('<Q', data, offset + i * size + 48)[0]
                 for i in range(count) if struct.unpack_from('<I', data, offset + i * size)[0] == 1]
        if not loads or any(align < 16384 for align in loads):
            raise RuntimeError(f'Native library needs 16 KiB LOAD alignment: {file}')
        z.write(file, 'lib/' + a.abi + '/' + source.name)
    for name, data in collect_menu_locales(ROOT / 'android/game/assets/locales').items():
        z.writestr(name, data)
    script_patches, script_patch_notice = collect_script_patches(ROOT / 'script-patches')
    z.writestr('assets/licenses/script-patches.txt', script_patch_notice)
    for layer, files in [('mobile-ui', collect_mobile_ui(ROOT / 'mobile-ui')),
                         ('engine-shaders', collect_shaders(a.engine_root)),
                         ('script-patches', script_patches)]:
        for name, data in files.items():
            z.writestr('assets/' + layer + '/' + name, data)
            staged = ROOT / 'build/apk-assets' / layer / name
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_bytes(data)
    for name, data in collect_ui_pack(ROOT, a.engine_root, a.ui_gallery).items():
        z.writestr('assets/ui/' + name, data)
    for file in sorted(a.ui_licenses.glob('*.txt')):
        z.write(file, 'assets/licenses/ui-' + file.name)
    for file in sorted((ROOT / 'mobile-ui/design/fonts').glob('OFL-*.txt')):
        z.write(file, 'assets/licenses/font-' + file.name)
    for project, filename in [('fmt', 'LICENSE'), ('spdlog', 'LICENSE'), ('fast_float', 'LICENSE-MIT'),
                              ('fast_float', 'LICENSE-APACHE')]:
        z.write(a.engine_sources / project / filename, 'assets/licenses/' + project + '-' + filename)
    # Single-file libraries: their licence is the comment block that ends the source.
    for filename in ('miniaudio.h', 'stb_vorbis.c'):
        source = (a.audio_sources / filename).read_text(encoding='utf-8')
        start = source.rindex('/*', 0, source.rindex('This software is available'))
        z.writestr('assets/licenses/' + filename.split('.')[0] + '.txt', source[start:])
    z.write(a.engine_root / 'tools/android/patches/miniaudio-0.11.23-error-cleanup.patch',
            'assets/licenses/miniaudio-storm-patch.txt')
    z.write(a.engine_root / 'third_party/licenses/GLM-MIT.txt', 'assets/licenses/GLM-MIT.txt')
    # bgfx (libbgfx.so), the ETC2 encoder, Android Frame Pacing and the far LODs' meshoptimizer (in libengine.so).
    for filename in ('bgfx-BSD-2-Clause.txt', 'bx-BSD-2-Clause.txt', 'bimg-BSD-2-Clause.txt', 'etcpak-BSD-3-Clause.txt',
                     'Swappy-Apache-2.0.txt', 'meshoptimizer-MIT.txt'):
        z.write(a.engine_root / 'third_party/licenses' / filename, 'assets/licenses/' + filename)
    # The graphics effects' third-party code in libengine.so (renderer_bgfx/fx/THIRD_PARTY.md):
    # SMAA, AMD FidelityFX FSR 1 (with the Michal Drobot notice), the ACES fit, the baked probes' ray BVH (madmann91/bvh), the lamp shadows' PCF.
    for filename in ('SMAA-MIT.txt', 'FidelityFX-FSR-MIT.txt', 'threejs-MIT.txt', 'bvh-MIT.txt', 'MJP-Shadows-MIT.txt'):
        z.write(a.engine_root / 'third_party/licenses' / filename, 'assets/licenses/' + filename)
    if tracy:
        z.write(a.engine_root / 'third_party/licenses/Tracy-BSD-3-Clause.txt', 'assets/licenses/Tracy-BSD-3-Clause.txt')
    # FFmpeg (LGPL-2.1+, shared libraries) and the exact configuration it was built with.
    z.write(a.engine_root / 'third_party/licenses/FFmpeg-LGPL-2.1.txt', 'assets/licenses/FFmpeg-LGPL-2.1.txt')
    # The relief: PBRify's models (CC0-1.0) with their provenance, run by ncnn (BSD-3-Clause) on the GPU or the CPU,
    # with its glslang (the GPU's compute shaders).
    for model in relief_models:
        z.write(model, 'assets/' + model.name)
    for filename in ('PBRify-CC0-1.0.txt', 'PBRify-PROVENANCE.txt', 'ncnn-BSD-3-Clause.txt', 'glslang-LICENSE.txt'):
        z.write(a.engine_root / 'third_party/licenses' / filename, 'assets/licenses/' + filename)
    z.write(a.ffmpeg_libraries / 'configure.txt', 'assets/licenses/FFmpeg-configure.txt')
    for file in sorted((a.engine_root / 'android/archive/licenses').glob('*.txt')):
        z.write(file, 'assets/licenses/archive-' + file.name)
    if library:
        for filename in flavors.TORRENT_LICENSES:
            z.write(a.engine_root / 'third_party/licenses' / filename, 'assets/licenses/' + filename)
    # Every engine notice ships: a new engine dependency must not reach the APK without its licence.
    # Only flavor- or build-specific code may leave its notice out, when that code is not packaged.
    absent = set() if library else set(flavors.TORRENT_LICENSES)
    # libtorrent and the archive reader use the same pinned Boost 1.90.0: its one notice is archive-boost_1_90_0.txt.
    engine_licenses = a.engine_root / 'third_party/licenses'
    if (engine_licenses / 'Boost-BSL-1.0.txt').read_bytes() != (a.engine_root / 'android/archive/licenses/boost_1_90_0.txt').read_bytes():
        raise RuntimeError('The torrent client\'s Boost notice differs from the archive reader\'s: package both')
    absent.add('Boost-BSL-1.0.txt')
    if not tracy:
        absent.add('Tracy-BSD-3-Clause.txt')
    missing = sorted(f.name for f in (a.engine_root / 'third_party/licenses').iterdir()
                     if f.name not in absent and 'assets/licenses/' + f.name not in z.namelist())
    if missing:
        raise RuntimeError('Engine licence notices not packaged (add them above): ' + ', '.join(missing))
    # The exact corresponding source for the About / licences screen: one commit of the repository holds the
    # application and the engine (GPL-3.0).
    commit = subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip()
    info = {'flavor': a.flavor, 'applicationId': a.application_id, 'versionName': version_name,
            'versionCode': version_code, 'targetSdk': int(TARGET_SDK), 'commit': commit,
            'testLoop': a.test_loop,
            'source': 'https://github.com/maxyo/seawolves/tree/' + commit}
    z.writestr('assets/build-info.json', json.dumps(info, indent=2) + '\n')
    # The repository's NOTICE (which parts are GPL-3.0-only and which -or-later, origins, no game content) and
    # the GPL-3.0 text, once for the application and once under the engine's name.
    if (REPO / 'LICENSE').read_bytes() != (a.engine_root / 'LICENSE.txt').read_bytes():
        raise RuntimeError('LICENSE and engine/LICENSE.txt must both be the GPL-3.0 text')
    z.write(REPO / 'NOTICE', 'assets/licenses/seawolves-NOTICE.txt')
    z.write(REPO / 'LICENSE', 'assets/licenses/seawolves-GPL-3.0.txt')
    z.write(a.engine_root / 'LICENSE.txt', 'assets/licenses/storm-GPLv3.txt')
    z.write(a.sdl_source / 'LICENSE.txt', 'assets/licenses/SDL.txt')
    z.write(a.ndk / 'NOTICE.toolchain', 'assets/licenses/NDK-toolchain-NOTICE.txt')
aligned = b / 'aligned.apk'
run(a.build_tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned)
key = Path(os.environ.get('STORM_ANDROID_KEYSTORE', a.default_keystore or b / 'debug.keystore')).resolve()
if not key.exists():
    if os.environ.get('STORM_ANDROID_KEYSTORE'):
        raise RuntimeError('Persistent signing keystore missing; refusing to generate a different update identity')
    run('keytool', '-genkeypair', '-keystore', key, '-storepass', 'android', '-keypass', 'android',
        '-alias', 'androiddebugkey', '-dname', 'CN=Android Debug,O=Android,C=US',
        '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000')
output = b / f'seawolves-{a.flavor}-{ABIS[a.abi][0]}-{a.build_type}.apk'
os.environ.setdefault('STORM_ANDROID_STORE_PASSWORD', 'android')
os.environ.setdefault('STORM_ANDROID_KEY_PASSWORD', 'android')
run(a.build_tools / 'apksigner', 'sign', '--ks', key,
    '--ks-key-alias', os.environ.get('STORM_ANDROID_KEY_ALIAS', 'androiddebugkey'),
    '--ks-pass', 'env:STORM_ANDROID_STORE_PASSWORD', '--key-pass', 'env:STORM_ANDROID_KEY_PASSWORD',
    '--out', output, aligned)
run(a.build_tools / 'apksigner', 'verify', '--verbose', output)
run(a.build_tools / 'zipalign', '-c', '-P', '16', '4', output)
if release:
    # The packaged manifest, not the one we wrote, is what Android reads.
    badging = subprocess.check_output([str(a.build_tools / 'aapt2'), 'dump', 'badging', str(output)], text=True)
    if 'application-debuggable' in badging:
        raise RuntimeError('Release APK is debuggable')
    for name, (before, after, build_id) in sorted(native_sizes.items()):
        print(f'{name}: {before} -> {after} bytes stripped, build-id {build_id}')
    print(f'Symbols: {symbols} and {b / "native-debug-symbols.zip"} (keep with this exact APK)')
print(f'Build ids of the native libraries: {b / "build-ids.json"} (tools/android/symbolize.py)')
print(f'Created {output} ({output.stat().st_size} bytes, {a.build_type}); not yet run on a device.')
