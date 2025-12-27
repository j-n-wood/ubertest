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
