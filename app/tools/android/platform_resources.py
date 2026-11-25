"""Exact original Android platform artwork included by the game application."""
from pathlib import Path

# Launcher icon: the drawables are generated from android/game/icon/*.svg by tools/android/app_icon.py.
SOURCE_FILES = frozenset({
    'drawable/ic_launcher_background.xml',
    'drawable/ic_launcher_foreground.xml',
    'drawable/ic_notification_download.xml',
    'drawable/ic_notification_pause.xml',
    'mipmap-anydpi/ic_launcher.xml',
    'mipmap-anydpi/ic_launcher_round.xml',
    'mipmap-anydpi-v33/ic_launcher.xml',
    'mipmap-anydpi-v33/ic_launcher_round.xml',
})


def _packaged(name: str) -> str:
    # AAPT2 adds the implicit -v21 of the anydpi qualifier.
    return 'res/' + name.replace('mipmap-anydpi/', 'mipmap-anydpi-v21/')


# The background's inline gradient (aapt:attr) is compiled into its own nested resource.
PACKAGED_ENTRIES = frozenset(map(_packaged, SOURCE_FILES)) | {'res/drawable/$ic_launcher_background__0.xml'}


def resource_directory(app: Path) -> Path:
    """Reject accidental artwork/data additions before compiling or publishing."""
    root = app / 'android/game/res'
    found = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
    if found != SOURCE_FILES:
        raise ValueError('Unexpected platform resources; missing=' + repr(sorted(SOURCE_FILES - found)) +
                         ', extra=' + repr(sorted(found - SOURCE_FILES)))
    return root
