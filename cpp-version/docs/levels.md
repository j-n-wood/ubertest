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
  `active=false` (skipped by update/render) and its world stops stepping, so they freeze **in
  place**. **Re-entry keeps every droid exactly where it froze** — no teleport
  (`game_reactivate_current_level`): it wakes them, heals them for the time away, rebuilds
  patrol AI resuming from the waypoint **nearest each droid's current position**, then rolls
  the level forward (see *Away-level catch-up*). So a level's droids, their health, and their
  positions all persist across visits (a same-level lift exit is not an activation, so it
  leaves everything untouched).
  > Earlier this **re-scattered** each droid to a random waypoint on every activation. On
  > levels with disconnected sub-areas that could teleport a droid into a region it could
  > never have walked to; the catch-up simulation below replaces that with real movement.

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

On re-entry a single pass regenerates each survivor's health for the time the level was
inactive:
`away_healed_health(current, max, gameClock − levelLastActive[L], AWAY_HEAL_FRACTION_PER_SEC)`
(`shared/units/heal.h`, ~2%/s → full in ~50s). `gameClock` accumulates gameplay time.

## Away-level catch-up (`game_simulate_level_catchup`)

Because droids persist **in place**, a level would otherwise look frozen exactly as the player
left it. Instead, at the end of reactivation the level is **rolled forward** by the time it was
away (`gameClock − levelLastActive[L]`) so its droids are where they'd have wandered to:

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
