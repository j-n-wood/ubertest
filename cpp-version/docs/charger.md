# Chargers

Chargers are animated, **walkable** map objects (in the original uberDroid, a droid
standing on one recharges its energy — `droid.cpp` `near_object_of_type(objecttype_charger)`).
They follow the same simulation/rendering split as [doors](doors.md), with two
differences: they don't block movement (no collision body), and their tile animation
is **free-running** (cycles continuously by time) rather than state-driven.

The recharge *interaction* is not implemented yet — only the object, the proximity
state, and the animation, so the interaction can be layered on later.

## Tile tagging

Charger tiles carry one custom TSX property (parsed into `TmxTileProperties`):

- `type` (string) = `"charger"`.

Each colour **row** of the atlas holds the charger's frames (≈4, but counted
data-driven); animation cycles along the **column (X)** within the authored tile's
row — same row = colour rule as doors.

## Simulation — `ChargerManager` (`shared/level/charger_manager.{h,cpp}`)

- Detected by `game_detect_chargers` (cells whose `tileProperties[gid-firstGid].type
  == "charger"`) → `ChargerSpec { physicsCenter, size, col, row }`.
- **No physics body** — chargers are non-colliding floor tiles.
- State `Idle` / `Charging` from unit proximity, polled each frame with
  `b2World_OverlapAABB` over the tile + `CHARGER_SENSOR_MARGIN`, filtered to
  `CATEGORY_UNIT` (projectiles don't count). Instantaneous for now; a charge
  accumulator/interaction can be added here later.
- Exposes read-only `ChargerView { worldPos, size, col, row, state }`.

## Presentation — `ChargerRenderer` (`shared/rendering/charger_renderer.{h,cpp}`)

- Consumes `ChargerView`s. Builds a "charger-only" level and runs it through the same
  `createLevelTileMeshCustom` / `createLevelTileModel` path as the level tiles (bump/
  lighting parity), exactly like `DoorRenderer`. Charger cells are excluded from the
  baked level mesh (`game_build_level_render_data`).
- **Free-running animation:** a time accumulator advances a shared frame index
  `frame = floor(time / CHARGER_FRAME_TIME)`; each charger shows its authored row's
  frame `ids[frame % count]` (frames sorted by column). Rebuilds the small model only
  when a frame changes. `CHARGER_FRAME_TIME = 0.05 s`.
- State is available in the view for a future "charging looks different" variant
  (e.g. a different frame set), but currently both states cycle the same frames.

## Wiring & debug

`game_create_chargers` (on level load/switch) inits the manager and builds the
renderer; `game_update` calls `chargerManager.update(dt)` + `chargerRenderer.update(dt, views)`;
`game_render` draws it after doors; teardown mirrors doors. The `V` debug overlay
draws a charger footprint + `IDLE`/`CHG` label (SKYBLUE / YELLOW).

## Tests

`tests/charger_test.cpp`: starts Idle, goes Charging when a unit is on it and back to
Idle when it leaves, and a projectile does not trigger charging.
