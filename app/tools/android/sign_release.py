#!/usr/bin/env python3
"""Sign the unsigned APK of a release build with the main release key.

    sign_release.py --tag v1.0.0-alpha.1 [--rehearse] --commit <tag commit> --unsigned <the build's dist directory>
        --build-tools <SDK build tools> --keystore <file to create> --out <empty directory>

Run by the release workflow's signing job on the tag's checkout, after the build: nothing is fetched, compiled or
packaged while the key exists. The key comes from the environment of this one step: ANDROID_KEYSTORE_BASE64 (the
keystore file in base64), ANDROID_KEY_ALIAS, ANDROID_STORE_PASSWORD, ANDROID_KEY_PASSWORD. The keystore is written
to --keystore (mode 0600) for the apksigner call alone and removed whether signing succeeds or fails; apksigner
reads the passwords from the environment, never from its command line.

It signs only what the publication would accept (release_version.py check; the build's artifact as
publish_release.py checks it: checksum, this commit, clean sources, release build, the tag's version) and leaves in
--out the signed, aligned APK whose only signer is the main release key (release_signing.py), its SHA256SUMS and
the build manifest with the signed APK's checksum: the artifact publish_release.py takes.
"""
import argparse
import base64
import binascii
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apk_payload  # noqa: E402
import release_version  # noqa: E402
from publish_release import APK, PublishError, check_artifact, require, sha256  # noqa: E402
from release_signing import SigningError, check_signature  # noqa: E402

KEYSTORE, ALIAS, STORE_PASSWORD, KEY_PASSWORD = SECRETS = (
    'ANDROID_KEYSTORE_BASE64', 'ANDROID_KEY_ALIAS', 'ANDROID_STORE_PASSWORD', 'ANDROID_KEY_PASSWORD')


def sign(apksigner, unsigned, signed, keystore):
    """Sign `unsigned` into `signed`; the key file `keystore` exists only during the apksigner call."""
    try:
        key = base64.b64decode(''.join(os.environ[KEYSTORE].split()), validate=True)
    except binascii.Error:
        raise SigningError(f'{KEYSTORE} is not base64') from None
    # apksigner needs the alias and the passwords, not the keystore's text.
    environment = {name: value for name, value in os.environ.items() if name != KEYSTORE}
    descriptor = os.open(keystore, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, 'wb') as file:
            file.write(key)
        result = subprocess.run(
            [str(apksigner), 'sign', '--ks', str(keystore), '--ks-key-alias', os.environ[ALIAS],
             '--ks-pass', 'env:' + STORE_PASSWORD, '--key-pass', 'env:' + KEY_PASSWORD,
             # No v4 signature: it is a second file (.idsig) beside the APK, which a release does not ship.
             '--v4-signing-enabled', 'false', '--out', str(signed), str(unsigned)],
            env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    finally:
        os.unlink(keystore)
    if result.returncode:
        raise SigningError(f'apksigner did not sign {unsigned.name} ({result.returncode}):\n{result.stdout}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--tag', required=True)
    p.add_argument('--rehearse', action='store_true', help='the tag need not exist yet (the workflow\'s dry run)')
    p.add_argument('--commit', required=True)
    p.add_argument('--unsigned', type=Path, required=True)
    p.add_argument('--build-tools', type=Path, required=True)
    p.add_argument('--keystore', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    try:
        missing = [name for name in SECRETS if not os.environ.get(name)]
        require(not missing, 'missing release signing secrets: ' + ', '.join(missing))
        require(re.fullmatch(r'[0-9a-f]{40}', a.commit), 'invalid commit SHA')
        require(release_version.git('rev-parse', 'HEAD') == a.commit, 'the checkout is not the commit being released')
        require(not a.out.exists() or not any(a.out.iterdir()), f'{a.out} is not empty')
        version, _ = release_version.check(a.tag, rehearse=a.rehearse)
        unsigned, unsigned_digest = check_artifact(a.unsigned, a.commit, version, apk_payload.apk_name('arm64', 'release'))
        a.out.mkdir(parents=True, exist_ok=True)
        signed = a.out / APK
        sign(a.build_tools / 'apksigner', unsigned, signed, a.keystore)
        try:
            certificate = check_signature(a.build_tools / 'apksigner', signed)
        except SigningError:
            signed.unlink()  # an APK of another key must not stay under the release APK's name
            raise
        aligned = subprocess.run([str(a.build_tools / 'zipalign'), '-c', '-P', '16', '4', str(signed)])
        require(aligned.returncode == 0, f'{signed.name} is not 16 KiB aligned after signing')
        digest = sha256(signed)
        manifest = json.loads((a.unsigned / 'build-manifest.json').read_text())
        manifest.update(apk_sha256=digest, unsigned_apk_sha256=unsigned_digest)
        manifest['verified'].append('APK signed by the main release key')
        (a.out / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (a.out / 'SHA256SUMS').write_text(f'{digest}  {APK}\n')
        print(f'{signed}: SHA-256 {digest}, signed with the main release key {certificate}')
    except (PublishError, release_version.ReleaseError, SigningError) as error:
        sys.exit(f'sign_release: {error}')


if __name__ == '__main__':
    main()
