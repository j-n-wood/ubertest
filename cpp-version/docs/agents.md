# Agent Guide - cpp-version Project

This document provides context for AI agents working on tool projects in the cpp-version directory. It references detailed documentation and captures key patterns.

## Project Overview

This project converts legacy game data (tiles, geometry, units) to modern formats and implements a top-down action game engine. Tool projects handle parsing, conversion, validation, and visualization. Gameplay systems are implemented in stages (see [gameplay_implementation_plan.md](gameplay_implementation_plan.md)).

## Key Documentation

| Document | Purpose |
|----------|---------|
| [DATA_CONVERSION_GUIDE.md](DATA_CONVERSION_GUIDE.md) | **Start here for conversion tools** - Architecture, validation, pitfalls |
| [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md) | Tile geometry, winding order, coordinate systems |
| [ORIGINAL_FORMATS.md](ORIGINAL_FORMATS.md) | Source data format specifications |
| [JSON_FORMATS.md](JSON_FORMATS.md) | Output format specifications |
| [unit_system.md](unit_system.md) | Unit/entity system design, combat state, body user data, collision filtering, projectile lifecycle |
| [projectile_system.md](projectile_system.md) | Projectile refactor design — Box2D bodies, contact events, API reference |
| [gameplay_implementation_plan.md](gameplay_implementation_plan.md) | Staged gameplay implementation — **read Design Patterns section first** |
| [GAME.md](GAME.md) | Game design document (mechanics, rules) |
| [UBERDROID_DATA_FORMATS.md](../tools/droid_tool/UBERDROID_DATA_FORMATS.md) | **Droid/unit conversion** - Legacy droidclasses.txt, renderobjects.txt formats |
| [legacy_data_format_reference.md](../../tools/legacy_data_format_reference.md) | Quick reference for legacy unit data structures |

## Critical Rules for Data Conversion

### 1. Never Reimplement Parsers
Copy original parsing code from `uber/source/uberdroid/` into tool projects. The original code handles all format variations correctly.

### 2. Preserve Semantic Flags
Format flags like `tileType` have rendering implications:
- `tileType=0`: Triangle fan
- `tileType=1`: Triangle strip (requires alternating winding)
- `tileType=2`: Explicit triangles
- `tileType>=3`: Archetile material index (render as fan)

### 3. Transform Coordinates After Parsing
Keep parsing code pure. Apply coordinate transforms in a separate, explicit conversion layer:
```cpp
// Game space: X-right, Y-forward, Z-up (inches)
// Render space: X-right, Y-up, Z-forward (meters)
Vector3 gameToRender(float x, float y, float z, float scale) {
    return {x * scale, z * scale, y * scale};
}
```

### 4. Validate Against Reference
Always compare converted output against known-correct rendering. Use the incremental_viewer with F7 (backface cull toggle) to detect winding issues.

### 5. Fix Winding Order
Original data uses CW winding (viewed from above). OpenGL expects CCW. Reverse indices:
```cpp
// Original: (a, b, c) → Corrected: (a, c, b)
```

## Tool Projects

### incremental_viewer
Visual validation tool for scene conversion.

**Key controls:**
- F3: Toggle tiles
- F4: Toggle geometry
- F5: Wireframe
- F6: Tile index overlay
- F7: Backface culling toggle (reveals winding issues)

### model_tool
GLTF model inspection and conversion.

### scene_tool
Batch scene conversion (non-interactive).

### unit_test
Unit/entity system testing. See [tools/unit_test/agents.md](../tools/unit_test/agents.md).

### level_tool
Converts FreedroidClassic Paradroid.maps to Tiled TMX format. See [tools/level_tool/README.md](../tools/level_tool/README.md).

### droid_tool
Converts legacy droid class definitions to modern unit JSON format. Parses droidclasses.txt and renderobjects.txt. See [tools/droid_tool/UBERDROID_DATA_FORMATS.md](../tools/droid_tool/UBERDROID_DATA_FORMATS.md) for format reference.

**Usage:**
```bash
./level_tool --convert                    # Use defaults
./level_tool --convert --input <path>     # Override input
./level_tool --convert -o <dir>           # Override output
```

**Output:** TMX files + tileset assets in `./output/ships/ship1/levels/`

**Features:**
- Converts 16 levels with tile data (CSV encoding)
- Exports waypoints as object layer with link-n properties
- Copies tileset assets (default.tsx, map_blocks.png) for standalone use

**Data:** 233 waypoints, 553 links across all levels

## Directory Structure

```
cpp-version/
├── docs/                    # Documentation (you are here)
├── shared/                  # Shared code for all projects
│   ├── rendering/          # Mesh generation, shaders
│   ├── scene_convert/      # Parsing and conversion
│   ├── model_convert/      # GLTF handling
│   ├── units/              # Unit system (definitions, instances, combat)
│   │   ├── unit_types.h    #   Definition structs, PropertyMap
│   │   ├── unit_instance.h #   Runtime structs (SectionInstance, UnitInstance, BodyUserData)
│   │   ├── unit_manager.*  #   Instance lifecycle, rendering, debris
│   │   ├── unit_json.*     #   JSON parsing & serialization
│   │   ├── combat_state.*  #   Combat state, damage model
│   │   └── weapon.*        #   Weapon definitions, fire/cooldown
│   ├── combat/             # Combat systems
│   │   └── projectile_manager.*  # Projectile Box2D bodies, contact events
│   ├── physics/            # Physics helpers
│   │   └── body_user_data.h      # BodyTag, BodyUserData, collision categories
│   └── level/              # TMX level loading, tile rendering, spawn config
├── tools/                   # Tool projects
│   ├── incremental_viewer/
│   ├── model_tool/
│   ├── scene_tool/
│   ├── unit_test/
│   ├── level_tool/
│   └── droid_tool/
├── tests/                   # GoogleTest suite
├── assets/                  # Shared assets (shaders, textures, unit definitions)
│   ├── units/              #   24 droid class JSON definitions
│   └── models/             #   GLTF models and textures
├── src/                     # Game-specific code
└── cmake/                   # Build configuration (SharedSources.cmake, Dependencies.cmake)
```

## Gameplay Systems

Gameplay is implemented in stages per [gameplay_implementation_plan.md](gameplay_implementation_plan.md). Each stage is independently testable. **Read the [Design Patterns](gameplay_implementation_plan.md#design-patterns) section before implementing any gameplay system.**

### Gameplay Design Patterns

These three rules apply to all gameplay code:

1. **Use raylib types** — `Vector2`, `Vector3`, `Color` for members and function parameters. Use raymath functions (`Vector2Normalize`, `Vector2Scale`, `Vector2Distance`, etc.) instead of manual float arithmetic.

2. **Use Box2D directly** — systems that need physics use Box2D API calls directly. Tests create a `b2World` and step the simulation. Don't write manual physics simulation or abstract Box2D behind wrapper interfaces.

3. **Single source of truth** — the unit instance collection is authoritative for all positions, orientations, and combat state. Access units via `BodyUserData` pointers on Box2D bodies during contact events. Don't build intermediate data structures that duplicate data from authoritative sources.

All Box2D bodies carry a `BodyUserData` struct (defined in `shared/physics/body_user_data.h`) that identifies what they are. See [unit_system.md](unit_system.md#body-user-data) for tag definitions and [unit_system.md](unit_system.md#collision-filtering) for the category bit table.

### Droid Property Access

Unit definitions store gameplay data in a typed `DroidProperties` struct (defined in `unit_types.h`). Access fields directly — no map lookups or variant casts:

```cpp
float armour = unit->definition->properties.armour;
int weaponId = unit->definition->properties.weapon;  // -1 = unarmed
```

### Droid Class Properties

| Property | Type | Range | Purpose |
|----------|------|-------|---------|
| `classId` | int | 0–23 | Droid class index |
| `energy` | int | 0–9 | Power level (maps to health: energy * 100, min 10) |
| `armour` | float | 20–65+ | Damage reduction percentage |
| `weapon` | float | 0+ | Weapon capability (Stage 2) |
| `droidType` | int | 0–3 | Aggression type (Stage 4) |
| `driveType` | int | 0–2 | Movement type |
| `brainType` | int | 0–3 | AI behaviour class (Stage 4) |
| `proximityRadius` | float | 16–30 | AI detection radius (on UnitDefinition, not properties) |

### Adding Gameplay Tests

Tests link `box2d` and `raylib` so gameplay systems can be tested with real physics. Add test files to `tests/CMakeLists.txt` with the corresponding `.cpp` source. Tests that need physics create a lightweight `b2World` (zero gravity) and step it with controlled dt values — no real-time clock needed.

```cmake
add_executable(run_tests
    ...
    weapon_test.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/weapon.cpp
    ${CMAKE_SOURCE_DIR}/shared/combat/projectile_manager.cpp
    ...
)
target_link_libraries(run_tests PRIVATE GTest::gtest_main raylib box2d ...)
```

## Common Patterns

### Adding Debug Toggles
```cpp
// In viewer struct
struct ViewerToggles {
    bool showFeature;
};

// In update
if (IsKeyPressed(KEY_FX)) {
    viewer->toggles.showFeature = !viewer->toggles.showFeature;
    TraceLog(LOG_INFO, "Feature %s", viewer->toggles.showFeature ? "ON" : "OFF");
}
```

### Logging for Debugging
```cpp
// Log first few items to verify parsing
if (index < 3) {
    TraceLog(LOG_INFO, "Item %d: field=%d", index, item.field);
}
```

### Coordinate Transform
```cpp
constexpr float SCALE = 0.0254f;  // inches to meters

Vector3 transform(const OldVec& v) {
    return {v.x * SCALE, v.z * SCALE, v.y * SCALE};
}
```

## Coordinate Systems and Conventions

### World Coordinate System (OpenGL/Raylib)
```
        +Y (up)
         |
         |
         +------ +X (right)
        /
       /
     +Z (forward, into screen)
```

- **X-axis**: Right (positive = right)
- **Y-axis**: Up (positive = up, used for height)
- **Z-axis**: Forward (positive = into screen / away from viewer)

### TMX to World Coordinate Transform

TMX files use a 2D coordinate system with origin at top-left:
- TMX X: Columns (0 = left, increases right)
- TMX Y: Rows (0 = top, increases down)

**Conversion:**
```cpp
// TMX grid to world coordinates (centered on origin)
float worldX = (col + 0.5f) * worldScale - halfWidth;   // TMX X → World X
float worldY = 0.0f;                                     // Floor plane
float worldZ = (row + 0.5f) * worldScale - halfHeight;  // TMX Y → World Z
```

This places TMX row 0 at -Z (back) and the last row at +Z (front), so when viewed from above (+Y looking down), the level appears as it does in Tiled.

### Camera Modes

**Perspective** (default):
- Standard orbit camera at configurable height and distance
- Orbit angle rotates camera position around target on XZ plane

**Top-down** (orthographic):
- Camera positioned directly above target (+Y)
- Up vector points toward -Z so TMX row 0 appears at top of screen
- Orbit angle rotates the up vector (rotates the view)

**Isometric** (orthographic):
- Camera at 45 degrees from horizontal
- Standard Y-up orientation

### Lighting System

**Blinn-Phong Shader Convention:**
The shader expects `lightDir` to point **toward** the light source (standard Blinn-Phong):
```glsl
// Directional light: lightDir points from surface toward light
lightDir = normalize(light0_position - light0_target);

// Diffuse: surfaces facing the light are bright
float diff = max(dot(normal, lightDir), 0.0);
```

**Configuring Directional Lights:**
```cpp
// Light shining DOWN from above:
// - position: where light originates (above scene)
// - target: where light rays aim (below position)
sceneRendererAddDirectionalLight(&renderer,
    (Vector3){0, 50, 0},   // Position (above)
    (Vector3){0, 0, 0},    // Target (below)
    WHITE);
```

The light direction (ray direction) is `normalize(target - position)`, but the shader uses `normalize(position - target)` for the Blinn-Phong `lightDir` (direction toward light).

### UV Mapping for Floor Tiles

Floor tiles are quads on the XZ plane (Y=0) with normals pointing +Y (up).

**Vertex layout** (viewed from above, CCW winding):
```
TL(3)----TR(2)
  |        |
  |   +Z   |
  |   ↑    |
BL(0)----BR(1)
     →+X
```

**UV assignment** (texture V increases downward):
```cpp
float uvs[4][2] = {
    {u0, v0},  // BL - maps to texture top
    {u1, v0},  // BR
    {u1, v1},  // TR - maps to texture bottom
    {u0, v1},  // TL
};
```

This maps texture row 0 (top) to world -Z and texture bottom to world +Z, matching TMX orientation.

### Triangle Winding Order

OpenGL uses CCW winding for front faces (when backface culling is enabled):
```cpp
// Indices for two triangles forming a quad (CCW from above)
// Triangle 0: BL, TR, BR
// Triangle 1: BL, TL, TR
mesh.indices[ii++] = baseVert + 0;  // BL
mesh.indices[ii++] = baseVert + 2;  // TR
mesh.indices[ii++] = baseVert + 1;  // BR

mesh.indices[ii++] = baseVert + 0;  // BL
mesh.indices[ii++] = baseVert + 3;  // TL
mesh.indices[ii++] = baseVert + 2;  // TR
```

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Geometry invisible | Wrong winding | Toggle F7, reverse indices |
| Partial geometry | tileType ignored | Handle fan/strip/tris modes |
| 0 tiles parsed | Format variation | Check original parser code |
| Wrong positions | Coordinate system | Apply game→render transform |
| Scale wrong | Missing scale factor | Apply 0.0254 (inches→meters) |

## Build System

Shared sources defined in `cmake/SharedSources.cmake`:
```cmake
set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/rendering/tile_mesh.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
    # ...
)
```

Tool CMakeLists.txt includes:
```cmake
include(${CMAKE_SOURCE_DIR}/cmake/SharedSources.cmake)
target_sources(${PROJECT_NAME} PRIVATE ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${SHARED_INCLUDE_DIR})
```
