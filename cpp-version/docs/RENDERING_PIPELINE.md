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

### Normals

All floor tiles use a hardcoded UP normal:
```cpp
static constexpr Vector3 UP_NORMAL = {0.0f, 1.0f, 0.0f};
```

This is correct because:
- Tiles are flat, horizontal surfaces
- No per-vertex normal calculation needed
- Cross-product computation is avoided

### Index Buffers

Each tile uses 4 vertices and 6 indices (2 triangles):
- Memory: 4 vertices instead of 6 (33% reduction)
- Index buffer: 6 × sizeof(unsigned short) = 12 bytes per tile

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
| Vertex order | CCW from above: BL(0), BR(1), TR(2), TL(3) |
| Triangle winding | CW from above (= CCW from normal direction) |
| Normal | (0, 1, 0) hardcoded UP |
| Vertices per tile | 4 |
| Indices per tile | 6 |
| Coordinate system | Y-up (render space) |
| Scale factor | 0.0254 (inches to meters) |
