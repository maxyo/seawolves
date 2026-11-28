# Test scenarios

Named game states for testing screens a new game does not reach (gambling, port office, shipyard repair, boarding,
the journal with many quests, ...). Test tooling only: original GPL code, never shipped, never part of the game.

Each `<name>.c` here is a small script of the game's language that includes `scenario.c` (the driver) and makes its
state from a **start save** with the game's own functions (`AddPassenger`, `SetCompanionIndex`, `GenerateShipExt`,
`AddQuestRecord`, `QuestToSeaLogin_*`, `DoQuestReloadToLocation`, ...). It runs through the game's own migration hook:
`scenarios.py` writes `PROGRAM/migrations/NNNN_scenario_<name>.c` (the next free number; `migrations.c` runs every
migration a save has not seen while it loads) plus `PROGRAM/scenarios/` into a throw-away copy of the game. Loading
the start save then runs:

1. `Scenario_Setup()` inside the load: money, items, officers, companions, ship and damage, quests, people at their
   places, and the destination (`Scenario_Place` a location, `Scenario_PlaceSea` the sea off an island);
2. once the loaded scene is on screen the driver takes the hero there with a quest reload or a quest sea login, so the
   place is entered as the player enters it (its people, shops, doors, the enemies at sea);
3. `Scenario_Arrived()` on arrival, then after 3 s on screen the game is saved as **`scenario-<name>`** in the start
   save's profile (the game's `SaveEngineState`), then `Scenario_Trigger()` (run-only actions, not saved; on devices only: the host sweep captures the saved state, its script gets no frames under a game screen);
4. the segment unloads itself. Progress is traced as `Scenario <name>: ...` (system.log, logcat `StormEngine`), a
   failure as `Scenario <name>: FAILED <reason>`.

Later runs load `scenario-<name>` directly. **The saves contain game data**: keep them only in a local folder outside
the repository (or an ignored one), never commit or publish them.

```sh
python3 tools/scene_sweep/scenarios.py list
# emulator (started with -read-only: the injected files go away with its overlay)
python3 tools/scene_sweep/scenarios.py device NAME -s emulator-5554 --start <start save> --saves <dir>
#   then load the start save on the device (it is the newest record: title -> Continue)
# host: the scene sweep's scenario:<name> scene (run.py --scenario), one engine per scenario
python3 tools/scene_sweep/scenarios.py sweep NAME... --game <game> --engine <sweep engine> --save <start save> \
    --out <report dir> --saves <dir> [run.py options]
```

The start save must be made on land (the boot stage's save, `$STORM_BOOT_SAVE`, is one: the hero in the streets of
Saint-Pierre, Martinique, early game, opening quest lock already lifted). `Scenario_Unlock()` lifts the opening
quest's lock the way the game does (Mishelle.c) for saves made before that.

| Scenario | State | Screens it opens to |
| --- | --- | --- |
| `gamblers` | Saint-Pierre tavern at noon, 50 000 pesos; a card player at table 1, a dice player at table 2 | tavern dialogue, cards, dice, rest (self-talk) |
| `church` | Saint-Pierre church at noon, 20 000 pesos | priest dialogue, donation |
| `port_office` | Saint-Pierre port office, 200 000 pesos, two companion ships | port master dialogue, leaving/taking back a ship |
| `shipyard_repair` | Saint-Pierre shipyard, 200 000 pesos, a brig at hull 45 %, 4 cannons lost | shipyard, repair, cannons, sails and coat of arms |
| `fleet` | Saint-Pierre streets, corvette, navigator, boatswain, fighter, two loaded companions; the run then opens the month's pay (not saved) | ship screen, ship exchange, cargo/crew transfer, salary |
| `sea_prize` | the open sea, corvette and brig, a pirate war tartane and lugger attacking; the run then boards the tartane (not saved) | sea battle, boarding, plunder, prize captain, transfer |
| `maps` | Saint-Pierre streets, the best map, the atlas with six island maps, both halves of a treasure map | best map, atlas, map reading |
| `questbook` | Saint-Pierre streets, fourteen real quests in the journal, five completed; the run then closes one more with a record (not saved) | journal, completed quests, a «Logbook Update» line leading to the completed tab |
| `overload` | Saint-Pierre streets, the hero over his carrying limit (whisky casks), totem_06 in the bag, not worn | walk-only gait, items screen (wearing the totem lifts the rule) |
| `sea_exchange` | the calm open sea, no enemy, corvette with officers and a loaded companion brig | sea HUD orders (sail to the companion), ship screen, ship exchange: cargo, crew, captain chooser |
| `sea_board` | the open sea, corvette with a full crew and two fighters, a pirate brig with a full crew whose captain may give up; the run then boards it (not saved) | deck fight, the next deck, the cabin, plunder, prize captain and crew |
| `treasurer` | Saint-Pierre streets, a lugger with a treasurer, standing orders of balls and powder, a brig companion; the run then opens the purchase list (not saved) | purchase list |
| `sea_surrender` | the open sea, corvette, a pirate brig with cargo; the run then boards it with the game's surrender switch on (not saved) | plunder: the surrender message, the captain's and the crew's fate, sink or keep the ship |
| `loot` | Saint-Pierre streets, a brig with 30 crew; the run then opens the plunder of the town's store (not saved) | colony capture loot |
| `contraband` | Saint-Pierre streets, a brig with goods; the run then opens the smugglers' trade (not saved) | contraband trade |
| `storage` | Saint-Pierre streets, a brig with goods; the run then opens the store's warehouse (not saved) | warehouse |
| `self_repair` | a Martinique shore as if landed from the sea, a damaged brig, planks and sailcloth, the self-repair perk | land menu, self repair |

A new scenario: copy one, keep its `// Scenario <name>: <state>` line (`list` prints it) and define the three hooks.
