#!/usr/bin/env python3
"""Real generator + FinalizeQueue, inert compute/storage. No native models/GPU.
The check waits on progress only; DEADLOCK_GUARD is not a time budget but the point at which a step that cannot
finish is reported (with the last step it printed).
"""

from pathlib import Path
import os, subprocess, sys, tempfile

DEADLOCK_GUARD = 300  # seconds; an idle run takes well under 1 s
root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='relief-staged-', dir=os.environ.get('TMPDIR')) as d:
    d = Path(d)
    binary = d / 'check'
    worker = root / 'src/libs/renderer_bgfx/src/effects/relief_generator.cpp'
    command = [
        os.environ.get('CXX', 'c++'),
        '-std=c++20',
        '-O0',
        '-g',
        '-pthread',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fno-access-control',
        '-ffunction-sections',
        '-fdata-sections',
        '-Wl,--gc-sections',
        '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',
    ]
    for path in [
        'tests/relief_scheduler/stubs',
        'src/libs/renderer_bgfx/src',
        'src/libs/renderer_bgfx/include',
        'src/libs/renderer_bgfx/fx/include',
        'src/libs/relief',
        'src/libs/renderer_bgfx/src/texture/core',
    ]:
        command += ['-I' + str(root / path)]
    subprocess.run(command + [str(here / 'check.cpp'), str(worker), '-o', str(binary)], check=True)
    try:
        r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=DEADLOCK_GUARD)
    except subprocess.TimeoutExpired as hung:
        steps = (hung.stdout or b'').decode(errors='replace').strip().splitlines()
        sys.exit(
            f'FAIL: no progress for {DEADLOCK_GUARD} s (a generator deadlock) at {steps[-1] if steps else "start"}'
        )
    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
    print(r.stdout, end='')
