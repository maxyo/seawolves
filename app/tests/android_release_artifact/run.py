#!/usr/bin/env python3
"""Release from a tag: version parsing, the release guards, the signing and the publication, with a fake tag, gh
and apksigner.

No GitHub, no APK execution, no tag in this repository. The scripts run from a throwaway Git repository laid out
like this one (app/tools/android, engine/, VERSION and CHANGELOG.md at the root) with the tags each case needs:

- tools/android/release_version.py: SemVer versions only, the versionCode scheme and its order, a tag that does
  not match VERSION, an untagged commit, a tag on another commit, a dirty tree (also under engine/), a draft or empty CHANGELOG section,
  a versionCode not above an earlier release;
- tools/android/sign_release.py: the build's unsigned APK is signed only when the publication would accept it, the
  key file exists (mode 0600) only while apksigner runs and is gone after a success and after a failure, an APK of
  another key is not kept, and what it leaves is the artifact the publication takes;
- tools/android/publish_release.py: the dry run stages the release and publishes nothing; the release of the tag is
  created (pre-release for a beta) only for the main key's APK matching its checksum, manifest and build-info, never
  over an existing release, and downloaded back;
- tools/android/build_debug.sh refuses a release build without a release tag, or with signing variables, before it
  builds anything.
"""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('release_version', root / 'tools/android/release_version.py')
rv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rv)
spec = importlib.util.spec_from_file_location('release_signing', root / 'tools/android/release_signing.py')
signing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signing)
MAIN = signing.MAIN_RELEASE_CERT_SHA256
OTHER_KEY = 'ab' * 32  # the certificate digest of any key that is not the main release key


def rejects(function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except rv.ReleaseError:
        return
    raise AssertionError(f'accepted: {function.__name__}{args}')


# Versions and codes.
codes = {'1.0.0-alpha': 100001000, '1.0.0-alpha.3': 100001300, '1.0.0-beta': 100004000,
         '1.0.0-beta.2': 100004200, '1.0.0-rc.1': 100007100, '1.0.0': 100009900, '1.0.1-beta': 100014000, '1.2.3': 102039900,
         '2.0.0-alpha': 200001000, '20.99.99': 2099999900}
for text, code in codes.items():
    assert rv.parse(text).code == code, (text, rv.parse(text).code, code)
assert rv.parse('1.0.0-beta').prerelease and rv.parse('1.0.0-rc.1').prerelease and not rv.parse('1.0.0').prerelease
assert rv.parse_tag('v1.0.0-beta').text == '1.0.0-beta' and rv.parse_tag('v1.0.0-beta.2').tag == 'v1.0.0-beta.2'
order = ['1.0.0-alpha', '1.0.0-alpha.19', '1.0.0-beta', '1.0.0-beta.1', '1.0.0-beta.19', '1.0.0-rc', '1.0.0-rc.19',
         '1.0.0', '1.0.1-alpha', '1.0.1', '1.1.0-beta', '1.99.99', '2.0.0-alpha']
assert [rv.parse(v).code for v in order] == sorted(rv.parse(v).code for v in order)
assert all(0 < rv.parse(v).code <= rv.ANDROID_MAX_CODE for v in order + ['20.99.99'])
for bad in ['1.0', '1.0.0-beta.', '1.0.0-Beta', '1.0.0-dev', '01.0.0', '1.00.0', '1.0.0-beta.01', '1.0.0b', '1.0.0b2',
            '1.0.0a', '1.0.0rc1', '1.0.0b01',
            '1.0.0-beta1', '1.0.0.beta', '1.0.0-beta+build', '0.9.0', '21.0.0', '1.100.0', '1.0.100', '1.0.0-beta.20',
            '1.0.0 ', ' 1.0.0']:
    rejects(rv.parse, bad)
for bad in ['1.0.0-beta', 'V1.0.0-beta', 'release-1.0.0', 'v']:
    rejects(rv.parse_tag, bad)
section = '## 1.0.0-beta — 2026-11-20\n\nNotes\n\n## 0.9 — 2026-01-01\n\nOld\n'
assert rv.changelog_section(section, '1.0.0-beta') == 'Notes'
assert rv.changelog_section('## 1.0.0-beta — unreleased\n\nDraft\n', '1.0.0-beta', rehearse=True) == 'Draft'
rejects(rv.changelog_section, '## 1.0.0-beta — unreleased\n\nDraft\n', '1.0.0-beta')
rejects(rv.changelog_section, '## 1.0.0-beta — 2026-11-20\n\n## 0.9 — 2026-01-01\nOld\n', '1.0.0-beta')
rejects(rv.changelog_section, section + section, '1.0.0-beta')
rejects(rv.changelog_section, section, '1.0.0-beta.1')
print('PASS release versions: SemVer only (1.0.0b refused), versionCode scheme and order, bad versions, CHANGELOG sections')

CHANGELOG = '# Changelog\n\n## 1.0.0-beta — {date}\n\nПервая бета. First beta.\n'
FAKE_GH = r'''#!/usr/bin/env python3
import json, os, shutil, sys
from pathlib import Path
state = Path(os.environ['FAKE_STATE'])
with open(state / 'calls', 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\n')
args = sys.argv[1:]
if args[:2] == ['release', 'view']:
    if not (state / 'released').exists():
        sys.exit(1)
    print(json.dumps({'url': 'https://example.invalid/releases/tag/' + args[2]}))
elif args[:2] == ['release', 'create']:
    (state / 'released').write_text('')
    shutil.copyfile(args[3], state / Path(args[3]).name)
elif args[:2] == ['release', 'download']:
    name = args[args.index('--pattern') + 1]
    shutil.copyfile(state / name, Path(args[args.index('--dir') + 1]) / name)
'''
# `sign` works only as sign_release.py must call it: the key in a 0600 file, the passwords in the environment.
FAKE_APKSIGNER = r'''#!/usr/bin/env python3
import os, shutil, stat, sys
from pathlib import Path
state = Path(os.environ['FAKE_STATE'])
args = sys.argv[1:]
if args[0] == 'sign':
    key = Path(args[args.index('--ks') + 1])
    passwords = [os.environ[args[args.index(option) + 1].removeprefix('env:')] for option in ('--ks-pass', '--key-pass')]
    if (stat.S_IMODE(key.stat().st_mode) != 0o600 or key.read_bytes() != b'keystore'
            or passwords != ['store-secret', 'key-secret'] or (state / 'sign-fails').exists()):
        sys.exit('apksigner: cannot sign')
    shutil.copyfile(args[-1], args[args.index('--out') + 1])
else:
    print('Signer #1 certificate DN: CN=test')
    print('Signer #1 certificate SHA-256 digest: ' + (state / 'digest').read_text())
'''


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()


def make_repo(path, version='1.0.0-beta', date='2026-11-20'):
    shutil.rmtree(path, ignore_errors=True)
    (path / 'app/tools/android').mkdir(parents=True)
    for name in ('release_version.py', 'release_signing.py', 'publish_release.py', 'sign_release.py', 'apk_payload.py'):
        shutil.copyfile(root / 'tools/android' / name, path / 'app/tools/android' / name)
    (path / 'VERSION').write_text(version + '\n')
    (path / 'engine').mkdir()
    (path / 'engine/CMakeLists.txt').write_text('project(engine)\n')
    (path / 'CHANGELOG.md').write_text(CHANGELOG.format(date=date))
    (path / '.gitignore').write_text('/build/\n__pycache__/\n')
    git(path.parent, 'init', '-q', path.name)
    for key, value in (('user.name', 'test'), ('user.email', 'test@example.invalid'), ('commit.gpgsign', 'false'),
                       ('tag.gpgsign', 'false')):
        git(path, 'config', key, value)
    git(path, 'add', '-A')
    git(path, 'commit', '-q', '-m', 'release')
    return git(path, 'rev-parse', 'HEAD')


def check(repo, tag, rehearse=False):
    result = subprocess.run([sys.executable, str(repo / 'app/tools/android/release_version.py'), 'check', '--tag', tag,
                             *(['--rehearse'] if rehearse else [])], capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix='release-check-') as temp:
    temp = Path(temp)
    repo = temp / 'app'
    commit = make_repo(repo)
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'untagged' in out, out
    code, out = check(repo, 'v1.0.0-beta', rehearse=True)
    assert code == 0 and 'version_code=100004000' in out and 'prerelease=true' in out, out
    git(repo, 'tag', '-a', '-m', 'beta', 'v1.0.0-beta')
    code, out = check(repo, 'v1.0.0-beta')
    assert code == 0 and 'version_name=1.0.0-beta' in out and 'tag=v1.0.0-beta' in out, out
    for tag in ('v1.0.0b', 'v1.0.0-beta.1', 'v1.0.0', 'v1.0.0-beta.x'):
        code, out = check(repo, tag)
        assert code, f'{tag}: accepted against VERSION 1.0.0-beta'
    (repo / 'stray.txt').write_text('dirty')
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'dirty' in out, out
    code, out = check(repo, 'v1.0.0-beta', rehearse=True)
    assert code and 'dirty' in out, 'a rehearsal of a dirty tree was accepted'
    (repo / 'stray.txt').unlink()
    # The engine is part of the same tree: a change under engine/ stops the release too.
    (repo / 'engine/CMakeLists.txt').write_text('project(changed)\n')
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'dirty' in out and 'engine/CMakeLists.txt' in out, out
    git(repo, 'checkout', '-q', '--', 'engine/CMakeLists.txt')
    (repo / 'VERSION').write_text('1.0.0-beta\n')  # unchanged content keeps the tree clean
    git(repo, 'commit', '-q', '--allow-empty', '-m', 'after the tag')
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'not HEAD' in out, out
    # A draft CHANGELOG section passes a rehearsal only.
    make_repo(repo, date='unreleased')
    git(repo, 'tag', 'v1.0.0-beta')
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'unreleased' in out, out
    assert check(repo, 'v1.0.0-beta', rehearse=True)[0] == 0
    # Monotonic: an earlier release tag with a higher or equal code stops the release.
    make_repo(repo, version='1.0.0-beta')
    git(repo, 'tag', 'v1.0.0-rc.1', 'HEAD')
    git(repo, 'tag', 'v1.0.0-beta')
    code, out = check(repo, 'v1.0.0-beta')
    assert code and 'v1.0.0-rc.1' in out, out
    git(repo, 'tag', '-d', 'v1.0.0-rc.1')
    git(repo, 'tag', 'v0-not-a-release')
    assert check(repo, 'v1.0.0-beta')[0] == 0
    make_repo(repo)
    print('PASS release guards: tag = v<VERSION>, untagged commit, tag on another commit, dirty tree, draft CHANGELOG, '
          'earlier release codes')

    # Publication.
    (temp / 'bin').mkdir()
    for name, text in (('gh', FAKE_GH), ('apksigner', FAKE_APKSIGNER), ('zipalign', '#!/bin/sh\n')):
        (temp / 'bin' / name).write_text(text)
        (temp / 'bin' / name).chmod(0o755)

    def artifact(directory, head, info=None, name='seawolves-arm64-release.apk', **changes):
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
        apk = directory / name
        with zipfile.ZipFile(apk, 'w') as z:
            z.writestr('assets/build-info.json', json.dumps(
                {'testLoop': False, 'commit': head,
                 'versionCode': 100004000, 'versionName': '1.0.0-beta', **(info or {})}))
        digest = hashlib.sha256(apk.read_bytes()).hexdigest()
        manifest = {'apk_sha256': digest, 'commit': head, 'dirty': False, 'build_type': 'release', 'native_build_type': 'Release',
                    'abi': 'arm64-v8a', 'game_resources_included': False, 'verified': [],
                    'version_code': 100004000, 'version_name': '1.0.0-beta', **changes}
        (directory / 'build-manifest.json').write_text(json.dumps(manifest))
        (directory / 'SHA256SUMS').write_text(f'{digest}  {apk.name}\n')
        return digest

    def publish(tag='v1.0.0-beta', dry_run=False, digest=MAIN, released=False, info=None, extra=None, signed=None,
                **changes):
        commit = git(repo, 'rev-parse', 'HEAD')
        state = temp / 'state'
        shutil.rmtree(state, ignore_errors=True)
        shutil.rmtree(repo / 'build', ignore_errors=True)
        state.mkdir()
        (state / 'digest').write_text(digest)
        if released:
            (state / 'released').write_text('')
        apk_digest = None if signed else artifact(repo / 'build/artifact', commit, info, **changes)
        if extra:
            (repo / 'build/artifact' / extra).write_text('extra')
        env = dict(os.environ, FAKE_STATE=str(state), PATH=f'{temp / "bin"}{os.pathsep}{os.environ["PATH"]}')
        result = subprocess.run([sys.executable, str(repo / 'app/tools/android/publish_release.py'), '--tag', tag,
                                 '--artifact', str(signed or repo / 'build/artifact'), '--commit', commit,
                                 '--apksigner', str(temp / 'bin/apksigner'), '--out', str(repo / 'build/release'),
                                 '--repository', 'example/app', *(['--dry-run'] if dry_run else [])],
                                env=env, capture_output=True, text=True)
        calls = [json.loads(line) for line in (state / 'calls').read_text().splitlines()] if (state / 'calls').exists() else []
        creates = [call for call in calls if call[:2] == ['release', 'create']]
        return result, creates, apk_digest

    # Dry run before the tag exists: everything staged, nothing published.
    result, creates, digest = publish(dry_run=True)
    assert result.returncode == 0 and not creates, result.stdout + result.stderr
    staged = repo / 'build/release'
    assert sorted(p.name for p in staged.iterdir()) == ['RELEASE-NOTES.md', 'SHA256SUMS', 'seawolves-1.0.0-beta-arm64.apk']
    assert (staged / 'SHA256SUMS').read_text() == f'{digest}  seawolves-1.0.0-beta-arm64.apk\n'
    notes = (staged / 'RELEASE-NOTES.md').read_text()
    assert notes.startswith('Первая бета. First beta.') and digest in notes and MAIN in notes and git(repo, 'rev-parse', 'HEAD') in notes
    result, creates, _ = publish()
    assert result.returncode and 'untagged' in result.stderr and not creates, 'published from an untagged commit'
    git(repo, 'tag', '-a', '-m', 'beta', 'v1.0.0-beta')
    refused = [
        ('a key other than the main release key', dict(digest=OTHER_KEY)),
        ('existing release', dict(released=True)),
        ('tag not matching VERSION', dict(tag='v1.0.0-beta.1')),
        ('debug build type', dict(build_type='debug')),
        ('RelWithDebInfo engine', dict(native_build_type='RelWithDebInfo')),
        ('dirty tree', dict(dirty=True)),
        ('build-info of another commit', dict(info={'commit': 'b' * 40})),
        ('other commit', dict(commit='b' * 40)),
        ('other versionCode', dict(version_code=100004100)),
        ('other versionName', dict(version_name='1.0.0-beta.1')),
        ('build-info versionCode', dict(info={'versionCode': 100004100})),
        ('test-loop APK', dict(info={'testLoop': True})),
        ('unexpected artifact file', dict(extra='RELEASE-NOTES.md')),
    ]
    for label, case in refused:
        result, creates, _ = publish(**case)
        assert result.returncode and not creates, f'{label}: accepted ({result.stdout}{result.stderr})'
    result, creates, digest = publish()
    assert result.returncode == 0, result.stdout + result.stderr
    assert len(creates) == 1 and creates[0][2] == 'v1.0.0-beta' and '--verify-tag' in creates[0]
    assert '--prerelease' in creates[0] and '--latest=false' in creates[0], creates
    assert creates[0][creates[0].index('--title') + 1] == '1.0.0-beta'
    assert 'downloaded back' in result.stdout and digest in result.stdout

    # Signing: the job between the build and the publication.
    SECRETS = {'ANDROID_KEYSTORE_BASE64': base64.b64encode(b'keystore').decode(), 'ANDROID_KEY_ALIAS': 'alias',
               'ANDROID_STORE_PASSWORD': 'store-secret', 'ANDROID_KEY_PASSWORD': 'key-secret'}
    keystore, signed = temp / 'release-signing.keystore', temp / 'signed'

    def sign(digest=MAIN, sign_fails=False, corrupt=False, without=None, **changes):
        commit = git(repo, 'rev-parse', 'HEAD')
        state = temp / 'state'
        for directory in (state, signed):
            shutil.rmtree(directory, ignore_errors=True)
        state.mkdir()
        (state / 'digest').write_text(digest)
        if sign_fails:
            (state / 'sign-fails').write_text('')
        artifact(temp / 'unsigned', commit, name='seawolves-arm64-release-unsigned.apk', **changes)
        if corrupt:
            with open(temp / 'unsigned/seawolves-arm64-release-unsigned.apk', 'ab') as file:
                file.write(b'changed after the build')
        env = {name: value for name, value in {**os.environ, **SECRETS, 'FAKE_STATE': str(state)}.items() if name != without}
        result = subprocess.run([sys.executable, str(repo / 'app/tools/android/sign_release.py'), '--tag', 'v1.0.0-beta',
                                 '--commit', commit, '--unsigned', str(temp / 'unsigned'), '--build-tools', str(temp / 'bin'),
                                 '--keystore', str(keystore), '--out', str(signed)], env=env, capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert not keystore.exists(), 'the key file was left behind: ' + output
        assert not any(secret in output for secret in ('store-secret', 'key-secret', SECRETS['ANDROID_KEYSTORE_BASE64'])), output
        return result.returncode, output

    code, output = sign()
    assert code == 0 and sorted(p.name for p in signed.iterdir()) == ['SHA256SUMS', 'build-manifest.json',
                                                                    'seawolves-arm64-release.apk'], output
    result, creates, _ = publish(dry_run=True, signed=signed)
    assert result.returncode == 0 and not creates, result.stdout + result.stderr
    unsigned_refused = [
        ('a key other than the main release key', dict(digest=OTHER_KEY)),
        ('apksigner failing', dict(sign_fails=True)),
        ('an APK changed after the build', dict(corrupt=True)),
        ('dirty tree', dict(dirty=True)),
        ('debug build type', dict(build_type='debug')),
        ('other commit', dict(commit='b' * 40)),
        ('a missing secret', dict(without='ANDROID_KEY_PASSWORD')),
    ]
    for label, case in unsigned_refused:
        code, output = sign(**case)
        assert code and not (signed / 'seawolves-arm64-release.apk').exists(), f'{label}: signed ({output})'
    print(f'PASS signing: the key file exists only for apksigner; {len(unsigned_refused)} refusals leave no key and no '
          'APK; the signed artifact passes the publication checks')

    # A final release is not a pre-release.
    make_repo(repo, version='1.0.0')
    (repo / 'CHANGELOG.md').write_text('## 1.0.0 — 2026-12-01\n\nRelease.\n')
    git(repo, 'commit', '-q', '-am', 'final')
    git(repo, 'tag', 'v1.0.0')
    result, creates, _ = publish(tag='v1.0.0', info={'versionCode': 100009900, 'versionName': '1.0.0'},
                                 version_code=100009900, version_name='1.0.0')
    assert result.returncode == 0 and '--prerelease' not in creates[0] and '--latest' in creates[0], result.stderr
    print(f'PASS publication: dry run stages and publishes nothing; {len(refused) + 1} unsafe candidates refused '
          '(incl. an untagged commit and another signing key); beta published as a pre-release, final as latest')

# build_debug.sh refuses a release build without a tag, before building anything.
env = {k: v for k, v in os.environ.items() if not k.startswith('STORM_')}
with tempfile.TemporaryDirectory(prefix='release-build-guard-') as work:
    env.update(STORM_ANDROID_BUILD_TYPE='release', STORM_CI_WORK=work)
    result = subprocess.run(['bash', str(root / 'tools/android/build_debug.sh')], env=env, capture_output=True, text=True)
    assert result.returncode == 2 and 'release tag' in result.stderr, result.stdout + result.stderr
    result = subprocess.run(['bash', str(root / 'tools/android/build_debug.sh')], env=dict(env, STORM_RELEASE_TAG='v0.0.1'),
                            capture_output=True, text=True)
    assert result.returncode != 0 and 'release_version' in result.stderr, result.stdout + result.stderr
    result = subprocess.run(['bash', str(root / 'tools/android/build_debug.sh')],
                            env=dict(env, STORM_RELEASE_TAG='v0.0.1', STORM_ANDROID_KEYSTORE='release.jks'),
                            capture_output=True, text=True)
    assert result.returncode == 2 and 'signs nothing' in result.stderr, result.stdout + result.stderr
    assert not any(Path(work).iterdir()), 'the refused release build wrote into its work directory'
print('PASS build_debug.sh: a release build without a valid release tag, or with signing variables, stops before building')
