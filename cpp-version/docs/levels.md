# Levels: per-level worlds & persistent units

Each level is a **separate Box2D world**, created at ship load and retained for the ship's
lifetime. Only the **active** level's world is stepped and rendered; inactive levels'
droids simply freeze in place in their own world. Separate worlds make cross-level overlap
structurally impossible (every level's geometry is centred on the origin), and the only
things that ever cross worlds are the **player device** and the **unit it is piloting**.

## Model

- **`Game::levelRuntime[L]`** (`struct LevelRuntime`) holds a level's *runtime* state, one entry
  per level, parallel to `Game::levels[L]` (the static `TmxLevel` map data). Fields: `world`
  (`b2WorldId`) and `origin` (the static motor-joint anchor body); `units` (the persistent
  roster of `UnitInstance*`); `populated` / `hadEnemies` / `cleared` flags; and `lastActive`
  (away-heal timing). It holds only value handles and non-owning pointers, so it is plain and
  copyable; the GPU/build artifacts (`levelRenderData[L]`, `levelCollisionData[L]`) stay in their
  own vectors. Sized once at ship load (levels are fixed) and torn down wholesale — a game reset
  or ship switch clears the ship's per-level data and reloads, so all these flags reset for free.
- Level 0 reuses the world from `physics_world_init`; the rest are created fresh.
  `game->physics.world_id` is repointed to `levelRuntime[active].world` on every activation, so
  all systems (units, AI, projectiles, doors, collision, stepping) operate on the active world
  with no per-call-site changes. `UnitInstance::levelIndex` records a unit's level (the player
  device is `-1`; it migrates).
- Droids are created **eagerly for every deck at ship load** — the starting deck via
  `game_spawn_enemies` (spawned + activated), every other deck via `game_populate_level_roster`
  (`resolveSpawns` once, fixing the roster of types) which spawns them **frozen** (`active=false`)
  in that deck's own world. Only each deck's waypoints are needed to place them, so no geometry is
  built for unvisited decks (`load3DLevelWaypoints`). This keeps the **ship-wide droid count
  accurate from the start** (see *Ship census* / `game_census_*`) rather than only for visited
  decks. The rosters then persist for the ship's lifetime. When a level is deactivated its droids
  are set `active=false` (skipped by update/render) and its world stops stepping, so they freeze
  **in place**. **Re-entry keeps every droid exactly where it froze** — no teleport
  (`game_reactivate_current_level`): it wakes them, heals them for the time away, rebuilds
  patrol AI resuming from the waypoint **nearest each droid's current position**, then rolls
  the level forward (see *Away-level catch-up*). So a level's droids, their health, and their
  positions all persist across visits (a same-level lift exit is not an activation, so it
  leaves everything untouched).
  > Earlier this **re-scattered** each droid to a random waypoint on every activation. On
  > levels with disconnected sub-areas that could teleport a droid into a region it could
  > never have walked to; the catch-up simulation below replaces that with real movement.
- **Ship-wide droid census** (`game_census_*`, `Game::shipDroidsRemaining`/`shipDroidsTotal`): a
  running count maintained *as units spawn / are defeated*, so it matches the spawn logic by
  construction (no spawn-def re-derivation). `game_census_spawn` increments it at each roster spawn;
  `game_census_defeat` decrements once per unit (guarded by `UnitInstance::defeatedCounted`) when a
  droid is **destroyed OR captured** (a captured droid still exists but is no longer an enemy);
  `game_census_despawn` drops undefeated units when a roster is torn down to be rebuilt (the debug
  renderer switch, which then re-spawns and re-counts). Because every deck is populated at load,
  `shipDroidsRemaining` is the true shipwide live count from the start, and `game_ship_is_clear` (all
  droids defeated) drives the "ship clear of droids" banner + the Ship Data console page.

## Waypoint spawn points (`start` flag) & player start (`transmat`)

Waypoints carry uber's flags; two matter for placement:

- **`start`** ("droid start") — the only valid **AI-spawn** points. `writeEntities` exports the flag,
  `loadWaypoints` reads it into `LevelRenderData::waypointIsStart`, and `resolveSpawns` restricts
  profile droids to start waypoints (falling back to all waypoints only if a level supplies no
  flags — the TMX path). This mirrors uber's `domain::startWaypoint` and keeps droids **out of
  isolated waypoint networks** (e.g. a glass-sealed loop) that are unreachable from the main area —
  otherwise units spawn where they can never be found, killed, or cleared. Explicitly-placed droids
  (`PLACEDROID` with a waypointIndex) still go to their exact waypoint.
- **`transmat`** ("player start", NOT on the AI network) — the **player** materialises here at ship
  start. `game_start_at_transmat` (called from `main` when there's no `--deck` override) scans every
  deck's `LevelRenderData::waypointIsTransmat`, collects the ship's pads, and places the player at one
  (currently the lowest deck number; **randomising over the pads is the planned "random start"**) via
  the normal `game_change_level` switch. Ship-specific: the pads come from the loaded ship's bundles.
  Falls back to `GAME_START_DECK`'s lift stop if a ship has no transmat pads. (ship1 has 3, on decks
  4/5/6; deck 7's was removed from the source because that pad sits inside a glass-sealed, otherwise
  unreachable area.)

## Glass walls (`CATEGORY_GLASS`)

A wall built from a **glass profile** (material drawtype 5, e.g. the glass tunnel that seals off
deck 7's isolated loop) is physically solid but **transparent to line-of-sight**. `buildWallCollision`
tags those quads (`WallCollisionQuad::glass`), `writeCollision` emits `"glass": true`, the loader
carries it (`Collision3D::polygonGlass`), and `game_create_level_collision` builds those bodies with
`CATEGORY_GLASS` instead of `CATEGORY_STATIC`. Because the **sight** raycasts mask only `CATEGORY_STATIC`
(+`CATEGORY_DOOR`) — render visibility (`game_has_line_of_sight`) and AI detection/firing LOS
(`AIManager::pathClear(..., seeThroughGlass=true)`) — they pass through glass. Everything **physical
or damaging** includes `CATEGORY_GLASS` and is blocked by it: unit movement (`MASK_UNIT`), projectiles
(`MASK_PROJECTILE`), beams (`BeamManager::castRay`), the disruptor blast (`disruptorBlast`), and
pathfinding (`pathClear` with the default `seeThroughGlass=false`, so units don't try to walk through
glass). Net: you can see through glass but not move, shoot, or disrupt through it.

## Level switch (`game_change_level`)

1. Release transfer control, remembering the piloted class + health.
2. `game_deactivate_level(old)` — freeze its droids, stamp `levelRuntime[old].lastActive`, tear down the
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

On re-entry a single pass regenerates each survivor's health for the time the level was
inactive:
`away_healed_health(current, max, gameClock − levelRuntime[L].lastActive, AWAY_HEAL_FRACTION_PER_SEC)`
(`shared/units/heal.h`, ~2%/s → full in ~50s). `gameClock` accumulates gameplay time.

## Away-level catch-up (`game_simulate_level_catchup`)

Because droids persist **in place**, a level would otherwise look frozen exactly as the player
left it. Instead, at the end of reactivation the level is **rolled forward** by the time it was
away (`gameClock − levelRuntime[L].lastActive`) so its droids are where they'd have wandered to:

- **Headless & patrol-only.** A bounded loop of `aiManager.update → doorManager.update →
  physics_world_step → processCollisions`, run with a **far-away synthetic player** so the AI
  stays in Patrol and nothing fires (`projectiles`/`beams`/`playerUnit` are all null). No input
  is read and `unitManager.update` is skipped — animation/render transforms are cosmetic and the
  AI reads body positions straight from Box2D, so the whole thing runs with no GPU/window touch.
- **Waypoint movement needs stepping.** The AI only writes motor-joint targets
  (`unit_set_move_target`); motion happens inside `physics_world_step`, so catch-up must step
  the level's world — it can't be a pure kinematic advance.
- **Fast & bounded.** A coarse fixed step (`CATCHUP_DT = 0.1 s`) and a cap (`CATCHUP_MAX_SECONDS
  = 60 s`) mean it covers the interval in one frame (faster than real time) with bounded cost.
- **Early-out.** `game_level_has_simulation()` gates it — today "are there any live enemy
  units?" It's an explicit predicate (a *test* in the roll-forward, not an assumption) so future
  off-screen sim sources (moving hazards, timed doors, …) can extend the condition.

Runs synchronously on activation. Overlapping it with the **lift screen** (pre-simulating the
destination while the player browses) is a future option — see *Deferred*.

## Death

`game_reap_dead` (each frame after combat) removes any droid whose health hit 0 from its
roster + the active `enemyUnits` and destroys it — permanent, so it won't return on
re-entry. (This finally wires enemy death; the captured unit's death is handled by the
transfer controller.)

## Tileset colour row & "lights out"

The level tileset atlas (`map_blocks.png`) stacks **several colour-variant rows of the same
tiles** (44 columns × 7 rows). `getTileUV(..., rowOffset)` shifts diffuse sampling down whole
rows (clamped to range; the independent bump atlas is unaffected); the tile mesh bakes those UVs
at build time, so a row change means (re)building the mesh — which already happens on every
level entry and is cheap enough for the occasional relight. `game_effective_tile_row(game)` is
the single source of the row, used by the floor mesh **and** the animated door/charger tiles
(`DoorRenderer`/`ChargerRenderer` take a `rowOffset` in `build()` and a `setRowOffset()` for the
relight) so all tiles on a deck share the same colour.

- **Base row** — a map-level TMX property `tileRow` (default 0), parsed into `TmxLevel::tileRow`,
  lets each deck pick a base palette.
- **Lights out** — when a level is first **cleared** (every enemy destroyed *or* captured —
  `game_level_hostiles_remain` false, gated by `hadEnemies`), `LevelRuntime::cleared` latches
  **permanently** and the tiles rebuild on the **last (darkened) row**. Detected on the rising
  edge right after `game_reap_dead` (captures already applied via `transfer_update`). A
  re-entered cleared level renders dark automatically because `game_build_level_render_data`
  reads `cleared` at build time. Effective row = `cleared ? lastRow : tileRow`.

## Shared models

Because a droid type's GLTF loads once via the [ModelCache](../shared/units/model_cache.h)
and is shared across instances (and across all per-level worlds and the droid library),
retaining every visited level's droids is cheap on the GPU. Only the single skeletal model
(`legs`) is per-instance. See docs/transfer.md for the device/capture rules.

## Teardown

`game_destroy` frees unit bodies (`unitManager.destroy()`), then the door/charger bodies,
then `b2DestroyWorld` for every `levelRuntime[L].world` (which frees origins, collision, and any
remainder), then the shared models — in that order so nothing double-frees.

## Deferred

- **Lift-screen overlap for catch-up.** Run `game_simulate_level_catchup` for the *destination*
  level while the player is still in the ship-view/lift screen (after the destination is known),
  spreading the cost so activation has no hitch. Blocked on the **single-level `AIManager`**: it
  holds components for the active level only, so simulating another level's world means either an
  early activation (heavy: player migration + geometry rebuild mid-lift) or making AI able to
  step a specific level's roster/world without repointing the active one. Worth doing — it also
  unlocks true concurrent off-level simulation — but only once the measured activation hitch
  warrants it. See the `AIManager` single-level assumption.
- Eager population of **all** levels at ship init (for simulating levels the player is not on)
  — the per-level worlds make this a later "also step world N / create all rosters up front" step.
- Continuous live off-level simulation (stepping inactive worlds every frame, not just a
  catch-up burst on entry) — same `AIManager` blocker as the lift-screen overlap.
