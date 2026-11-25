#!/usr/bin/env python3
"""Isolated synthetic audio checks; no game data, SDK download or terms acceptance."""

import argparse
import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=pathlib.Path, default=ROOT / 'build/audio')
# The pinned miniaudio as the engine compiles it, patched (tests/CMakeLists.txt passes the build's copy).
p.add_argument('--miniaudio', type=pathlib.Path, required=True)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
if shutil.which("ffmpeg") is None:
    raise SystemExit("Host audio tests require ffmpeg with libmp3lame and libvorbis encoders.")
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
miniaudio = a.miniaudio.resolve()

# A check that cannot finish (a decoder or stream worker that stops making progress) fails after this many seconds.
DEADLOCK_GUARD = 300


def run(cmd, timeout=None):
    subprocess.run(list(map(str, cmd)), check=True, timeout=timeout)


flags = ['-std=c++17', '-O1', '-g', '-pthread', '-I' + str(miniaudio), '-I' + str(miniaudio / 'extras'),
         '-I' + str(ROOT / 'android/audio')]
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
run([a.cxx, *flags, '-c', ROOT / 'android/audio/implementation.cpp', '-o', build / 'implementation.o'])
run(
    [
        a.cxx,
        *flags,
        build / 'implementation.o',
        ROOT / 'tests/audio/probe.cpp',
        '-Wl,--no-undefined',
        '-ldl',
        '-lm',
        '-o',
        build / 'audio-probe',
    ]
)
run(
    [
        a.cxx,
        *flags,
        build / 'implementation.o',
        ROOT / 'android/audio/service_backend.cpp',
        ROOT / 'tests/audio/service.cpp',
        '-Wl,--no-undefined',
        '-ldl',
        '-lm',
        '-o',
        build / 'audio-service-check',
    ]
)
fixtures = build / 'fixtures'
fixtures.mkdir(exist_ok=True)
# All codec files derive from our generated mono sine wave.
run(
    [
        'ffmpeg',
        '-nostdin',
        '-v',
        'error',
        '-y',
        '-f',
        'lavfi',
        '-i',
        'sine=frequency=440:sample_rate=48000:duration=3',
        '-ac',
        '1',
        '-c:a',
        'pcm_s16le',
        fixtures / 'tone.wav',
    ]
)
for ext, codec in [('mp3', 'libmp3lame'), ('ogg', 'libvorbis')]:
    run(
        [
            'ffmpeg',
            '-nostdin',
            '-v',
            'error',
            '-y',
            '-i',
            fixtures / 'tone.wav',
            '-c:a',
            codec,
            fixtures / ('tone.' + ext),
        ]
    )
for ext, codec in [('wav', 'pcm_s16le'), ('mp3', 'libmp3lame'), ('ogg', 'libvorbis')]:
    run(
        [
            'ffmpeg',
            '-nostdin',
            '-v',
            'error',
            '-y',
            '-i',
            fixtures / 'tone.wav',
            '-ar',
            '44100',
            '-c:a',
            codec,
            fixtures / ('tone441.' + ext),
        ]
    )
run([build / 'audio-probe', fixtures], DEADLOCK_GUARD)
run([build / 'audio-service-check', fixtures], DEADLOCK_GUARD)

long_fixtures = build / 'long-fixtures'
long_fixtures.mkdir(exist_ok=True)
for ext, codec in [('mp3', 'libmp3lame'), ('ogg', 'libvorbis')]:
    run(
        [
            'ffmpeg',
            '-nostdin',
            '-v',
            'error',
            '-y',
            '-f',
            'lavfi',
            '-i',
            'sine=frequency=440:sample_rate=48000:duration=180',
            '-ac',
            '1',
            '-c:a',
            codec,
            long_fixtures / ('tone.' + ext),
        ]
    )
peaks = []
for directory in [fixtures, long_fixtures]:
    output = subprocess.check_output([str(build / 'audio-probe'), str(directory), '--stream-memory'], text=True)
    peaks.append(int(output.strip().split('=')[1]))
if peaks[1] - peaks[0] > 8192:
    raise SystemExit(f'Streaming memory grew by more than 8 MiB: {peaks}')
print(f'Two-stream peak RSS KiB (3s, 180s): {peaks}; growth <= 8 MiB PASS')
