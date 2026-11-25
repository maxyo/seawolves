#!/usr/bin/env python3
"""Check libengine.so: ELF architecture, entry points, runtime libraries, one bgfx and the entity registrations."""

import argparse
from pathlib import Path
import subprocess
import android_abi

p = argparse.ArgumentParser()
p.add_argument('--library', type=Path, required=True)
p.add_argument('--ndk', type=Path, required=True)
p.add_argument('--abi', choices=sorted(android_abi.ABIS), default=android_abi.DEFAULT)
a = p.parse_args()
try:
    android_abi.check_elf(a.library, a.abi, ndk=a.ndk)
except RuntimeError as error:
    raise SystemExit(str(error))
bin = a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
exported = subprocess.check_output(
    [str(bin / 'llvm-nm'), '-D', '--defined-only', '--demangle', str(a.library)], text=True
)
# Release builds use hidden visibility: only the entry points stay in .dynsym,
# the rest is checked in the full symbol table (empty for a stripped library).
local = subprocess.run(
    [str(bin / 'llvm-nm'), '--defined-only', '--demangle', str(a.library)], text=True, capture_output=True
).stdout
symbols = exported + local
if ' SDL_main' not in exported:
    raise SystemExit('SDL_main is not exported; SDLActivity cannot start the engine')
if 'Java_org_libsdl_app_SDLControllerManager_' in symbols and 'Java_' not in exported:
    raise SystemExit('JNI entry points are not exported')
dynamic = android_abi.read_elf(a.library, '--dynamic-table', ndk=a.ndk)['DynamicSection']
deps = {entry['Library'] for entry in dynamic if entry['Type'] == 'NEEDED'}
# FFmpeg (in-game video) ships beside libengine.so; see tools/ffmpeg/README.md.
# libbgfx.so (bgfx, bx, bimg) and libstorm_ui.so (the UI runtime) are shared with the application
# screens; the process must have exactly one bgfx, so libengine.so must not carry its own.
for library in [
    'libSDL2.so',
    'libGLESv3.so',
    'libbgfx.so',
    'libstorm_ui.so',
    'libc++_shared.so',
    'libavformat.so',
    'libavcodec.so',
    'libswscale.so',
    'libswresample.so',
    'libavutil.so',
]:
    if library not in deps:
        raise SystemExit('Missing runtime dependency: ' + library)
# Android allocates with Bionic and libc++ (cmake/android.cmake): an allocator of the engine's own corrupts the heap.
if any('mimalloc' in library for library in deps):
    raise SystemExit('libengine.so needs mimalloc; Android must use the platform allocator')
if 'bgfx::init(' in symbols:
    raise SystemExit('libengine.so links its own bgfx; it must use libbgfx.so')
print(f'PASS: engine {a.abi} linked, SDL_main exported, runtime libraries and the shared bgfx')

# Entities, services and script libraries register only through the static
# <name>vmaci objects of the CREATE_* macros (src/libs/core/include/vma.hpp);
# nothing references them except their static constructor, so a module that
# loses whole-archive linking, or a section/LTO discard, drops them silently and
# the game only fails later when a script creates that class. The registrations
# the build compiled are those defined in the objects and archives the link of
# libengine.so consumed (Ninja's inputs of it in its build directory).
build = next((d for d in a.library.resolve().parents if (d / 'build.ninja').is_file()), None)
if build is None:
    raise SystemExit(f'No build.ninja above {a.library}: the registration check needs the build directory')
inputs = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'inputs', str(a.library.resolve().relative_to(build))], text=True
).split()
linked_objects = [str(build / name) for name in inputs if name.endswith(('.o', '.a'))]
if not linked_objects:
    raise SystemExit(f'Ninja lists no objects or archives as inputs of {a.library}')


def registrations(nm_output):
    # A registration inside a namespace demangles as ns::<name>vmaci.
    return {
        line.split()[-1].rsplit('::', 1)[-1][: -len('vmaci')]
        for line in nm_output.splitlines()
        if line.endswith('vmaci') and len(line.split()) >= 2
    }


compiled = registrations(
    subprocess.check_output(
        [str(bin / 'llvm-nm'), '--defined-only', '--demangle', '--print-file-name', *linked_objects], text=True
    )
)
if not compiled:
    raise SystemExit('No CREATE_CLASS/CREATE_SERVICE/CREATE_SCRIPTLIBRIARY registrations in the inputs of the link')
if not local:
    raise SystemExit('Registration check needs the unstripped library (build output, not the packaged one)')
missing = sorted(compiled - registrations(local))
if missing:
    raise SystemExit('Registrations compiled but missing from libengine.so: ' + ', '.join(missing))
print(f'PASS: all {len(compiled)} entity/service/script-library registrations the build compiled are linked')

# Placement new is harmless; owning global new/delete must remain in libc++.
# Exact names: a class's own operator new (RmlUi's layout boxes) is not the global one.
defined = {line.split(' ', 2)[2] for line in symbols.splitlines() if line.count(' ') >= 2}
for symbol in (
    'operator new(unsigned long)',
    'operator new[](unsigned long)',
    'operator delete(void*)',
    'operator delete[](void*)',
):
    if symbol in defined:
        raise SystemExit('Android must use the platform C++ allocator: ' + symbol)
print('PASS: Android uses the platform C/C++ allocation (no mimalloc, no global new/delete of its own)')
