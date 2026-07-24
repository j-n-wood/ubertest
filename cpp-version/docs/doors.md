# Doors

Doors are automatic: they open when a unit approaches and re-close on a timer once
units leave. Closed doors block units and projectiles; open doors block neither.
This document records how the **original uberDroid** engine handled doors and the
design of the **current 2D reimplementation**, which deliberately separates the
door *simulation* from its *presentation* so the same logic can drive the interim
2D tile renderer today and a 3D renderer later (or run headless / with a debug
overlay renderer).

---

## 1. Historic handling (original uberDroid)

Source: `uber/source/uberdroid/door.{h,cpp}` — `class door : public object`.

- **States** (`door.h:6-12`): `closed, opening, open, closing`. Only `closed`/`open`
  were actually used; opening/closing were declared but never assigned (the visual
  transition was a physical slide, not a discrete state — see Animation).
- **Registration / spawn**: `autoObjectRegister<door> arObject("door")` (`door.cpp:8`);
  doors are spawned by the `"door"` keyword while parsing a domain file
  (`domain.cpp:153-156`). Placement is data-driven (`object::load`: position,
  rotation, `collide_radius`, `proximity_radius`, render index). Orientation comes
  from `rotation.z`. Constructor sets `collide_radius = 20`, `proximity_radius = 35`,
  `mOpaque`, `render_always` (`door.cpp:10-26`).
- **Open triggers** (units only, never projectiles):
  - Proximity each frame (`door.cpp:95-108`): a `droid`/`player` within
    `proximity_radius` → `open()`; if already open, refresh `last_opened`.
  - Touch (`door.cpp:70-88`): `if (with->is_a(objecttype_shot)) return false;` —
    **shots never open a door**; a droid/player touching a closed door opens it.
- **Auto-close** (`door.cpp:64-67`): while open, `if (mTimer - last_opened > 2500.0) close();`
  — 2.5 s after the last time a unit was near; every proximity/touch pushes
  `last_opened` forward, so it stays open while units linger.
- **Solidity** (`door.cpp:90-93`): `bool solid() { return state != open; }`. The
  collision system blocks only when both parties are solid; a mover early-outs when
  the target is non-solid, so an open door is walked through. A droid ramming a
  **closed** door is *stunned* (`droid.cpp:477-482`: `stunned=true; pause_time += 300ms; stop();`)
  — it is **not** rerouted.
- **Animation**: NOT tile cycling. The whole door model slides along Z
  (`door.cpp:110-131`: `startParametricMovement` to `z=-57` to open, back to `z=0`
  to close) with a `doormove5.wav` sound and a toggled 64×32 occlusion polygon. The
  door renders as a 3D model (`renderobjects.txt` `DoorTiles`/`door.asc`). A
  commented-out `//render->set_index(1)` hints an earlier tile-index approach was
  dropped in favour of the slide.

**What we keep vs. change:** keep the *semantics* — sensor-open (units only),
timer-close, solid-unless-open, and don't-reroute-on-door-contact. Change the
*presentation* — the 3D Z-slide becomes 2D tile-index animation in the interim
renderer.

## 2. Authoring representation (`scene_convert::Door`, future 3D path)

The offline uberDroid→scene converter already models doors
(`shared/scene_convert/scene_types.h:203-212`): `id, position(Vector3),
rotation(Vector3), size(Vector2), state(0=closed/1=open), waypoints[2],
DoorProperties{mass, alwaysRender, spin}, FeatureCollision`. `domain_parser.cpp`
`parseDoor` reads it and derives a Box collision from `size` (`:296-298`); there is
a JSON round-trip (`scene_json.cpp` `doorToJson`/`jsonToDoor`, which applies a
sim→render coordinate scale). The runtime game does **not** consume this yet (it is
TMX-only), but it is the future 3D spawn source and shows doors already carry
`state` + `waypoints` (the sensor/console-gating hooks).

---

## 3. Current 2D reimplementation

### Architecture — simulation ↔ presentation split

The runtime already separates simulation from rendering by convention (`game_update`
pure sim vs `game_render` pure draw; managers expose `update()` + read-only
accessors that renderers consume; `AIManager` has no render method at all). Doors
follow this and are the **first non-unit dynamic level entity**.

- **Simulation (renderer-agnostic):** `Door` + `DoorManager`
  (`shared/level/door_manager.{h,cpp}`). A Box2D **sensor** shape detects units; a
  Box2D **collision** body blocks physics; the manager owns the state machine and a
  continuous `openFraction ∈ [0,1]`. No tile/mesh/texture knowledge.
- **Render-state boundary:** `DoorView { orientation, worldPos, size, state,
  openFraction }`, returned read-only from `DoorManager::views()` — the single seam a
  renderer consumes.
- **Presentation (interim 2D):** `DoorRenderer` (`shared/rendering/door_renderer.{h,cpp}`)
  consumes `DoorView`s. It builds a **"door-only" level** (all cells empty except door
  cells set to the current animation-frame GID) and runs it through the *same*
  `createLevelTileMeshCustom` / `createLevelTileModel` path as the level tiles — so
  doors get identical bump/lighting — rebuilding only when a door's discrete frame
  changes (`openFraction → frame 0..4 → GID`). Door cells are excluded from the baked
  level mesh (`game_build_level_render_data` zeroes them) so only the animated door
  draws there. `UnloadModel` keeps the shared atlas/bump/shader alive, so rebuilding
  the small door model is cheap and safe. A future 3D renderer or the debug overlay
  consumes the same `DoorView`s. (No multi-renderer framework is built now — the
  read-only boundary just makes it a later opt-in.)
- **Source-agnostic spawn:** a `DoorSpec { orientation, physicsCenter, size, col,
  row }` is produced by a TMX detector now; a `scene_convert::Door → DoorSpec`
  adapter is the future 3D path. `DoorManager` never sees TMX/tiles.

### Tile / index convention

Map cells store GIDs; `firstGid = 1`; the tileset PNG's 0-based indices are local
IDs = `gid - firstGid`. **Horizontal** door: local `18`(closed)…`22`(open);
**vertical**: local `27`(closed)…`31`(open). Detect via `localId = gid - firstGid`,
`[18,22]`→H, `[27,31]`→V; frame `f∈0..4` ⇒ `localId = base + f` (base 18/27). Tiles
are 64 px, `worldScale = 1.0` ⇒ 1 tile = 1 world unit; door collision box **1.0×0.5**
(H) / **0.5×1.0** (V) world units, centred on the tile centre `(col+0.5, row+0.5)`.

### State machine

`Closed → Opening` when units-in-range > 0; `Opening` advances `openFraction` 0→1
over `DOOR_OPEN_TIME`; `Open` holds while units are in range, else after
`DOOR_CLOSE_DELAY` → `Closing`; `Closing` runs 1→0 (reverts to `Opening` if a unit
re-enters); at 0 → `Closed`. **Solid whenever `state != Open`** (closed/opening/
closing all block — "partially open counts as closed"). Projectiles never open a
door (the sensor is unit-only). Because the sensor opens the door before a
normal-speed unit arrives, doors typically don't block units unless fast-moving.

### Physics / collision

- New `CATEGORY_DOOR` (`shared/physics/body_user_data.h`), `BodyTag::Door`; OR'd into
  `MASK_UNIT` and `MASK_PROJECTILE`.
- Door body (static) carries a single **collision** shape (category `DOOR`, mask
  `UNIT|PROJECTILE`, the 1.0×0.5 / 0.5×1.0 box).
- Conditional collision via `b2Shape_SetFilter`: mask `UNIT|PROJECTILE` when
  `state != Open`, mask `0` when `Open` (toggled only on state change).
- **Sensing is polled, not event-driven.** Each frame `DoorManager::update` runs a
  `b2World_OverlapAABB` over the door's proximity region (box + `DOOR_SENSOR_MARGIN`)
  filtered to `CATEGORY_UNIT`; any hit ⇒ a unit is in range. This is deterministic
  and dodges a Box2D v3.0.0 quirk where sensor *end* events don't fire on a
  teleport. Projectiles are excluded by the filter, so they never open a door.

### AI / projectile crossover

- `pathClear` casts against `CATEGORY_STATIC` only ⇒ **doors are transparent to unit
  routing** (units path through doorways) — no change needed.
- `AIManager::processCollisions` **skips the reroute** when the contacted body's tag
  is `Door` (the door will open).
- Projectiles are blocked by closed doors (`CATEGORY_DOOR` in `MASK_PROJECTILE`) and
  pass through open ones (filter → 0).

### Delivery stages

1. **Stage 1 (done)** — simulation core (proximity sensor + state machine +
   `openFraction`) and the 2D door renderer (tile-index animation). Doors visibly
   open/close as units approach.
2. **Stage 2 (done)** — conditional collision body toggled by state, plus the
   AI/projectile crossover above.

Both stages are implemented and covered by tests; the tile animation is
build-verified and awaits an in-game visual check.

### Files

- New: `shared/level/door_manager.{h,cpp}`, `shared/rendering/door_renderer.{h,cpp}`
  (+ `cmake/SharedSources.cmake`).
- `shared/physics/body_user_data.h` — `Door` tag, `CATEGORY_DOOR`, masks.
- `shared/physics/physics_world.{h,cpp}` — door body/sensor/collision + filter toggle.
- `shared/level/level_renderer.cpp` — exclude door GIDs from the baked tile mesh.
- `shared/ai/ai_manager.cpp` — skip reroute on `tag == Door`.
- `src/game.{h,cpp}` — detect doors on load; `DoorManager::update` in `game_update`;
  door render in `game_render`; teardown on level switch/shutdown; tuning constants.

### Verification

- Headless unit tests: opens when units-in-range > 0, closes after `DOOR_CLOSE_DELAY`,
  projectile never opens, `openFraction` progression, collision solid unless `Open`,
  AI ignores door contacts for rerouting, projectile blocked by closed / passes open.
- In-game (`V` overlay): a **door debug renderer** (a second `DoorView` consumer,
  independent of the tile renderer) draws each door's footprint coloured by state —
  RED closed, ORANGE opening, GREEN open, GOLD closing — with the solid block
  retracting as it opens, plus a `STATE nn%` label. Use it to confirm doors open as
  units approach and re-close after they leave, that units pass without rerouting,
  and that shots stop at closed doors and pass open ones — all before the tile
  animation renderer lands. (This overlay is exactly the multi-renderer pattern: the
  same simulation state drives both the eventual tile renderer and this debug view.)
