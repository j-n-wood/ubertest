# Floor decals ("dirty marks")

Runtime alpha-textured quads laid flat on the floor, ported from uber's `dirty_t` system. Two kinds
today, both **cleanable** (a cleaner droid fades them away): **blastmarks** (scorch where a
destructible object explodes) and **drips** (fluid a damaged, moving droid leaks). See
`shared/effects/decal_manager.{h,cpp}` and the render pass in `game_render_gameplay`
(`src/game.cpp`). Level-authored (permanent) decals are deferred — the `Decal::cleanable` flag
(`false`) reserves them.

## Data model — `DecalManager`

A `Decal` is lightweight (`pos` XZ, `size` half-extent, random `rotation`, `alpha`, `texture`,
`cleanable`) and has **no lifetime** — it lives until a cleaner fades its `alpha` to 0, then it's
reaped. Decals **persist per deck**: the manager holds a `std::vector<Decal>` per level index and a
current `active_` level (`setActiveLevel`, driven each frame from `game->currentLevel`), so marks the
player leaves behind are still there on return — mirroring the eager per-deck rosters. Only the
active deck is spawned into / faded / rendered (cleaners on other decks are frozen). A per-deck cap
(`DECAL_MAX_PER_LEVEL = 256`) drops the oldest mark so drips can't grow without bound.

API: `build(levelCount)`, `setActiveLevel`, `spawnBlastmark/spawnDrip(pos, size)`, `update(dt)`
(reap fully-faded), `active()` (render), `nearestCleanable(pos, maxDist)` + `cleanAt(idx, dt)`
(cleaner queries), `clear()` (teardown). Textures `TEX_DECAL_BLASTMARK` / `TEX_DECAL_DRIP` are RGBA
masks in `assets/textures/decals/` (derived from uber's `scorch1`/`splat1`: luminance → alpha, a
dark scorch / green drip colour).

## Spawning

- **Blastmark** — in `game_reap_objects` (`src/game.cpp`), when a destructible object explodes:
  `decalManager.spawnBlastmark({pos.x, pos.z}, 0.5·explodeSize)`. Only destructible-object
  explosions, not unit deaths (matches uber).
- **Drip** — `game_update_drips(game, dt)` in the sim block. A unit drips when its def's
  `dripThreshold` (health, `DroidProperties::dripThreshold`; 0 = never — authored per class in
  `assets/units/droid_class_*.json`) is met (`0 < currentHealth < dripThreshold`), it's **moving**
  (Box2D linear velocity > `ANIM_MOVING_SPEED`), and its per-unit `dripCooldown` has elapsed. The
  cooldown is re-randomized on each drip and **shortens as health drops** (`base + jitter +
  healthFrac·k`), so a near-dead droid leaks faster. `UnitInstance::dripCooldown` holds the timer.

## Cleaner droids

A droid whose **`typeCode` is in 102–199** is a cleaner (`AIComponent::isCleaner`, set at
`AIManager::init`; in ship1 this is the Class-1 / Class-2 droids, typeCode 123 / 139, matching uber's
`PERFORMCLEAN` cleaners). The AI adds an `AIState::Clean`: in `updatePatrol`, a cleaner that spots a
`nearestCleanable` decal within `AI_CLEAN_DETECT` switches to `Clean` (lower priority than chasing a
detected player). `updateClean` walks to the mark and, within `AI_CLEAN_REACH`, `holdPosition`s and
`cleanAt(idx, dt)` fades it (`DECAL_CLEAN_RATE`/s, ~2 s) until it's gone, then resumes Patrol.
`AIManager::update` takes an optional `DecalManager*` (threaded from `game.cpp`; null in tests/
catch-up disables cleaning). Cleaners only ever touch **cleanable** runtime marks — never level
decals.

## Rendering (`game_render_gameplay`)

Drawn after the floor/objects and before units (units + walls sit on top), as manual ground-plane
`RL_QUADS` at `Y ≈ 0.03` (just above the floor), `BLEND_ALPHA`, depth-tested-but-not-written, with
per-vertex `alpha`. Two efficiency measures: decals are **grouped by texture** (one bind + one
`rlBegin/rlEnd` batch per texture, not per mark) and **frustum-culled** (skip marks whose centre
projects well off-screen via `GetWorldToScreen` — decals persist and many are off-camera).

RAII render guards (`shared/rendering/render_scope.h`) manage the paired GL state: `BlendModeScope`,
`DisableDepthMaskScope`, `DisableBackfaceCullScope`, and — crucially — `RenderBatchFlushScope`.
rlgl draws batched immediate-mode geometry lazily (at `EndMode3D`); the flush scope is constructed
**last** (so it destroys **first**) to submit the horizontal quads while culling is still disabled,
otherwise they'd be drawn later with culling back on and silently culled.

## Tests

`tests/decal_test.cpp` (render-free) covers spawn, per-deck persistence, `nearestCleanable`
range/flag, the fade→reap lifecycle, and `clear`. `ai_test.cpp` checks the cleaner-band predicate
(typeCode 102–199), and `unit_json_test.cpp` round-trips `dripThreshold`.

## Deferred / future

- **Level-authored decals** (uber's permanent map `Feature` decal layer) — needs converter + bundle
  export; the `cleanable = false` flag reserves them, and cleaners already ignore non-cleanable marks.
- **Weapon-impact marks** (uber `MARKINDEX`/`MARKRADIUS`) — a trivial add via `spawnBlastmark`.
- Blastmark/drip texture colour + `DECAL_CLEAN_RATE` are easy tuning knobs.
