# Effects & realtime damage

Transient world **effects** — the first being **explosions** — managed by `EffectManager`
([`shared/effects/effect_manager.h`](../shared/effects/effect_manager.h)), a peer of the
door/charger managers. Effects are spawned dynamically (not from tiles), don't collide, and are
rendered as animated additive billboards by the game.

## EffectManager

Lifecycle mirrors the other managers: `init(worldId)` (bind the active level's Box2D world,
clear old effects), `spawnExplosion(pos, ownerGroup)`, `update(dt)`, `destroy()`, and
`getEffects()` for the renderer. It owns no Box2D bodies (`destroy()` just clears). Wired in
`src/game.cpp`: `init` alongside `game_create_chargers` (game start + each level switch),
`update(simDt)` in the un-paused sim block (before `game_reap_dead`), render in
`game_render_gameplay`, `destroy()` in `game_destroy` before the worlds are torn down.

`Effect` = `{ type, pos, age, rotationDeg, ownerGroup, active }`. `EffectType` currently has
only `Explosion`; new kinds (some non-damaging) get a new enum value with their own
`update`/render branch.

## Explosions

**Spawned when a unit is destroyed** — enemies in `game_reap_dead` (`src/game.cpp`) and
captured/created units in `destroyUnit` (`src/transfer_control.cpp`, which covers a piloted unit
dying and the old captured unit discarded on transfer). Spawned at the unit's body position with
`ownerGroup = unit->collisionGroupId`, before the body is freed.

**Area damage over time** — each frame while alive, an `b2World_OverlapAABB` (radius
`EXPLOSION_RADIUS`, `maskBits = CATEGORY_UNIT`) finds units; each is resolved to a
`UnitInstance*` via `BodyUserData`. Damage per second falls off from the centre:

```
dps(r) = EXPLOSION_DPS * clamp(EXPLOSION_CORE / max(r, EXPLOSION_CORE), 0, 1)   for r <= R, else 0
```

i.e. full `EXPLOSION_DPS` within the core, ~1/r beyond, nothing past `EXPLOSION_RADIUS`. The
frame's `dps(r) * dt` is **accumulated** onto the unit (not applied immediately — see below).

**Exclusion ("same unit")** — a unit whose `collisionGroupId == ownerGroup` is skipped (the
blast doesn't hurt its source). Invulnerable units are excluded for free: transfer
invulnerability clears the device's collision filter, so a non-collidable unit never appears in
the `CATEGORY_UNIT` overlap — which is why exploding the old captured unit while transferring
doesn't hurt the device/new unit.

**Animation & lifetime** — an 8-frame additive sprite sheet (`rlboom.png`, `TEX_RLBOOM`) at
10 fps via `SpriteAnimation` (`game->explosionAnim`), frame chosen from the effect's own `age`.
Each explosion gets a fixed **random screen-space rotation** (`rotationDeg`, applied via the
`DrawBillboardPro` rotation about the view axis). It plays once and expires at
`EXPLOSION_LIFETIME = EXPLOSION_FRAMES / EXPLOSION_FPS` (0.8 s); visual diameter is
`2 * EXPLOSION_RADIUS`.

**Tunables** (`effect_manager.h`): `EXPLOSION_DPS = 10`, `EXPLOSION_RADIUS = 0.75`,
`EXPLOSION_CORE = 0.25`, `EXPLOSION_FPS = 10`, `EXPLOSION_FRAMES = 8`.

## Realtime damage model (units)

Continuous sources like explosions must not fire on-damage reactions every frame, so units now
track **realtime damage** separately from single-hit damage
([`shared/units/combat_state.h`](../shared/units/combat_state.h)):

- `accumulateRealtimeDamage(state, raw)` sums raw damage into `pendingDamage`.
- `updateRealtimeDamage(state, dt)` — called per unit each frame from `UnitManager::update` —
  advances a timer and every `REALTIME_DAMAGE_INTERVAL` (0.1 s) flushes the accumulated total
  through `applyDamage` in one hit, then resets. So **armour is applied once per tick** (not per
  frame) and a future on-damage sound would fire at ≤10 Hz.
- **Single-hit** damage (projectiles) is unchanged — still an immediate `applyDamage`.

## Tests

`tests/effect_test.cpp`: damage within range, 1/r falloff (closer takes more), same-group
exclusion, out-of-range untouched, expiry after lifetime. `tests/combat_state_test.cpp`:
`updateRealtimeDamage` accumulation + 0.1 s flush, armour once per flush, zero-dt no-op.

## Deferred / notes

- Enemy death uses `destroyInstance` (no debris); the explosion is added there. The player
  **device** dying isn't reaped today, so it spawns no explosion.
- Rendering is inline per `EffectType` (explosions are billboards, not tile meshes) — extensible
  by adding a branch; no separate renderer class yet.
