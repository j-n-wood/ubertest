# Incremental Scene Viewer Tool

A new tool for validating scene data conversion with integrated GLTF reference rendering.

## Overview

This tool replaces the existing `scene_tool` with a cleaner architecture that:
1. Starts from the proven `model_tool` rendering foundation
2. Converts source data to JSON and writes to output folder
3. Reloads the JSON using the same shared code the game will use
4. Renders the reloaded data for validation
5. Displays a reference GLTF model (Suzanne) for visual comparison

The existing `scene_tool` should be archived to `tools/scene_tool_archive/` before implementing this replacement.

## Design Principles

### Write-Then-Reload Workflow
- Source data is parsed and converted to JSON format
- JSON is written to output files on disk
- JSON is then **reloaded from disk** using shared loading code
- Rendering uses **only** the reloaded data, never the in-memory conversion result
- This ensures:
  - JSON output can be manually inspected independent of rendering
  - If JSON is manifestly incorrect, it will be obvious in the file
  - The same loading code path is exercised as the game will use
  - No hidden state differences between tool and game

### Reference Model for Visual Validation
- Suzanne.gltf is rendered at position `(-1, 0, -1)` - outside the expected data range
- All converted scene data should be at coordinates `>= (0, 0, 0)`
- The reference model validates that the rendering pipeline is working correctly
- If Suzanne renders correctly but scene data doesn't, the issue is in conversion or JSON format

### Shared Code with Game
- JSON loading code lives in `shared/` directory
- Both this tool and the game use identical loading functions
- Use the same `SceneRenderer` from `shared/rendering/`
- Use the same lighting shader setup as `model_tool`
- Ensures no GLTF rendering regressions and identical behavior

## Architecture

```
incremental_viewer/
├── main.cpp                 # Entry point, CLI parsing
├── viewer.h                 # Viewer state and interface
├── viewer.cpp               # Viewer implementation
├── tile_converter.h         # Source → JSON conversion (tool-only)
├── tile_converter.cpp
└── CMakeLists.txt

shared/
├── scene_data/
│   ├── tile_types.h         # Tile data structures (shared with game)
│   ├── tile_loader.h        # JSON loading functions (shared with game)
│   └── tile_loader.cpp
└── rendering/
    ├── scene_renderer.h     # Lighting shader setup
    ├── scene_renderer.cpp
    └── tile_mesh.h          # Mesh generation from loaded data
    └── tile_mesh.cpp
```

### Code Organization
- **Tool-only code**: Source file parsing, JSON writing (in `incremental_viewer/`)
- **Shared code**: JSON loading, data structures, mesh generation (in `shared/`)
- The game will only link against shared code, never tool-specific conversion code

### Dependencies (same as model_tool)
- raylib - Rendering
- raymath - Vector math
- nlohmann/json - JSON serialization
- shared/rendering/scene_renderer - Lighting shader
- shared/scene_data/tile_loader - JSON loading (shared with game)

## Coordinate System and Scale

### Scale Factor

The scale factor converts original game units to GLTF meters.

```cpp
// Independent constant for easy adjustment
// Initial assumption: original units are inches, convert to meters
constexpr float SCALE_UNITS_TO_METERS = 0.0254f;  // 1 inch = 0.0254 meters

// Alternative values to try:
// constexpr float SCALE_UNITS_TO_METERS = 1.0f;      // No scaling
// constexpr float SCALE_UNITS_TO_METERS = 0.01f;     // cm to m
// constexpr float SCALE_UNITS_TO_METERS = 1.0f/64.0f; // 64 units = 1 meter
```

This constant should be defined in a single header file (e.g., `scale_config.h`) and used consistently throughout the conversion.

### Coordinate Transform

Original game coordinates:
- X: horizontal
- Y: horizontal (forward/back in game)
- Z: vertical (height)

GLTF/Raylib 3D coordinates:
- X: horizontal (unchanged)
- Y: vertical (up)
- Z: horizontal (depth)

Transform:
```cpp
Vector3 toRenderCoords(float gameX, float gameY, float gameZ, float scale) {
    return {
        gameX * scale,      // X unchanged
        gameZ * scale,      // Game Z (height) -> Render Y
        gameY * scale       // Game Y -> Render Z
    };
}
```

## Implementation Phases

### Phase 1: Viewer Foundation

**Goal**: Establish working viewer with reference model rendering.

**Tasks**:
1. Create new tool directory `tools/incremental_viewer/`
2. Copy `model_tool` main loop structure as starting point
3. Initialize `SceneRenderer` with lighting shader
4. Load and render `Suzanne.gltf` at position `(-1, 0, -1)`
5. Implement camera controls (WASD, mouse wheel zoom)
6. Add grid rendering for spatial reference

**Validation**: Suzanne renders correctly with lighting, identical to model_tool output.

**Files**:
```cpp
// main.cpp
int main(int argc, char* argv[]) {
    // Parse args: input source path, output directory

    InitWindow(1280, 720, "Incremental Scene Viewer");
    SetTargetFPS(60);

    Viewer viewer;
    viewerInit(&viewer, "shaders/");

    // Load reference model
    viewerLoadReference(&viewer, "Suzanne.gltf");

    while (!WindowShouldClose()) {
        viewerUpdate(&viewer, GetFrameTime());

        BeginDrawing();
        ClearBackground(DARKGRAY);
        viewerRender(&viewer);
        viewerDrawOverlay(&viewer);
        EndDrawing();
    }

    viewerCleanup(&viewer);
    CloseWindow();
    return 0;
}
```

### Phase 2: Tile Data Type

**Goal**: Convert and render tile geometry.

**Source Format** (from `xmapfile{n}.txt`):
```
Tile
<vertex_count>
<x1> <y1> <z1>
<x2> <y2> <z2>
...
<u1_1> <v1_1>
...
<texture_index_1> <texture_index_2> <tile_type>
[DiffuseColour <r> <g> <b>]
```

**JSON Output Format**:
```json
{
  "tiles": [
    {
      "vertices": [
        {"position": [x, y, z], "uv1": [u, v], "uv2": [u, v]}
      ],
      "textureIndex1": 79,
      "textureIndex2": 1,
      "tileType": 0,
      "diffuseColor": [0.8, 0.8, 1.0]
    }
  ]
}
```

**Conversion Implementation**:
```cpp
// tile_converter.h
struct TileVertex {
    Vector3 position;
    Vector2 uv1;
    Vector2 uv2;
};

struct ConvertedTile {
    std::vector<TileVertex> vertices;
    int textureIndex1;
    int textureIndex2;
    int tileType;
    Vector3 diffuseColor;
};

// Convert from source format, applying scale
ConvertedTile convertTile(const SourceTile& src, float scale);

// Generate Raylib mesh from converted tiles
Mesh createTileMesh(const std::vector<ConvertedTile>& tiles);
```

**Mesh Generation**:
```cpp
Mesh createTileMesh(const std::vector<ConvertedTile>& tiles) {
    // Count triangles (triangle strip: n vertices = n-2 triangles)
    int totalTriangles = 0;
    for (const auto& tile : tiles) {
        if (tile.vertices.size() >= 3) {
            totalTriangles += tile.vertices.size() - 2;
        }
    }

    int vertexCount = totalTriangles * 3;

    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = totalTriangles;
    mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));

    int vi = 0;
    for (const auto& tile : tiles) {
        // Convert triangle strip to triangle list
        for (size_t i = 0; i + 2 < tile.vertices.size(); ++i) {
            // Get vertex indices (flip winding for odd triangles)
            int i0 = (i % 2 == 0) ? i : i + 1;
            int i1 = (i % 2 == 0) ? i + 1 : i;
            int i2 = i + 2;

            // Get transformed positions
            Vector3 p0 = toRenderCoords(tile.vertices[i0].position, SCALE_UNITS_TO_METERS);
            Vector3 p1 = toRenderCoords(tile.vertices[i1].position, SCALE_UNITS_TO_METERS);
            Vector3 p2 = toRenderCoords(tile.vertices[i2].position, SCALE_UNITS_TO_METERS);

            // Compute normal from cross product
            Vector3 edge1 = Vector3Subtract(p1, p0);
            Vector3 edge2 = Vector3Subtract(p2, p0);
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

            // Store vertex data for all 3 vertices of this triangle
            // ... (positions, UVs, normals, colors)

            vi += 3;
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}
```

**Validation**:
- Tiles render with correct lighting (not black)
- Press `2` for normal debug mode - should show varying colors
- Tiles appear at expected positions relative to reference Suzanne

### Phase 3: Write-Then-Reload Workflow

**Goal**: Write converted data to JSON, then reload using shared game code.

**Output Structure**:
```
output/
├── tiles.json          # All converted tile data
├── metadata.json       # Conversion metadata (scale, source path, timestamp)
└── Suzanne.gltf        # Copy of reference model for standalone viewing
```

**Workflow** (critical: reload from disk, don't use in-memory data):
```cpp
bool convertAndView(const char* sourcePath, const char* outputDir) {
    // =========================================
    // PHASE A: CONVERSION (tool-only code)
    // =========================================

    // 1. Parse source data (tool-only: source file parsing)
    SourceData source = parseSourceFile(sourcePath);

    // 2. Convert to JSON format (tool-only: conversion logic)
    nlohmann::json tilesJson = convertTilesToJson(source.tiles, SCALE_UNITS_TO_METERS);

    // 3. Write output files to disk
    std::string tilesPath = std::string(outputDir) + "/tiles.json";
    writeJsonFile(tilesPath, tilesJson);
    writeMetadata(std::string(outputDir) + "/metadata.json", sourcePath, SCALE_UNITS_TO_METERS);

    LOG_INFO("Wrote JSON to: " << tilesPath);
    LOG_INFO(">>> JSON can now be manually inspected <<<");

    // =========================================
    // PHASE B: RELOAD (shared code - same as game)
    // =========================================

    // 4. CRITICAL: Discard in-memory conversion result
    //    Load fresh from disk using shared loader
    //    This is the SAME code path the game will use
    std::vector<LoadedTile> tiles;  // Note: LoadedTile, not ConvertedTile
    if (!loadTilesFromJson(tilesPath, tiles)) {
        LOG_ERROR("Failed to reload tiles from: " << tilesPath);
        return false;
    }

    LOG_INFO("Reloaded " << tiles.size() << " tiles from JSON");

    // 5. Generate mesh from loaded data (shared code)
    Mesh tileMesh = createTileMeshFromLoaded(tiles);

    // 6. Render
    // ...
}
```

**Key Separation**:
```cpp
// === TOOL-ONLY (incremental_viewer/) ===
// Source parsing - reads xmapfile format
SourceTile parseSourceTile(std::ifstream& file);

// Conversion - transforms to JSON-ready format
nlohmann::json convertTilesToJson(const std::vector<SourceTile>& src, float scale);

// === SHARED (shared/scene_data/) ===
// Data structure - used by both tool (after reload) and game
struct LoadedTile {
    std::vector<TileVertex> vertices;
    int textureIndex1;
    int textureIndex2;
    int tileType;
    Vector3 diffuseColor;
};

// JSON loading - identical code in tool and game
bool loadTilesFromJson(const std::string& path, std::vector<LoadedTile>& tiles);

// === SHARED (shared/rendering/) ===
// Mesh generation - identical code in tool and game
Mesh createTileMeshFromLoaded(const std::vector<LoadedTile>& tiles);
```

**Why This Matters**:
1. If JSON is wrong, you can see it in the file before rendering
2. The tool exercises the exact same loading code the game will use
3. Any JSON schema changes are immediately caught
4. No "works in tool, fails in game" surprises

## Command Line Interface

```
incremental_viewer [options] <source_path>

Options:
  -o, --output <dir>     Output directory (default: ./output)
  -s, --scale <factor>   Scale factor override (default: 0.0254)
  --no-reference         Don't load reference model
  --help                 Show help

Examples:
  # Convert and view a domain file
  incremental_viewer ../uber/uberdroid/ship1/xmapfile0.txt -o output/

  # View with custom scale
  incremental_viewer -s 0.01 ../uber/uberdroid/ship1/xmapfile0.txt
```

## Viewer Controls

| Key | Action |
|-----|--------|
| W/S | Move forward/backward |
| A/D | Move left/right |
| Q/E | Move up/down |
| Mouse wheel | Zoom |
| Shift | Move faster |
| R | Reset camera |
| 0 | Normal rendering |
| 1 | Diffuse only |
| 2 | Show normals |
| 3 | Show specular |
| 4 | Show lighting |
| 5 | Show UVs |
| F1 | Toggle grid |
| F2 | Toggle reference model |
| F3 | Toggle tiles |
| F4 | Toggle wireframe overlay |
| H | Toggle help |

## Debug Output

Console logging for each conversion step:
```
[INFO] === CONVERSION STARTED ===
[INFO] Source: ../uber/uberdroid/ship1/xmapfile0.txt
[INFO] Output: output/
[INFO] Scale: 0.0254 (inch to meters)

[INFO] --- PHASE A: PARSING & CONVERSION ---
[INFO] Parsing source file...
[INFO] Found 156 tiles in source
[INFO] Converting tiles to JSON format...
[DEBUG] Tile 0: 4 vertices, texture 79
[DEBUG]   Source v0: (544, 976, 1)
[DEBUG]   Scaled v0: (13.82, 24.79, 0.025)

[INFO] --- WRITING TO DISK ---
[INFO] Writing output/tiles.json...
[INFO] Written 156 tiles to JSON
[INFO] Writing output/metadata.json...
[INFO] >>> Files written - can be manually inspected <<<

[INFO] --- PHASE B: RELOAD (shared code) ---
[INFO] Loading tiles from output/tiles.json...
[INFO] Loaded 156 tiles from JSON (using shared loader)
[INFO] Generating mesh from loaded data...
[DEBUG] Triangle 0 normal: (0, 1, 0)
[DEBUG]   v0: (13.82, 0.025, 24.79)
[DEBUG]   v1: (13.82, 0.025, 26.42)
[DEBUG]   v2: (15.44, 0.025, 24.79)
[INFO] Mesh created: 1872 vertices, 624 triangles

[INFO] === READY FOR VIEWING ===
```

The two-phase logging makes it clear:
- Phase A uses tool-only code (parsing, conversion, writing)
- Phase B uses shared code (loading, mesh generation) - same as game

## Success Criteria

1. **Reference model works**: Suzanne renders at (-1, 0, -1) with correct lighting
2. **JSON is readable**: Output files can be opened and inspected in a text editor
3. **JSON is correct**: Manual inspection shows expected vertex positions and tile counts
4. **Reload succeeds**: Shared loader successfully parses the written JSON
5. **Tiles are visible**: Not black, properly lit after reload
6. **Normals correct**: Debug mode 2 shows varying colors based on triangle orientation
7. **Scale reasonable**: Tiles appear at sensible size relative to Suzanne
8. **No stale data**: Changing source and re-running shows updated geometry
9. **Game compatibility**: JSON can be loaded by game using identical shared code

## Future Phases (Out of Scope)

After tile rendering is validated:
- Phase 4: Archetile expansion
- Phase 5: Texture loading and binding
- Phase 6: Procedural geometry (XML paths)
- Phase 7: Features (render objects)
- Phase 8: Full domain conversion
- Phase 9: Ship-level navigation

## File Locations

| Item | Path |
|------|------|
| New tool | `cpp-version/tools/incremental_viewer/` |
| Archive old tool | `cpp-version/tools/scene_tool_archive/` |
| Shared scene data | `cpp-version/shared/scene_data/` |
| Shared renderer | `cpp-version/shared/rendering/scene_renderer.*` |
| Shared tile mesh | `cpp-version/shared/rendering/tile_mesh.*` |
| Shaders | `cpp-version/assets/shaders/` |
| Reference model | `cpp-version/assets/models/Suzanne.gltf` |
| Output JSON | `cpp-version/tools/incremental_viewer/build/output/` |

### Shared Code Files (used by both tool and game)

| File | Purpose |
|------|---------|
| `shared/scene_data/tile_types.h` | `LoadedTile`, `TileVertex` structs |
| `shared/scene_data/tile_loader.h` | `loadTilesFromJson()` declaration |
| `shared/scene_data/tile_loader.cpp` | JSON parsing implementation |
| `shared/rendering/tile_mesh.h` | `createTileMeshFromLoaded()` declaration |
| `shared/rendering/tile_mesh.cpp` | Mesh generation from loaded tiles |

### Tool-Only Code Files (not used by game)

| File | Purpose |
|------|---------|
| `incremental_viewer/tile_converter.h` | Source parsing, JSON conversion |
| `incremental_viewer/tile_converter.cpp` | `parseSourceFile()`, `convertTilesToJson()` |

---

## Appendix A: Easy Improvements

The following improvements were identified during Phase 2 implementation and can be added incrementally without architectural changes.

### A.1 Hot Reload

**Effort**: Low
**Value**: High for iteration speed

Add file watching to automatically reload JSON when files change on disk:

```cpp
// In viewer.cpp
void viewerUpdate(Viewer* viewer, float deltaTime) {
    // Check file modification time every N frames
    static int frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        auto modTime = fs::last_write_time(viewer->jsonPath);
        if (modTime != viewer->lastModTime) {
            viewer->lastModTime = modTime;
            viewerReloadFromJson(viewer, viewer->jsonPath.c_str());
            TraceLog(LOG_INFO, "Hot reloaded: %s", viewer->jsonPath.c_str());
        }
    }
}
```

### A.2 Texture Batching

**Effort**: Medium
**Value**: High for performance

Currently all tiles render in a single mesh with vertex colors. Add texture batching:

1. Sort tiles by texture index
2. Create separate meshes per unique texture combination
3. Load textures from `texture_mapping.json`
4. Render each batch with appropriate texture bound

```cpp
// tile_mesh.h
struct TileBatch {
    Mesh mesh;
    int textureIndex1;
    int textureIndex2;
};

std::vector<TileBatch> createBatchedTileMeshes(
    const std::vector<Tile>& tiles,
    float scale
);
```

### A.3 Area Culling

**Effort**: Low
**Value**: Medium for large scenes

Only render tiles within currently visible areas:

```cpp
// viewer.cpp
void viewerRender(Viewer* viewer) {
    for (const auto& area : viewer->loadedDomain.areas) {
        // Frustum culling per area
        if (isAreaVisible(viewer->camera, area.bounds)) {
            DrawModel(viewer->areaMeshes[area.index], ...);
        }
    }
}
```

### A.4 Feature/Object Rendering

**Effort**: Medium
**Value**: High for validation

Render features and objects with placeholder geometry:

```cpp
// In viewerRender after tiles
if (viewer->toggles.showFeatures) {
    for (const auto& area : viewer->loadedDomain.areas) {
        for (const auto& feature : area.features) {
            Vector3 pos = toRenderCoords(feature.position, viewer->scale);
            DrawCube(pos, 0.5f, 0.5f, 0.5f, RED);
        }
    }
}
```

### A.5 JSON Schema Validation

**Effort**: Low
**Value**: Medium for debugging

Add validation during JSON load:

```cpp
// scene_json.cpp
bool validateDomainJson(const json& j, std::string& errorMsg) {
    if (!j.contains("areas")) {
        errorMsg = "Missing 'areas' array";
        return false;
    }
    for (const auto& area : j["areas"]) {
        if (!area.contains("tiles")) {
            errorMsg = "Area missing 'tiles' array";
            return false;
        }
    }
    return true;
}
```

### A.6 Per-Area Mesh Generation

**Effort**: Low
**Value**: Medium for memory/debugging

Generate separate meshes per area instead of one combined mesh:

```cpp
// Current: single domain mesh
TileMeshResult createDomainMesh(const Domain& domain, float scale);

// Improved: per-area meshes
std::vector<TileMeshResult> createAreaMeshes(const Domain& domain, float scale);
```

Benefits:
- Easier debugging (can toggle areas individually)
- Better memory management (unload distant areas)
- Enables area culling (A.3)

### A.7 Waypoint Visualization

**Effort**: Low
**Value**: Medium for navigation validation

Render waypoints and connections:

```cpp
if (viewer->toggles.showWaypoints) {
    for (const auto& wp : viewer->loadedDomain.waypoints) {
        Vector3 pos = toRenderCoords(wp.position, viewer->scale);
        DrawSphere(pos, 0.1f, GREEN);

        // Draw connections to neighbors
        for (int neighborId : wp.neighbors) {
            if (neighborId > 0) {
                const auto& neighbor = findWaypoint(neighborId);
                Vector3 nPos = toRenderCoords(neighbor.position, viewer->scale);
                DrawLine3D(pos, nPos, LIME);
            }
        }
    }
}
```

### A.8 Domain Bounds Visualization

**Effort**: Very Low
**Value**: Low but helpful for debugging

Show domain/area bounding boxes:

```cpp
if (viewer->toggles.showBounds) {
    BoundingBox domainBox = {
        toRenderCoords(viewer->loadedDomain.bounds.min, viewer->scale),
        toRenderCoords(viewer->loadedDomain.bounds.max, viewer->scale)
    };
    DrawBoundingBox(domainBox, YELLOW);
}
```

### A.9 Export to GLTF

**Effort**: High
**Value**: Very High for external tools

Export converted domain data to GLTF format for validation in external tools (Blender, etc.):

```cpp
bool exportDomainToGltf(const Domain& domain, const char* outputPath, float scale);
```

This would use the existing tinygltf library already in dependencies.

### A.10 Unit Test Coverage

**Effort**: Medium
**Value**: High for reliability

Current tests cover:
- [x] Archetile parsing
- [x] Archetile cache
- [x] Domain parsing
- [x] JSON round-trip
- [x] Coordinate transforms

Additional tests needed:
- [ ] Mesh vertex count validation
- [ ] Triangle winding verification
- [ ] Bounds calculation accuracy
- [ ] Empty domain handling
- [ ] Malformed JSON error handling

### Priority Ranking

1. **Hot Reload (A.1)** - Essential for rapid iteration
2. **Feature Rendering (A.4)** - Important for full scene validation
3. **Per-Area Meshes (A.6)** - Enables culling and better debugging
4. **Waypoint Visualization (A.7)** - Validates navigation data
5. **Texture Batching (A.2)** - Performance for large scenes
6. **GLTF Export (A.9)** - External validation
7. **Schema Validation (A.5)** - Better error messages
8. **Area Culling (A.3)** - Performance optimization
9. **Bounds Visualization (A.8)** - Quick debugging aid
10. **Unit Tests (A.10)** - Ongoing quality
