# Projectile System Refactor Plan

## Context

The Stage 2 projectile system (`shared/combat/projectile_manager.h/cpp`) was built as a standalone, testable implementation with manual simulation. It needs refactoring before game integration because:

1. **No physics integration** — projectiles are manually simulated (`pos += vel * dt`) in their own coordinate space and pass through walls, doors, and level geometry because they don't exist in the Box2D world
2. **Redundant target list** — `checkHits()` requires the caller to build a `std::vector<ProjectileTarget>` each frame, duplicating position/radius/combat data already held by the `UnitInstance` collection which is the authoritative source for all game objects
3. **Inconsistent types** — `Projectile` and `ProjectileTarget` use separate `float posX, posY, velX, velY` fields instead of raylib's `Vector2`, which provides `Vector2Normalize`, `Vector2Add`, `Vector2Scale`, `Vector2Length`, `Vector2Distance` etc. via `raymath.h`

### Intended outcome

Projectiles become Box2D bodies. They collide with walls and units naturally through the physics simulation. Hit detection uses Box2D contact events, which reference unit instances directly via body user data — no separate target list is maintained. The unit instance collection remains the single source of truth for all object positions and state.

---

## Current State

### Projectile system (`shared/combat/projectile_manager.h/cpp`)

The `Projectile` struct stores position and velocity as separate floats:

```cpp
struct Projectile {
    float posX, posY;           // Position
    float velX, velY;           // Velocity (world units/second)
    float damage;
    float maxRange;             // Max travel distance
    float distanceTravelled;    // Accumulated distance
    int32_t ownerId;            // Collision group ID of firing unit
    bool active;
};
```

`ProjectileTarget` is a lightweight struct for hit detection, built each frame from unit data:

```cpp
struct ProjectileTarget {
    float x, y;
    float radius = 0.5f;
    int32_t groupId;
    UnitCombatState* combatState;
};
```

`ProjectileManager` methods:
- `spawn(float x, y, dirX, dirY, speed, damage, maxRange, ownerId)` — normalizes direction manually, creates Projectile
- `update(float dt)` — moves projectiles: `pos += vel * dt`, accumulates distance, deactivates when `distanceTravelled >= maxRange`
- `checkHits(vector<ProjectileTarget>&)` — manual circle-vs-circle: `sqrt(dx*dx + dy*dy) <= target.radius + PROJECTILE_RADIUS`, calls `applyDamage()` on hit
- `cleanup()` — erase-remove inactive projectiles
- `activeCount()`, `getProjectiles()` — read access

The system is **not yet integrated** into the game loop. No game.cpp code references ProjectileManager.

### Box2D integration (existing)

**Version:** Box2D v3.0.0 (C API, handle-based: `b2BodyId`, `b2ShapeId`)

**World setup** (`src/physics/physics_world.h/cpp`):
- Zero-gravity world (top-down game)
- `physics_world_step()` calls `b2World_Step(worldId, dt, 4)`
- `physics_create_static_box()` creates wall collision bodies
- `physics_create_dynamic_circle()` creates dynamic circle bodies

**Unit bodies** (`shared/units/unit_manager.cpp`, `createInstance()` around line 117):
- Each `UnitInstance` has a single `b2BodyId bodyId`
- Dynamic circle body: `b2_dynamicBody`, radius from `definition->collisionRadius`
- Collision filtering: each unit gets a unique negative `groupIndex` (via `m_nextCollisionGroup--`). Box2D semantics: bodies with the same negative groupIndex never collide with each other.
- Damping: `linearDamping = 4.0f`, `angularDamping = 8.0f`
- Transform sync: `UnitManager::update(dt)` reads `b2Body_GetPosition()` and copies to `SectionInstance::worldPosition`

**Static walls** (`src/game.cpp`, `game_create_level_collision()`):
- Static box bodies from merged collision rectangles

**No collision event handling exists anywhere** — Box2D resolves collisions automatically but no code queries `b2World_GetContactEvents()` or `b2World_GetSensorEvents()`.

### Combat state (`shared/units/combat_state.h/cpp`)

```cpp
struct UnitCombatState { float currentHealth, maxHealth, armour; bool alive; };
```

- `applyDamage(state, rawDamage)` — armour is percentage reduction, clamps health to 0, sets alive=false on death
- Attached to `UnitInstance::combatState`, initialized from unit definition properties in `createInstance()`

### Test infrastructure (`tests/weapon_test.cpp`)

10 tests total: 5 weapon tests + 5 projectile tests. Test executable links: `GTest::gtest_main raylib nlohmann_json::nlohmann_json tinyxml2`. Does **not** currently link `box2d`.

### Relevant files

| File | Path | Role |
|------|------|------|
| Projectile header | `shared/combat/projectile_manager.h` | Projectile, ProjectileTarget structs; ProjectileManager class |
| Projectile impl | `shared/combat/projectile_manager.cpp` | Manual simulation, circle-vs-circle hit detection |
| Unit instance | `shared/units/unit_instance.h` | UnitInstance struct with b2BodyId, UnitCombatState |
| Unit manager | `shared/units/unit_manager.cpp` | Body creation, transform sync |
| Combat state | `shared/units/combat_state.h/cpp` | Damage model |
| Physics world | `src/physics/physics_world.h/cpp` | Box2D world wrapper |
| Unit types | `shared/units/unit_types.h` | Vector2, PropertyMap, PhysicsProperties |
| Weapon system | `shared/units/weapon.h/cpp` | WeaponDefinition, fire/cooldown logic |
| Tests | `tests/weapon_test.cpp` | 10 GoogleTest cases |
| Test build | `tests/CMakeLists.txt` | Test executable config |
| Shared sources | `cmake/SharedSources.cmake` | Shared source file lists |
| Unit system docs | `docs/unit_system.md` | Unit system documentation |

---

## Design Decisions

### 1. Single class — no Box2D/non-Box2D split

`ProjectileManager` uses Box2D directly. Tests link Box2D and create a lightweight world. This avoids maintaining two divergent code paths (manual vs physics). Tests advance the simulation by calling `b2World_Step()` with chosen dt values — no real-time clock needed.

### 2. Body user data for identification

All Box2D bodies carry a `BodyUserData` struct set via `bodyDef.userData`:

```cpp
enum class BodyTag : uint8_t { None, Unit, Projectile, Debris, Static };

struct BodyUserData {
    BodyTag tag = BodyTag::None;
    void* owner = nullptr;  // UnitInstance*, Projectile* index, etc.
};
```

Contact event processing calls `b2Body_GetUserData()` on each body, casts to `BodyUserData*`, and uses the tag to determine behavior. This is defined in a new header `shared/physics/body_user_data.h` with no Box2D dependency (just plain types).

### 3. Non-sensor dynamic bodies for projectiles

Sensor events in Box2D v3 only fire between sensor shapes and **dynamic** shapes — static walls would not trigger them. Projectiles must use **non-sensor dynamic bodies** with `enableContactEvents = true` on the shape definition so that `b2World_GetContactEvents()` reports contacts with both walls and units.

Projectile body properties:
- `b2_dynamicBody` with `isBullet = true` (continuous collision detection prevents tunneling through thin walls)
- `restitution = 0.0f` (no bouncing)
- `linearDamping = 0.0f` (constant velocity)
- `gravityScale = 0.0f` (explicit, though world gravity is already zero)
- Low density
- Circle shape with `PROJECTILE_RADIUS` (0.1f)
- `shapeDef.enableContactEvents = true`

### 4. Collision filtering via category bits + groupIndex

Category bits control which types of objects interact:

| Category | Bit | Collides with |
|----------|-----|---------------|
| `CATEGORY_UNIT` | 0x0001 | Unit, Projectile, Static, Debris |
| `CATEGORY_PROJECTILE` | 0x0002 | Unit, Static |
| `CATEGORY_STATIC` | 0x0004 | Everything (0xFFFF) |
| `CATEGORY_DEBRIS` | 0x0008 | Unit, Static, Debris |

Projectiles use `categoryBits = CATEGORY_PROJECTILE`, `maskBits = CATEGORY_UNIT | CATEGORY_STATIC`.

Self-damage prevention reuses the existing negative groupIndex system: the projectile gets the firing unit's `collisionGroupId`. Since each unit has a unique negative groupIndex, and Box2D prevents collision between bodies sharing the same negative groupIndex, the projectile will skip only its specific owner.

### 5. Lifetime replaces distance tracking

At constant velocity, `lifetime = maxRange / speed` — equivalent effect, simpler implementation. The `update(dt)` method just decrements `remainingLifetime`. No per-frame distance calculation needed.

### 6. Remove `ProjectileTarget` and `checkHits()`

Replaced by `processContactEvents(b2WorldId)` which queries `b2World_GetContactEvents()`. Each contact event provides two shape IDs. We get the body from each shape (`b2Shape_GetBody()`), then the user data (`b2Body_GetUserData()`). If one body is a projectile and the other is a unit, apply damage and deactivate the projectile. If the other is a wall (or anything else), just deactivate the projectile.

### 7. Pointer stability for projectile user data

The `BodyUserData` for each projectile stores the projectile's array index (cast to `void*`) rather than a raw pointer, since vector reallocation would invalidate pointers. The `processContactEvents()` method casts back to an index and accesses the projectile array. Unit bodies use stable `UnitInstance*` pointers since units are stored as `unique_ptr` (the pointer-to-object doesn't move on vector growth).

---

## Implementation Steps

### Step 1: Create `shared/physics/body_user_data.h` (new file)

Contains `BodyTag` enum, `BodyUserData` struct, and `CATEGORY_*` inline constexpr constants. No Box2D or raylib dependencies — just `<cstdint>`.

### Step 2: Refactor `shared/combat/projectile_manager.h`

- Add `#include "raymath.h"`, `#include "box2d/box2d.h"`, `#include "physics/body_user_data.h"`
- Replace `Projectile` struct fields: `Vector2 position`, `Vector2 velocity`, `float damage`, `float remainingLifetime`, `int32_t ownerId`, `bool active`, `b2BodyId bodyId`, `BodyUserData userData`
- Remove `ProjectileTarget` struct entirely
- Remove `checkHits()` method
- Change `spawn()` signature to: `void spawn(b2WorldId worldId, Vector2 position, Vector2 direction, float speed, float damage, float lifetime, int32_t ownerId)`
- Add `void syncFromPhysics()` — copies Box2D body positions to `Projectile::position`
- Add `void processContactEvents(b2WorldId worldId)` — reads contact events, applies damage, deactivates projectiles
- `cleanup()` now also destroys Box2D bodies for inactive projectiles
- `update(dt)` only decrements `remainingLifetime` (no position update — Box2D handles movement)

### Step 3: Rewrite `shared/combat/projectile_manager.cpp`

**`spawn()`:**
1. Normalize direction with `Vector2Normalize()`
2. Store projectile data (position, `Vector2Scale(normalDir, speed)` as velocity, damage, lifetime, ownerId)
3. Create `b2BodyDef`: `b2_dynamicBody`, `isBullet = true`, position from Vector2
4. Set `BodyUserData` on the projectile (tag = Projectile, owner = index cast to `void*`), assign `bodyDef.userData`
5. Create body, create circle shape with `PROJECTILE_RADIUS`, filter = `{CATEGORY_PROJECTILE, CATEGORY_UNIT | CATEGORY_STATIC, ownerId}`, `enableContactEvents = true`
6. Set linear velocity: `b2Body_SetLinearVelocity(bodyId, {vel.x, vel.y})`

**`update(dt)`:** Decrement `remainingLifetime` for active projectiles. Deactivate when <= 0.

**`syncFromPhysics()`:** For each active projectile with valid bodyId, `position = b2Body_GetPosition(bodyId)`.

**`processContactEvents(worldId)`:**
1. `b2ContactEvents events = b2World_GetContactEvents(worldId)`
2. For each `events.beginEvents[i]`: get both bodies via `b2Shape_GetBody()`, get user data via `b2Body_GetUserData()`
3. Identify which body is a projectile (check `BodyTag`)
4. If the other body is a unit (`BodyTag::Unit`): cast owner to `UnitInstance*`, call `applyDamage(unit->combatState, projectile->damage)`
5. Deactivate the projectile regardless of what it hit

**`cleanup()`:** Iterate projectiles, destroy Box2D bodies for inactive ones, erase-remove to compact.

### Step 4: Add `BodyUserData` to `UnitInstance`

In `shared/units/unit_instance.h`:
- Add `#include "physics/body_user_data.h"`
- Add `BodyUserData bodyUserData;` field to `UnitInstance` struct

In `shared/units/unit_manager.cpp` `createInstance()`:
- Before `b2CreateBody()`: set `instance->bodyUserData = {BodyTag::Unit, instance.get()}`, set `bodyDef.userData = &instance->bodyUserData`
- Update shape filter: `shapeDef.filter.categoryBits = CATEGORY_UNIT`, `shapeDef.filter.maskBits = CATEGORY_UNIT | CATEGORY_PROJECTILE | CATEGORY_STATIC | CATEGORY_DEBRIS`

In `createDebrisFromSection()`: similar with `BodyTag::Debris` and `CATEGORY_DEBRIS`.

### Step 5: Update `src/physics/physics_world.cpp`

In `physics_create_static_box()`: add `shape_def.filter.categoryBits = CATEGORY_STATIC`, `shape_def.filter.maskBits = 0xFFFF`.

### Step 6: Update `tests/CMakeLists.txt`

Add `box2d` to `target_link_libraries(run_tests PRIVATE ...)`.

### Step 7: Rewrite projectile tests in `tests/weapon_test.cpp`

Create a test fixture that sets up a `b2World` (zero gravity). Tests step the simulation with `b2World_Step(worldId, dt, 4)` to advance projectiles to desired positions, then call `processContactEvents()` to check for hits.

For tests that need a target unit, create a dynamic circle body with `BodyUserData{BodyTag::Unit, ...}` and an associated `UnitCombatState`. This is a lightweight stand-in — no full `UnitInstance` needed, just a body with user data pointing to a combat state wrapper.

| Test | Approach |
|------|----------|
| `MovesAlongHeading` | Spawn projectile along +X at speed 10. Step world 1.0s. `syncFromPhysics()`. Check position ~(10, 0). |
| `ExpiresAtLifetime` | Spawn with lifetime 0.5s. `update(0.3f)` → still active. `update(0.3f)` → expired. |
| `HitsTarget` | Create target body at (5,0) with radius 0.5. Spawn projectile along +X at speed 10. Step world 0.5s. `processContactEvents()`. Verify damage applied, projectile deactivated. |
| `MissesDistantTarget` | Target body at (5,3), projectile along +X. Step. `processContactEvents()`. No damage. Projectile still active. |
| `IgnoresOwner` | Target body and projectile share same negative groupIndex. Step. `processContactEvents()`. No damage. |

The 5 weapon tests (CooldownPreventsRapidFire, DamageScalesWithWeaponStat, NoWeaponForNegativeId, WeaponTypesParsed, AllWeaponsLoaded) are unchanged — they don't use ProjectileManager.

### Step 8: Build and run all tests

```bash
/opt/homebrew/bin/cmake --build build --target run_tests
./build/tests/run_tests
```

All 10 weapon/projectile tests should pass. All other pre-existing passing tests should still pass.

### Step 9: Update `docs/unit_system.md`

Add a "Projectile Physics" section after "Physics Integration" covering:
- Body user data: BodyTag enum, what each tag means
- Collision filtering: category bits table
- Projectile lifecycle: spawn → physics step → sync positions → process contact events → lifetime tick → cleanup
- Source-of-truth principle: the unit instance collection is authoritative for all positions, orientations, and combat state. Rendering, AI, and combat all read from the same data. Box2D contact events reference instances directly via body user data — no separate target list is maintained.

---

## Box2D v3 API Reference (for implementer)

Key API calls used in this refactor:

```c
// World
b2ContactEvents b2World_GetContactEvents(b2WorldId worldId);

// Body
b2BodyId b2CreateBody(b2WorldId worldId, const b2BodyDef* def);
void b2DestroyBody(b2BodyId bodyId);
void b2Body_SetLinearVelocity(b2BodyId bodyId, b2Vec2 velocity);
b2Vec2 b2Body_GetPosition(b2BodyId bodyId);
void* b2Body_GetUserData(b2BodyId bodyId);
void b2Body_SetUserData(b2BodyId bodyId, void* userData);
bool b2Body_IsValid(b2BodyId bodyId);

// Shape
b2ShapeId b2CreateCircleShape(b2BodyId bodyId, const b2ShapeDef* def, const b2Circle* circle);
b2BodyId b2Shape_GetBody(b2ShapeId shapeId);

// Contact events struct
typedef struct b2ContactBeginTouchEvent {
    b2ShapeId shapeIdA;
    b2ShapeId shapeIdB;
} b2ContactBeginTouchEvent;

typedef struct b2ContactEvents {
    b2ContactBeginTouchEvent* beginEvents;
    int32_t beginCount;
    // ... endEvents, hitEvents ...
} b2ContactEvents;

// b2ShapeDef fields used
shapeDef.filter.categoryBits  // uint16_t
shapeDef.filter.maskBits      // uint16_t
shapeDef.filter.groupIndex    // int32_t (negative = same group never collides)
shapeDef.enableContactEvents  // bool — must be true for contact events to fire
shapeDef.isSensor             // bool — NOT used for projectiles (sensors don't detect static)

// b2BodyDef fields used
bodyDef.isBullet              // bool — enables CCD for fast-moving bodies
bodyDef.userData              // void* — points to BodyUserData
```

---

## Verification

1. **Unit tests**: `./build/tests/run_tests --gtest_filter="Projectile*:Weapon*"` — all 10 tests pass with real Box2D physics
2. **Full build**: game target compiles with updated ProjectileManager
3. **No regressions**: full test suite passes (30 previously-passing tests still pass)
