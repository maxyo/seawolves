#!/usr/bin/env python3
"""Run the control flow of the shipped pirate-frequency patch.

Requires --game pointing at an unchanged TEHO 1.7.3 game and --engine pointing at engine sources. The engine's
applier patches copies of the targets (the game is never changed); the patched GenerateMapEncounter, the game's
world map wrappers and its scheduler are compiled as C++ (refs and attributes lowered by text substitution) over
stubs of the game's services. The unpatched function is the negative control. The layer itself and the applier
are checked by tools/android/tests/script_patches; this does not replace an in-game population measurement.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
app = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', required=True, type=Path)
parser.add_argument('--engine', required=True, type=Path)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('patch_assets', app / 'tools/android/script_patch_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


def function(source, name):
    start = re.search(r'(?:bool|void) ' + name + r'\(', source).start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def lower(source):
    source = source.replace('ref iEncounter1', 'int &iEncounter1').replace('ref iEncounter2', 'int &iEncounter2')
    source = source.replace('ref rEncounter1, rEncounter2;', 'Encounter *rEncounter1 = nullptr, *rEncounter2 = nullptr;')
    source = re.sub(r'\brEncounter([12])\.', r'rEncounter\1->', source)
    source = source.replace('string ', 'std::string ')
    source = re.sub(r'ENCOUNTER_GROUP \+ iEncounter([12])', r'std::string("egroup__") + std::to_string(iEncounter\1)', source)
    source = source.replace('&i1, &i2', 'i1, i2')
    source = source.replace('pchar.', 'pchar->')
    return source


prefix = r'''
#include <cassert>
#include <cstdio>
#include <string>
enum { PIRATE=4, ENGLAND=0, FRANCE=1, RELATION_ENEMY=2,
       WDM_ETYPE_MERCHANT=0, WDM_ETYPE_FOLLOW=1, WDM_ETYPE_WARRING=2, WDM_ETYPE_SPECIAL=3,
       ENCOUNTER_TYPE_PIRATE_SMALL=10, ENCOUNTER_TYPE_PIRATE_SCOUNDREL=13 };
struct Encounter { int nation=0, RealEncounterType=10; bool bUse=false, quest=false; std::string GroupName; };
Encounter MapEncounters[4];
struct { std::string island="island"; } worldMap;
struct PChar { struct { int shipcounter=0; } worldmap; int worldmapencountersoff=0; } player, *pchar=&player;
int candidateNation1=PIRATE, candidateNation2=FRANCE, generated=0, released=0, live=0;
int draws=0, keepRoll=0, numShips=0, merchants=0, specials=0;
bool quest1=false, quest2=false, candidateValid=true, stopFollow=false, forceFollow=false;
int sti(int value) { return value; }
bool CheckAttribute(Encounter *e, const char *key) { assert(std::string(key)=="qID"); return e->quest; }
bool CheckAttribute(PChar *, const char *) { return false; }
bool Island_IsEncountersEnable(const std::string &) { return true; }
int GetMainCharacterIndex() { return 0; }
int GetNationRelation(int, int) { return RELATION_ENEMY; }
void fill(int &slot, int index, int nation, bool quest) {
    slot=index; MapEncounters[index]={nation,10,true,quest,{}}; ++generated;
}
bool GenerateMapEncounter_Merchant(const std::string &, int &slot) { fill(slot,0,ENGLAND,false); return candidateValid; }
bool GenerateMapEncounter_War(const std::string &, int &slot, int) { fill(slot,0,candidateNation1,quest1); return candidateValid; }
bool GenerateMapEncounter_Battle(const std::string &, int &a, int &b) {
    fill(a,0,candidateNation1,quest1); fill(b,1,candidateNation2,quest2); return candidateValid;
}
bool GenerateMapEncounter_Special(const std::string &, int &slot) { fill(slot,0,ENGLAND,false); return candidateValid; }
void ManualReleaseMapEncounter(int slot) { assert(MapEncounters[slot].bUse); MapEncounters[slot]={}; ++released; }
void ReleaseMapEncounters() { for(auto &e:MapEncounters) e={}; }
int keepPercent=20, asks=0;
int PirateEncounterPercent() { ++asks; return keepPercent; }
int game_rand(int bound) {
    if(bound==99) { ++draws; return keepRoll; }
    if(bound==1001) return forceFollow ? 0 : 1001;
    return 0;
}
#define rand game_rand
bool wdmCreateFollowShipByIndex(float, int index, std::string *, int) {
    assert(MapEncounters[index].bUse); ++live; return true;
}
bool wdmCreateWarringShipsByIndex(int a, int b, std::string *, std::string *, int) {
    assert(MapEncounters[a].bUse && MapEncounters[b].bUse); live+=2; return true;
}
int wdmGetNumberShipEncounters() { return numShips; }
void wdmCreateMerchantShip(float) { ++merchants; }
void wdmCreateSpecial(float) { ++specials; }
bool IsStopMapFollowEncounters() { return stopFollow; }
float wdmTimeOfLastMerchant=0, wdmTimeOfLastWarring=0, wdmTimeOfLastFollow=0, wdmTimeOfLastSpecial=0;
float iEncountersRate=1;
constexpr float WDM_MERCHANTS_RATE=.07f, WDM_WARRING_RATE=.012f, WDM_FOLLOW_RATE=.020f, WDM_SPECIAL_RATE=.0015f;
'''

main = r'''
void reset() {
    ReleaseMapEncounters(); generated=released=live=draws=merchants=specials=asks=0;
    candidateNation1=PIRATE; candidateNation2=FRANCE; quest1=quest2=false;
    candidateValid=true; forceFollow=false; numShips=0; stopFollow=false;
    wdmTimeOfLastMerchant=wdmTimeOfLastWarring=wdmTimeOfLastFollow=wdmTimeOfLastSpecial=0;
}
int main() {
    // Every setting over all hundred RNG outcomes; the actual wrappers, not a copied gate.
    for(int percent:{20,50,100}) for(bool battle:{false,true}) {
        int kept=0;
        for(int roll=0;roll<100;++roll) {
            reset(); keepPercent=percent; keepRoll=roll;
            bool ok=battle ? wdmCreateWarringShips() : wdmCreateFollowShip(1);
            kept+=ok;
            assert(asks==1);
            assert(draws==(percent<100 ? 1:0));
            assert(live==(ok ? (battle ? 2:1):0));
            assert(released==(ok ? 0:(battle ? 2:1)));
            for(auto &e:MapEncounters) assert(!e.bUse);
        }
        assert(kept==percent);
    }
    keepPercent=20;
    // Pirate in either battle slot; non-pirate battles and quest events never ask or roll.
    for(int roll=0;roll<100;++roll) {
        reset(); keepRoll=roll; candidateNation1=FRANCE; candidateNation2=PIRATE;
        assert(wdmCreateWarringShips()==(roll<20)); assert(draws==1);
        reset(); keepRoll=roll; candidateNation1=FRANCE;
        assert(wdmCreateFollowShip(1)); assert(draws==0 && asks==0);
        assert(wdmCreateWarringShips()); assert(draws==0 && asks==0);
        reset(); keepRoll=roll; quest1=true;
        assert(wdmCreateFollowShip(1)); assert(draws==0 && asks==0);
        reset(); keepRoll=roll; quest2=true;
        assert(wdmCreateWarringShips()); assert(draws==0 && asks==0);
        reset(); keepRoll=roll;
        int a=-1,b=-1;
        assert(GenerateMapEncounter(WDM_ETYPE_MERCHANT,"",a,b)); assert(draws==0);
        reset(); keepRoll=roll;
        assert(GenerateMapEncounter(WDM_ETYPE_SPECIAL,"",a,b)); assert(draws==0);
    }
    reset(); keepRoll=99; int a=91,b=92;
    assert(!GenerateMapEncounter(WDM_ETYPE_WARRING,"",a,b));
    assert(a==-1 && b==-1 && released==2 && live==0);
    // A rejected scheduler hit resets BEFORE create and never retries this tick.
    reset(); keepRoll=99; forceFollow=true;
    wdmTimeOfLastMerchant=-10000; wdmTimeOfLastWarring=-10000; wdmTimeOfLastSpecial=-10000;
    wdmTimeOfLastFollow=1002;
    wdmShipEncounter(1,0,0,0);
    assert(draws==1 && live==0 && wdmTimeOfLastFollow==0);
    // At the unchanged shared cap the original scheduler clears all accumulators.
    reset(); numShips=8;
    wdmTimeOfLastMerchant=wdmTimeOfLastWarring=wdmTimeOfLastFollow=wdmTimeOfLastSpecial=2000;
    wdmShipEncounter(1,0,0,0);
    assert(draws==0 && live==0 && merchants==0 && specials==0);
    assert(wdmTimeOfLastFollow==0 && wdmTimeOfLastMerchant==0 && wdmTimeOfLastWarring==0 && wdmTimeOfLastSpecial==0);
    std::puts("PASS actual-script control flow: the set share (20/50/100%) of pirate events kept, 100% rolls nothing, "
              "quest/nonpirate bypass, clean slots, no retry, shared cap");
}
'''

with tempfile.TemporaryDirectory(prefix='pirate-spawn-', dir=os.environ.get('TMPDIR')) as tmp:
    work = Path(tmp)
    files, _ = assets.collect(app / 'script-patches')
    for name, value in files.items():
        path = work / 'layer' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)
    data = json.loads((app / 'script-patches/sets/teho-1.7.3-x64/set.json').read_text())
    for entry in data['identify'] + data['targets']:
        target = work / 'game' / entry['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.game / entry['path'], target)
    tool = work / 'applier'
    cxx = os.environ.get('CXX', 'c++')
    subprocess.run([cxx, '-std=c++20', '-O0', '-I', str(args.engine / 'src/libs/util/include'),
                    str(args.engine / 'tools/script_patches/script_patch_tool.cpp'), '-o', str(tool)], check=True)
    subprocess.run([str(tool), 'apply', str(work / 'layer'), str(work / 'game'), str(work / 'derived')], check=True)
    target = next(t for t in data['targets'] if t['path']=='PROGRAM/Encounters/encounters_map.c')
    derived = list((work / 'derived').rglob('encounters_map.c'))
    assert len(derived)==1
    patched = derived[0].read_bytes()
    assert hashlib.sha256(patched).hexdigest()==target['result_sha256']
    # The script compiler needs a native declaration before the library that registers the function.
    head = patched.decode('latin-1')
    declared, library = head.find('native int PirateEncounterPercent();'), head.find('#libriary "PirateFrequencyScriptLib"')
    assert 0 <= declared < library, 'PirateEncounterPercent() must be declared native before its #libriary line'
    baseline = (args.game / target['path']).read_bytes()
    upstream = args.game / 'PROGRAM/worldmap'
    wrappers = (upstream / 'worldmap_globals.c').read_text(encoding='latin-1')
    scheduler = (upstream / 'worldmap_encgen.c').read_text(encoding='latin-1')
    common = '\n'.join(lower(function(wrappers, n)) for n in ('wdmCreateFollowShip','wdmCreateWarringShips'))
    common += '\n' + lower(function(scheduler, 'wdmShipEncounter'))
    for name, script in [('patched',patched), ('baseline-negative',baseline)]:
        cpp = work / (name + '.cpp')
        cpp.write_text(prefix + lower(function(script.decode('latin-1'),'GenerateMapEncounter')) + common + main)
        binary = work / name
        subprocess.run([cxx,'-std=c++20','-O0',str(cpp),'-o',str(binary)],check=True)
        result = subprocess.run([str(binary)],capture_output=True,text=True)
        if name=='patched':
            assert result.returncode==0, result.stderr
            print(result.stdout,end='')
        else:
            assert result.returncode!=0 and 'asks==1' in result.stderr, result.stderr
            print('PASS negative control: unchanged baseline rejects the pirate-frequency contract')
print('PASS pirate patch control flow; runtime population NOT_RUN')
