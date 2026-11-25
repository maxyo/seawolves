#!/usr/bin/env python3
"""Check that every Java source under app/ is formatted (tools/android/java_format.py --check)."""
from pathlib import Path
import subprocess
import sys

app = Path(__file__).resolve().parents[2]
sys.exit(subprocess.run([sys.executable, str(app / 'tools/android/java_format.py'), '--check']).returncode)
