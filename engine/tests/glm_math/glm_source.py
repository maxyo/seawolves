"""The pinned GLM (cmake/glm.cmake) for the checks that compile engine code directly: the host check build passes it
(--glm-source, or $STORM_GLM_SOURCE in every script check's environment: tests/CMakeLists.txt)."""

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENVIRONMENT = 'STORM_GLM_SOURCE'


def glm_source(explicit=None):
    """--glm-source, else $STORM_GLM_SOURCE; an error otherwise."""
    candidate = explicit or os.environ.get(ENVIRONMENT)
    if not candidate:
        raise SystemExit(f'No pinned GLM: pass --glm-source or set {ENVIRONMENT} (the host check build sets it)')
    if not (Path(candidate) / 'glm/glm.hpp').is_file():
        raise SystemExit(f'{candidate}: no glm/glm.hpp in this GLM source')
    return Path(candidate).resolve()


def math_include_flags(explicit=None):
    """Compiler flags giving a translation unit storm_glm.h and GLM itself."""
    return ['-I' + str(ROOT / 'src/libs/math/include'), '-isystem', str(glm_source(explicit))]
