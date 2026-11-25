"""The pinned sources of a native library built outside the engine (android/archive, android/torrent).

Its sources/CMakeLists.txt declares each dependency with storm_fetch_declare (cmake/StormBuildCache.cmake): fetched
once per machine with its SHA-256 checked, shared read-only, upstream fixes applied as patch files in the build
directory's own view. Configuring it on every build re-applies a changed patch.
"""

import json
from pathlib import Path
import subprocess


def fetch(project: Path, build: Path) -> dict[str, Path]:
    """Configures project in build and returns {dependency: source directory}."""
    subprocess.run(['cmake', '-S', str(project), '-B', str(build), '-G', 'Ninja'], check=True)
    return {name: Path(path) for name, path in json.loads((build / 'sources.json').read_text()).items()}
