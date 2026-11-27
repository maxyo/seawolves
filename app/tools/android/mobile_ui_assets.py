"""Allowlist and fingerprint the open-source Android interface layer."""
import hashlib
import json
from pathlib import Path


def collect(root: Path):
    files = {}
    for path in sorted(root.rglob('*')):
        if not path.is_file():
            continue
        name = path.relative_to(root).as_posix()
        lower = name.lower()
        if lower.startswith('design/'):
            continue  # UI design sources; their generated pack is packaged by collect_ui_pack
        if path.name == 'README.md':
            continue  # the documents beside the scripts are not payload
        allowed = name == 'SOURCE.json' or (
            lower.startswith('program/interface/') and path.suffix.lower() in ('.c', '.h'))
        if path.is_symlink() or not allowed:
            raise ValueError('Ineligible mobile UI payload: ' + name)
        files[name] = path.read_bytes()
    # The scripts' capability ids, generated from the one list (design/capabilities.json).
    design = _design_build(root)
    files['PROGRAM/interface/touch/capabilities.h'] = design.capability_header(design.load_capabilities()).encode()
    # The scripts are GPL-3.0 (NOTICE): the layer carries the text, kept once in the repository's LICENSES/.
    files['LICENSE'] = (root.resolve().parents[1] / 'LICENSES/GPL-3.0-only.txt').read_bytes()
    manifest = json.dumps({name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
                          sort_keys=True, indent=2).encode() + b'\n'
    files['manifest.json'] = manifest
    return files


def _design_build(root: Path):
    """mobile-ui/design/build.py as a module."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('mobile_ui_design_build', root / 'design/build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def collect_ui_pack(repo: Path, engine: Path, gallery: bool):
    """Generate the UI pack from mobile-ui/design and return its files (path -> bytes).

    Only generated text, the icon pack, the OFL fonts, our own material textures
    (mobile-ui/design/textures) and our own pictures (mobile-ui/design/images): the generator has no game data. The debug gallery pages
    only go with the gallery activity (gallery=True).
    """
    import subprocess
    import sys
    pack = repo / 'build/ui-pack'
    # The engine sources let the design build check the UI string keys its screens use.
    subprocess.run([sys.executable, str(repo / 'mobile-ui/design/build.py'), '--pack', str(pack),
                    '--engine-root', str(engine)], check=True)
    files = {}
    for path in sorted(pack.rglob('*')):
        if not path.is_file():
            continue
        name = path.relative_to(pack).as_posix()
        if name.startswith('gallery/') and not gallery:
            continue
        picture = (name.startswith('textures/') and path.suffix.lower() == '.jpg' or
                   name.startswith('images/') and path.suffix.lower() == '.png')
        if path.is_symlink() or not (picture or path.suffix.lower() in ('.json', '.rml', '.rcss', '.ttf')):
            raise ValueError('Ineligible UI pack file: ' + str(path))
        files[name] = path.read_bytes()
    if not gallery:
        index = json.loads(files['pack.json'])
        index['gallery'] = []
        files['pack.json'] = json.dumps(index, ensure_ascii=False, indent=1).encode()
    return files


def collect_shaders(engine: Path):
    """Engine-owned GPL technique sources (text only), never game assets."""
    import subprocess
    files = {}
    for path in sorted((engine / 'src/techniques').rglob('*')):
        if not path.is_file():
            continue
        if path.is_symlink() or path.suffix.lower() not in ('.fx', '.h', '.sha'):
            raise ValueError('Ineligible engine shader: ' + str(path))
        files['RESOURCE/techniques/' + path.relative_to(engine / 'src/techniques').as_posix()] = path.read_bytes()
    if not any(n.endswith('.fx') for n in files):
        raise ValueError('Missing engine techniques')
    files['LICENSE'] = (engine / 'LICENSE.txt').read_bytes()
    revision = subprocess.check_output(['git', '-C', str(engine), 'rev-parse', 'HEAD'], text=True).strip()
    files['SOURCE.json'] = json.dumps({'repository': 'https://github.com/maxyo/seawolves',
        'revision': revision, 'source': 'engine/src/techniques', 'license': 'GPL-3.0-only',
        'purpose': 'Open engine technique sources; no commercial game assets'}, indent=2).encode()
    files['manifest.json'] = json.dumps({n: hashlib.sha256(d).hexdigest() for n,d in files.items()},
                                       sort_keys=True, indent=2).encode() + b'\n'
    return files


def main():
    """mobile_ui_assets.py layer <mobile-ui dir> <out dir>: write the mobile-ui layer as the APK ships it (the engine's
    host checks run the interface scripts from it)."""
    import shutil
    import sys
    if len(sys.argv) != 4 or sys.argv[1] != 'layer':
        sys.exit(main.__doc__)
    out = Path(sys.argv[3])
    if out.exists():
        shutil.rmtree(out)
    for name, data in collect(Path(sys.argv[2]).resolve()).items():
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        (out / name).write_bytes(data)


if __name__ == '__main__':
    main()
