# Proposal: Simplified Unit Physics System

## Summary

**Remove per-section physics simulation entirely.** Replace with a single physics body per unit. Child section rendering will be positioned programmatically based on the unit's physics rotation and section-specific rotation rules.

## Rationale

The current multi-section physics implementation with weld joints has proven problematic:

- Persistent axis system inconsistencies between physics and rendering
- Complex coordinate transforms required for each section
- Weld joint physics adds simulation overhead without gameplay benefit
- Joint breaking for debris requires fragile physics-to-rendering synchronization
- Excessive development time spent debugging coordinate issues

The proposed approach is simpler:
- One physics shape per intact unit
- Deterministic section positioning via code
- Clean debris conversion when unit is destroyed

---

## Current Implementation (To Be Removed)

### Files Affected

| File | Current Role |
|------|-------------|
| [unit_types.h](../shared/units/unit_types.h) | Defines `PhysicsProperties` per section |
| [unit_instance.h](../shared/units/unit_instance.h) | `SectionInstance` holds `b2BodyId` and `b2JointId` |
| [unit_manager.cpp](../shared/units/unit_manager.cpp) | Creates physics bodies and weld joints per section |
| [unit_json.cpp](../shared/units/unit_json.cpp) | Parses per-section physics from JSON |
| [unit_generator.cpp](../tools/droid_tool/unit_generator.cpp) | Generates per-section physics shapes |
| [test_scene.cpp](../tools/unit_test/test_scene.cpp) | Tests physics-based child positioning |

### Current Section Physics (droid_class_3.json example)

```json
"children": [
  {
    "name": "section_1",
    "model": "models/ag_base_small.gltf",
    "physics": {
      "shape": { "type": "circle", "radius": 0.152400 },
      "density": 0.5,
      ...
    },
    "jointBreakForce": 500.0,
    "jointBreakTorque": 100.0
  }
]
```

Each child section currently gets:
- Its own Box2D body
- A weld joint to its parent
- Independent physics simulation

**This entire approach will be removed.**

---

## Proposed Implementation

### 1. Unit-Level Physics Shape

Add collision and proximity radii to `UnitDefinition`:

```cpp
// unit_types.h
struct UnitDefinition {
    std::string name;
    std::string id;
    float collisionRadius = 0.5f;       // NEW: Collision shape radius
    float proximityRadius = 1.0f;       // NEW: Proximity detection radius (AI/sensing)
    SectionDefinition rootSection;
    PropertyMap properties;
};
```

JSON format:
```json
{
  "name": "Class 3",
  "id": "droid_class_3",
  "collisionRadius": 0.3,
  "proximityRadius": 1.0,
  "properties": { ... },
  "rootSection": { ... }
}
```

**Source data availability**: The original `droidclasses.txt` format already provides these values on line 5 of each class definition:
- `collideRadius` - parsed in `droidclass_parser.cpp` as `DroidClass::collideRadius`
- `proximityRadius` - parsed in `droidclass_parser.cpp` as `DroidClass::proximityRadius`

These values are currently parsed but **not written** to the unit JSON. The `unit_generator.cpp` should expose them directly rather than computing from model bounds.

### 2. Section Rotation Rules

Sections may face different directions than the unit's movement. Add rotation mode to `SectionDefinition`:

```cpp
// unit_types.h
enum class SectionRotationMode {
    FollowUnit,         // Section rotates with unit physics rotation (default)
    FollowFacing,       // Section rotates to face a target angle (e.g., turret)
    Fixed               // Section maintains fixed world rotation
};

struct SectionDefinition {
    std::string name;
    std::string modelPath;
    Vector2 localOffset = {0, 0};
    float localRotation = 0.0f;
    float height = 0.0f;
    Vector3 scale = {1, 1, 1};

    SectionRotationMode rotationMode = SectionRotationMode::FollowUnit;  // NEW

    // Physics debris properties (used ONLY when section becomes debris)
    std::optional<PhysicsProperties> physics;   // Repurposed: now only for debris

    PropertyMap properties;
    std::vector<SectionDefinition> children;

    // REMOVED: jointBreakForce, jointBreakTorque (no longer needed)
};
```

### 3. Section Instance (Simplified)

Remove per-section physics bodies entirely:

```cpp
// unit_instance.h
struct SectionInstance {
    const SectionDefinition* definition = nullptr;

    // Model
    Model model = {};
    bool hasModel = false;

    // Hierarchy
    SectionInstance* parent = nullptr;
    std::vector<SectionInstance*> children;

    // World transform (computed from unit physics + offset rules)
    Vector2 worldPosition = {0, 0};
    float worldRotation = 0.0f;

    // Rotation override for FollowFacing mode
    float facingAngle = 0.0f;   // Target angle when rotationMode == FollowFacing

    // REMOVED: b2BodyId bodyId
    // REMOVED: b2JointId jointId
    // REMOVED: bool attached
    // REMOVED: bool hasPhysics
};
```

### 4. Unit Instance (Single Physics Body)

```cpp
// unit_instance.h
struct UnitInstance {
    const UnitDefinition* definition = nullptr;

    // Single physics body for the entire unit
    b2BodyId bodyId = b2_nullBodyId;

    // Section hierarchy (rendering only)
    SectionInstance* rootSection = nullptr;
    std::vector<SectionInstance*> allSections;

    // Unit-level collision group
    int32_t collisionGroup = 0;
};
```

### 5. Programmatic Section Positioning

Rewrite `updateSectionTransforms()` in `unit_manager.cpp`:

```cpp
void UnitManager::updateSectionTransforms(UnitInstance* unit) {
    // Get unit physics state (single body)
    b2Vec2 unitPos = b2Body_GetPosition(unit->bodyId);
    float unitRot = b2Body_GetAngle(unit->bodyId);

    // Recursively position all sections from code
    updateSection(unit->rootSection, {unitPos.x, unitPos.y}, unitRot);
}

void UnitManager::updateSection(SectionInstance* section, Vector2 parentPos, float parentRot) {
    // Compute world position (always follows parent)
    float cos_r = cosf(parentRot);
    float sin_r = sinf(parentRot);
    Vector2 offset = section->definition->localOffset;

    section->worldPosition = {
        parentPos.x + offset.x * cos_r - offset.y * sin_r,
        parentPos.y + offset.x * sin_r + offset.y * cos_r
    };

    // Compute world rotation based on mode
    switch (section->definition->rotationMode) {
        case SectionRotationMode::FollowUnit:
            section->worldRotation = parentRot + section->definition->localRotation;
            break;
        case SectionRotationMode::FollowFacing:
            section->worldRotation = section->facingAngle;
            break;
        case SectionRotationMode::Fixed:
            section->worldRotation = section->definition->localRotation;
            break;
    }

    // Update children recursively
    for (auto* child : section->children) {
        updateSection(child, section->worldPosition, section->worldRotation);
    }
}
```

### 6. Unit Dismantling / Debris Conversion

New function to convert a unit into debris objects when destroyed:

```cpp
// unit_manager.h
struct DebrisObject {
    b2BodyId bodyId;
    Model model;
    float height;
    Vector2 worldPosition;
    float worldRotation;
};

class UnitManager {
public:
    // Convert unit to debris - destroys unit, returns debris objects
    std::vector<DebrisObject> dismantleUnit(UnitInstance* unit);

    // Update and render debris (separate from units)
    void updateDebris(float dt);
    void renderDebris();

    // REMOVED: breakJoint(), breakAllJoints()
};
```

Implementation:

```cpp
std::vector<DebrisObject> UnitManager::dismantleUnit(UnitInstance* unit) {
    std::vector<DebrisObject> debris;

    // Get current unit velocity before destruction
    b2Vec2 unitVel = b2Body_GetLinearVelocity(unit->bodyId);
    float unitAngVel = b2Body_GetAngularVelocity(unit->bodyId);
    Vector2 unitPos = unit->rootSection->worldPosition;

    // Create debris for each section that has physics properties defined
    for (auto* section : unit->allSections) {
        if (!section->definition->physics.has_value()) {
            continue;  // Skip sections without debris physics
        }

        DebrisObject obj;
        const auto& phys = section->definition->physics.value();

        // Create physics body at section's current world position
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {section->worldPosition.x, section->worldPosition.y};
        bodyDef.rotation = b2MakeRot(section->worldRotation);
        bodyDef.linearDamping = phys.linearDamping;
        bodyDef.angularDamping = phys.angularDamping;

        obj.bodyId = b2CreateBody(m_worldId, &bodyDef);

        // Create shape
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = phys.density;
        shapeDef.friction = phys.friction;
        shapeDef.restitution = phys.restitution;

        if (phys.shapeType == PhysicsShapeType::Circle) {
            b2Circle circle = {{0, 0}, phys.circle.radius};
            b2CreateCircleShape(obj.bodyId, &shapeDef, &circle);
        } else if (phys.shapeType == PhysicsShapeType::Box) {
            b2Polygon box = b2MakeBox(phys.box.width / 2, phys.box.height / 2);
            b2CreatePolygonShape(obj.bodyId, &shapeDef, &box);
        }

        // Inherit unit velocity plus rotational contribution
        Vector2 relPos = {
            section->worldPosition.x - unitPos.x,
            section->worldPosition.y - unitPos.y
        };
        b2Vec2 debrisVel = {
            unitVel.x - unitAngVel * relPos.y,
            unitVel.y + unitAngVel * relPos.x
        };
        b2Body_SetLinearVelocity(obj.bodyId, debrisVel);
        b2Body_SetAngularVelocity(obj.bodyId, unitAngVel);

        // Copy model and height
        obj.model = section->model;
        obj.height = section->definition->height;
        obj.worldPosition = section->worldPosition;
        obj.worldRotation = section->worldRotation;

        debris.push_back(obj);
    }

    // Destroy the unit
    destroyInstance(unit);

    return debris;
}
```

### 7. GLTF Physics Extras

The existing `include_physics_shape` option in `GLTFExportOptions` will embed debris physics properties in each model's GLTF extras:

```json
{
  "asset": { "version": "2.0" },
  "extras": {
    "physics": {
      "shape": { "type": "circle", "radius": 0.15 },
      "density": 0.5,
      "friction": 0.3,
      "restitution": 0.1,
      "linearDamping": 2.0,
      "angularDamping": 4.0
    }
  }
}
```

When loading a section, if `physics` is not specified in the unit JSON, the loader can read it from the GLTF extras. This provides default debris physics derived from the model bounds.

### 8. droid_tool Changes

Modify `unit_generator.cpp`:

1. **Expose collision and proximity radii from source data**: Write `collideRadius` and `proximityRadius` from `DroidClass` to the unit JSON (these are already parsed but not written)
2. **Remove per-section physics from active simulation**: Physics blocks only define debris behavior
3. **Write GLTF physics extras**: Embed physics properties in each model file
4. **Add rotation mode**: Default to `FollowUnit`, set `FollowFacing` for turret sections (based on `hasTurret` property)

Add `radiusScale` to `UnitGeneratorOptions`:

```cpp
// In unit_generator.h:
struct UnitGeneratorOptions {
    // ... existing fields ...
    float radiusScale = 2.0f;  // Scale factor for collision/proximity radii
};
```

```cpp
// In generateUnitJSON():

// Write collision and proximity radii from source data
// radiusScale is separate from model scale - allows independent tuning
fprintf(jsonFile, "  \"collisionRadius\": %.6f,\n", droidClass.collideRadius * options.radiusScale);
fprintf(jsonFile, "  \"proximityRadius\": %.6f,\n", droidClass.proximityRadius * options.radiusScale);

// For turret sections:
if (droidClass.hasTurret && section.name == "head") {
    sectionJson["rotationMode"] = "FollowFacing";
}
```

**Source data analysis** (from `droidclasses.txt`):

| Class Range | collideRadius | Description |
|-------------|---------------|-------------|
| 1-5 | 0.1 - 0.12 | Small service/cleaning robots |
| 6-10 | 0.15 - 0.2 | Messenger/maintenance robots |
| 11-13 | 0.25 - 0.3 | Crew droids |
| 14-16 | 0.35 - 0.4 | Sentinel droids |
| 17-22 | 0.45 - 0.5 | Battle/security droids |
| 23 | 0.7 | Command cyborg |

Average collision radius: **0.31** (excluding Class 0)

With `radiusScale = 2.0`, these values scale to collision radii from 0.2m to 1.4m, which better matches the rendered model sizes.

### 9. unit_test Changes

The unit_test tool serves as the primary viewer for verifying unit definitions. It must use the same rendering code and lighting as the main topdown_game to ensure visual consistency.

#### Rendering Requirements

- **Shared rendering code**: Use the same `SceneRenderer` and shader pipeline as topdown_game
- **Lighting**: Match topdown_game lighting setup (same light positions, colors, ambient)
- **Camera**: Perspective view from 45° top-left-front corner, 3m distance from unit center

```cpp
// test_scene.cpp - Camera setup
void setupCamera(TestScene* scene) {
    // 45° from top-left-front corner at 3m distance
    float distance = 3.0f;
    float angle45 = PI / 4.0f;  // 45 degrees

    // Position: 45° azimuth (left-front), 45° elevation
    scene->camera.position = {
        -distance * cosf(angle45) * cosf(angle45),  // X: left
        distance * sinf(angle45),                    // Y: up
        -distance * cosf(angle45) * sinf(angle45)   // Z: front
    };
    scene->camera.target = {0.0f, 0.3f, 0.0f};  // Unit center (slight Y offset)
    scene->camera.up = {0.0f, 1.0f, 0.0f};
    scene->camera.fovy = 45.0f;
    scene->camera.projection = CAMERA_PERSPECTIVE;
}
```

#### Manual Rotation Control

Allow manual rotation of the unit to verify parent/child section behavior:

```cpp
// test_scene.h
struct TestScene {
    // ... existing fields ...

    // Manual rotation control
    float manualRotation = 0.0f;      // Current unit rotation (radians)
    float rotationSpeed = 0.0f;       // Auto-rotation speed (radians/sec), 0 = stopped
    bool autoRotate = false;          // Toggle auto-rotation
};
```

```cpp
// test_scene.cpp - Input handling
void testSceneHandleInput(TestScene* scene) {
    // Manual rotation (left/right arrows or A/D)
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
        scene->manualRotation += 2.0f * GetFrameTime();
    }
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
        scene->manualRotation -= 2.0f * GetFrameTime();
    }

    // Toggle auto-rotation (R key)
    if (IsKeyPressed(KEY_R)) {
        scene->autoRotate = !scene->autoRotate;
        scene->rotationSpeed = scene->autoRotate ? 0.5f : 0.0f;
    }

    // Apply rotation to unit physics body
    if (scene->currentUnit && b2Body_IsValid(scene->currentUnit->bodyId)) {
        b2Body_SetTransform(
            scene->currentUnit->bodyId,
            b2Body_GetPosition(scene->currentUnit->bodyId),
            b2MakeRot(scene->manualRotation)
        );
    }
}
```

#### Simplified Features

Remove physics-related child positioning options:

```cpp
// test_scene.h - REMOVE these fields:
bool usePhysicsForChildren = true;  // DELETE

// test_scene.cpp - REMOVE:
// - Physics/offset mode toggle (P key)
// - Child body/joint creation code
// - Joint break force monitoring and display
// - Per-section physics body debug rendering

// ADD:
// - Debris spawn test (D key) - calls dismantleUnit() and manages debris
// - Section facing angle control for FollowFacing sections (arrow keys when section selected)
```

#### Controls Summary

| Key | Action |
|-----|--------|
| Left/Right or A/D | Rotate unit manually |
| R | Toggle auto-rotation |
| D | Dismantle unit into debris |
| Up/Down | Adjust facing angle (for FollowFacing sections) |
| Tab | Cycle selected section |
| Space | Apply impulse force |
| F1 | Toggle debug overlay |
| F2 | Toggle info panel |

---

## Changes Summary

### Files to Modify

| File | Changes |
|------|---------|
| [unit_types.h](../shared/units/unit_types.h) | Add `collisionRadius` to UnitDefinition, add `SectionRotationMode` enum, remove `jointBreakForce`/`jointBreakTorque` |
| [unit_instance.h](../shared/units/unit_instance.h) | Remove `b2BodyId`/`b2JointId`/`attached`/`hasPhysics` from SectionInstance, add `facingAngle` |
| [unit_instance.cpp](../shared/units/unit_instance.cpp) | Remove section physics body creation |
| [unit_manager.h](../shared/units/unit_manager.h) | Add `dismantleUnit()`, add DebrisObject management, remove `breakJoint()`/`breakAllJoints()` |
| [unit_manager.cpp](../shared/units/unit_manager.cpp) | Remove weld joint code, rewrite transforms to be code-only, implement debris creation |
| [unit_json.h](../shared/units/unit_json.h) | Add `collisionRadius` and `rotationMode` fields |
| [unit_json.cpp](../shared/units/unit_json.cpp) | Parse new fields, remove `jointBreakForce`/`jointBreakTorque` parsing |
| [unit_generator.cpp](../tools/droid_tool/unit_generator.cpp) | Compute unit collision radius, set rotation modes, simplify physics output |
| [gltf_export.cpp](../shared/model_convert/gltf_export.cpp) | Ensure physics extras written to GLTF (already supported) |
| [test_scene.h](../tools/unit_test/test_scene.h) | Remove `usePhysicsForChildren`, add debris test state |
| [test_scene.cpp](../tools/unit_test/test_scene.cpp) | Remove physics child mode toggle, add debris test, simplify controls |

### JSON Format Changes

**Before (per-section physics with joints):**
```json
{
  "name": "Class 3",
  "rootSection": {
    "name": "hull",
    "physics": { "shape": {...}, "density": 1.0 },
    "children": [
      {
        "name": "turret",
        "physics": { "shape": {...} },
        "jointBreakForce": 500.0,
        "jointBreakTorque": 100.0
      }
    ]
  }
}
```

**After (unit-level collision, optional debris physics):**
```json
{
  "name": "Class 3",
  "collisionRadius": 0.3,
  "rootSection": {
    "name": "hull",
    "children": [
      {
        "name": "turret",
        "rotationMode": "FollowFacing",
        "physics": { "shape": {...} }
      }
    ]
  }
}
```

Note: Root section no longer needs `physics` block (unit uses `collisionRadius`). Child sections only need `physics` if they should become debris when dismantled.

### Removed Functionality

- Per-section physics bodies during normal unit operation
- Weld joints between sections
- Joint break force/torque monitoring
- `breakJoint()` / `breakAllJoints()` functions
- `SectionInstance::bodyId`, `jointId`, `attached`, `hasPhysics` fields
- Physics vs offset mode toggle in unit_test (P key)
- Per-section collision group assignment

### Added Functionality

- `UnitDefinition::collisionRadius` - single collision shape for entire unit (from source data)
- `UnitDefinition::proximityRadius` - proximity detection radius for AI/sensing (from source data)
- `SectionRotationMode` enum (FollowUnit, FollowFacing, Fixed)
- `SectionInstance::facingAngle` for turret targeting
- `UnitManager::dismantleUnit()` - converts intact unit to debris objects
- `DebrisObject` struct and debris management
- Debris test mode in unit_test (D key)

---

## Implementation Process

### Step 1: Update Type Definitions

**Files**: `unit_types.h`

1. Add `collisionRadius` and `proximityRadius` fields to `UnitDefinition`
2. Add `SectionRotationMode` enum
3. Add `rotationMode` field to `SectionDefinition`
4. Remove `jointBreakForce` and `jointBreakTorque` from `SectionDefinition`

**Verification**: Code compiles (will have errors in other files until updated)

### Step 2: Simplify Section Instance

**Files**: `unit_instance.h`, `unit_instance.cpp`

1. Remove from `SectionInstance`:
   - `b2BodyId bodyId`
   - `b2JointId jointId`
   - `bool attached`
   - `bool hasPhysics`
2. Add `float facingAngle = 0.0f` for FollowFacing mode
3. Update any initialization code

**Verification**: Code compiles

### Step 3: Update JSON Parsing

**Files**: `unit_json.h`, `unit_json.cpp`

1. Add parsing for `collisionRadius` and `proximityRadius` in unit definition
2. Add parsing for `rotationMode` in section definition
3. Remove parsing for `jointBreakForce` and `jointBreakTorque`
4. Update serialization (if used) to match

**Verification**: Can load existing unit JSON files (new fields use defaults)

### Step 4: Rewrite Unit Manager

**Files**: `unit_manager.h`, `unit_manager.cpp`

This is the largest change:

1. **Remove** from `UnitManager`:
   - `breakJoint()`
   - `breakAllJoints()`
   - `checkJointBreaking()`
   - Per-section body/joint creation in `createSectionInstance()`

2. **Modify** `createInstance()`:
   - Create single physics body using `definition->collisionRadius`
   - Store body in `UnitInstance::bodyId`
   - Create sections as render-only (no physics bodies)

3. **Rewrite** `updateSectionTransforms()`:
   - Read position/rotation from single unit body
   - Recursively compute section transforms using offset math
   - Apply `SectionRotationMode` logic for each section

4. **Add** debris system:
   - `DebrisObject` struct (or add to header)
   - `std::vector<DebrisObject> m_debris` member
   - `dismantleUnit()` implementation
   - `updateDebris()` and `renderDebris()` methods

**Verification**:
- Unit loads and renders at correct position
- Unit moves as single physics body
- Sections follow unit correctly

### Step 5: Update droid_tool

**Files**: `unit_generator.h`, `unit_generator.cpp`

1. Add `float radiusScale = 2.0f` to `UnitGeneratorOptions`
2. Write `collisionRadius` and `proximityRadius` to JSON output:
   ```cpp
   fprintf(jsonFile, "  \"collisionRadius\": %.6f,\n", droidClass.collideRadius * options.radiusScale);
   fprintf(jsonFile, "  \"proximityRadius\": %.6f,\n", droidClass.proximityRadius * options.radiusScale);
   ```
3. Remove per-section physics generation for root section
4. Keep section physics only for debris properties (optional)
5. Add `rotationMode` output for turret sections

**Verification**: Run droid_tool, inspect generated JSON

### Step 6: Update unit_test

**Files**: `test_scene.h`, `test_scene.cpp`

The unit_test tool is the primary viewer for verifying unit definitions.

1. **Rendering setup**:
   - Use same `SceneRenderer` and shaders as topdown_game
   - Match topdown_game lighting (same light positions, colors, ambient)
   - Perspective camera at 45° top-left-front corner, 3m from unit

2. **Remove**:
   - `bool usePhysicsForChildren` field
   - Physics/offset mode toggle (P key handler)
   - Joint break force display in debug overlay
   - Per-section physics debug rendering

3. **Add**:
   - Manual rotation control (Left/Right or A/D keys)
   - Auto-rotation toggle (R key)
   - Debris vector to hold dismantled debris
   - D key handler to call `dismantleUnit()` and store debris
   - Debris rendering in render loop

4. **Camera setup**:
   ```cpp
   float distance = 3.0f;
   float angle45 = PI / 4.0f;
   camera.position = {
       -distance * cosf(angle45) * cosf(angle45),
       distance * sinf(angle45),
       -distance * cosf(angle45) * sinf(angle45)
   };
   camera.target = {0.0f, 0.3f, 0.0f};
   ```

**Verification**:
- Unit renders with same lighting as topdown_game
- Manual rotation shows correct parent/child behavior
- D key dismantles unit into debris
- Debris simulates independently

### Step 7: Regenerate Unit Definitions

**Commands**:
```bash
cd cpp-version
./build/tools/droid_tool/droid_tool --regenerate-units
```

**Verification**:
- All unit JSON files have `collisionRadius` and `proximityRadius`
- No `jointBreakForce`/`jointBreakTorque` in output
- Load units in unit_test to verify

### Step 8: Testing

1. **Basic rendering**: Load each unit type, verify sections render correctly
2. **Physics movement**: Apply forces, verify unit moves as single body
3. **Section rotation**: Test FollowUnit, FollowFacing modes
4. **Debris**: Dismantle unit, verify debris inherits velocity
5. **Collision**: Verify unit collides with walls using new radius

---

## Implementation Order (Summary)

1. **unit_types.h** - Add `collisionRadius`, `proximityRadius`, `SectionRotationMode`
2. **unit_instance.h/cpp** - Simplify SectionInstance, add `facingAngle`
3. **unit_json.h/cpp** - Parse new JSON fields, remove old fields
4. **unit_manager.h/cpp** - Single body per unit, code-based transforms, debris system
5. **unit_generator.cpp** - Write radii from source data, add `radiusScale`
6. **test_scene.h/cpp** - Remove physics child mode, add debris test
7. **Regenerate unit JSONs** - Run droid_tool to update all unit definitions

---

## Benefits

1. **No more axis confusion**: Only one physics body means one coordinate transform to get right
2. **Predictable section positioning**: Deterministic code instead of physics simulation artifacts
3. **Clean debris transition**: Explicit `dismantleUnit()` call instead of monitoring joint forces
4. **Flexible section rotation**: Turrets can face independently of unit movement direction
5. **Reduced physics overhead**: Fewer bodies, no joints, less solver work
6. **Easier debugging**: Section positions are computed by code, fully traceable
7. **Simpler mental model**: Units have one collision shape; sections are just rendering
