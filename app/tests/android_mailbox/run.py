#!/usr/bin/env python3
"""Run the production MobileMailbox on the host: a purge keeps the latest state messages and drops
only commands; every message the application queues is classified (MobileMailbox.kindOf)."""
from pathlib import Path
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
# The literal start of every queued message: "graphics:reload", "camera:zoom\t", "back", ...
queued = sorted({m for path in java.glob('*.java')
                 for m in re.findall(r'queueMobileAction\("([^"]*)"', path.read_text(encoding='utf-8'))})
assert queued, 'no queueMobileAction("...") call found'
with tempfile.TemporaryDirectory(prefix='storm-mailbox-') as temporary:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary,
                    str(java / 'MobileMailbox.java'), str(java / 'AudioFocusState.java'), str(here / 'MobileMailboxCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary, 'org.stormengine.port.MobileMailboxCheck', *queued], check=True)
