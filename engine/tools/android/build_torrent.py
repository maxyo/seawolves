#!/usr/bin/env python3
"""Build the standalone torrent JNI library, or its host tests.

The pinned sources and the fix applied to libtorrent are declared in android/torrent/sources/CMakeLists.txt
(fetched once per machine after SHA-256 verification, pinned_sources.py); builds stay under --work. No engine or APK
build is performed.
"""

import argparse
import os
import re
from pathlib import Path
import shutil
import subprocess

import android_abi
import pinned_sources

ROOT = Path(__file__).resolve().parents[2]


def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, help="Omit to build and run host tests")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--abi",
        choices=sorted(android_abi.ABIS),
        default=android_abi.DEFAULT,
        help="Android ABI with --ndk: arm64-v8a (releases) or x86_64 (emulator debug builds)",
    )
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    sources = pinned_sources.fetch(ROOT / "android/torrent/sources", work / "pinned")
    torrent, boost, openssl = sources["libtorrent"], sources["boost"], sources["openssl"]
    sfx = android_abi.suffix(args.abi)
    build = work / ("android" + sfx if args.ndk else "host")
    # The API level is in the OpenSSL build directory's name, so a build for another minimum API never reuses it.
    ssl_build = work / (f"openssl-android{sfx}-api{android_abi.API}" if args.ndk else "openssl-host")
    ssl_prefix = ssl_build / "prefix"
    # Built once per OpenSSL copy: its path names the pin, so a changed pin builds again.
    built_from = ssl_build / "source"
    if not (ssl_prefix / "lib/libssl.a").exists() or not built_from.is_file() or built_from.read_text() != str(openssl):
        shutil.rmtree(ssl_build, ignore_errors=True)
        ssl_build.mkdir(parents=True)
        env = os.environ.copy()
        target = "linux-x86_64"
        if args.ndk:
            target = android_abi.openssl_target(args.abi)
            env["ANDROID_NDK_ROOT"] = str(args.ndk.resolve())
            env["PATH"] = (
                str(args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin") + os.pathsep + env["PATH"]
            )
        command = [
            "perl",
            openssl / "Configure",
            target,
            "no-shared",
            "no-tests",
            "no-apps",
            "no-docs",
            "no-module",
            "-fPIC",
            f"--prefix={ssl_prefix}",
            "--libdir=lib",
        ]
        if args.ndk:
            command += [f"-D__ANDROID_API__={android_abi.API}"]
        run(command, cwd=ssl_build, env=env)
        # Compile through the Storm ccache launcher with the compiler Configure chose.
        compiler = re.search(r"^CC=(.+)$", (ssl_build / "Makefile").read_text(), re.M)[1].strip()
        run(["make", f"-j{args.jobs}", f"CC={ROOT / 'tools/ccache/launcher'} {compiler}"], cwd=ssl_build, env=env)
        run(["make", "install_sw"], cwd=ssl_build, env=env)
        built_from.write_text(str(openssl))
    command = [
        "cmake",
        "-S",
        ROOT / "android/torrent",
        "-B",
        build,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DSTORM_LIBTORRENT_SOURCE={torrent}",
        f"-DBOOST_ROOT={boost}",
        f"-DBoost_INCLUDE_DIR={boost}",
        f"-DOPENSSL_ROOT_DIR={ssl_prefix}",
        f"-DOPENSSL_INCLUDE_DIR={ssl_prefix}/include",
        f"-DOPENSSL_SSL_LIBRARY={ssl_prefix}/lib/libssl.a",
        f"-DOPENSSL_CRYPTO_LIBRARY={ssl_prefix}/lib/libcrypto.a",
    ]
    if args.ndk:
        command += [
            f"-DCMAKE_TOOLCHAIN_FILE={args.ndk.resolve()}/build/cmake/android.toolchain.cmake",
            f"-DANDROID_ABI={args.abi}",
            f"-DANDROID_PLATFORM=android-{android_abi.API}",
            "-DANDROID_STL=c++_shared",
        ]
    cmake_env = os.environ.copy()
    cmake_env["PKG_CONFIG_LIBDIR"] = str(ssl_prefix / "lib/pkgconfig")
    cmake_env["PKG_CONFIG_PATH"] = str(ssl_prefix / "lib/pkgconfig")
    run(command, env=cmake_env)
    run(["cmake", "--build", build, "--parallel", args.jobs])
    if not args.ndk:
        run(["ctest", "--test-dir", build, "--output-on-failure"])
    else:
        run(
            [
                args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip",
                "--strip-unneeded",
                build / "libstorm_torrent.so",
            ]
        )
        android_abi.check_elf(build / "libstorm_torrent.so", args.abi)
    print(build / "libstorm_torrent.so")


if __name__ == "__main__":
    main()
