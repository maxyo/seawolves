#!/usr/bin/env python3
"""Check a packaged APK against the sources and write its distributable copy with a build manifest.

    write_manifest.py --app <app/> --build-type debug|release --apk-root <dir> --output <dist dir>
        [--aapt2 <build-tools>/aapt2]

The APK may hold only the interface pack, the engine's shaders, the script patches, the relief models, the
platform resources, the native libraries and the licence notices; anything else fails the build, as does a
missing native library or third-party notice. The output directory gets the APK under its distribution name, SHA256SUMS and build-manifest.json (commit, clean or dirty tree, build types, version),
which the release signing and publication check again (sign_release.py, publish_release.py). The manifest records what the build checked; it
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
from platform_resources import PACKAGED_ENTRIES, resource_directory

ABI = 'arm64-v8a'
# The layers build_apk.py writes from the sources (mobile_ui_assets.py, script_patch_assets.py).
PAYLOAD_PREFIXES = ('assets/mobile-ui/', 'assets/engine-shaders/', 'assets/script-patches/', 'assets/ui/',
                    'assets/licenses/', f'lib/{ABI}/', 'META-INF/')


def git_output(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def check_apk(apk, app):
    """Fail on a missing or unexpected APK entry; return the APK's build-info."""
    with zipfile.ZipFile(apk) as z:
        names = z.namelist()
        resource_directory(app)  # refuses a resource file that platform_resources.py does not name
        missing_resources = PACKAGED_ENTRIES - set(names)
        if missing_resources:
            raise SystemExit('Missing platform artwork: ' + repr(sorted(missing_resources)))
        allowed = {'AndroidManifest.xml', 'classes.dex', 'resources.arsc', 'assets/build-info.json',
                   *PACKAGED_ENTRIES, *('assets/' + name for name in apk_payload.RELIEF_MODELS)}
        unexpected = [name for name in names if name not in allowed and not name.startswith(PAYLOAD_PREFIXES)]
        if unexpected:
            raise SystemExit('Unexpected APK payload: ' + repr(unexpected))
        problems = apk_payload.apk_problems(names)
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
    p.add_argument('--build-type', choices=('debug', 'release'), default='debug')
    p.add_argument('--apk-root', type=Path, help='Directory holding the APK build_apk.py made (apk_payload.apk_name)')
    a = p.parse_args()

    app = a.app.resolve()
    repository = app.parent
    # The APK keeps its name in the output directory: the release signing expects it (sign_release.py).
    name = apk_payload.apk_name('arm64', a.build_type)
    a.output.mkdir(parents=True, exist_ok=True)
    apk = a.output / name
    shutil.copyfile((a.apk_root or app / 'build/apk') / name, apk)
    info = check_apk(apk, app)
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
        'verified': ['native libraries linked for arm64-v8a', 'Java and DEX compile',
                     'APK alignment' if a.build_type == 'release' else 'APK signature and alignment',
                     'APK entries are the packaged layers'],
        'not_verified': ['installation on a device', 'game import', 'launch and gameplay'],
    }
    if a.aapt2:
        manifest['version_code'], manifest['version_name'] = packaged_version(a.aapt2, apk, info)
    (a.output / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (a.output / 'SHA256SUMS').write_text(f'{digest}  {apk.name}\n')
    print(apk, digest)


if __name__ == '__main__':
    main()
