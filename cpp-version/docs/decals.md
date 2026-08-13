# Floor decals ("dirty marks")

Alpha-textured quads laid flat on the floor, ported from uber's `dirty_t` system. Two families share
one data model, render pass, and manager (`shared/effects/decal_manager.{h,cpp}`; render pass in
`game_render_gameplay`, `src/game.cpp`):

- **Runtime "dirty marks"** — spawned during play, **cleanable** (a cleaner droid fades them away):
  **blastmarks** (scorch where a destructible object explodes) and **drips** (fluid a damaged,
  moving droid leaks).
- **Level-authored decals** — permanent floor graphics placed by the map author (biohazard symbols,
  "STORAGE AREA"/"DANGER" text strips, processing markings). Loaded per deck at ship load, `cleanable
  = false`, never faded or reaped. See [Level-authored decals](#level-authored-decals) below.

## Data model — `DecalManager`

A `Decal` is lightweight (`pos` XZ, `size` half-extent along the depth/V axis, random `rotation`,
`alpha`, `texture`, `cleanable`, and `aspect` = width/depth so text strips can render wide) and has
**no lifetime** — a runtime mark lives until a cleaner fades its `alpha` to 0, then it's reaped.
Decals **persist per deck**, mirroring the eager per-deck rosters. The manager keeps **two** parallel
per-level stores and a current `active_` level (`setActiveLevel`, driven each frame from
`game->currentLevel`):

- `byLevel_` — the runtime marks (`active()`): spawned, capped (`DECAL_MAX_PER_LEVEL = 256`, drops the
  oldest so drips can't grow without bound), faded, and reaped. Only the active deck is touched
  (cleaners on other decks are frozen).
- `levelDecals_` — the permanent level-authored decals (`activeLevelDecals()`, added via
  `addLevelDecal`): the cap/clean/reap logic never touches them, so a cleaner **structurally** can't
  see them (`nearestCleanable` scans only `byLevel_`) and `update()` never removes them.

API: `build(levelCount)`, `setActiveLevel`, `spawnBlastmark/spawnDrip(pos, size)`, `update(dt)`
(reap fully-faded), `active()` (render), `nearestCleanable(pos, maxDist)` + `cleanAt(idx, dt)`
(cleaner queries), `clear()` (teardown). Textures `TEX_DECAL_BLASTMARK` / `TEX_DECAL_DRIP` are RGBA
masks in `assets/textures/decals/` (derived from uber's `scorch1`/`splat1`: luminance → alpha, a
dark scorch / green drip colour).

## Spawning

- **Blastmark** — in `game_spawn_explosion` (`src/game.cpp`), the shared entry point for **every**
  explosion (unit deaths and destructible-object deaths both route through it):
  `decalManager.spawnBlastmark(pos, 0.4·sizeScale)`, so the scorch scales with the blast (a tank's
  `explodeSize` vs a unit's 1.0). (uber only marked destructible explosions; marking all of them is
  our choice so combat visibly scorches the floor.)
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
per-vertex `alpha`. One pass draws **both** stores (`active()` then `activeLevelDecals()`). Each quad
is `size*aspect` half-wide (texture U) by `size` half-deep (V), rotated by the decal's yaw. Two
efficiency measures: decals are **grouped by texture** (one bind + one `rlBegin/rlEnd` batch per
texture across both stores, not per mark) and **frustum-culled** (skip marks whose centre projects
well off-screen via `GetWorldToScreen` — decals persist and many are off-camera).

RAII render guards (`shared/rendering/render_scope.h`) manage the paired GL state: `BlendModeScope`,
`DisableDepthMaskScope`, `DisableBackfaceCullScope`, and — crucially — `RenderBatchFlushScope`.
rlgl draws batched immediate-mode geometry lazily (at `EndMode3D`); the flush scope is constructed
**last** (so it destroys **first**) to submit the horizontal quads while culling is still disabled,
otherwise they'd be drawn later with culling back on and silently culled.

## Level-authored decals

uber places permanent floor graphics as map `Feature`s whose type id is in the **decal band 29–34**
(`uber/uberdroid/data/features.txt`) — biohazard symbols, "STORAGE AREA"/"DANGER"/"PROCESSING" text
strips. They render exactly like dirty marks but are spawned as part of the level and never cleaned.
ship1 has 14 of them across decks 0, 6, 7, 12, 14, 15.

Pipeline:
1. **Convert** — `writeEntities` (`tools/incremental_viewer/level_export.cpp`) filters
   `area.features` for `renderIndex ∈ [29,34]` and emits a `decals[]` array (`type` + render-metric
   `pos`/`rot`) into each `level_<n>.entities.json`. Positions are verbatim (already render-metric via
   `featureToJson`'s `saveCoords`); `writeCollision` ignores features, so decals add no collision.
   Regenerate bundles with `incremental_viewer --export-all <dir>`.
2. **Load** — `load3DLevelDecals` (`shared/level/level3d_loader.cpp`) maps each `type` (29–34) through
   a static table → `{TextureId, half-extent (from uber `VISIBLERADIUS`·0.0254), aspect (source
   texture w:h)}`, yaws from `rot.z` (negated for the game→render Y inversion), and marks it
   `cleanable = false`. `game_init` (`src/game.cpp`) loads decals for **every** deck (including the
   current one, unlike rosters) and feeds them to `DecalManager::addLevelDecal`.
3. **Textures** — `assets/textures/decals/{biohaz,storagearea,processingarea,text_biohazard,
   text_danger}.png`, converted from uber's decal BMPs: **alpha = source luminance** (the light
   graphic on its black background becomes the opaque part) and **RGB forced to black**, so the mark
   reads as a dark grey-black graphic on the light floor rather than a white one. One `TEX_DECAL_*`
   id each. Type 34 (`text_storage_area`, never placed in ship1) has no texture and is skipped.

## Tests

`tests/decal_test.cpp` (render-free) covers spawn, per-deck persistence, `nearestCleanable`
range/flag, the fade→reap lifecycle, and `clear`, plus `LevelDecalsPersistPerDeckAndAreNeverCleaned`
(per-deck isolation, `cleanable`/`aspect` handling, and that `nearestCleanable`/`update` never touch
the level-decal store). `ai_test.cpp` checks the cleaner-band predicate (typeCode 102–199), and
`unit_json_test.cpp` round-trips `dripThreshold`.

## Deferred / future

- **Weapon-impact marks** (uber `MARKINDEX`/`MARKRADIUS`) — a trivial add via `spawnBlastmark`.
- Blastmark/drip texture colour + `DECAL_CLEAN_RATE` are easy tuning knobs.
- **Projected decals** (marks that climb trim/walls + follow floor-height, instead of the flat
  `Y≈0.03` quads) need a render-to-texture scene-depth pass — planned together with the distortion
  post-effect (both need the same offscreen target). See
  [render_to_texture.md](render_to_texture.md).
