#!/usr/bin/env python3
"""The main release key check shared by every path that publishes an APK.

    release_signing.py --apksigner <build-tools>/apksigner <apk>...

Each APK must carry exactly one signer whose certificate SHA-256 digest is MAIN_RELEASE_CERT_SHA256. Any other key
(a local debug key) is refused: only the release workflow signs with the main key, so a locally built APK never
reaches a release. Used by publish_release.py and by the release workflow right after it signs.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

# The main release key (GitHub secrets ANDROID_KEYSTORE_BASE64/ANDROID_KEY_ALIAS of the repository, used only by
# the release workflow): the SHA-256 digest of its signing certificate, as `apksigner verify --print-certs` prints it.
# Public information; changing it means a new update identity, which no installed build accepts.
MAIN_RELEASE_CERT_SHA256 = '01096bac94d9135ed3e45ff23bbe78f002ca09fb5aa7e1a5f16e44810ab09749'


class SigningError(Exception):
    pass


def signer_digests(output):
    """The certificate SHA-256 digests in `apksigner verify --print-certs` output, one per signer."""
    return re.findall(r'^Signer #\d+ certificate SHA-256 digest: ([0-9a-f]{64})$', output, re.M)


def check_signature(apksigner, apk):
    """Return the main key's digest when `apk` is signed by it alone; raise SigningError otherwise."""
    result = subprocess.run([str(apksigner), 'verify', '--print-certs', str(apk)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise SigningError(f'apksigner rejects {Path(apk).name}:\n{result.stdout}')
    digests = signer_digests(result.stdout)
    if len(digests) != 1:
        raise SigningError(f'{Path(apk).name}: expected exactly one signer, found {len(digests)}:\n{result.stdout}')
    if digests[0] != MAIN_RELEASE_CERT_SHA256:
        raise SigningError(f'{Path(apk).name} is signed with certificate {digests[0]}, not the main release key '
                           f'{MAIN_RELEASE_CERT_SHA256}: published APKs come only from the release workflow '
                           '(tools/android/RELEASE.md)')
    return digests[0]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--apksigner', type=Path, required=True)
    p.add_argument('apks', type=Path, nargs='+')
    a = p.parse_args()
    try:
        for apk in a.apks:
            print(f'{apk}: signed with the main release key {check_signature(a.apksigner, apk)}')
    except SigningError as error:
        sys.exit(f'release_signing: {error}')


if __name__ == '__main__':
    main()
