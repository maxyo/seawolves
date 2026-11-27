# Effects

The `Effects` entity and the `EFFECTS_LIBRARY` natives a game declares for its Effekseer effects (Caribbean Legend's
`Program/particles.c`). Effects are played and drawn through `storm/render/effekseer.hpp` (renderer_bgfx/effekseer).

## Effect ids

`Resource/Effects/effects.json` maps an id to `{"file", "speed"}`; keys with an empty string are section titles. An
id is case-insensitive. A file ending in `.efkefc` is an Effekseer effect (`resource/effects/<file>`), one ending in
`.xps` a particle system of the `particles` entity. The table is read on first use; entries it cannot use are logged
and left out (`effect_table.hpp`).

## Natives

| Native | Behaviour |
| --- | --- |
| `int CheckAndCreateEffect(id, x, y, z, ax, ay, az, lifetime, isDirect)` | an Effekseer id plays at the position, turned by the angles, at the table's speed, and returns its handle (-1 when it cannot play); a particle system id goes to the `particles` entity (`PS_CREATE`, or `PS_CREATEX` with `isDirect`) with the lifetime, and returns -1; an id the table does not name creates nothing and returns -1 (logged once per id). `isDirect`: the angles are a direction (`storm::math::directionAngles`). An Effekseer effect takes no lifetime: the scripts end one through its dynamic input 3 (`SetEffectLifeTime`). |
| `int ReloadEffect(id)` | reads a loaded effect again, or loads it: 1 / 0 |
| `void ReloadAllEffects()` | reads every loaded effect of the table again |
| `int UnloadEffect(id)` | releases a loaded effect: 1, or 0 when it was not loaded |
| `void UnloadAllEffects()` | releases every effect |

## Messages (`MSG_EFFECT_*`, the values of the game's `Resource/shared/messages.h`)

| Message | Format after the code | Result |
| --- | --- | --- |
| `PLAY` | `s` id, `fff` position, then optionally `fff` angles, `fff` scale, `l` start frame | handle or -1 |
| `PLAY_FILE` | `s` game path, then as `PLAY` | handle or -1 |
| `DELETE`, `STOP_ROOT`, `IS_PLAYING` | `l` handle | `IS_PLAYING`: 1 / 0 |
| `DELETE_ALL` | — | |
| `UPDATE_POS`, `ADD_SHIFT`, `UPDATE_ROT`, `SET_TARGET_POS`, `SET_SCALE` | `l` handle, `fff` | |
| `GET_POS` | `l` handle, `eee` float variables | |
| `SET_PARAMETER` / `GET_PARAMETER` | `l` handle, `l` dynamic input, `f` value / `e` float variable | |
| `SEND_TRIGGER` | `l` handle, `l` trigger | |
| `PAUSE` | `l` handle, `l` paused | |
| `PAUSE_ALL` | `l` paused | |
| `SET_SPEED` / `GET_SPEED` | `l` handle, `f` / `e` | |
| `SET_COLOR` | `l` handle, `l` ARGB | |

`SET_CAPTURE` and `STOP_CAPTURED` (the editor's recording) are logged as not handled: no script sends them.

The scripts create the entity without layers: it adds itself to `EXECUTE` and `SEA_EXECUTE` (the update) and to
`REALIZE` and `SEA_REALIZE` at 65536, where the scripts put the `particles` entity (the draw). The land and the sea
pairs are never active together.
