"""Android ABIs the engine builds: arm64-v8a for every release, x86_64 for emulator debug builds, and the ELF facts
of their libraries (read_elf: the pinned NDK's llvm-readelf).

The arm64 build directories keep their unsuffixed names; other ABIs append suffix() so that
their build trees never share (or invalidate) the warm arm64 ones.
"""
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pins  # noqa: E402

ABIS = {
    # ABI: (toolchain arch: cmake/toolchains/android-<arch>.cmake, NDK triple, ELF e_machine, OpenSSL target)
    'arm64-v8a': ('arm64', 'aarch64-linux-android', 183, 'android-arm64'),
    'x86_64': ('x86_64', 'x86_64-linux-android', 62, 'android-x86_64'),
}
DEFAULT = 'arm64-v8a'
# The minimum Android API of every native library (cmake/toolchains/android-common.cmake ANDROID_PLATFORM).
API = 26


def arch(abi):
    return ABIS[abi][0]


def triple(abi):
    return ABIS[abi][1]


def elf_machine(abi):
    return ABIS[abi][2]


def openssl_target(abi):
    return ABIS[abi][3]


def suffix(abi):
    return '' if abi == DEFAULT else '-' + arch(abi)


def read_elf(path, *options, ndk=None):
    """What llvm-readelf of the pinned NDK (pins.ndk(), or ndk) reports with options such as --file-header,
    --program-headers, --notes or --dynamic-table, as its JSON object for the file."""
    readelf = pins.llvm_tool('llvm-readelf', ndk)
    result = subprocess.run([str(readelf), '--elf-output-style=JSON', *options, str(path)], capture_output=True,
                            text=True)
    if result.returncode:
        raise RuntimeError(f'{path}: {result.stderr.strip()}')
    return json.loads(result.stdout)[0]


def build_id(elf):
    """The GNU build id of read_elf(..., '--notes'), or None."""
    for section in elf.get('NoteSections', []):
        for note in section['NoteSection']['Notes']:
            if note['Type'].startswith('NT_GNU_BUILD_ID'):
                return note['Build ID']
    return None


def check_elf(path, abi, ndk=None):
    header = read_elf(path, '--file-header', ndk=ndk)['ElfHeader']
    ident = header['Ident']
    if ident['Class']['Value'] != 2 or ident['DataEncoding']['Value'] != 1 or header['Machine']['Value'] != elf_machine(abi):
        raise RuntimeError(f'Not an ELF64 little-endian {abi} library: {path}')
