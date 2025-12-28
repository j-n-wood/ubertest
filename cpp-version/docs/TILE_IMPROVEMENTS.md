# Tile System Improvements

This document outlines potential improvements to the tile-based data structures and rendering system. Items are organized by priority and complexity.

## Current Architecture Summary

- **Data structures**: `Tile`, `TileVertex`, `TileProperties` in `scene_types.h`
- **Mesh generation**: `tile_mesh.cpp` creates batched Raylib meshes from tile data
- **Texture loading**: `texture_loader.cpp` parses `textures.txt` and manages texture cache
- **Rendering**: Tiles are rendered as batched meshes grouped by texture indices

---

## High Priority

### 1. UV Coordinate Validation and Debugging

**Problem**: Texture mapping issues are difficult to diagnose. UV coordinates may be incorrect after coordinate system transforms.

**Suggestions**:
- Add UV bounds checking in `createMeshFromTiles()` - warn if UVs are outside [0,1] or have unusual ranges
- Add debug visualization mode to render UV coordinates as colors (u=red, v=green)
- Log min/max UV ranges per batch during mesh creation

### 2. Texture Atlas Support

**Problem**: Each unique texture pair creates a separate batch, leading to many draw calls on complex scenes.

**Suggestions**:
- Implement texture atlas generation that packs multiple tile textures into one texture
- Remap UVs to atlas coordinates during mesh creation
- Could significantly reduce batch count from hundreds to a handful
- Consider separate atlases for diffuse and normal maps

### 3. Tile Type Handling

**Problem**: `TileProperties.tileType` is stored but not used for rendering decisions.

**Suggestions**:
- Define tile type enum: `FLOOR`, `WALL`, `CEILING`, `RAMP`, etc.
- Generate appropriate normals per tile type instead of hardcoded UP
- Handle walls with proper tangent/normal orientation
- Support non-horizontal tiles (ramps, angled surfaces)

---

## Medium Priority

### 4. Level-of-Detail (LOD) System

**Problem**: All tiles render at full detail regardless of camera distance.

**Suggestions**:
- Implement distance-based LOD for tile batches
- Far tiles could use simplified geometry or merged quads
- Consider texture mipmapping for distant tiles
- Frustum culling per batch (currently renders everything)

### 5. Dynamic Tile Updates

**Problem**: Any tile change requires rebuilding the entire batch mesh.

**Suggestions**:
- Implement dirty flags per batch for incremental updates
- Support single-tile updates without full mesh rebuild
- Consider instanced rendering for tiles with identical geometry
- Separate static vs dynamic tiles (doors, destructibles could animate)

### 6. Memory Optimization

**Problem**: Vertex data is duplicated per tile (4 vertices each).

**Suggestions**:
- Consider shared vertex pool for adjacent tiles
- Tiles sharing edges could share vertices (complex but significant savings)
- Evaluate 16-bit vs 32-bit index buffers based on tile count
- Current: `unsigned short` limits to 65536 vertices (~16K tiles per batch)

### 7. Vertex Color Usage

**Problem**: Diffuse color is stored per-vertex but could be per-tile.

**Suggestions**:
- If all 4 vertices have same color (common case), store once per tile
- Support vertex color gradients for lighting effects (ambient occlusion baking)
- Consider storing tile ID in vertex color alpha for picking/debugging

---

## Lower Priority / Future

### 8. Instanced Rendering

**Problem**: Many tiles share identical texture and properties but are rendered individually.

**Suggestions**:
- Group identical tiles (same texture, same size) for instanced rendering
- Store per-instance transform in instance buffer
- Could dramatically reduce draw calls for repetitive floor patterns

### 9. Normal Map Quality

**Problem**: Tangent vectors assume axis-aligned tiles.

**Suggestions**:
- Compute tangents from actual UV mapping direction
- Support rotated tiles (`texRotate` property exists but may not affect tangents)
- Consider mikktspace tangent generation for better normal map compatibility

### 10. Effect Texture Support

**Problem**: `TileTextures.effect` and related properties (`effectTexture`, `effectRenderMode`, `effectBlendSource`, `effectBlendDest`) are parsed but not rendered.

**Suggestions**:
- Implement third texture slot for special effects
- Support additive/alpha blending modes per tile (`additiveBlend`, `alphaBlend` properties)
- Handle animated effect textures (scrolling, pulsing)
- Separate effect tiles into transparency-sorted batch

### 11. Collision Data Integration

**Problem**: `CollisionData` is stored separately from tile rendering.

**Suggestions**:
- Generate collision shapes from tile geometry automatically
- Debug visualization of collision bounds
- Ensure collision and render geometry stay synchronized

### 12. Streaming and Chunking

**Problem**: Entire domain is loaded and rendered at once.

**Suggestions**:
- Divide large domains into spatial chunks
- Load/unload chunks based on camera position
- Stream tile data for very large levels
- Consider octree or quadtree spatial organization

---

## Data Structure Suggestions

### TileVertex Optimization

```cpp
// Current: 36 bytes per vertex
struct TileVertex {
    Vector3 position;  // 12 bytes
    Vector2 uv1;       // 8 bytes
    Vector2 uv2;       // 8 bytes (often unused)
};

// Proposed: 20 bytes per vertex (if uv2 rarely used)
struct TileVertexCompact {
    Vector3 position;  // 12 bytes
    Vector2 uv;        // 8 bytes
};
// Store uv2 separately only for tiles that need it
```

### Batch Key Enhancement

```cpp
// Current
struct TileBatchKey {
    int textureIndex1;
    int textureIndex2;
};

// Proposed: Include render state
struct TileBatchKey {
    int textureIndex1;
    int textureIndex2;
    int effectTexture;
    uint8_t blendMode;    // 0=opaque, 1=alpha, 2=additive
    uint8_t tileType;     // floor, wall, etc.
};
```

### Spatial Organization

```cpp
// Proposed: Chunk-based tile storage
struct TileChunk {
    Vector2 chunkCoord;           // Grid position
    Bounds bounds;                // World bounds
    std::vector<Tile> tiles;
    TileBatchCollection batches;  // Pre-built meshes
    bool visible;                 // Frustum cull flag
    bool dirty;                   // Needs rebuild
};

struct ChunkedDomain {
    Vector2 chunkSize;            // e.g., 10x10 meters
    std::map<std::pair<int,int>, TileChunk> chunks;
};
```

---

## Performance Metrics to Track

When implementing improvements, measure:

1. **Draw calls per frame** - Target: < 100 for typical scene
2. **Texture binds per frame** - Target: < 50
3. **Triangle count** - Current: 2 per tile (optimal)
4. **Vertex count** - Current: 4 per tile (could share with adjacent)
5. **Batch count** - Current: one per texture pair
6. **Memory usage** - Vertex/index buffer sizes
7. **Load time** - JSON parse + mesh generation
8. **Frame time** - Render loop performance

---

## Implementation Order Recommendation

1. **UV validation/debugging** - Low effort, high diagnostic value
2. **Tile type handling** - Enables walls and non-floor geometry
3. **Effect texture support** - Uses existing parsed data
4. **Texture atlas** - Significant performance improvement
5. **Frustum culling** - Quick win for large scenes
6. **LOD system** - Performance for distant geometry
7. **Chunking** - Required for very large levels
8. **Instancing** - Optimization for repetitive patterns
