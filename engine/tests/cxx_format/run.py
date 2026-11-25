#!/usr/bin/env python3
"""Check that the engine's own C/C++ sources are formatted (tools/format/cxx_format.py --check). Without the NDK's
clang-format it exits 77, which the host check runner reports as NOT_RUN."""
from pathlib import Path
import subprocess
import sys

engine = Path(__file__).resolve().parents[2]
sys.exit(subprocess.run([sys.executable, str(engine / 'tools/format/cxx_format.py'), '--check']).returncode)
