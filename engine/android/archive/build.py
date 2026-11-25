#!/usr/bin/env python3
"""Build pinned archive readers into one library. No game data or installers run.

The versioned open-source dependencies and the fixes applied to them are declared in sources/CMakeLists.txt
(fetched once per machine after SHA-256 verification, tools/android/pinned_sources.py).
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
STORM = ROOT.parents[1]
sys.path.insert(0, str(STORM / 'tools/android'))
import android_abi
import pinned_sources


def run(args):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', required=True, type=Path)
    p.add_argument('--ndk', type=Path)
    p.add_argument('--jobs', type=int, default=4)
    # Android ABI with --ndk: arm64-v8a (default, releases) or x86_64 (emulator debug builds).
    p.add_argument('--abi', choices=sorted(android_abi.ABIS), default=android_abi.DEFAULT)
    # Host only: allocator fault controls (STORM_ARCHIVE_TESTING) and the JNI bridge, for tests/archive.
    p.add_argument('--host-testing', action='store_true')
    a = p.parse_args()
    if a.host_testing and a.ndk:
        p.error('--host-testing builds the host reader; never with --ndk')
    build = a.build_dir.resolve()
    src = pinned_sources.fetch(ROOT / 'sources', build / 'pinned')
    prefix = build / 'prefix'
    prefix.mkdir(parents=True, exist_ok=True)
    common = [f'-DCMAKE_{lang}_COMPILER_LAUNCHER={STORM / "tools/ccache/launcher"}' for lang in ('C', 'CXX')] + [
        '-G',
        'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DBUILD_SHARED_LIBS=OFF',
        f'-DCMAKE_INSTALL_PREFIX={prefix}',
    ]
    if a.ndk:
        common += [
            f'-DCMAKE_TOOLCHAIN_FILE={a.ndk.resolve()}/build/cmake/android.toolchain.cmake',
            f'-DANDROID_ABI={a.abi}',
            f'-DANDROID_PLATFORM=android-{android_abi.API}',
            '-DANDROID_STL=c++_shared',
        ]

    def cmake(name, source, extra=(), target=None):
        dest = build / name
        run(['cmake', '-S', source, '-B', dest, *common, *extra])
        run(['cmake', '--build', dest, '--parallel', a.jobs, *(['--target', target] if target else [])])
        return dest

    cmake(
        'zlib',
        src['zlib'],
        ['-DZLIB_BUILD_TESTING=OFF', '-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON'],
        'install',
    )
    cmake(
        'xz',
        src['xz'],
        [
            '-DXZ_TOOL_XZ=OFF',
            '-DXZ_TOOL_XZDEC=OFF',
            '-DXZ_TOOL_LZMADEC=OFF',
            '-DXZ_TOOL_LZMAINFO=OFF',
            '-DXZ_TOOL_SCRIPTS=OFF',
            '-DXZ_NLS=OFF',
            '-DBUILD_TESTING=OFF',
        ],
        'install',
    )
    bz = build / 'bzip-source'
    bz.mkdir(parents=True, exist_ok=True)
    (bz / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(storm_bzip C)\nadd_library(bz2 STATIC '
        + ' '.join(
            str(src['bzip2'] / f)
            for f in ['blocksort.c', 'huffman.c', 'crctable.c', 'randtable.c', 'compress.c', 'decompress.c', 'bzlib.c']
        )
        + ')\ninstall(TARGETS bz2 ARCHIVE DESTINATION lib)\ninstall(FILES "'
        + str(src['bzip2'] / 'bzlib.h')
        + '" DESTINATION include)\n'
    )
    cmake('bzip', bz, target='install')
    disable = [
        'OPENSSL',
        'MBEDTLS',
        'NETTLE',
        'LIBB2',
        'LZ4',
        'LZO',
        'ZSTD',
        'LIBXML2',
        'BSDXML',
        'EXPAT',
        'PCREPOSIX',
        'PCRE2POSIX',
        'TAR',
        'CPIO',
        'CAT',
        'UNZIP',
        'TEST',
        'ACL',
        'XATTR',
        'ICONV',
    ]
    cmake(
        'libarchive',
        src['libarchive'],
        [
            '-DCMAKE_C_FLAGS=-fexceptions',
            f'-DCMAKE_PREFIX_PATH={prefix}',
            f'-DCMAKE_FIND_ROOT_PATH={prefix}',
            f'-DZLIB_ROOT={prefix}',
            f'-DLIBLZMA_LIBRARY={prefix}/lib/liblzma.a',
            f'-DLIBLZMA_INCLUDE_DIR={prefix}/include',
            f'-DBZIP2_LIBRARY_RELEASE={prefix}/lib/libbz2.a',
            f'-DBZIP2_INCLUDE_DIR={prefix}/include',
            *[f'-DENABLE_{x}=OFF' for x in disable],
        ],
        'install',
    )
    cmake(
        'reader',
        ROOT,
        [
            f'-DSTORM_ARCHIVE_INNOEXTRACT={src["innoextract"]}',
            f'-DSTORM_ARCHIVE_BOOST={src["boost"]}',
            f'-DSTORM_ARCHIVE_PREFIX={prefix}',
            *(['-DSTORM_ARCHIVE_TESTING=ON', '-DSTORM_ARCHIVE_HOST_JNI=ON'] if a.host_testing else []),
        ],
    )
    # The notices the APK carries (licenses/) are the pinned releases' own.
    for name, notice, filename in [
        ('innoextract', 'innoextract-1.9', 'LICENSE'),
        ('libarchive', 'libarchive-3.8.10', 'COPYING'),
        ('xz', 'xz-5.8.4', 'COPYING.0BSD'),
        ('bzip2', 'bzip2-1.0.8', 'LICENSE'),
        ('boost', 'boost_1_90_0', 'LICENSE_1_0.txt'),
        ('zlib', 'zlib-1.3.2', 'LICENSE'),
    ]:
        if (src[name] / filename).read_bytes() != (ROOT / 'licenses' / (notice + '.txt')).read_bytes():
            raise RuntimeError(f'licenses/{notice}.txt differs from {filename} of the pinned {name}')
    output = build / 'output'
    output.mkdir(exist_ok=True)
    shutil.copyfile(build / 'reader/libstorm_archive.so', output / 'libstorm_archive.so')
    if a.ndk:
        run(
            [
                a.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip',
                '--strip-unneeded',
                output / 'libstorm_archive.so',
            ]
        )
        android_abi.check_elf(output / 'libstorm_archive.so', a.abi)
    print('Archive library:', output / 'libstorm_archive.so')


if __name__ == '__main__':
    main()
