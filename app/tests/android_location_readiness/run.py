#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
here=Path(__file__).resolve().parent
source=here.parents[1]/'android/game/src/org/stormengine/port'
def method(text, signature):
    start=text.index(signature)
    brace=text.index('{',start)
    depth=1
    end=brace+1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}')
        end+=1
    return text[start:end]

with tempfile.TemporaryDirectory(prefix='storm-readiness-ui-') as out:
    hud=(source/'MobileHud.java').read_text()
    bodies='\n'.join(method(hud,s) for s in (
        'public boolean dispatchTouchEvent(MotionEvent event)',
        'private void rebuild()', 'void showReadiness(LocationReadiness.Snapshot next)'))
    actual=Path(out)/'MobileHudLoadingCheck.java'
    actual.write_text((here/'MobileHudLoadingCheck.java').read_text().replace('// PRODUCTION_METHODS',bodies))
    subprocess.run(['javac','-source','8','-target','8','-nowarn','-d',out,
                    str(actual),str(source/'LocationReadiness.java'),str(source/'MobileMailbox.java'),str(here/'LocationReadinessCheck.java')],check=True)
    subprocess.run(['java','-ea','-cp',out,'org.stormengine.port.LocationReadinessCheck'],check=True)
    subprocess.run(['java','-ea','-cp',out,'org.stormengine.port.MobileHudLoadingCheck'],check=True)
