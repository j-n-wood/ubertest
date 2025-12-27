# Rendering Pipeline

This document describes the rendering architecture used by both the game and tool projects, including lighting, bump mapping, and asset sharing conventions.

## Architecture Overview

The rendering system uses a unified Blinn-Phong lighting shader that handles both GLTF models and procedural tile geometry. All rendering code is in the `shared/` directory for reuse across projects.

```
shared/
├── rendering/
│   ├── scene_renderer.h      # Main renderer interface
│   └── scene_renderer.cpp    # Shader management, lighting, materials
├── lighting/
│   ├── light.h               # Light structure definitions
│   └── light.cpp             # Light uniform binding
```

## SceneRenderer

The `SceneRenderer` struct manages the lighting shader and provides a consistent interface for all tools and the game.

### Initialization

```cpp
SceneRenderer renderer;
sceneRendererInit(&renderer, "shaders/");
```

The renderer:
1. Loads `lighting.vs` and `lighting.fs` shaders
2. Gets uniform locations for lighting parameters
3. Loads the default flat normal map from `textures/flat_normal.png`
4. Sets default ambient (0.1), specular power (32), and bump intensity (1.0)

### Applying to Models

```cpp
Model model = LoadModel("model.gltf");
sceneRendererApplyShader(&renderer, &model);
```

This assigns the lighting shader to all materials and ensures each material has a valid normal map (using the flat default if none assigned).

## Lighting Shader

### Vertex Shader (lighting.vs)

Transforms geometry to world space and computes tangent-space basis for normal mapping:

| Input | Description |
|-------|-------------|
| `vertexPosition` | Model-space position |
| `vertexTexCoord` | UV coordinates |
| `vertexNormal` | Model-space normal |
| `vertexTangent` | Model-space tangent (vec4, w = handedness) |
| `vertexColor` | Per-vertex color |

| Output | Description |
|--------|-------------|
| `fragPosition` | World-space position |
| `fragTexCoord` | UV coordinates |
| `fragNormal` | World-space normal |
| `fragTangent` | World-space tangent |
| `fragBitangent` | World-space bitangent |

### Fragment Shader (lighting.fs)

Implements Blinn-Phong lighting with normal/bump mapping support.

| Uniform | Description |
|---------|-------------|
| `texture0` | Diffuse texture (MATERIAL_MAP_DIFFUSE) |
| `texture2` | Normal/bump map (MATERIAL_MAP_NORMAL) |
| `colDiffuse` | Material diffuse color |
| `colSpecular` | Specular color (RGB) + shininess (A) |
| `ambient` | Global ambient light color |
| `viewPos` | Camera position for specular |
| `useNormalMap` | Enable/disable bump mapping |
| `bumpIntensity` | Strength of bump effect |

### Debug Modes

Set via `sceneRendererSetDebugMode(&renderer, mode)`:

| Mode | Visualization |
|------|---------------|
| 0 | Normal rendering |
| 1 | World-space normals (with bump applied) |
| 2 | Light direction |
| 3 | Specular component only |
| 4 | View direction |
| 5 | Half vector |
| 6 | Raw normal map texture |

## Bump/Normal Mapping

### How It Works

The shader supports tangent-space normal mapping:

1. **Normal map texture** encodes surface perturbations as RGB values (0-1 range)
2. **TBN matrix** transforms from tangent space to world space
3. **Perturbed normal** is used for all lighting calculations

### Geometry Types

| Geometry | Tangent Source | Normal Map |
|----------|----------------|------------|
| GLTF models | Vertex attribute (`vertexTangent`) | Default flat (no perturbation) |
| Procedural tiles | Screen-space derivatives (computed in shader) | From tile definition |

### Default Normal Map

Models without explicit bump textures use `flat_normal.png` - a flat normal map (RGB 128,128,255) that produces no surface perturbation. This ensures:

- Consistent shader path for all geometry
- No branching in shader code
- Easy to add bump maps to any model later

### Tile Bump Textures

Tile definitions include a secondary texture path for bump mapping:

```cpp
struct TileTextures {
    std::string diffuse;  // Primary color texture
    std::string bump;     // Normal/bump map texture
    std::string effect;   // Special effect texture
};
```

The bump texture path references files in `uber/uberdroid/textures/bump/`.

## Lights

The shader supports up to 4 lights (defined by `MAX_LIGHTS`).

### Adding Lights

```cpp
// Directional light (sun-like)
sceneRendererAddDirectionalLight(&renderer,
    (Vector3){0, 100, 0},      // Position (light source)
    (Vector3){0, 0, 0},        // Target (where light points)
    WHITE);

// Point light
sceneRendererAddPointLight(&renderer,
    (Vector3){10, 5, 10},      // Position
    YELLOW);
```

### Light Uniforms

Each light has indexed uniforms:
- `light0_enabled` - Active state
- `light0_type` - 0 = directional, 1 = point
- `light0_position` - World position
- `light0_target` - Target for directional lights
- `light0_color` - RGBA color

## Shared Code Conventions

### Directory Structure

```
cpp-version/
├── shared/                    # Reusable code for game + tools
│   ├── rendering/            # Shader management
│   ├── lighting/             # Light system
│   ├── scene_convert/        # Level data parsing
│   ├── model_convert/        # Model format conversion
│   └── utils/                # String utilities, etc.
├── assets/                   # Shared assets
│   ├── shaders/             # GLSL shaders
│   └── textures/            # Common textures (flat_normal.png)
├── src/                      # Game-specific code
└── tools/                    # Tool projects
    ├── model_tool/
    ├── scene_tool/
    └── unit_test/
```

### CMake Integration

Shared sources are defined in `cmake/SharedSources.cmake`:

```cmake
set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    # ...
)
set(SHARED_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/shared)
```

### Asset Copying

Each tool's CMakeLists.txt copies required assets post-build:

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/textures
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures)
```

This ensures each tool has access to shaders and the default normal map texture.

## Material Setup

### GLTF Models

Materials are loaded from the GLTF file. The renderer applies:
1. Lighting shader to all materials
2. Default normal map if `MATERIAL_MAP_NORMAL` is empty

### Procedural Geometry

For procedurally generated meshes (tiles):

```cpp
Model model = LoadModelFromMesh(mesh);
// Set diffuse color
model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = color;
// Optionally set bump texture
if (bumpTexture.id > 0) {
    model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = bumpTexture;
}
sceneRendererApplyShader(&renderer, &model);
```

## API Reference

### SceneRenderer Functions

| Function | Description |
|----------|-------------|
| `sceneRendererInit(renderer, shaderPath)` | Initialize with shader directory |
| `sceneRendererDestroy(renderer)` | Cleanup resources |
| `sceneRendererApplyShader(renderer, model)` | Apply shader + default normal map |
| `sceneRendererUpdateCamera(renderer, pos)` | Update view position for specular |
| `sceneRendererSetAmbient(renderer, r, g, b, a)` | Set ambient light level |
| `sceneRendererSetSpecular(renderer, power, intensity)` | Set specular properties |
| `sceneRendererSetDebugMode(renderer, mode)` | Set debug visualization |
| `sceneRendererSetNormalMapEnabled(renderer, enabled)` | Toggle bump mapping |
| `sceneRendererSetBumpIntensity(renderer, intensity)` | Adjust bump strength |
| `sceneRendererAddDirectionalLight(...)` | Add directional light |
| `sceneRendererAddPointLight(...)` | Add point light |
| `sceneRendererSetLightEnabled(renderer, index, enabled)` | Toggle light |
| `sceneRendererGetShader(renderer)` | Get raw shader handle |

## Tile Geometry

This section documents the conventions for tile mesh generation, coordinate systems, and triangle winding order.

### Coordinate Systems

**Game Space** (source data):
- X: horizontal (east-west)
- Y: horizontal (north-south / forward)
- Z: vertical (height / up)

**Render Space** (JSON and runtime):
- X: horizontal (east-west) - unchanged
- Y: vertical (up) - was Z in game space
- Z: horizontal (depth) - was Y in game space

Transformation is applied during JSON serialization:
```cpp
// gameToRenderCoords in tile_mesh.h
Vector3 renderPos = {
    gameX * scale,      // X unchanged
    gameZ * scale,      // Game Z (height) -> Render Y (up)
    gameY * scale       // Game Y (forward) -> Render Z (depth)
};
```

Scale factor: `SCALE_UNITS_TO_METERS = 0.0254f` (original units are inches).

### Tile Vertex Order

Tiles are axis-aligned quads with 4 vertices stored in **counter-clockwise order** when viewed from above (looking down from +Y toward -Y):

```
    TL(3) -------- TR(2)
      |            |
      |            |
      |            |
    BL(0) -------- BR(1)

    +X →
    +Z ↑ (in render space, depth axis)
```

Vertex indices:
- 0: Bottom-Left (BL) - min X, min Z
- 1: Bottom-Right (BR) - max X, min Z
- 2: Top-Right (TR) - max X, max Z
- 3: Top-Left (TL) - min X, max Z

### Triangle Winding Order

For raylib/OpenGL, front faces have **counter-clockwise winding when viewed from the front** (the direction the normal points).

For floor tiles with UP normal (+Y), the camera views from above (looking down from +Y toward -Y). This means:
- Vertices must wind **clockwise in the X-Z plane** when viewed from above
- This equals counter-clockwise when viewed from the normal direction (+Y)

Triangle indices (CW when viewed from above):
```cpp
// Triangle 0: BL(0), TR(2), BR(1) - lower-right half
mesh.indices[0] = 0;  // BL
mesh.indices[1] = 2;  // TR
mesh.indices[2] = 1;  // BR

// Triangle 1: BL(0), TL(3), TR(2) - upper-left half
mesh.indices[3] = 0;  // BL
mesh.indices[4] = 3;  // TL
mesh.indices[5] = 2;  // TR
```

Visual representation:
```
    TL(3) -------- TR(2)
      | \          |
      |   \   T1   |
      |     \      |
      |  T0   \    |
      |         \  |
    BL(0) -------- BR(1)

T0: 0 → 2 → 1 (BL → TR → BR) - clockwise from above
T1: 0 → 3 → 2 (BL → TL → TR) - clockwise from above
```

### Normals and Tangents

All floor tiles use hardcoded normal and tangent vectors for bump mapping:

```cpp
// Normal (facing UP)
static constexpr Vector3 UP_NORMAL = {0.0f, 1.0f, 0.0f};

// Tangent (aligned with +X / U texture coordinate, w=1 for right-handed)
static constexpr float FLOOR_TANGENT[4] = {1.0f, 0.0f, 0.0f, 1.0f};
```

The TBN (Tangent-Bitangent-Normal) matrix for floor tiles:
- **T (Tangent)**: (1, 0, 0) - along +X axis, aligned with U texture coordinate
- **B (Bitangent)**: (0, 0, 1) - along +Z axis, aligned with V texture coordinate (computed as N × T × handedness)
- **N (Normal)**: (0, 1, 0) - facing UP

This is correct because:
- Tiles are flat, horizontal surfaces
- Geometry is known, so tangents can be computed exactly
- No per-vertex or screen-space derivative calculation needed
- Enables proper bump/normal mapping on tile surfaces

### Index Buffers

Each tile uses 4 vertices and 6 indices (2 triangles):
- Memory: 4 vertices instead of 6 (33% reduction)
- Index buffer: 6 × sizeof(unsigned short) = 12 bytes per tile

### Tile Types and Variable Vertex Counts

The original game supports three tile rendering modes, stored in `tileType`:

| Value | Name | Description |
|-------|------|-------------|
| 0 | `tt_fan` | Triangle fan - vertex 0 shared, triangles radiate out |
| 1 | `tt_strip` | Triangle strip - consecutive vertices form triangles |
| 2 | `tt_tris` | Explicit triangles - every 3 vertices is a triangle |
| ≥3 | (material) | Archetile material index - treated as fan |

**Triangle count calculation:**
```cpp
if (tileType == 2) {
    triangles = vertexCount / 3;  // Explicit triangles
} else {
    triangles = vertexCount - 2;  // Fan or strip
}
```

**Important:** Values ≥ 3 are material indices from archetile expansion, not actual tile types. These should be treated as triangle fans.

### Winding Order for Variable Tiles

The source data uses CW winding (when viewed from above). For correct rendering with backface culling:

**Fan triangles** (vertex 0 shared):
```cpp
// Original CW: (0, i+1, i+2)
// Reversed for CCW from above: (0, i+2, i+1)
mesh.indices[ii + 0] = baseVertex;
mesh.indices[ii + 1] = baseVertex + t + 2;
mesh.indices[ii + 2] = baseVertex + t + 1;
```

**Strip triangles** (alternating winding):
```cpp
if (t % 2 == 0) {
    // Even: reversed from (i, i+1, i+2)
    mesh.indices[ii + 0] = baseVertex + t;
    mesh.indices[ii + 1] = baseVertex + t + 2;
    mesh.indices[ii + 2] = baseVertex + t + 1;
} else {
    // Odd: reversed from (i+1, i, i+2)
    mesh.indices[ii + 0] = baseVertex + t + 1;
    mesh.indices[ii + 1] = baseVertex + t + 2;
    mesh.indices[ii + 2] = baseVertex + t;
}
```

**Explicit triangles:**
```cpp
// Reversed from (i*3, i*3+1, i*3+2)
mesh.indices[ii + 0] = baseVertex + t * 3;
mesh.indices[ii + 1] = baseVertex + t * 3 + 2;
mesh.indices[ii + 2] = baseVertex + t * 3 + 1;
```

### Common Pitfalls

1. **Wrong winding order**: If tiles don't render with backface culling ON but appear with culling OFF, the winding order is reversed. The camera looks from +Y down toward Y=0, so CCW winding when viewed from +Y is required.

2. **Assuming 4-vertex tiles**: Not all tiles have exactly 4 vertices. Strip tiles (tileType=1) commonly have more vertices. Always check `tile.vertices.size()` and use the correct triangle generation algorithm.

3. **Treating tileType ≥ 3 as special**: Values ≥ 3 are material indices from archetile expansion, not rendering modes. Always clamp: `if (tileType > 2) tileType = 0;`

4. **Strip winding alternation**: Triangle strips require alternating winding to maintain consistent face direction. Forgetting to alternate produces half the triangles facing backwards.

### Texture Batching

Tiles are grouped by texture indices for efficient rendering:
```cpp
struct TileBatchKey {
    int textureIndex1;
    int textureIndex2;
};
```

Each batch becomes a separate `Mesh` with all tiles sharing those texture indices. This minimizes texture binding changes during rendering.

### Summary Table

| Property | Value |
|----------|-------|
| Vertex order | Variable - depends on tile type |
| Source winding | CW from above (original game) |
| Output winding | CCW from above (reversed for OpenGL) |
| Normal | (0, 1, 0) hardcoded UP |
| Tangent | (1, 0, 0, 1) - +X axis with right-handed |
| Vertices per tile | Variable (minimum 3) |
| Triangles per tile | Fan/Strip: n-2, Triangles: n/3 |
| Coordinate system | Y-up (render space) |
| Scale factor | 0.0254 (inches to meters) |
| tileType values | 0=fan, 1=strip, 2=triangles, ≥3=fan |
