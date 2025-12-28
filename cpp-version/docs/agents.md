# Agent Guide - cpp-version Project

This document provides context for AI agents working on tool projects in the cpp-version directory. It references detailed documentation and captures key patterns.

## Project Overview

This project converts legacy game data (tiles, geometry, units) to modern formats for a new game engine. Tool projects handle parsing, conversion, validation, and visualization.

## Key Documentation

| Document | Purpose |
|----------|---------|
| [DATA_CONVERSION_GUIDE.md](DATA_CONVERSION_GUIDE.md) | **Start here for conversion tools** - Architecture, validation, pitfalls |
| [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md) | Tile geometry, winding order, coordinate systems |
| [ORIGINAL_FORMATS.md](ORIGINAL_FORMATS.md) | Source data format specifications |
| [JSON_FORMATS.md](JSON_FORMATS.md) | Output format specifications |
| [unit_system.md](unit_system.md) | Unit/entity system design |

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
│   └── units/              # Entity system
├── tools/                   # Tool projects
│   ├── incremental_viewer/
│   ├── model_tool/
│   ├── scene_tool/
│   ├── unit_test/
│   └── level_tool/
├── assets/                  # Shared assets (shaders, textures)
└── src/                     # Game-specific code
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
