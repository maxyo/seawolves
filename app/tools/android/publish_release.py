#!/usr/bin/env python3
"""Publish the GitHub release of a tag from the APK its release workflow run just built.

    publish_release.py --tag v1.0.0-beta.1 --artifact <downloaded android-release-apk> --commit <tag commit>
        --apksigner <build-tools>/apksigner --out <empty directory> [--repository <account>/<name>] [--dry-run]

Run by the release workflow's publish job on the tag's checkout. It publishes nothing unless all of this holds:

- the tag is a release tag for VERSION, names this commit, its CHANGELOG section is dated and its versionCode is
  above every earlier one (release_version.py check; `--dry-run` rehearses a tag that does not exist yet);
- the artifact is exactly the github release APK, SHA256SUMS and build-manifest.json; the APK matches
  its checksum and the manifest (this commit, a clean tree, release build of the Release engine, the tag's
  versionName and versionCode) and its packaged build-info;
- the APK has one signer, the main release key (release_signing.py).

It stages the assets in --out (seawolves-<version>-arm64.apk, SHA256SUMS for that name, RELEASE-NOTES.md from the
CHANGELOG section with the checksum, the signing certificate and the exact sources) and, unless --dry-run, creates
the release of the existing tag (a pre-release for alpha, beta and rc), downloads the APK back and compares it. An
existing release is never replaced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_version  # noqa: E402
from release_signing import SigningError, check_signature  # noqa: E402

APK = 'seawolves-arm64-release.apk'
ARTIFACT_FILES = {APK, 'SHA256SUMS', 'build-manifest.json'}
REPOSITORY = 'maxyo/seawolves'


class PublishError(Exception):
    pass


def require(condition, reason):
    if not condition:
        raise PublishError(reason)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as file:
        for block in iter(lambda: file.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def check_artifact(directory, commit, version):
    """Validate the downloaded android-release-apk artifact; return (apk path, its SHA-256)."""
    entries = list(directory.iterdir())
    require({path.name for path in entries} == ARTIFACT_FILES,
            f'unexpected android-release-apk contents: {sorted(path.name for path in entries)}')
    require(all(path.is_file() and not path.is_symlink() for path in entries), 'artifact entries must be regular files')
    apk = directory / APK
    digest = sha256(apk)
    require((directory / 'SHA256SUMS').read_text() == f'{digest}  {APK}\n', 'SHA256SUMS does not match the APK')
    manifest = json.loads((directory / 'build-manifest.json').read_text())
    expected = {'apk_sha256': digest, 'commit': commit, 'dirty': False, 'flavor': 'github',
                'build_type': 'release', 'native_build_type': 'Release', 'abi': 'arm64-v8a',
                'game_resources_included': False,
                'version_code': version.code, 'version_name': version.text}
    for field, value in expected.items():
        require(manifest.get(field) == value, f'build manifest {field} is {manifest.get(field)!r}, expected {value!r}')
    with zipfile.ZipFile(apk) as z:
        info = json.loads(z.read('assets/build-info.json'))
    for field, value in {'flavor': 'github', 'testLoop': False, 'commit': commit,
                         'versionCode': version.code, 'versionName': version.text}.items():
        require(info.get(field) == value, f'APK build-info {field} is {info.get(field)!r}, expected {value!r}')
    return apk, digest


def release_notes(section, version, apk_name, digest, certificate, commit, repository):
    return (f'{section}\n\n---\n\n'
            f'- APK: `{apk_name}`, versionName `{version.text}`, versionCode `{version.code}`\n'
            f'- SHA-256: `{digest}`\n'
            f'- Signing certificate SHA-256: `{certificate}`\n'
            f'- Source (application and engine): [seawolves@{commit[:12]}](https://github.com/{repository}/tree/{commit})\n')


def gh(*args):
    result = subprocess.run(['gh', *args], stdout=subprocess.PIPE, text=True)
    require(result.returncode == 0, 'gh ' + ' '.join(args) + f' failed ({result.returncode})')
    return result.stdout


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--tag', required=True)
    p.add_argument('--artifact', type=Path, required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--apksigner', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--repository', default=REPOSITORY)
    p.add_argument('--dry-run', action='store_true', help='stage and check everything, publish nothing')
    a = p.parse_args()
    try:
        require(re.fullmatch(r'[0-9a-f]{40}', a.commit), 'invalid commit SHA')
        require(release_version.git('rev-parse', 'HEAD') == a.commit, 'the checkout is not the commit being released')
        require(not a.out.exists() or not any(a.out.iterdir()), f'{a.out} is not empty')
        version, section = release_version.check(a.tag, rehearse=a.dry_run)
        apk, digest = check_artifact(a.artifact, a.commit, version)
        certificate = check_signature(a.apksigner, apk)
        a.out.mkdir(parents=True, exist_ok=True)
        name = f'seawolves-{version.text}-arm64.apk'
        shutil.copyfile(apk, a.out / name)
        require(sha256(a.out / name) == digest, 'the staged APK differs from the verified one')
        (a.out / 'SHA256SUMS').write_text(f'{digest}  {name}\n')
        notes = a.out / 'RELEASE-NOTES.md'
        notes.write_text(release_notes(section, version, name, digest, certificate, a.commit, a.repository))
        kind = 'pre-release' if version.prerelease else 'release'
        if a.dry_run:
            print(f'Dry run: {kind} {a.tag} staged in {a.out} (versionCode {version.code}, APK SHA-256 {digest}, '
                  f'main key {certificate}); nothing published')
            return
        exists = subprocess.run(['gh', 'release', 'view', a.tag, '--repo', a.repository],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        require(exists.returncode != 0, f'release {a.tag} exists already; a release is never replaced')
        flags = ['--prerelease', '--latest=false'] if version.prerelease else ['--latest']
        gh('release', 'create', a.tag, str(a.out / name), str(a.out / 'SHA256SUMS'), '--repo', a.repository,
           '--verify-tag', '--title', f'Seawolves {version.text}', '--notes-file', str(notes), *flags)
        with tempfile.TemporaryDirectory(prefix='release-download-') as back:
            gh('release', 'download', a.tag, '--repo', a.repository, '--pattern', name, '--dir', back)
            require(sha256(Path(back) / name) == digest, f'the APK downloaded from {a.tag} differs from the verified one')
        url = json.loads(gh('release', 'view', a.tag, '--repo', a.repository, '--json', 'url'))['url']
        print(f'Published {kind} {url}\nAPK {name} SHA-256 {digest} (downloaded back and compared)\n'
              f'Signer certificate SHA-256 {certificate} (main release key)\nCommit {a.commit}')
    except (PublishError, release_version.ReleaseError, SigningError) as error:
        sys.exit(f'publish_release: {error}')


if __name__ == '__main__':
    main()
