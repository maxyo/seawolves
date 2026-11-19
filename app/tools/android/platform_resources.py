"""Exact original Android platform artwork included by the game application."""
from pathlib import Path

# Launcher icon: generated from android/game/icon/*.svg by tools/android/app_icon.py.
LAUNCHER_PNGS = frozenset(f'mipmap-{density}/ic_launcher{suffix}.png'
                          for density in ('mdpi', 'hdpi', 'xhdpi', 'xxhdpi', 'xxxhdpi') for suffix in ('', '_round'))
SOURCE_FILES = frozenset({
    'drawable/ic_launcher_background.xml',
    'drawable/ic_launcher_foreground.xml',
    'drawable/ic_notification_download.xml',
    'drawable/ic_notification_pause.xml',
    'mipmap-anydpi-v26/ic_launcher.xml',
    'mipmap-anydpi-v26/ic_launcher_round.xml',
    'mipmap-anydpi-v33/ic_launcher.xml',
    'mipmap-anydpi-v33/ic_launcher_round.xml',
}) | LAUNCHER_PNGS


def _packaged(name: str) -> str:
    # AAPT2 adds the implicit -v4 to density-qualified directories.
    folder, file = name.split('/')
    return f'res/{folder}-v4/{file}' if folder.startswith('mipmap-') and 'anydpi' not in folder else f'res/{name}'


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
