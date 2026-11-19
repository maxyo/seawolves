#!/usr/bin/env python3
"""Check a packaged APK against the sources and write its distributable copy with a build manifest.

    write_manifest.py --app <app/> --build-type debug|release --apk-root <dir>
        --relief-assets <engine build>/game/assets --output <dist dir> [--aapt2 <build-tools>/aapt2]

The APK's payload must be exactly what the sources give: the interface pack, the engine's shaders, the script
patches, the menu locales, the relief models, the platform resources, the native libraries (with the archive reader
and the torrent client) and the licence notices; anything else in the archive fails the build. The output directory gets the APK under its
distribution name, SHA256SUMS and build-manifest.json (commit, clean or dirty tree, build types, version),
which the release publication checks again (publish_release.py). The manifest records what the build checked; it
says nothing about installation or gameplay on a device.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

import apk_payload
from menu_locales import collect as collect_menu_locales
from mobile_ui_assets import collect as collect_mobile_ui, collect_shaders, collect_ui_pack
from platform_resources import PACKAGED_ENTRIES, resource_directory
from relief_models import RELIEF_MODELS
from script_patch_assets import collect as collect_script_patches

ABI = 'arm64-v8a'
# libfmt and ncnn are linked into libengine.so.
REQUIRED_LIBRARIES = ('engine', 'SDL2', 'c++_shared', 'storm_ui', 'bgfx')


def git_output(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def expected_payload(app, engine, relief_assets, gallery):
    """Every APK entry that comes from the sources, with its exact bytes."""
    payload = {'assets/mobile-ui/' + name: data for name, data in collect_mobile_ui(app / 'mobile-ui').items()}
    payload.update({'assets/engine-shaders/' + name: data for name, data in collect_shaders(engine).items()})
    script_patches, script_patch_notice = collect_script_patches(app / 'script-patches')
    payload.update({'assets/script-patches/' + name: data for name, data in script_patches.items()})
    payload['assets/licenses/script-patches.txt'] = script_patch_notice
    payload.update(collect_menu_locales(app / 'android/game/assets/locales'))
    payload.update({'assets/ui/' + name: data for name, data in collect_ui_pack(app, engine, gallery).items()})
    payload.update({'assets/' + name: (relief_assets / name).read_bytes() for name in RELIEF_MODELS})
    return payload


def check_apk(apk, app, engine, relief_assets):
    """Fail on any payload difference; return the APK's build-info."""
    with zipfile.ZipFile(apk) as z:
        names = z.namelist()
        resource_directory(app)  # refuses a resource file that platform_resources.py does not name
        missing_resources = PACKAGED_ENTRIES - set(names)
        if missing_resources:
            raise SystemExit('Missing platform artwork: ' + repr(sorted(missing_resources)))
        for library in REQUIRED_LIBRARIES:
            if f'lib/{ABI}/lib{library}.so' not in names:
                raise SystemExit(f'APK without lib{library}.so')
        dex = b''.join(z.read(name) for name in names if re.fullmatch(r'classes[0-9]*\.dex', name))
        gallery = b'Lorg/stormengine/port/UiGalleryActivity;' in dex
        payload = expected_payload(app, engine, relief_assets, gallery)
        for name, data in payload.items():
            if z.read(name) != data:
                raise SystemExit('UI payload mismatch: ' + name)
        allowed = {'AndroidManifest.xml', 'classes.dex', 'resources.arsc', 'assets/build-info.json',
                   *PACKAGED_ENTRIES, *payload}
        unexpected = [name for name in names if name not in allowed
                      and not name.startswith((f'lib/{ABI}/', 'assets/licenses/', 'META-INF/'))]
        if unexpected:
            raise SystemExit('Unexpected APK payload: ' + repr(unexpected))
        # The archive import and the download library with its torrent client.
        problems = apk_payload.apk_problems(names, dex)
        if problems:
            raise SystemExit('\n'.join(problems))
        return json.loads(z.read('assets/build-info.json'))


def packaged_version(aapt2, apk, info):
    """versionCode and versionName as Android reads them from the packaged manifest."""
    badging = subprocess.check_output([str(aapt2), 'dump', 'badging', str(apk)], text=True)
    package = re.search(r"package: name='" + re.escape(info['applicationId']) + r"' versionCode='([0-9]+)'", badging)
    if not package:
        raise SystemExit('Missing APK versionCode')
    name = re.search(r"versionName='([^']*)'", badging)
    if not name or name[1] != info['versionName']:
        raise SystemExit('APK versionName missing or differs from build-info')
    code = int(package[1])
    requested = os.environ.get('STORM_ANDROID_VERSION_CODE')
    if requested is not None and code != int(requested):
        raise SystemExit('APK versionCode differs from the requested release version')
    return code, name[1]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--aapt2', type=Path)
    # The engine build's assets/ next to libengine.so: the relief models packaged are checked byte for byte against it.
    p.add_argument('--relief-assets', type=Path, required=True)
    p.add_argument('--build-type', choices=('debug', 'release'), default='debug')
    p.add_argument('--apk-root', type=Path, help='Directory holding seawolves-arm64-<build type>.apk')
    a = p.parse_args()

    app = a.app.resolve()
    repository = app.parent
    # The APK keeps its name in the output directory: the release publication expects it (publish_release.py).
    name = f'seawolves-arm64-{a.build_type}.apk'
    a.output.mkdir(parents=True, exist_ok=True)
    apk = a.output / name
    shutil.copyfile((a.apk_root or app / 'build/apk') / name, apk)
    info = check_apk(apk, app, repository / 'engine', a.relief_assets)
    digest = hashlib.sha256(apk.read_bytes()).hexdigest()
    manifest = {
        'application_id': info['applicationId'],
        'target_sdk': info['targetSdk'],
        'commit': git_output(repository, 'rev-parse', 'HEAD'),
        'dirty': bool(git_output(repository, 'status', '--porcelain')),
        'abi': ABI,
        'apk_sha256': digest,
        'build_type': a.build_type,
        'native_build_type': os.environ.get('STORM_NATIVE_BUILD_TYPE', 'RelWithDebInfo'),
        'game_resources_included': False,
        'verified': ['native libraries linked for arm64-v8a', 'Java and DEX compile', 'APK signature and alignment',
                     'APK payload equals the sources'],
        'not_verified': ['installation on a device', 'game import', 'launch and gameplay'],
    }
    if a.aapt2:
        manifest['version_code'], manifest['version_name'] = packaged_version(a.aapt2, apk, info)
    (a.output / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (a.output / 'SHA256SUMS').write_text(f'{digest}  {apk.name}\n')
    print(apk, digest)


if __name__ == '__main__':
    main()
