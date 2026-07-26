# Levels: per-level worlds & persistent units

Each level is a **separate Box2D world**, created at ship load and retained for the ship's
lifetime. Only the **active** level's world is stepped and rendered; inactive levels'
droids simply freeze in place in their own world. Separate worlds make cross-level overlap
structurally impossible (every level's geometry is centred on the origin), and the only
things that ever cross worlds are the **player device** and the **unit it is piloting**.

## Model

- `Game::levelWorlds[L]` — the `b2WorldId` for level L; `levelOrigins[L]` — the static
  motor-joint anchor in that world. Level 0 reuses the world from `physics_world_init`; the
  rest are created fresh. `game->physics.world_id` is repointed to the active level's world
  on every activation, so all systems (units, AI, projectiles, doors, collision, stepping)
  operate on the active world with no per-call-site changes.
- `Game::levelUnits[L]` — the **persistent roster** of that level's droids (`UnitInstance*`),
  living in `levelWorlds[L]`. `UnitInstance::levelIndex` records a unit's level (the player
  device is `-1`; it migrates).
- Droids are created **lazily on first entry** (`game_spawn_enemies` → `resolveSpawns` once,
  fixing the roster of types), then persist. When a level is deactivated its droids are set
  `active=false` (skipped by update/render) and its world stops stepping, so they freeze.
  **Re-entry re-scatters them to random waypoints** (`game_reactivate_current_level` assigns
  each droid a random waypoint — via a shuffled list of waypoint *indices*; the waypoints
  themselves are never modified — and repositions the droid there), heals them for the time
  away, and rebuilds fresh patrol AI. So a level's *set of droids and their health* persist, but their
  positions/AI are re-randomised on every activation (a same-level lift exit is not an
  activation, so it leaves everything untouched).

## Level switch (`game_change_level`)

1. Release transfer control, remembering the piloted class + health.
2. `game_deactivate_level(old)` — freeze its droids, stamp `levelLastActive`, tear down the
   old collision bodies.
3. Point `physics.world_id` at the new world; rebuild render data + collision + doors +
   chargers + consoles into it (single managers, rebuilt per entry — their `init()` destroys
   the previous world's bodies first).
4. Migrate the player device into the new world at the arrival tile via `unit_rebind_world`
   (destroys the body/joint in the old world, recreates in the new; health/render tree and
   the collision filter are preserved).
5. Populate (first visit) or reactivate the new level's roster.
6. Re-pilot the carried unit type, restoring its carried health.

Same-level lift exit does **not** change level (no deactivate/reactivate) — the live state
is simply retained.

## Away-level healing

Droids on inactive levels don't simulate; instead, on re-entry a single pass regenerates
each survivor's health for the time the level was inactive:
`away_healed_health(current, max, gameClock − levelLastActive[L], AWAY_HEAL_FRACTION_PER_SEC)`
(`shared/units/heal.h`, ~2%/s → full in ~50s). `gameClock` accumulates gameplay time.

## Death

`game_reap_dead` (each frame after combat) removes any droid whose health hit 0 from its
roster + the active `enemyUnits` and destroys it — permanent, so it won't return on
re-entry. (This finally wires enemy death; the captured unit's death is handled by the
transfer controller.)

## Shared models

Because a droid type's GLTF loads once via the [ModelCache](../shared/units/model_cache.h)
and is shared across instances (and across all per-level worlds and the droid library),
retaining every visited level's droids is cheap on the GPU. Only the single skeletal model
(`legs`) is per-instance. See docs/transfer.md for the device/capture rules.

## Teardown

`game_destroy` frees unit bodies (`unitManager.destroy()`), then the door/charger bodies,
then `b2DestroyWorld` for every `levelWorlds[L]` (which frees origins, collision, and any
remainder), then the shared models — in that order so nothing double-frees.

## Deferred

- Eager population of **all** levels at ship init (for simulating levels the player is not
  on) — the per-level worlds make this a later "also step world N / create all rosters up
  front" step.
- Live off-level simulation (stepping inactive worlds).
