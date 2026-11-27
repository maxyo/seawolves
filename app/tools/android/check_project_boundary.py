#!/usr/bin/env python3
"""Keep the two parts of the repository apart: native code under engine/, the Android application under app/.

The repository holds two parts: engine/ (every library, native C/C++ code, the renderer and its shaders) and app/
(the Android shell, the interface pack and its scripts, APK packaging). The APK packages the engine's libraries from
its build; no native code or third-party native library is kept in app/, no Android packaging in engine/, and
nothing lives at the root but the documents, the licences and the repository's configuration files. docs/ holds
only what the README shows: the app icon (README_ICON, rendered from app/android/game/icon) and the three
screenshots in docs/screenshots (SCREENSHOTS, named in NOTICE).
"""
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[3]
native_suffixes = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.ipp', '.inl',
                   '.so', '.a', '.o', '.obj', '.dll', '.dylib', '.lib', '.glsl', '.fx', '.sc'}
# Application packaging (the engine keeps only the Java hosts of its own JNI tests).
packaging_suffixes = {'.gradle', '.apk', '.aar', '.dex', '.keystore', '.jks'}
ROOT_FILES = {'.gitattributes', '.gitignore', 'CHANGELOG.md', 'CODE_OF_CONDUCT.md', 'CONTRIBUTING.md', 'LICENSE',
              'NOTICE', 'PRIVACY.md', 'README.md', 'README.ru.md', 'SECURITY.md', 'VERSION'}
ROOT_DIRECTORIES = {'.github', 'LICENSES', 'app', 'docs', 'engine'}
SCREENSHOTS = {'docs/screenshots/interior.jpg', 'docs/screenshots/sea.jpg', 'docs/screenshots/hero.jpg'}
README_ICON = 'docs/icon.png'
native, packaging, links, stray = [], [], [], []
for line in filter(None, subprocess.check_output(['git', '-C', str(repo), 'ls-files', '--stage', '-z']).decode().split('\0')):
    mode, name = line.split(' ', 1)[0], line.split('\t', 1)[1]
    path = Path(name)
    if mode == '160000':
        links.append(name)
    top = path.parts[0]
    if top not in (ROOT_FILES if len(path.parts) == 1 else ROOT_DIRECTORIES):
        stray.append(name)
    elif top == 'docs':
        # The README's pictures only: its icon, and the screenshots NOTICE names as the one exception for game content.
        if name not in SCREENSHOTS and name != README_ICON:
            stray.append(name)
    elif top == 'engine':
        if path.suffix.lower() in packaging_suffixes or path.name == 'AndroidManifest.xml':
            packaging.append(name)
    elif top == 'app':
        # These are interpreted game UI scripts, not C translation units.
        if name.startswith('app/mobile-ui/PROGRAM/') and path.suffix == '.c':
            continue
        if path.suffix.lower() in native_suffixes or name.startswith(('app/third_party/', 'app/vendor/')):
            native.append(name)
if native:
    raise SystemExit('Native sources/libraries belong under engine/:\n' + '\n'.join(native))
if packaging:
    raise SystemExit('Android packaging belongs under app/:\n' + '\n'.join(packaging))
if stray:
    raise SystemExit('Not under app/ or engine/ and not a root document (ROOT_FILES here) or a README picture (SCREENSHOTS, README_ICON here):\n' + '\n'.join(stray))
if links or (repo / '.gitmodules').exists():
    raise SystemExit('The engine is the plain directory engine/: no Git submodules\n' + '\n'.join(links))
if not (repo / 'engine/CMakeLists.txt').is_file() or not (repo / 'app/android/game/AndroidManifest.xml').is_file():
    raise SystemExit('engine/CMakeLists.txt or app/android/game/AndroidManifest.xml is missing')
print('PASS: native code and dependencies are under engine/, the application under app/; no submodules')
