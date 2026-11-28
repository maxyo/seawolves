"""The reader's C API (storm_archive.h) for the host checks, through ctypes."""

import contextlib
import ctypes as C
import json
import os
from pathlib import Path
import tempfile


class Limits(C.Structure):
    _fields_ = [
        (n, C.c_uint64) for n in ('total', 'file', 'entries', 'seconds', 'scratch', 'depth', 'ratio', 'reserve')
    ]


def limits(**values):
    """Source limits for the checks: the app's (ArchiveInstaller) with a small reserve, any field overridden."""
    defaults = dict(
        total=1 << 34, file=1 << 32, entries=400000, seconds=600, scratch=1 << 34, depth=3, ratio=100, reserve=1 << 20
    )
    defaults.update(values)
    return Limits(**defaults)


Progress = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_char_p, C.c_uint64, C.c_uint64, C.c_uint64)


def load(library):
    lib = C.CDLL(str(Path(library).resolve()))
    lib.storm_archive_discover.argtypes = [
        C.c_char_p,
        C.c_char_p,
        C.c_char_p,
        C.c_void_p,
        C.POINTER(Limits),
        Progress,
        C.c_void_p,
        C.POINTER(C.c_void_p),
        C.c_char_p,
        C.c_size_t,
    ]
    lib.storm_archive_extract_root.argtypes = [
        C.c_char_p,
        C.c_char_p,
        C.c_char_p,
        C.c_char_p,
        C.c_char_p,
        C.POINTER(Limits),
        Progress,
        C.c_void_p,
        C.POINTER(C.c_void_p),
        C.c_char_p,
        C.c_size_t,
    ]
    lib.storm_archive_patch_layer.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.c_char_p, C.c_size_t]
    lib.storm_archive_layer_directory.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t]
    lib.storm_archive_layer_directory.restype = C.c_void_p
    lib.storm_archive_layer_free.argtypes = [C.c_void_p]
    lib.storm_archive_free.argtypes = [C.c_void_p]
    lib.storm_archive_remove_tree.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t]
    return lib


def _document(lib, data):
    document = json.loads(C.string_at(data).decode()) if data.value else None
    lib.storm_archive_free(data)
    return document


@contextlib.contextmanager
def patch_layer(lib, path):
    """The script patch layer in the directory `path` (storm_archive_layer_directory); OSError when it holds none."""
    error = C.create_string_buffer(2048)
    handle = lib.storm_archive_layer_directory(os.fsencode(path), error, len(error))
    if not handle:
        raise OSError(error.value.decode(errors='replace'))
    try:
        yield handle
    finally:
        lib.storm_archive_layer_free(handle)


def discover(lib, source, scratch, password=None, bounds=None, progress=None, layer=None):
    """(status, document, error) of storm_archive_discover; `layer` from patch_layer."""
    data, error = C.c_void_p(), C.create_string_buffer(2048)
    code = lib.storm_archive_discover(
        os.fsencode(source),
        os.fsencode(scratch),
        password.encode() if password else None,
        layer,
        C.byref(bounds or limits()),
        Progress(progress) if progress else Progress(),
        None,
        C.byref(data),
        error,
        len(error),
    )
    return code, _document(lib, data), error.value.decode(errors='replace')


def extract_root(lib, source, scratch, root_id, staging, password=None, bounds=None, progress=None):
    """(status, document, error) of storm_archive_extract_root."""
    data, error = C.c_void_p(), C.create_string_buffer(2048)
    code = lib.storm_archive_extract_root(
        os.fsencode(source),
        os.fsencode(scratch),
        password.encode() if password else None,
        root_id.encode(),
        os.fsencode(staging),
        C.byref(bounds or limits()),
        Progress(progress) if progress else Progress(),
        None,
        C.byref(data),
        error,
        len(error),
    )
    return code, _document(lib, data), error.value.decode(errors='replace')


def install(lib, source, staging, **options):
    """Discovers the game in source (a directory or one file in it) and extracts it into staging, as the app does:
    (status, error). A failed container ends it with its issue's status and error; a source with no game, with
    status 9."""
    with tempfile.TemporaryDirectory(prefix='storm-archive-scratch-') as scratch:
        bounds = options.get('bounds')
        code, document, error = discover(lib, source, scratch, options.get('password'), bounds)
        if code:
            return code, error
        if not document['candidates']:
            issues = document['issues']
            return (issues[0]['status'], issues[0]['error']) if issues else (9, 'No game root')
        code, _, error = extract_root(
            lib, source, scratch, document['candidates'][0]['id'], staging, options.get('password'), bounds,
            options.get('progress'),
        )
        return code, error
