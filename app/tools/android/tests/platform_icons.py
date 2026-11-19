#!/usr/bin/env python3
"""Compile/link only the original artwork as a resource library, then compile Java.

No APK, DEX, native build, signing, installation or device access is performed.
"""
from pathlib import Path
import argparse
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/android'))
from platform_resources import PACKAGED_ENTRIES, resource_directory
import apk_payload

NS = '{http://schemas.android.com/apk/res/android}'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--android-jar', type=Path, required=True)
parser.add_argument('--aapt2', type=Path, required=True)
parser.add_argument('--sdl-source', type=Path, required=True)
args = parser.parse_args()
res = resource_directory(ROOT)
manifest = ET.parse(ROOT / 'android/game/AndroidManifest.xml')
application = manifest.getroot().find('application')
assert application.get(NS + 'icon') == '@mipmap/ic_launcher'
assert application.get(NS + 'roundIcon') == '@mipmap/ic_launcher_round'
for name in ('download', 'pause'):
    vector = ET.parse(res / f'drawable/ic_notification_{name}.xml').getroot()
    assert vector.tag == 'vector'
    assert vector.get(NS + 'width') == vector.get(NS + 'height') == '24dp'
    for path in vector.iter('path'):
        assert path.get(NS + 'strokeColor') == '#FFFFFFFF'
        assert path.get(NS + 'fillColor') in ('#FFFFFFFF', '@android:color/transparent')
for version in ('v26', 'v33'):
    for icon in ('ic_launcher', 'ic_launcher_round'):
        adaptive = ET.parse(res / f'mipmap-anydpi-{version}/{icon}.xml').getroot()
        assert adaptive.tag == 'adaptive-icon'
        assert adaptive.find('background').get(NS + 'drawable') == '@drawable/ic_launcher_background'
        assert adaptive.find('foreground').get(NS + 'drawable') == '@drawable/ic_launcher_foreground'
        assert (adaptive.find('monochrome') is not None) == (version == 'v33')
        if version == 'v33':
            assert adaptive.find('monochrome').get(NS + 'drawable') == '@drawable/ic_launcher_foreground'
# The icon packaging must not become a blanket res/ allowlist: an unexpected resource is refused.
with tempfile.TemporaryDirectory(prefix='unexpected-platform-art-') as temp:
    fake = Path(temp)
    copy = fake / 'android/game/res'
    copy.mkdir(parents=True)
    for path in res.rglob('*'):
        if path.is_file():
            target = copy / path.relative_to(res)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
    (copy / 'unexpected.xml').write_text('<resources/>')
    try:
        resource_directory(fake)
    except ValueError:
        pass
    else:
        raise AssertionError('Unexpected resources were accepted')
with tempfile.TemporaryDirectory(prefix='platform-icons-') as temp:
    work = Path(temp)
    generated = work / 'generated'
    generated.mkdir()
    manifest.getroot().set('package', 'com.dustoff.seawolves')
    manifest.write(work / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)
    subprocess.run([str(args.aapt2), 'compile', '--dir', str(res), '-o', str(work / 'compiled.zip')], check=True)
    library = work / 'resources.ap_'
    subprocess.run([str(args.aapt2), 'link', '--static-lib', '-I', str(args.android_jar),
                    '--manifest', str(work / 'AndroidManifest.xml'),
                    '--custom-package', 'org.stormengine.port', '--java', str(generated), '--min-sdk-version', '24',
                    '--target-sdk-version', '36', '-o', str(library), str(work / 'compiled.zip')], check=True)
    with zipfile.ZipFile(library) as archive:
        entries = set(archive.namelist())
        assert entries == PACKAGED_ENTRIES | {'AndroidManifest.xml', 'resources.pb'}, entries
    text = (generated / 'org/stormengine/port/R.java').read_text()
    for symbol in ('ic_notification_download', 'ic_notification_pause', 'ic_launcher', 'ic_launcher_round',
                   'ic_launcher_background', 'ic_launcher_foreground'):
        assert re.search(r'\b' + symbol + r'=', text), symbol + ' missing from R.java'
    sources = sorted((args.sdl_source / 'android-project/app/src/main/java').rglob('*.java'))
    # Every optional source.
    sources += apk_payload.app_java_sources(ROOT, ui_gallery=True, test_loop=True)
    sources += sorted(generated.rglob('*.java'))
    subprocess.run(['javac', '-source', '8', '-target', '8', '-classpath', str(args.android_jar),
                    '-d', str(work / 'classes'), *map(str, sources)], check=True)
    print('PASS platform vectors: exact resource payload, adaptive/themed/legacy layers, white notification art, '
          f'generated R, {len(sources)} Java sources; no APK/device claims')
