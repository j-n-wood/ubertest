# Shared Model State Across Renderer Reload

## Context

The game can switch its level renderer at runtime (G key → `game_cycle_renderer` →
`game_switch_renderer`, `src/game.cpp`) and at startup (`--renderer`). Today a switch does a **full
in-place level reload**: it destroys the enemy roster, destroys static collision bodies, rebuilds the
lift network, doors, chargers, consoles, repositions the player, and **re-populates enemies from
scratch** — discarding their health/positions/AI state and the level's lights-out progress.

That cost exists because **simulation/model state is entangled with rendering data**. The clearest
symptom: the navigation graph the AI and spawner depend on lives *inside* the render struct
`LevelRenderData`, so rebuilding the visual mesh necessarily rebuilds the nav model, which cascades
into re-spawning units. Units should not need to be destroyed/respawned, and lifts should not depend
on render data at all.

The intended outcome: **a switch that keeps the coordinate frame rebuilds only the visual mesh**, and
every piece of simulation state (units, AI, nav graph, collision, lifts, doors/chargers, progress
flags) is *shared* across it. Cross-frame switches (2D↔3D) still reload, because the two frames place
geometry differently (see below).

## Current coupling (findings)

- **The nav graph is render-owned.** `LevelRenderData` (`shared/level/level_types.h:178-201`) mixes
  GPU/render fields (`tileMesh`, `tileModel`, `glassMeshIndices`, `boundsMin/Max`) with **simulation
  data**: `waypointPositions`, `waypointAdjacency`, `waypointLinks` (`:189-193`). These feed player
  spawn (`game_level_spawn_pos`), enemy spawn (`game_spawn_enemies`), and AI init
  (`aiManager.init(..., renderData.waypointPositions, renderData.waypointAdjacency, ...)`), all in
  `src/game.cpp`. `AiManager` copies the graph by value on each `init`.
- **`LevelRuntime` already persists sim state** (`src/game.h`): per-level Box2D `world`, `origin`,
  the droid `units` roster, and flags `populated`/`hadEnemies`/`cleared`/`lastActive`. It survives
  level switches. It does **not** hold the nav graph, collision, or lifts — the gap this plan closes.
- **The player already survives a switch** (rebound via `unit_rebind_world`, not destroyed) — the
  precedent for "shared over reload" exists; we extend it to the rest of the sim.
- **`game_switch_renderer` discards, on every switch**, the nav graph (via
  `game_build_level_render_data`), the enemy roster + combat/AI state, the lights-out flags, the
  collision bodies, and the lift network — even when the switch does not change the coordinate frame.

## Coordinate frames (why cross-frame differs, and same-frame does not)

Two renderers, two frames for the *same* deck:

- **TMX frame** (Tilemap, CustomTiles): tile grid **centred on the level midpoint**, flat at Y=0,
  depth +Z with TMX row. `tmxGridToWorld`/`tmxPixelToWorld` (`shared/level/level_renderer.cpp`).
- **Domain / render-metric frame** (Objects3D): **corner origin**, real Y height, depth **Z = −(game
  forward)**. Bundle files (`entities.json`, `collision.json`, `transporters.json`) are authored here.

The relation is `TMX_X = domain_X − halfWidth`, `TMX_Z = −domain_Z − halfHeight`, `TMX_Y = 0`: a
translation **plus a depth-axis reflection** (opposite handedness), and the TMX vs domain nav/door/
lift data are **separately authored sets** with no id correspondence. Scale is already unified
(`WORLD_SCALE = 1.6256`, `shared/world_scale.h`); origin and handedness are not.

Consequence that drives the design: **`Tilemap` and `CustomTiles` are the SAME frame** — a switch
between them moves nothing in the sim; only the tile mesh (flat vs bump) differs. `Objects3D` is a
different frame — a switch across the 2D/3D boundary legitimately relocates every position.

## Per-subsystem data sources (what's already external)

| Subsystem | 2D source (frame) | 3D source (frame) | Shared external source exists? |
|---|---|---|---|
| Waypoints/nav | TMX object layer (TMX) | `entities.json waypoints[]` (domain) | Yes (per frame) |
| Collision | `generateLevelCollision` TMX rects (TMX) | `collision.json polygons[]` (domain) | Yes |
| Doors | TMX tile scan (TMX) | `entities.json doors[]` (domain) | Yes |
| Chargers | TMX tile scan (TMX) | `entities.json chargers[]` (domain) | Yes |
| Consoles | TMX tile scan (TMX) | `entities.json consoles[]` **(unused)** | Latent (unwired) |
| Lifts | TMX `lifts[]` (TMX) | `transporters.json` (domain) | Yes (ship-wide) |
| Spawns (roster) | `spawns.json` by **deck number** | same | **Already unified** |

Lifts never read render data — they read `transporters.json`/TMX — yet are rebuilt on every switch.
Spawn *selection* is already renderer-independent; only spawn *placement* rides the active waypoint
frame.

## Candidates for state sharing over reload

On a **same-frame** switch (Tilemap↔CustomTiles), all of the following are frame-invariant and must be
**preserved untouched** (they are re-derived today):

1. **Navigation graph** — `waypointPositions` / `waypointAdjacency` / `waypointLinks`. Relocate out of
   `LevelRenderData` into the persistent per-level sim state. Topology is frame-invariant; positions
   are identical within a frame.
2. **Enemy roster + per-unit state** — health, position, patrol target. Stop despawning
   (`game_despawn_enemies`) and re-rolling from `spawns.json`.
3. **AiManager state** — the graph copy + per-unit AI. Stop re-`init`-ing.
4. **Static collision bodies** (`Game::collisionBodies`) — independent of the tile visual.
5. **Lift network** (`LiftManager`) — already render-independent; simply not rebuilt.
6. **Doors / chargers / consoles** managers **and their sim state** (door `openFraction`, charger
   state) — preserved, not re-init'd.
7. **Progress flags** — `populated` / `hadEnemies` / `cleared` (already in `LevelRuntime`); stop
   resetting.
8. **Player unit** — already preserved.
9. **Effects / particles** — valid in the same frame; keep instead of clearing.

The **only** thing a same-frame switch rebuilds: the tile mesh/model (`tileMesh`, `tileModel`,
Tilemap-flat vs CustomTiles-bump).

## Recommended approach

**Give the nav graph (and, by extension, the sim) a home independent of the render mesh, and make the
switch frame-aware.**

1. **Move the nav graph into `LevelRuntime`** (`src/game.h`) — its natural home (per-level, persistent,
   already owns the roster the graph serves). Add:
   - `std::vector<Vector3> navPositions;`
   - `std::vector<std::vector<int>> navAdjacency;`
   - `std::vector<std::pair<int,int>> navLinks;`
   - `LevelFrame navFrame; bool navBuilt = false;` (frame the positions are in).
   Remove the three waypoint fields from `LevelRenderData` (or leave them unused and deprecated).

2. **Split `game_build_level_render_data`** into:
   - `game_build_level_nav(game)` — populates `levelRuntime[L].nav*` from the active frame's source
     (bundle `loadWaypoints` for domain; `createLevelRenderData`'s waypoint pass for TMX). Sets
     `navFrame`.
   - `game_build_level_visual(game)` — builds **only** the tile mesh/model for the current mode (the
     existing `createLevelRenderData`/`createLevelTileMesh[Custom]`/`load3DLevel` mesh paths), no nav.

3. **Repoint sim consumers** at `levelRuntime[L].nav*` instead of `levelRenderData[L].waypoint*`:
   `game_spawn_enemies`, `game_level_spawn_pos`, `game_reactivate_current_level`, and the two
   `aiManager.init(...)` call sites (`src/game.cpp`).

4. **Add a frame helper** `LevelFrame game_frame_of(LevelRenderMode)`: `Tilemap`/`CustomTiles` → `Tmx`,
   `Objects3D` → `Domain`.

5. **Make `game_switch_renderer` frame-aware**:
   - `if (game_frame_of(old) == game_frame_of(new))`: **visual-only** — set the mode, call
     `game_build_level_visual`, refresh the door/charger tile row, and **return**. No despawn, no
     collision/lift/door/charger/console rebuild, no player reposition, no re-populate.
   - `else`: the existing full reload (rebuild nav in the new frame, collision/doors/chargers/consoles/
     lifts, reposition player, re-populate) — unchanged.

6. **Lift decoupling confirmation** — `game_build_lift_network` is only invoked on level load and on a
   *frame-changing* switch, never on a same-frame switch. It already reads external state, not render
   data; this removes the spurious rebuild.

### Files

- `shared/level/level_types.h` — move the waypoint trio out of `LevelRenderData`.
- `src/game.h` — add nav fields + `LevelFrame`/`navFrame` to `LevelRuntime`.
- `src/game.cpp` — split nav vs visual build; repoint consumers; frame-aware `game_switch_renderer`;
  `game_frame_of`.
- Consumers of `renderData.waypoint*` in the viewer/debug draw (`game.cpp` waypoint overlay) point at
  the new location.

## What still reloads, and why

A **2D↔3D** switch (frame change) keeps the current full reload: the domain and TMX frames place
geometry by a translation + depth reflection and carry separately-authored nav/door/lift sets, so unit
and waypoint positions genuinely move. Preserving unit *state* across that boundary would require
re-projecting every body through the frame relation — deferred (see Future).

## Verification

- **Build/tests:** `cmake --build build --target topdown_game run_tests -j4`; keep 152 tests green.
- **Same-frame sharing (the win):** run `--renderer custom`, spawn/observe enemies, damage one, then
  press G to `Tilemap` and G again back to `CustomTiles`. Confirm the roster is **identical** (same
  count, same damaged unit, same positions), the lift network and collision are untouched, and the log
  shows a visual-only rebuild (no "Populated N enemies" line, no lift rebuild). A `SAME_FRAME` debug
  assert can check `levelRuntime[L].units` pointers are unchanged.
- **Cross-frame still correct:** G from `CustomTiles`→`Objects3D` performs the full reload (existing
  behaviour); units/doors/collision align with the 3D geometry as today.
- **No leaks:** collision/door/charger bodies are created once per frame-build, not per visual rebuild
  (watch Box2D body counts across repeated same-frame toggles).

## Future (out of scope — canonicalization)

The complete elimination of cross-frame reloads would adopt the **render-metric domain frame as the
single sim frame**, sourced from the bundle (`entities.json`/`collision.json`/`transporters.json`) for
both renderers, with the 2D tile mesh drawn under a frame-align transform (translate + Z-reflect) or
regenerated from the domain. It also implies wiring the currently-unused `entities.json consoles[]`.
This removes the TMX-embedded sim entirely (TMX becomes a pure 2D visual). It was deliberately
deferred: the frame reflection and the separately-authored TMX/domain grids make faithful 2D overlay
alignment uncertain, and 3D is already the default gameplay frame.
