# Unit System

The Unit system provides composite game entities composed of multiple interconnected sections, each with its own physics body and rendering model. Units support hierarchical attachment via Box2D weld joints and runtime deconstruction.

## Overview

### Core Concept

A **Unit** is a game entity composed of multiple **Sections** arranged in a tree hierarchy:

```
Unit (e.g., "Heavy Tank")
└── Root Section (hull)
    ├── Child Section (turret)
    │   └── Child Section (barrel)
    ├── Child Section (left track)
    └── Child Section (right track)
```

Each section has:
- **Model**: GLTF reference for rendering
- **Physics**: Optional Box2D body with shape/mass properties
- **Local Transform**: Offset relative to parent (position, rotation, height)
- **Joint**: Box2D weld joint connecting to parent (breakable)
- **Properties**: Custom game-specific key-value data

### Definition vs Instance

- **UnitDefinition**: Loaded from JSON, shared across all instances of a unit type. Contains templates for all sections.
- **UnitInstance**: Runtime instance with actual physics bodies and models. Multiple instances can share one definition.

### Key Features

1. **Definition/Instance Split**: JSON definitions loaded once; runtime instances have actual physics bodies
2. **Physics Joints**: Child sections use Box2D weld joints (not transform-only attachment)
3. **Deconstruction**: Joints can break under force or on-demand
4. **Entity Integration**: Units extend the existing Entity system

---

## Class/Struct Hierarchy

### Definition Structures (from JSON)

```cpp
// Physics shape variants
enum class PhysicsShapeType { None, Circle, Box, Polygon };

struct CircleShapeDef {
    float radius = 0.5f;
    Vector2 offset = {0, 0};
};

struct BoxShapeDef {
    float width = 1.0f;
    float height = 1.0f;
    Vector2 offset = {0, 0};
};

struct PolygonShapeDef {
    std::vector<Vector2> vertices;  // Max 8 for Box2D
};

// Physics properties for a section
struct PhysicsProperties {
    PhysicsShapeType shapeType = PhysicsShapeType::None;
    CircleShapeDef circle;
    BoxShapeDef box;
    PolygonShapeDef polygon;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    float linearDamping = 4.0f;
    float angularDamping = 8.0f;
    bool isSensor = false;
};

// Custom property value (variant)
using PropertyValue = std::variant<bool, int, float, std::string, Vector2, Vector3>;
using PropertyMap = std::unordered_map<std::string, PropertyValue>;

// Section template (recursive)
struct SectionDefinition {
    std::string name;
    std::string modelPath;              // Relative path to GLTF file

    // Transform relative to parent
    Vector2 localOffset = {0, 0};       // 2D offset (physics coords)
    float localRotation = 0.0f;         // Radians
    float height = 0.0f;                // Y offset for 3D rendering
    Vector3 scale = {1, 1, 1};          // Model scale

    // Physics (optional)
    std::optional<PhysicsProperties> physics;

    // Joint properties
    float jointBreakForce = 0.0f;       // 0 = unbreakable
    float jointBreakTorque = 0.0f;      // 0 = unbreakable

    // Custom properties
    PropertyMap properties;

    // Children (recursive)
    std::vector<SectionDefinition> children;
};

// Unit template
struct UnitDefinition {
    std::string name;
    std::string id;                     // Unique identifier
    SectionDefinition rootSection;
    PropertyMap properties;             // Unit-level properties
};
```

### Runtime Structures

```cpp
// Runtime section with physics body
struct SectionInstance {
    const SectionDefinition* definition;

    // Physics
    b2BodyId bodyId = b2_nullBodyId;
    bool hasPhysics = false;

    // Joint to parent
    b2JointId parentJoint = b2_nullJointId;

    // Rendering
    Model model = {};
    bool hasModel = false;

    // Hierarchy
    SectionInstance* parent = nullptr;
    std::vector<std::unique_ptr<SectionInstance>> children;

    // State
    bool attached = true;               // False after joint break

    // Cached world transform
    Vector2 worldPosition = {0, 0};
    float worldRotation = 0.0f;
};

// Runtime unit
struct UnitInstance {
    const UnitDefinition* definition;
    std::unique_ptr<SectionInstance> rootSection;

    // Flattened lists for iteration
    std::vector<SectionInstance*> allSections;
    std::vector<b2JointId> allJoints;

    // State
    bool active = true;
};
```

### UnitManager

```cpp
class UnitManager {
public:
    void init(PhysicsWorld* physics, Renderer* renderer);
    void destroy();

    // Definition loading
    const UnitDefinition* loadDefinition(std::string_view path);
    const UnitDefinition* getDefinition(std::string_view id) const;
    void preloadDefinitions(std::string_view directory);

    // Instance management
    UnitInstance* createInstance(std::string_view definitionId, Vector2 position, float rotation);
    UnitInstance* createInstance(const UnitDefinition* def, Vector2 position, float rotation);
    void destroyInstance(UnitInstance* instance);

    // Deconstruction
    void breakJoint(SectionInstance* section);
    void breakAllJoints(UnitInstance* unit);

    // Update
    void update(float dt);

    // Rendering
    void renderAll();

private:
    PhysicsWorld* m_physics = nullptr;
    Renderer* m_renderer = nullptr;

    std::unordered_map<std::string, std::unique_ptr<UnitDefinition>> m_definitions;
    std::vector<std::unique_ptr<UnitInstance>> m_instances;
};
```

---

## JSON Schema

### Unit Definition Format

```json
{
  "name": "Display Name",
  "id": "unique_id",
  "properties": {
    "key": "value"
  },
  "rootSection": {
    "name": "section_name",
    "model": "path/to/model.glb",
    "localOffset": [0, 0],
    "localRotation": 0,
    "height": 0,
    "scale": [1, 1, 1],
    "physics": {
      "shape": {
        "type": "circle|box|polygon",
        "radius": 0.5,
        "width": 1.0,
        "height": 1.0,
        "offset": [0, 0],
        "vertices": [[x, y], ...]
      },
      "density": 1.0,
      "friction": 0.3,
      "restitution": 0.0,
      "linearDamping": 4.0,
      "angularDamping": 8.0,
      "isSensor": false
    },
    "jointBreakForce": 0,
    "jointBreakTorque": 0,
    "properties": {},
    "children": [
      { /* child section */ }
    ]
  }
}
```

### Example: Multi-Section Vehicle

```json
{
  "name": "Heavy Tank",
  "id": "tank_heavy",
  "properties": {
    "faction": "blue",
    "maxHealth": 500
  },
  "rootSection": {
    "name": "hull",
    "model": "models/tank_hull.glb",
    "physics": {
      "shape": { "type": "box", "width": 3.0, "height": 5.0 },
      "density": 5.0,
      "friction": 0.5
    },
    "properties": { "health": 300 },
    "children": [
      {
        "name": "turret",
        "model": "models/tank_turret.glb",
        "localOffset": [0, -0.5],
        "height": 1.2,
        "physics": {
          "shape": { "type": "circle", "radius": 1.2 },
          "density": 2.0
        },
        "jointBreakForce": 50000,
        "properties": { "health": 150, "canRotate": true },
        "children": [
          {
            "name": "barrel",
            "model": "models/tank_barrel.glb",
            "localOffset": [0, -1.5],
            "height": 1.4,
            "physics": {
              "shape": { "type": "box", "width": 0.3, "height": 2.0 },
              "density": 1.0
            },
            "jointBreakForce": 20000,
            "properties": { "health": 50 }
          }
        ]
      },
      {
        "name": "track_left",
        "model": "models/tank_track.glb",
        "localOffset": [-1.5, 0],
        "physics": {
          "shape": { "type": "box", "width": 0.5, "height": 4.5 },
          "density": 1.5,
          "friction": 0.8
        },
        "jointBreakForce": 100000,
        "properties": { "health": 100 }
      },
      {
        "name": "track_right",
        "model": "models/tank_track.glb",
        "localOffset": [1.5, 0],
        "scale": [-1, 1, 1],
        "physics": {
          "shape": { "type": "box", "width": 0.5, "height": 4.5 },
          "density": 1.5,
          "friction": 0.8
        },
        "jointBreakForce": 100000,
        "properties": { "health": 100 }
      }
    ]
  }
}
```

### Example: Simple Character

```json
{
  "name": "Robot Worker",
  "id": "robot_worker",
  "rootSection": {
    "name": "body",
    "model": "models/robot_body.glb",
    "physics": {
      "shape": { "type": "circle", "radius": 0.5 },
      "density": 1.0
    },
    "properties": { "health": 100 },
    "children": [
      {
        "name": "arm_left",
        "model": "models/robot_arm.glb",
        "localOffset": [-0.6, 0],
        "height": 0.5,
        "physics": {
          "shape": { "type": "box", "width": 0.2, "height": 0.6 },
          "density": 0.5
        },
        "jointBreakForce": 5000,
        "properties": { "health": 25 }
      },
      {
        "name": "arm_right",
        "model": "models/robot_arm.glb",
        "localOffset": [0.6, 0],
        "height": 0.5,
        "scale": [-1, 1, 1],
        "physics": {
          "shape": { "type": "box", "width": 0.2, "height": 0.6 },
          "density": 0.5
        },
        "jointBreakForce": 5000,
        "properties": { "health": 25 }
      }
    ]
  }
}
```

---

## Loading & Instantiation

### Definition Loading Flow

```
loadDefinition("assets/units/tank.json")
    │
    ├─► Check cache → Found → Return cached pointer
    │
    └─► Not found:
        ├─► Read JSON file
        ├─► Parse with nlohmann/json
        ├─► Build UnitDefinition recursively
        ├─► Store in cache
        └─► Return pointer
```

### Instance Creation Flow

```
createInstance("tank_heavy", position, rotation)
    │
    ├─► Get definition from cache
    │
    ├─► Allocate UnitInstance
    │
    └─► Create root section:
        ├─► Create physics body at world position
        ├─► Load model (Raylib caches internally)
        │
        └─► For each child section:
            ├─► Calculate world position from parent + offset
            ├─► Create physics body
            ├─► Create weld joint to parent
            ├─► Load model
            └─► Recurse for grandchildren
```

---

## Physics Integration

### Weld Joint Creation

Child sections are attached to parents using Box2D weld joints:

```cpp
b2WeldJointDef jointDef = b2DefaultWeldJointDef();
jointDef.bodyIdA = parentBody;
jointDef.bodyIdB = childBody;
jointDef.localAnchorA = localOffset;
jointDef.localAnchorB = {0, 0};
jointDef.referenceAngle = localRotation;
b2JointId joint = b2CreateWeldJoint(worldId, &jointDef);
```

### Joint Break Detection

Box2D v3 does not auto-break joints. Check forces manually in update:

```cpp
void UnitManager::update(float dt) {
    for (auto* unit : m_instances) {
        for (auto* section : unit->allSections) {
            if (!section->attached) continue;
            if (b2Joint_IsValid(section->parentJoint)) {
                b2Vec2 force = b2Joint_GetConstraintForce(section->parentJoint);
                float torque = b2Joint_GetConstraintTorque(section->parentJoint);

                auto* def = section->definition;
                if (def->jointBreakForce > 0 && b2Length(force) > def->jointBreakForce) {
                    breakJoint(section);
                }
                if (def->jointBreakTorque > 0 && std::abs(torque) > def->jointBreakTorque) {
                    breakJoint(section);
                }
            }
        }
    }
}
```

---

## Deconstruction System

### Breaking a Single Joint

```cpp
void UnitManager::breakJoint(SectionInstance* section) {
    if (!section || !section->attached) return;

    // Destroy Box2D joint
    if (b2Joint_IsValid(section->parentJoint)) {
        b2DestroyJoint(section->parentJoint);
    }
    section->parentJoint = b2_nullJointId;
    section->attached = false;

    // Section body now simulates independently
}
```

### Breaking All Joints

```cpp
void UnitManager::breakAllJoints(UnitInstance* unit) {
    for (auto& jointId : unit->allJoints) {
        if (b2Joint_IsValid(jointId)) {
            b2DestroyJoint(jointId);
        }
    }
    unit->allJoints.clear();

    for (auto* section : unit->allSections) {
        section->attached = false;
        section->parentJoint = b2_nullJointId;
    }
}
```

### Post-Break Behavior

After breaking:
- Physics body continues with current velocity
- Section renders at physics-driven position
- Parent-child hierarchy retained for cleanup, but transforms independent

---

## Rendering

### Transform Hierarchy

When attached, transforms are computed hierarchically:

```cpp
void updateSectionTransforms(SectionInstance* section, Vector2 parentPos, float parentRot) {
    if (section->attached && section->parent) {
        // Compute world position from parent
        auto& offset = section->definition->localOffset;
        float c = std::cos(parentRot);
        float s = std::sin(parentRot);
        section->worldPosition = {
            parentPos.x + offset.x * c - offset.y * s,
            parentPos.y + offset.x * s + offset.y * c
        };
        section->worldRotation = parentRot + section->definition->localRotation;
    } else if (section->hasPhysics) {
        // Detached: get from physics
        b2Vec2 pos = b2Body_GetPosition(section->bodyId);
        section->worldPosition = {pos.x, pos.y};
        section->worldRotation = b2Rot_GetAngle(b2Body_GetRotation(section->bodyId));
    }

    for (auto& child : section->children) {
        updateSectionTransforms(child.get(), section->worldPosition, section->worldRotation);
    }
}
```

### Coordinate Mapping

| Box2D (2D) | Raylib (3D) |
|------------|-------------|
| X          | X           |
| Y          | Z           |
| —          | Y (height)  |

### Rendering Sections

```cpp
void renderSection(SectionInstance* section) {
    if (!section->hasModel) return;

    Vector3 position = {
        section->worldPosition.x,
        section->definition->height,
        section->worldPosition.y
    };

    DrawModelEx(
        section->model,
        position,
        {0, 1, 0},
        section->worldRotation * RAD2DEG,
        section->definition->scale,
        WHITE
    );

    for (auto& child : section->children) {
        renderSection(child.get());
    }
}
```

---

## Entity System Integration

### Extended Entity Struct

```cpp
typedef enum EntityType {
    ENTITY_PLAYER,
    ENTITY_ENEMY,
    ENTITY_OBSTACLE,
    ENTITY_PROP,
    ENTITY_UNIT,    // Unit-backed entity
    ENTITY_DEBRIS   // Detached section
} EntityType;

typedef struct Entity {
    EntityType type;
    Vector3 position;
    float rotation;
    float desired_rotation;
    Model model;
    bool has_model;
    PhysicsBody physics;
    bool active;

    // Unit system
    UnitInstance* unit;
    bool has_unit;
} Entity;
```

### Spawning Units as Entities

```cpp
Entity* spawnUnitEntity(Game* game, const char* unitId, Vector3 pos) {
    Entity* entity = allocateEntity(game, ENTITY_UNIT);
    entity->position = pos;

    Vector2 physicsPos = {pos.x, pos.z};
    entity->unit = game->units.createInstance(unitId, physicsPos, 0);
    entity->has_unit = (entity->unit != nullptr);

    return entity;
}
```

---

## File Organization

```
cpp-version/
├── shared/
│   └── units/
│       ├── unit_types.h      # Definition structs, PropertyMap
│       ├── unit_instance.h   # Runtime structs
│       ├── unit_manager.h    # Manager interface
│       ├── unit_manager.cpp  # Core implementation
│       ├── unit_json.h       # JSON load/save declarations
│       ├── unit_json.cpp     # nlohmann/json parsing & serialization
│       └── unit_physics.cpp  # Box2D body/joint helpers
├── assets/
│   └── units/
│       ├── test_simple.json
│       ├── test_multi.json
│       └── test_breakable.json
└── tools/
    └── unit_test/
        ├── CMakeLists.txt
        ├── main.cpp
        ├── test_scene.h
        ├── test_scene.cpp
        ├── unit_editor.h
        └── unit_editor.cpp
```

---

## Test Tool (Unit Inspector)

### Usage

```bash
./unit_test assets/units/tank_heavy.json
```

### Controls

| Key | Action |
|-----|--------|
| `WASD` | Move camera |
| `Mouse` | Look around |
| `Space` | Apply impulse to root body |
| `B` | Break all joints |
| `R` | Reset (respawn unit) |
| `I` | Toggle info overlay |
| `P` | Pause/resume physics |
| `F1` | Toggle debug draw |
| `E` | Enter edit mode |
| `Ctrl+S` | Save definition to JSON |
| `Esc` | Exit |

### Edit Mode

- Click to select section
- Arrow keys to adjust offset
- Modify properties via overlay
- Save changes back to JSON

### Debug Visualization

- Physics body outlines
- Joint connections (green = intact, red = broken)
- Section hierarchy tree
- Velocity vectors

---

## Implementation Phases

1. **Data Structures**: Create type definitions
2. **JSON Loading**: Parse definitions with nlohmann/json
3. **Physics Integration**: Create bodies and weld joints
4. **Rendering**: Hierarchical transform and model drawing
5. **Deconstruction**: Joint breaking and force detection
6. **Entity Integration**: Connect to existing entity system
