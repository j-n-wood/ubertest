# Gameplay Implementation Plan

This plan covers the staged implementation of gameplay systems defined in [GAME.md](GAME.md), building on the existing engine foundation. Each stage is independently testable and acts as a gate before the next stage begins.

## Current State

The engine foundation is complete:

| System | Status | Detail |
|--------|--------|--------|
| Rendering | Done | Raylib, lighting shader, bump mapping, debug modes |
| Physics | Done | Box2D top-down, dynamic/static bodies, collision |
| Level loading | Done | 16 TMX levels with tile rendering and collision |
| Unit system | Done | 24 droid classes, hierarchical sections, GLTF models |
| Unit instances | Done | Create/destroy, debris, animation support |
| Player control | Done | WASD movement, mouse-aim rotation (PD controller) |
| Camera | Done | Top-down perspective, follows player |
| Tests | Done | GoogleTest infrastructure, parser/serialization tests |
| Combat stats | Done | Stage 1 complete — UnitCombatState, damage model, 10 tests passing |
| Weapons & projectiles | Done | Stage 2 complete — 9 weapons from data file, projectile lifecycle, 10 tests passing |
| Enemy spawning | Done | Stage 3 complete — level_spawns.json (3 ships × 16 levels), type-based random spawn resolution, 4 tests passing |
| AI movement & aggression | Done | Stage 4 complete — waypoint patrol, chase/flee states, turret/omni/standard firing, 14 tests passing |

What GAME.md defines but is **not yet implemented**:

- Combat rendering (projectile visuals, player firing)
- Unit capture mechanic (the core gameplay loop)
- Level navigation via lifts
- Level/ship completion and progression
- Lighting from player position

---

## Design Patterns

These patterns apply to all gameplay stages. They were established during Stages 1–3 and refined during the Stage 2 projectile refactor.

### 1. Use raylib types for members and parameters

Use `Vector2`, `Vector3`, `Color`, and other raylib primitives instead of separate `float x, y` fields. This gives access to raymath functions (`Vector2Normalize`, `Vector2Scale`, `Vector2Length`, etc.) and keeps interfaces consistent with the rest of the codebase.

**Do:** `Vector2 position`, `Vector2 velocity`, `void spawn(Vector2 pos, Vector2 dir, ...)`
**Don't:** `float posX, posY`, `void spawn(float x, float y, float dirX, float dirY, ...)`

### 2. Use Box2D directly — don't abstract it away

Systems that need physics should use Box2D directly, including in tests. Tests create a lightweight `b2World` (zero gravity) and step the simulation with controlled dt values. This validates real physics behaviour rather than maintaining a divergent manual code path. The test executable links `box2d`.

**Do:** spawn Box2D bodies, use `b2World_GetContactEvents()`, test with `b2World_Step()`
**Don't:** write manual position integration (`pos += vel * dt`) or manual circle-vs-circle intersection checks

### 3. Single source of truth — no redundant data

The unit instance collection is authoritative for all positions, orientations, and combat state. Systems read from it directly via body user data pointers on Box2D bodies. Don't build intermediate data structures that duplicate information already held by authoritative sources.

**Do:** `processContactEvents()` reads `BodyUserData` from Box2D bodies to find `UnitInstance*` directly
**Don't:** build a `vector<Target>` each frame by copying position/radius/state from unit instances

### 4. Player and AI are simulated identically (remote-control model)

The game concept is that the player gains **remote control** of a unit: player-generated instructions replace the AI's, but the thing being controlled — and how it is simulated — is unchanged. Therefore movement, rotation, and control are **identical** for an AI-driven unit and the player-driven unit. Only the *source* of the desired target differs (AI waypoint logic vs. player input).

This is enforced structurally:
- Every movable unit (including the player) is created through the same `UnitManager::createInstance()` path and receives a `b2MotorJoint` anchoring it to a static world-origin body.
- The joint's `linearOffset` is the desired world position and `angularOffset` the desired world facing. Both the AI and the player set them through the **single** shared entry point `unit_set_move_target(unit, targetPos, facing)`.
- There is one shared parameter set in [`shared/units/movement_tuning.h`](../shared/units/movement_tuning.h) (`UNIT_MOTOR_MAX_FORCE`, `UNIT_MOTOR_MAX_TORQUE`, `UNIT_MOTOR_CORRECTION_FACTOR`, `UNIT_MOVE_LOOKAHEAD`, `TURRET_SLEW_RATE`) used as a fallback. No player-specific override — tune globally.

**Per-type movement limits.** Each `UnitDefinition` carries `maxSpeed`, `acceleration`, and `deceleration` taken verbatim from the original `droidclasses.txt` data (parsed by `droidclass_parser`, emitted by `unit_generator`, read by `unit_json`). The movement layer scales them with `MOVEMENT_UNIT_SCALE` and maps them onto the motor joint. Force authority is **decoupled** from top speed via `UNIT_MOTOR_AUTHORITY`: per-frame `maxForce = mass × acceleration × MOVEMENT_UNIT_SCALE × UNIT_MOTOR_AUTHORITY` while speeding up toward the target (or `× deceleration` while braking — holding position or reversing), and per-unit `linearDamping = acceleration × UNIT_MOTOR_AUTHORITY / maxSpeed`. The authority factor cancels out of terminal velocity (`= maxSpeed × MOVEMENT_UNIT_SCALE`) but boosts absolute force so units can overcome contacts instead of jamming/freezing on collision. This is still type-level, not driver-level — an AI-driven and player-driven instance of the same type behave identically. A unit with no movement data (`maxSpeed == 0`, e.g. test fixtures) falls back to the global constants. **Debug:** `F1`/`F2` cycle the player-controlled unit type at runtime (`game_cycle_player_unit`) so each type's speed/accel/decel can be tested in place.

**Facing convention.** The renderer draws a unit facing world direction `(-sinθ, cosθ)` for physics body angle `θ` (model +Z forward, drawn at `-worldRotation` about +Y). The single helper `facing_angle_to(dx, dz)` in `movement_tuning.h` returns the `θ` that faces `(dx, dz)`; the player, AI body facing, turret head tracking, and fire-alignment (`canFire`) all use it so every facing agrees.

**Why a motor joint instead of open-loop force?** Movement is a target-seeking constraint solved *simultaneously with contacts* inside `b2World_Step`, with bounded force/torque. Collisions negotiate with the drive rather than overpowering it, so a unit knocked off course recovers smoothly instead of flying off (the failure mode of the previous constant-force model). Turret/head facing remains a render-only scalar (`facingAngle`), slewed the shortest way toward its target — no physics body for cosmetic sections.

**Do:** `unit_set_move_target(unit, carrot, facing)` for both AI and player; one tuning header.
**Don't:** apply `b2Body_ApplyForceToCenter`/`b2Body_ApplyTorque` for locomotion, or give the player a separate control path or tuning.

### Reference documents

- [unit_system.md](unit_system.md) — body user data tags, collision filtering categories, projectile lifecycle
- [projectile_system.md](projectile_system.md) — full projectile refactor design with Box2D API reference

---

## Stage 1 — Unit Combat Stats & Damage Model ✓ COMPLETE

**Goal:** Unit definitions carry gameplay-relevant stats. Damage can be applied and tested purely in logic with no rendering.

### What's added

- Runtime `UnitCombatState` struct: current health, max health, armour, alive flag
- Initialise combat state from unit definition properties (`energy` -> health, `armour` -> armour)
- `applyDamage(state, rawDamage)` — armour reduces damage, health decrements, returns alive/dead
- `isAlive()`, `destroy()` helpers
- Attach `UnitCombatState` to `UnitInstance`
- `DroidProperties` struct on `UnitDefinition` — typed fields for all droid gameplay data (energy, armour, weapon, etc.)
- Combat state auto-initialised in `UnitManager::createInstance()`

### Files

- `shared/units/combat_state.h` — struct, constants, and free function declarations
- `shared/units/combat_state.cpp` — implementation
- `tests/combat_state_test.cpp` — unit tests (10 cases)

### Implementation details

**Property value types from JSON:**
- `energy` is parsed as `int` variant (values 0–9 across the 24 droid classes)
- `armour` is parsed as `float` variant (values 20.0–65.0+)

**Health scaling:**
- `maxHealth = max(MIN_HEALTH, energy * HEALTH_PER_ENERGY)` where `HEALTH_PER_ENERGY = 100.0` and `MIN_HEALTH = 10.0`
- Class 0 (energy=0) → 10 HP, Class 1 (energy=1) → 100 HP, Class 15 (energy=6) → 600 HP

**Damage model:**
- Armour is a percentage damage reduction (0–100), clamped on init
- `effectiveDamage = rawDamage * (1.0 - armour / 100.0)`
- Damage on dead units is a no-op; negative damage is a no-op
- Health clamped to 0 on overkill

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `DamageModel.BasicDamage` | Raw damage exceeding health kills unit |
| `DamageModel.ArmourReduction` | Armour absorbs/reduces damage correctly |
| `DamageModel.ZeroDamage` | No-op when damage is zero |
| `DamageModel.OverkillClamps` | Health does not go negative |
| `CombatState.InitFromProperties` | Correct extraction from unit JSON properties (energy=3 → 300 HP, armour=40.0) |
| `CombatState.InitFromPropertiesZeroEnergy` | Class 0 gets MIN_HEALTH (10 HP) |
| `CombatState.InitFromPropertiesMissingKeys` | Missing properties default safely |
| `CombatState.DestroyKillsUnit` | `destroy()` sets health to 0 and alive to false |
| `CombatState.DamageOnDeadUnitIsNoop` | No state change when applying damage to dead unit |
| `CombatState.ArmourClampedTo100` | Armour > 100 clamped; 100% armour blocks all damage |

### Gate

All 10 damage model tests pass. Units can be killed programmatically. ✓

---

## Stage 2 — Weapon Definitions & Projectile Logic ✓ COMPLETE

**Goal:** Weapons are defined per-unit. Projectiles can be spawned and resolved against targets — all testable without rendering.

### What's added

- `WeaponDefinition` struct: id, name, damage, speed, fireRate, maxRange, optimumRange, type, damageType, twin
- `WeaponType` enum: `Projectile`, `Beam`, `Instant`, `Area`
- `DamageType` enum: `Plasma`, `Flame`, `Cutter`, `Laser`, `Projectile`, `Disruptor`, `Impact`
- `WeaponState` struct: definition + cooldown timer
- Weapon definitions loaded from `assets/data/weapons.json` (9 weapons, derived from original `weapons.txt`)
- `loadWeaponsFromFile(path)` / `loadWeaponsFromJson(string)` — JSON-driven weapon table
- `ProjectileManager` — spawns projectiles as Box2D bodies, uses contact events for hit detection
- `BodyUserData` / `BodyTag` — tags on all Box2D bodies for contact event identification (see [unit_system.md](unit_system.md#body-user-data))
- Collision filtering via category bits — projectiles collide with units and walls, skip owner via shared negative `groupIndex` (see [unit_system.md](unit_system.md#collision-filtering))
- Droid JSON `weapon` property corrected from original data (was mapped to wrong column — see notes below)

### Files

- `shared/units/weapon.h` / `weapon.cpp` — weapon definitions, JSON loading, fire/cooldown logic
- `shared/combat/projectile_manager.h` / `projectile_manager.cpp` — Box2D projectile bodies, contact event processing
- `shared/physics/body_user_data.h` — `BodyTag` enum, `BodyUserData` struct, collision category constants
- `assets/data/weapons.json` — weapon data (converted from original `weapons.txt`)
- `tests/weapon_test.cpp` — unit tests (10 cases, projectile tests use Box2D world)

### Implementation details

**Data source:** Original weapon definitions from `uber/uberdroid/data/weapons.txt`, parsed by `uber/source/uberdroid/weapon.h/cpp`. See [Original Data Reference](#original-data-reference) below for format details.

**Conversion from original units to world units:**
- `fireRate`: milliseconds → seconds (÷ 1000)
- `speed`: original game units → world units/second (÷ 20)
- `maxRange` / `optimumRange`: original game units → world units (÷ 20)
- `damage`: kept as-is (compatible with Stage 1 health scaling)

**Scale factor rationale:** The ÷20 factor was derived from comparing original coordinate ranges (speeds 250–550, ranges 150–500) against the game world where visibility is ~30 world units and player terminal velocity is ~12.5 world units/s. This produces projectile speeds of 12–28 u/s and ranges of 7–25 u, which feel appropriate for top-down combat at this scale.

**Weapon table (9 weapons from `weapons.txt`):**

| ID | Name | Damage | Speed | Rate | Range | Type | DmgType |
|----|------|--------|-------|------|-------|------|---------|
| 0 | Plasma Bolt | 11.0 | 17.5 | 0.8s | 20.0 | projectile | plasma |
| 1 | Gas Axe | 3.5 | 12.5 | 1.2s | 7.5 | beam | cutter |
| 2 | Laser Rifle | 20.0 | 20.0 | 0.6s | 20.0 | projectile | laser |
| 3 | Plasma Cannon | 33.0 | 12.5 | 1.1s | 20.0 | projectile | plasma |
| 4 | Rapid Laser | 16.0 | 20.0 | 0.45s | 25.0 | projectile | laser |
| 5 | Plasma Torch | 6.0 | 27.5 | 0.15s | 9.0 | projectile | plasma |
| 6 | Disruptor | 40.0 | 0.0 | 1.7s | 25.0 | area | disruptor |
| 7 | Twin Particle Cannon | 22.0 | 16.0 | 1.1s | 20.0 | projectile | plasma |
| 8 | Exterminator | 6.0 | 12.5 | 1.2s | 8.5 | beam | cutter |

**Droid weapon ID fix:** The original JSON conversion (`droid_tool`) had a column mapping error — the `weapon` property was populated with the original `armour` field value, not the `weapon` field. This was fixed by re-reading `droidclasses.txt` line 2 (`weapon pulses`) for each class. Many low-rank droids (classes 1–8, 10–13) have weapon=-1 (unarmed). See the property mapping table in [Original Data Reference](#original-data-reference) for the full field layout.

**Weapon type notes:**
- `Projectile` weapons (IDs 0, 2, 3, 4, 5, 7) work with the current `ProjectileManager`
- `Beam` weapons (IDs 1, 8) will need a different damage model (continuous, not discrete hits)
- `Area` weapons (ID 6, Disruptor) deal instant damage in a radius with no projectile travel
- Only `Projectile` type is fully implemented in Stage 2; other types are defined in data for future use

**Projectile physics integration:** Projectiles are Box2D bodies (`isBullet = true` for CCD). Hit detection uses `b2World_GetContactEvents()` — no separate target list is maintained. Each Box2D body carries a `BodyUserData` struct identifying what it is (unit, projectile, debris, static wall). Contact event processing reads this to apply damage directly to the hit unit's `combatState`. Range limiting uses lifetime (`remainingLifetime = maxRange / speed`) instead of distance tracking. Tests create a lightweight `b2World` (zero gravity) and step the simulation with controlled dt values. See [projectile_system.md](projectile_system.md) for full design details.

### GoogleTest coverage (10 tests)

| Test | Validates |
|------|-----------|
| `WeaponTestFixture.CooldownPreventsRapidFire` | Cannot fire faster than fire rate |
| `WeaponTestFixture.DamageScalesWithWeaponStat` | Original damage values verified (3.5, 20.0, 33.0) |
| `WeaponTestFixture.NoWeaponForNegativeId` | weapon=-1 produces no weapon; tryFire returns false |
| `WeaponTestFixture.WeaponTypesParsed` | WeaponType/DamageType enums, twin flag parsed correctly |
| `WeaponTestFixture.AllWeaponsLoaded` | All 9 weapons loaded from JSON |
| `ProjectileTestFixture.MovesAlongHeading` | Box2D body position matches expected after world step |
| `ProjectileTestFixture.ExpiresAtLifetime` | Projectile deactivated after lifetime expires |
| `ProjectileTestFixture.HitsTarget` | Contact event triggers damage via body user data |
| `ProjectileTestFixture.MissesDistantTarget` | No contact event when projectile misses |
| `ProjectileTestFixture.IgnoresOwner` | Shared negative groupIndex prevents self-collision |

### Visual integration

Render projectiles as simple shapes (cylinder or small model). Player fires with mouse button.

### Gate

All 10 tests pass. Weapons loaded from data file. Droid weapon assignments match original data. ✓

---

## Stage 3 — Enemy Spawning & Level Population ✓ COMPLETE

**Goal:** Levels spawn enemy units at waypoints using level-specific spawn definitions.

### What's added

- `LevelSpawnDef` struct: per-level spawn profile (9 type counts) + placed droids list
- `buildTypeClassMap()` — derives type-to-class mapping from loaded `DroidProperties` (type = `typeCode / 100`)
- `loadShipSpawns()` — loads per-ship spawn data from `assets/ships/<shipN>/spawns.json`
- `resolveSpawns()` — resolves type profiles to concrete class IDs with random selection within type groups, assigns distinct waypoints, avoids player waypoint, handles insufficient waypoints gracefully
- `getSpawnDef()` — access spawn definitions by ship/level index
- Per-ship `spawns.json` files — spawn data for each ship's 16 levels, converted from original `PROFILE` data in mapfiles
- Placed droid support (e.g., Ship 1 Level 8 has Command Cyborg at waypoint 7)

### Data extraction from original

**Source:** `PROFILE` keyword in `paradomain::loadTokens()` (parsed from mapfile footer after geometry data). Each profile is 9 integers = droid count per type group (type 1 at index 0, type 9 at index 8). The `droidClasses::getRandomClass(type)` method picks a random class within each type group at spawn time.

**Type → class mapping** (derived from `droidClasses::load()` which builds `m_ClassBase`/`m_ClassProfile` arrays from the `type` field in `droidclasses.txt`):

| Type | Classes | Example units |
|------|---------|---------------|
| 1 | 1, 2 | Cleaning Robot, Trash Compactor |
| 2 | 3, 4, 5 | Service Robots |
| 3 | 6, 7 | Messengers |
| 4 | 8, 9, 10 | Maintenance |
| 5 | 11, 12, 13 | Crew |
| 6 | 14, 15, 16 | Low Security |
| 7 | 17, 18, 19 | Battle |
| 8 | 20, 21, 22 | High Security |
| 9 | 23 | Command Cyborg |

**Cross-ship differences:** Ships 2 and 3 have identical profiles except level 8 moves the Command Cyborg from a `PLACEDROID` entry (ship 1) into the type profile (ships 2, 3 have `profile[8]=1`).

**Level names** (from `levels.txt`): Maintenance, Engineering, Robostores, Quarters, Repairs, Staterooms, Stores, Research, Bridge, Observation, Airlock, Reactor, Upper Cargo, Mid Cargo, Vehicle Hold, Shuttle Bay.

### Files

- `shared/level/spawn_config.h` / `spawn_config.cpp` — spawn definitions, JSON loading, spawn resolution
- `assets/data/level_spawns.json` — spawn data (3 ships, 16 levels each, converted from original mapfiles)
- `tests/spawn_test.cpp` — unit tests (4 cases)

### GoogleTest coverage (4 tests)

| Test | Validates |
|------|-----------|
| `SpawnFixture.RespectsUnitCounts` | Correct total count and per-type distribution; placed droid included |
| `SpawnFixture.UsesDistinctWaypoints` | No two units share a waypoint when waypoints are sufficient |
| `SpawnFixture.AvoidsPlayerWaypoint` | Player's starting waypoint is excluded from spawn assignment |
| `SpawnFixture.HandlesInsufficientWaypoints` | All droids still spawned; waypoints reused; indices valid |

### Visual integration

Enter a level and see enemy units standing at waypoints. They do not move yet.

### Gate

All 4 spawn tests pass. Levels are populated with correct unit types and counts. ✓

---

## Stage 4 — AI: Movement & Aggression ✓ COMPLETE

**Goal:** All enemy units patrol between waypoints. Armed units that detect or are attacked by the player become hostile and pursue via waypoints, firing when in range.

**Design notes:** Follow the [Design Patterns](#design-patterns) established in earlier stages. AI operates directly on `UnitInstance` references from the unit manager — no separate "AI entity" list. Detection uses `Vector2Distance()` between unit positions read from Box2D bodies. Movement drives each unit's `b2MotorJoint` target via `unit_set_move_target()` — identical to player control (see [Design Pattern 4](#4-player-and-ai-are-simulated-identically-remote-control-model)). Attack state triggers weapon fire through `ProjectileManager::spawn()` using the unit's `b2WorldId`. AI tests create a Box2D world and step the simulation. All droid movement is constrained to waypoint paths — droids never move off the waypoint graph.

### Waypoint patrol system

All droids begin in a **Patrol** state. Patrol uses the waypoint link graph defined in the TMX level files.

**TMX waypoint data available at runtime:**
- `TmxWaypoint` struct: `id`, `x`, `y` (pixel coords), `links` (vector of connected waypoint IDs)
- Stored on `TmxLevel::waypoints` — parsed from `<objectgroup name="waypoints">`
- `LevelRenderData::waypointPositions` — pre-converted to 3D world coordinates (via `tmxPixelToWorld()`)
- `LevelRenderData::waypointLinks` — deduplicated index pairs for visualization
- Each waypoint has 1–4 `link-N` properties referencing other waypoint IDs, forming a bidirectional navigation graph

**Patrol behaviour:**
1. Each droid is assigned a **current waypoint** (its spawn waypoint from Stage 3) and a **target waypoint** (initially null)
2. When idle at a waypoint (no target), choose a random linked waypoint as the next destination
3. Apply a **back-avoidance bias**: the waypoint the droid just arrived from has reduced selection probability (e.g. 0.2× weight vs 1.0× for other links), unless it is the only option — this prevents droids ping-ponging on two-waypoint corridors
4. Move toward the target waypoint by setting the unit's motor-joint target to a bounded "carrot" ahead along the heading via `unit_set_move_target()` (same control path as player movement — see Design Pattern 4)
5. On arrival (within a threshold distance of the target), select the next waypoint. If the next waypoint is approximately colinear with the arrival direction (i.e. the droid would continue in roughly the same heading), skip the dwell pause and continue moving immediately. Otherwise, pause briefly (a random dwell in the unit's per-type range) before heading to the next waypoint. Colinearity is tested by the dot product of the arrival direction and the new heading — above a threshold (e.g. 0.7) counts as colinear. The dwell range is derived from `typeCode` at init (`dwellRangeForType`, cached on the `AIComponent`): the default is `[AI_DWELL_MIN, AI_DWELL_MAX]` = 0.5–1.2s, and typeCode 300–399 uses 0/0 (never pauses — those types run straight through waypoints)
6. The AI needs an index-based adjacency list built from `TmxWaypoint::links` at level load time, mapping each waypoint index to its connected waypoint indices (similar to the ID→index resolution already done in `level_renderer.cpp` lines 444–461)

### Aggression and hostility

Aggression is predicated on the **presence of a weapon** (weapon ≥ 0), not brain type. Unarmed droids never become hostile — they flee if damaged.

**Transitions to hostile:**
- Armed droids become hostile when they can **see** the player: within `proximityRadius`, a clear line of sight (`pathClear` including closed doors), and — for head units — inside the head vision cone. Proximity alone is not enough; a player behind a wall/closed door is not detected.
- Any droid that takes damage becomes hostile (armed → Chase, unarmed → Flee), regardless of sight.

**Disengagement (lose-sight timeout):**
- A chasing droid tracks how long it has gone **without sight** of the player (`loseSightTimer`) — out of visual range (`visualRadius`), line of sight broken around a corner or behind a closed door, or (head units) the player left the vision cone. Seeing the player resets the timer, so brief occlusion doesn't drop the pursuit.
- After `AI_LOSE_SIGHT_TIME` (2 s) without sight it gives up: reverts to Patrol, clears hostility, and resumes random waypoint patrol from its current position. Because detection also requires line of sight, it won't immediately re-detect a player still standing behind the wall it lost them behind.

### Collision response (non-hostile only)

Keeps wandering droids from jamming against each other or walls, adapted from the original uberdroid `collide()`/`blocked()`. Handled in `AIManager` (`onCollision`/`processCollisions`, called from the game loop after `b2World_Step` using Box2D contact events; projectile contacts are ignored — those are damage). Only **non-hostile** units react — a chasing/fleeing unit pushes straight through.

**Reverse course, don't stop.** On collision a unit *reverses along the edge it is on* — it turns around and heads back to the waypoint it came from (`targetWaypoint = currentWaypoint`), retracing a link it was already following, so the straight line back is a valid path. It then treats the node it was blocked reaching as its `previousWaypoint`, so back-avoidance biases the next hop away from it. Only if it has no current waypoint to fall back to (knocked off the graph) does it drop the target and reselect. This replaced the earlier "head to `previousWaypoint`" rule, which aimed at a node *off* the current edge and so could route the unit straight through a wall. It keeps moving the whole time. A short decision cooldown (`AI_COLLIDE_COOLDOWN`) debounces the reversal so a sustained contact doesn't re-decide every frame.

**Fine arrival tolerance.** `AI_WAYPOINT_ARRIVAL_DIST` is small (0.15) so a unit reaches close to a waypoint's centre before turning toward the next. The map is tile-based with narrow doorways; the old coarse tolerance (0.5) let units turn half a tile early, cutting angled paths that clipped solid tiles. Reaching centre keeps trajectories aligned to the grid-laid waypoint links.

**Stuck detection + off-course recovery.** A wall contact emits a Box2D begin-touch event only *once*, so it can't keep nudging a unit that stays pinned — such a unit would "just stop". Instead, `updatePatrol` watches speed: if a unit is trying to move toward its target but stays below `AI_STUCK_SPEED` for `AI_STUCK_TIME`, it recovers. *How* it recovers depends on a circle cast (`pathClear`) from the unit to its target:

- **Path wall-clear** (blocked by another unit / a deadlock): abandon the route, set it as `previousWaypoint`, drop the target and reselect — back-avoidance steers elsewhere.
- **Path wall-blocked** (the unit has been knocked off the graph, so the straight line to its target now crosses a wall): re-acquire the graph by heading to the **nearest reachable waypoint** (`nearestReachableWaypoint` — nearest node whose path is itself wall-clear; ported from the original `domain::nearestWaypoint`).

`pathClear` uses `b2World_CastCircle` with a spherical width equal to the unit's collision radius, filtered to `CATEGORY_STATIC` so **other units are not treated as blockers**. The timer resets whenever the target changes. This is the general recovery for wall-pinning, off-course displacement, and mutual deadlocks alike.

**Wall sliding.** Unit shapes are near-frictionless (`UNIT_CONTACT_FRICTION ≈ 0`). The contact solver truncates the into-wall velocity component; with almost no friction the motor's tangential component carries the unit *along* the wall instead of pinning. This is the first line of defence when a straight path to a waypoint grazes an obstacle — the unit slides around it — with stuck-detection/re-acquire as the fallback for genuine dead-ends.

> Known limitations (flagged in `ai_manager.cpp`, not yet solved):
> - **Straight-line reachability.** `pathClear` is a single line-of-sight cast, so interior geometry (pillars, tables, stub walls) can make a trivially-reachable waypoint fail the cast when the direct line clips it. The real fix is routing over the waypoint graph (BFS/A* on `adjacency_`) or intermediate steering points; wall sliding mitigates most cases meanwhile.
> - **Door tiles** have their own `CATEGORY_DOOR` and an `includeDoors` flag on `pathClear`: pathfinding casts exclude doors (a unit doesn't reroute around a door that opens on proximity), while firing/sight casts include them (a closed door blocks the shot and the sightline; an open door clears its filter and doesn't).
> - **Unit ranking.** Casts exclude other units outright rather than ranking/soft-avoiding paths that pass near them.

> Earlier iterations "stunned" the unit (held position on contact, retreated only after a frustration counter). That was removed: in a cluster, contacts with several different obstacles refreshed the stun every frame and reset the counter, locking units permanently. The rule of thumb is *stop moving toward the collision, not stop moving altogether*.

**Debug overlay (`V`).** Draws the waypoint graph (nodes + links) and a line from each AI unit to its intended target waypoint, plus a per-unit label showing state (`P`/`C`/`F`), target waypoint index, and `R` while a collision-redirect cooldown is active. See `game_draw_ai_debug_3d`/`_2d`.

### Waypoint-based chase

Hostile armed droids do **not** leave the waypoint graph. Chase behaviour uses waypoint selection biased toward the player rather than arbitrary movement:

- Instead of choosing a random linked waypoint, choose the linked waypoint that is **closest to the player's current position** (greedy waypoint pursuit)
- This keeps all movement on established paths and avoids needing return-to-patrol logic
- Droids continue moving along waypoint paths while hostile; firing is independent of waypoint arrival (see Aiming and firing below)

### Aiming and firing

Droids can fire **at any point** during movement — firing is not gated on reaching a waypoint. The conditions are range, cooldown, and (for most weapons) facing.

**Orientation rules (non-combat):**
- All droids orient their body to face their direction of movement along waypoint paths

**Orientation rules (combat — hostile state):**
- **Standard droids** (no turret, not omnidirectional): turn their entire body to face the player. Since their facing is tied to movement direction, they must **stop moving to turn and fire**. When within weapon `optimumRange` of the player, they halt in place, turn to aim, and fire. They resume waypoint movement after firing or if the player moves out of range
- **Turret droids** (`hasTurret` flag): the head section turns independently to aim the weapon at the player while the body continues to face the movement direction. They do **not** stop to fire — they keep moving along waypoints while the head tracks the player
- **Omnidirectional droids** (`OMNIDIRECTIONAL` flag): their body facing is decoupled from movement direction. They turn their body to face the player while continuing to move. They do **not** stop to fire. Their head also faces the player
- All hostile droids turn their **head** to face the player. For non-turret droids this means the whole body turns (head follows body). For turret droids only the head turns independently

**Weapon section identification:**
- The weapon is mounted on either the body or the head, depending on the droid type
- Turret droids fire from the head section (the turret); all others fire from the body
- `FIREOFFSET` property (from droidclasses.txt) gives the projectile spawn offset relative to the firing section

**Firing conditions:**
- Player is within weapon `maxRange`
- Weapon cooldown has elapsed
- The firing section (body or head) is facing the player within an angular threshold
- **Exception — Disruptor** (weapon type `Area`): area-effect weapon that damages everything within range with line of sight. No facing condition required — fires in all directions simultaneously

**Movement consequences of firing:**
- Standard droids halt within optimum range to turn and fire → they stay at approximately optimum distance
- Turret and omnidirectional droids keep moving → they may close to less than optimum range since they don't stop to engage

### What's added

- `AIState` enum: `Patrol`, `Chase`, `Flee`
- `AIComponent` per enemy unit: state, current waypoint index, target waypoint index, previous waypoint index (for back-avoidance), dwell timer, hostile flag, detection radius (from `proximityRadius`), visual range (from `visualRadius`)
- Waypoint adjacency list: built at level load from `TmxWaypoint::links`, stored as `vector<vector<int>>` indexed by waypoint index
- `ai_update(dt)` state machine:
  - **Patrol:** move between linked waypoints with back-avoidance bias and dwell pauses; all droids start here
  - **Chase:** armed hostile droids select waypoints biased toward player; standard droids halt to fire when in optimum range; turret/omnidirectional droids fire while moving; disengage if player leaves visual range
  - **Flee:** unarmed droids that take damage select waypoints biased away from player
- Aiming system: head orientation (all hostile droids face head toward player), body orientation (standard + omnidirectional droids face body toward player; turret droids face body along movement)
- Apply forces to enemy physics bodies (same force model as player)

### Files

- `shared/ai/ai_component.h` / `ai_component.cpp` — AIState, AIComponent, state machine, waypoint selection, aiming logic
- `shared/ai/ai_manager.h` / `ai_manager.cpp` — updates all AI components, builds waypoint adjacency list
- `tests/ai_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `AI.PatrolSelectsLinkedWaypoint` | Target waypoint is always a linked neighbour of the current waypoint |
| `AI.PatrolBackAvoidanceBias` | Previous waypoint is chosen less frequently (statistical test over many selections) |
| `AI.PatrolDwellAtWaypoint` | Droid pauses at waypoint before selecting next target |
| `AI.ArmedDroidBecomesHostileOnDetection` | Armed droid transitions Patrol → Chase when player enters detection radius |
| `AI.UnarmedDroidIgnoresPlayer` | Unarmed droid continues patrol when player is nearby |
| `AI.DamageTriggerHostileArmed` | Armed droid taking damage transitions to Chase |
| `AI.DamageTriggerFleeUnarmed` | Unarmed droid taking damage transitions to Flee |
| `AI.ChaseSelectsWaypointCloserToPlayer` | Hostile droid picks linked waypoint nearest to player position |
| `AI.StandardDroidHaltsToFire` | Non-turret, non-omnidirectional droid stops in place when within optimum range and facing player |
| `AI.TurretDroidFiresWhileMoving` | Turret droid continues moving between waypoints while head tracks and fires |
| `AI.OmnidirectionalFiresWhileMoving` | Omnidirectional droid continues moving while body faces player and fires |
| `AI.DisruptorIgnoresFacing` | Area-effect weapon fires without facing condition |
| `AI.DisengageOnVisualRangeLost` | Hostile droid returns to Patrol when player exceeds visual range |
| `AI.FleeSelectsWaypointAwayFromPlayer` | Fleeing droid picks linked waypoint farthest from player |

### Gate

All AI state tests pass. Enemies visibly patrol waypoints, armed droids chase via waypoints and fire, turret/omnidirectional droids fire on the move.

---

## Stage 5 — Unit Capture Mechanic

**Goal:** The core GAME.md mechanic — player (type 0) can capture enemy units.

### What's added

- `CaptureSystem` — manages capture state:
  - Capture trigger (proximity + key press, or zero-health takeover)
  - `capturedUnit` pointer on player state
  - One-capture-at-a-time rule: capturing another destroys the current capture
- Rendering: type 0 model rendered at +Y offset above captured unit's root section
- Weapon delegation: if captured unit has weapons, use those; otherwise use type 0 weapons
- Physics: player now drives the captured unit's physics body
- Captured unit's combat state replaces player's for damage purposes

### Files

- `shared/units/capture_system.h` / `capture_system.cpp` — capture logic
- `src/game.cpp` — integrate capture into update/render
- `tests/capture_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `Capture.PlayerCanCapture` | Unit transitions to captured state |
| `Capture.OnlyOneCaptured` | Second capture destroys first |
| `Capture.WeaponDelegation` | Correct weapon source selected |
| `Capture.PlayerRendersAbove` | Type 0 offset calculation correct |
| `Capture.CapturedUnitTakesDamage` | Damage hits captured unit, not type 0 |

### Gate

All capture tests pass. Player can capture an enemy, ride it, use its weapons, and switch captures.

---

## Stage 6 — Capture Decay & Unit Destruction Loop

**Goal:** Captured units degrade over time. Destruction returns player to type 0.

### What's added

- `capture_decay_update(dt)` — captured unit loses health at a configurable rate
- When captured unit health reaches zero:
  - Dismantle captured unit (debris system already exists)
  - Return player to type 0 control
  - Restore type 0 physics body
- Unit destruction VFX: trigger debris scatter (existing dismantle system)
- Death of player type 0 = game over (or respawn, configurable)

### Files

- `shared/units/capture_system.cpp` — extend with decay logic
- `tests/capture_decay_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `Decay.HealthDecrementsOverTime` | Linear decay rate applied correctly |
| `Decay.DestructionReturnToType0` | Player state correctly restored |
| `Decay.DebrisCreated` | Dismantled sections become debris objects |
| `Decay.Type0SurvivesCaptureLoss` | Type 0 health is independent |

### Gate

All decay tests pass. Captured unit visibly degrades and eventually shatters; player resumes as type 0.

---

## Stage 7 — Level Navigation (Lifts)

**Goal:** Player can use lift tiles to move between the 16 ship levels.

### What's added

- `LiftDefinition` — links lift tile positions across levels
- Lift tile detection (player standing on lift tile + activation key)
- Level transition: unload current level render/collision, load target level
- Player spawn at corresponding lift tile in target level
- Retain captured unit across level transitions
- `currentLevel` tracking already exists in Game struct

### Files

- `shared/level/lift_system.h` / `lift_system.cpp` — lift definitions and transitions
- `src/game.cpp` — integrate lift activation and level switching
- `tests/lift_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `Lift.DefinitionLinksLevels` | Lift connects correct source/target levels |
| `Lift.PlayerTransfersPosition` | Spawn at target lift tile position |
| `Lift.CapturedUnitRetained` | Capture survives level transition |
| `Lift.InvalidLiftHandled` | Graceful handling of broken lift links |

### Gate

All lift tests pass. Player can activate a lift and appear on another level with enemies spawned.

---

## Stage 8 — Level & Ship Completion

**Goal:** Clearing all enemies completes a level; clearing all levels completes the ship.

### What's added

- `LevelState` tracking: enemy count remaining per level
- Level completion trigger: all enemies destroyed or captured -> dim level lights
- Ship completion trigger: all 16 levels cleared -> transition to next ship
- Light dimming effect (reduce ambient + directional light intensity for cleared levels)
- Ship loading: load next ship data (or cycle/end screen)
- Captured unit transfer to next ship (per GAME.md)

### Files

- `src/game_state.h` / `game_state.cpp` — level/ship completion tracking
- `src/game.cpp` — integrate completion checks and transitions
- `tests/completion_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `Completion.LevelClearedWhenNoEnemies` | Detection logic for level clear |
| `Completion.CapturedCountsAsCleared` | Captured enemies count as cleared |
| `Completion.ShipClearedWhenAllLevels` | Ship-level aggregation works |
| `Completion.LightsDimOnClear` | Ambient values change on completion |
| `Completion.CapturedUnitTransfersToNewShip` | Capture survives ship transition |

### Gate

All completion tests pass. Clearing a level visibly dims the lights. Clearing all levels triggers ship transition.

---

## Stage 9 — Player-Origin Lighting

**Goal:** Per GAME.md — lighting effects originate from the player unit, not the camera.

### What's added

- Dynamic point light attached to player position
- Directional light `target` follows player (simulates directional visibility)
- Shader `effectiveEyeHeight` already exists — repurpose to player unit height
- Fog-of-war effect: areas far from player are darker (distance attenuation)
- Light intensity varies with player unit type (some units have better sensors)

### Files

- `src/game.cpp` — update light positions each frame
- `assets/shaders/lighting.fs` — attenuation additions if needed
- `tests/lighting_test.cpp` — unit tests

### GoogleTest coverage

| Test | Validates |
|------|-----------|
| `Lighting.PointLightFollowsPlayer` | Light position matches player unit |
| `Lighting.AttenuationWithDistance` | Intensity drops with range |

### Gate

Lighting visibly emanates from the player. Moving away from an area darkens it.

---

## Stage Dependency Graph

```
Stage 1 (Combat Stats)
  |         \
  v          v
Stage 2    Stage 3        Stage 9 (Lighting)
(Weapons)  (Spawning)     [independent, parallel]
  |         /
  v        v
Stage 4 (AI)
  |
  v
Stage 5 (Capture)
  |
  v
Stage 6 (Decay)         Stage 7 (Lifts)
  |                      [requires Stage 3]
  |         /
  v        v
Stage 8 (Completion)
```

## Summary Table

| Stage | Feature | Tests | Depends On |
|-------|---------|-------|------------|
| 1 | Combat stats & damage model | 5 | — |
| 2 | Weapons & projectiles | 7 | Stage 1 |
| 3 | Enemy spawning | 4 | Stage 1 |
| 4 | AI behaviour | 6 | Stages 2, 3 |
| 5 | Unit capture | 5 | Stage 4 |
| 6 | Capture decay & destruction | 4 | Stage 5 |
| 7 | Level navigation (lifts) | 4 | Stage 3 |
| 8 | Level & ship completion | 5 | Stages 4, 7 |
| 9 | Player-origin lighting | 2 | — (parallel) |

Stages 1-3 can partially overlap. Stage 9 is independent and can run in parallel with any stage. Each stage produces a runnable, testable build with visible progress.

---

## Original Data Reference

This section documents how gameplay data was extracted from the original Uberdroid source code and data files. It serves as a reference for future stages that will need to pull additional data from the original project.

### Source locations

| Purpose | Source code | Data file |
|---------|------------|-----------|
| Droid class definitions | `uber/source/uberdroid/droid_class.h/cpp` | `uber/uberdroid/data/droidclasses.txt` |
| Weapon definitions | `uber/source/uberdroid/weapon.h/cpp` | `uber/uberdroid/data/weapons.txt` |
| Level/ship layout | `uber/source/uberdroid/area.h/cpp` | `uber/uberdroid/ship1/xmapfile*.txt` |
| Tile geometry | `uber/source/uberdroid/archetile.h/cpp` | `uber/uberdroid/data/tiles.txt` |
| Path geometry (floors) | `uber/source/uberdroid/pathgeometry.h/cpp` | XML files in ship directories |
| Render objects (models) | `uber/source/uberdroid/renderobjects.h/cpp` | `uber/uberdroid/data/asc/` (MilkShape ASCII) |

### droidclasses.txt format

Each class entry starts with `Class N` followed by structured data lines. The `droid_class::load()` function in `droid_class.cpp` parses them:

```
Class <N>
<render_index> <number> <type> <energy> <armour>
<weapon> <pulses>
<speed> <acceleration> <deceleration> <scan_rate>
<visual> <aural> <ultrasonic> <subsonic> <infrared> <ultraviolet> <radar> <disruptor_shielded>
<drain_rate> <collide_radius> <proximity_radius> <aggression>
vrad <visual_radius>
head <head_index>
[optional keyword lines...]
END
```

**Field types from `droid_class.h`:**

| Field | C type | Description |
|-------|--------|-------------|
| `render_index` | `int` | Index into render object table |
| `number` | `int` | Type code (e.g. 101, 476, 999) — the visible unit number |
| `type` | `char` | Classification tier (0–9) — higher = more advanced |
| `energy` | `float` | Power/health rating (20.0–100.0) |
| `armour` | `float` | Damage resistance (0.0–7.0) |
| `weapon` | `char` | Index into weapons table (-1 = unarmed, 0–8 = weapon ID) |
| `pulses` | `char` | Sensor pulse count |
| `speed` | `float` | Movement speed (original game units) |
| `acceleration` | `float` | Acceleration rate |
| `deceleration` | `float` | Deceleration rate |
| `drain_rate` | `float` | Energy drain multiplier (for capture decay) |
| `collide_radius` | `float` | Collision shape radius (original game units) |
| `proximity_radius` | `float` | Detection radius for AI sensing |
| `aggression` | `float` | AI aggression factor |
| `visual_radius` | `float` | Visual detection range |

**Optional keyword lines** (parsed after the fixed fields, before END):

| Keyword | Data | Description |
|---------|------|-------------|
| `TURRET` | (none) | Unit has a turret that rotates independently |
| `OMNIDIRECTIONAL` | (none) | Can face any direction while moving |
| `FIREOFFSET` | `x y z` | Projectile spawn offset from unit centre |
| `HEADOFFSET` | `x y z` | Head/turret position offset |
| `ROTATIONRATE` | `float` | Body rotation speed (rad/ms) |
| `HEADROTATIONRATE` | `float` | Turret rotation speed (rad/ms) |
| `SOUND` | `sample rate looped volume constant` | Normal movement sound |
| `ULTRASONIC` | `sample rate looped volume constant` | Ultrasonic sensor sound |
| `SUBSONIC` | `sample rate looped volume constant` | Subsonic sensor sound |
| `SENSOR` | `type` | Additional sensor type (visual/aural/ultrasonic/subsonic/infrared/ultraviolet/radar) |
| `SPECIALSAMPLE` | `int` | Sound for special action (e.g. cleaning) |
| `SPECIALRATE` | `float` | Rate of special action |
| `SPECIALSAMPLERANGE` | `int` | Random range for special sound |
| `DESCRIPTION` | `linenum text` | Multi-line description |
| `DROIDTYPE` | `int` | Library classification (0=device, 1=robot, 2=droid, 3=cyborg) |
| `DRIVETYPE` | `int` | Drive system (0=wheels, 1=antigrav, 2=bipedal, 3=tracks, 4=hover, 5=tank) |
| `BRAINTYPE` | `int` | AI class (0=none, 1=simple, 2=standard, 3=primode) |
| `DRIPTHRESHOLD` | `float` | Damage level that triggers fluid drip effect |
| `TALKTHRESHOLD` | `float` | Time between idle vocalisations |
| `TARGETRETICULE` | (none) | Shows targeting reticule when firing |
| `PERFORMCLEAN` | (none) | AI performs cleaning behaviour |
| `HEADHEIGHT` | `float` | Height for placing the influence device when captured |
| `SECTION` | block | Multi-section body definition (Render/Parent/Tag/Rotate/Offset/Rotation) |
| `HEADSECTION` | `int` | Which section index is the head |

### Property mapping: original → JSON

The `droid_tool` converted original droid class data to JSON unit definitions. The property names were remapped for gameplay clarity:

| JSON property | Original field | Original range | Notes |
|---------------|---------------|----------------|-------|
| `classId` | (array index) | 0–23 | Class index |
| `typeCode` | `number` | 101–999 | Visible unit type number |
| `energy` | `type` | 0–9 (int) | Tier classification → used as health scaling factor |
| `armour` | `energy` | 20.0–100.0 | Original power rating → repurposed as % damage reduction |
| `weapon` | `weapon` | -1–8 (int) | Weapon table index (-1 = unarmed) |
| `droidType` | `mDroidType` | 0–3 | Device/robot/droid/cyborg |
| `driveType` | `mDriveType` | 0–5 | Wheels/antigrav/bipedal/tracks/hover/tank |
| `brainType` | `mBrainType` | 0–3 | None/simple/standard/primode |
| `hasTurret` | `turret` | bool | Fires from rotating turret head |
| `description` | `mDescription` | string | Concatenated description lines |

**Important:** The `energy` and `armour` JSON property names do NOT correspond to the same-named fields in the original. The JSON `energy` is the original `type` (0–9 tier), and the JSON `armour` is the original `energy` (20–100 power level). This remapping was intentional — the original `type` field serves as a natural energy tier for health scaling, and the original `energy` field (which is the unit's overall power rating) works well as a percentage-based armour value.

**Bug found and fixed (Stage 2):** The original JSON conversion placed the original `armour` value (0.0–7.0) in the `weapon` field instead of the actual `weapon` index (-1 to 8). This was corrected by re-reading `droidclasses.txt` and extracting the correct weapon field from the second data line of each class.

### weapons.txt format

Each weapon entry starts with `weapon <id> <name>` and is terminated by `END`. The `weapon::load()` function in `weapon.cpp` parses them:

```
weapon <id> <name>
Gfx <index0> <index1> <index2> <index3> <index4> <rotates>
Colour <r> <g> <b>
Light <emits> <intensity>
Damage <damage> <lifetime> <speed> <damageType>
sample <fire_sample>
fizzle <fizzle_time>
rate <fire_rate>
max_range <max_range>
optimum_range <optimum_range>
splash <splash_index>
[TYPE <type>]
[MARKINDEX <index>]
[MARKRADIUS <radius>]
[TWIN]
END
```

**Field descriptions:**

| Field | Used in gameplay | Description |
|-------|:---:|-------------|
| `damage` | ✓ | Raw damage per hit |
| `speed` | ✓ | Projectile velocity (original game units) |
| `fire_rate` (as `rate`) | ✓ | Cooldown between shots (milliseconds) |
| `max_range` | ✓ | Maximum projectile travel distance |
| `optimum_range` | ✓ | AI preferred engagement distance |
| `lifetime` | — | Projectile duration (ms) — replaced by max_range in our system |
| `mType` (as `TYPE`) | ✓ | 0=projectile, 1=beam, 2=instant, 3=area |
| `mDamageType` | ✓ | plasma/flame/cutter/laser/projectile/disruptor |
| `mTwin` (as `TWIN`) | ✓ | Fires two projectiles |
| `Gfx` | — | Sprite/animation indices (rendering only) |
| `Colour` | — | Projectile colour (rendering only) |
| `Light` | — | Light emission (rendering only) |
| `sample` | — | Fire sound effect index |
| `fizzle` | — | Death animation duration |
| `splash` | — | Impact sprite index |
| `MARKINDEX/MARKRADIUS` | — | Ground scorch mark (rendering only) |

**Damage type string → enum mapping** (from `strToDamageType` in weapon.cpp):
`"plasma"` → Plasma, `"flame"` → Flame, `"cutter"` → Cutter, `"laser"` → Laser, `"projectile"` → Projectile, `"disruptor"` → Disruptor

**Original armour interaction** (from `droid_class.cpp` lines 37–43):
- All damage types use the full `armour` value as flat reduction, except:
- Disruptor damage ignores armour entirely (`mArmour[dt_disruptor] = 0.0`)
- Cutter damage gets 50% armour effectiveness (`mArmour[dt_cutter] = 0.5 * armour`)

Note: Our current damage model uses percentage-based armour reduction (Stage 1) rather than the original's per-damage-type flat reduction. The per-type armour system could be added as a refinement.

### Droid class → weapon assignment (complete table)

| Class | Type | Name | Weapon ID | Weapon Name |
|-------|------|------|-----------|-------------|
| 0 | 0 | Influence Device (101) | 0 | Plasma Bolt |
| 1 | 1 | Cleaning Robot (123) | -1 | (unarmed) |
| 2 | 1 | Trash Compactor (139) | -1 | (unarmed) |
| 3 | 2 | Service Robot (247) | -1 | (unarmed) |
| 4 | 2 | Basic Service (249) | -1 | (unarmed) |
| 5 | 2 | Serving Robot (296) | -1 | (unarmed) |
| 6 | 3 | Messenger (302) | -1 | (unarmed) |
| 7 | 3 | Messenger AG (329) | -1 | (unarmed) |
| 8 | 4 | Ext. Maintenance (420) | -1 | (unarmed) |
| 9 | 4 | Maintenance (476) | 1 | Gas Axe |
| 10 | 4 | Network Repairs (493) | -1 | (unarmed) |
| 11 | 5 | Basic Crew (516) | -1 | (unarmed) |
| 12 | 5 | Cargo Crew (571) | -1 | (unarmed) |
| 13 | 5 | Adv. Passenger (598) | -1 | (unarmed) |
| 14 | 6 | Low Security Sentinel (614) | 2 | Laser Rifle |
| 15 | 6 | Low Security Battle (615) | 5 | Plasma Torch |
| 16 | 6 | Simple Sentinel (629) | 3 | Plasma Cannon |
| 17 | 7 | General Battle (711) | 3 | Plasma Cannon |
| 18 | 7 | Scout Battle (742) | 4 | Rapid Laser |
| 19 | 7 | Heavy Battle (751) | 4 | Rapid Laser |
| 20 | 8 | High Security Patrol (821) | 7 | Twin Particle Cannon |
| 21 | 8 | Security Disruptor (834) | 6 | Disruptor |
| 22 | 8 | High Security Interior (883) | 8 | Exterminator |
| 23 | 9 | Command Cyborg (999) | 3 | Plasma Cannon |

### Data relevant to future stages

**Stage 3 (Spawning): ✓ COMPLETE** — Level population extracted from `PROFILE` keyword in `paradomain::loadTokens()`. Each mapfile's footer contains a 9-integer profile (droid count per type 1–9) plus optional `PLACEDROID` entries. Data converted to `assets/data/level_spawns.json` with type→class mapping from `droidClasses::load()`. The `Droids`/`Classes` keywords at mapfile headers are vestigial and unused.

**Stage 4 (AI):** Key fields from `droidclasses.txt` for AI behaviour:
- `brainType`: 0=none, 1=simple, 2=standard, 3=primode — determines AI complexity
- `aggression`: float — how aggressively the unit pursues targets
- `speed`, `acceleration`, `deceleration` — movement parameters (need scale conversion)
- `proximityRadius` — detection range (already in JSON, already scaled)
- `OMNIDIRECTIONAL` flag — can strafe/move in any direction
- `ROTATIONRATE` / `HEADROTATIONRATE` — turning speeds (rad/ms in original, need ÷1000 for rad/s)
- `drain_rate` — energy drain multiplier (relevant for Stage 6 capture decay)

**Stage 5 (Capture):** Key fields:
- `HEADHEIGHT` — where to place the influence device model above the captured unit (original game units, needs scale conversion)
- `type` (0–9) — determines capture difficulty / control duration

**Stage 6 (Decay):**
- `drain_rate` — controls how fast captured unit energy depletes

**Stage 7 (Lifts):** Lift positions are defined in the TMX level data, not in the droid class files. The original lift system is in `uber/source/uberdroid/` (search for lift-related code).

**Stage 9 (Lighting):** Sensor fields from `droidclasses.txt`:
- `visual`, `aural`, `ultrasonic`, `subsonic`, `infrared`, `ultraviolet`, `radar` — sensor capabilities
- `visual_radius` — base visibility range
- These could modulate light intensity/radius when controlling different unit types

### Scale conversion notes

The original game uses its own coordinate system. Observed scale relationships:

| Property | Original range | JSON/world range | Approx factor |
|----------|---------------|-------------------|---------------|
| `collide_radius` | 8–15 | 0.2–1.0 | varies (from GLTF bounds, not linear) |
| `proximity_radius` | 15–25 | 16–36 | ~1.0–1.4 (already converted in JSON) |
| weapon `speed` | 250–550 | 12.5–27.5 | ÷ 20 |
| weapon `max_range` | 150–500 | 7.5–25.0 | ÷ 20 |
| weapon `fire_rate` | 150–1700 ms | 0.15–1.7 s | ÷ 1000 |
| `ROTATIONRATE` | 0.002–0.008 rad/ms | — | × 1000 for rad/s |

The `SCALE_INCHES_TO_METERS` constant (0.0254) from the geometry conversion pipeline is distinct from the gameplay scale factor. Weapon/AI values use ÷20 for distances and ÷1000 for times.
