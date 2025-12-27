# Tile Batching Implementation Plan

This document describes the implementation plan for optimized tile rendering using texture atlases, vertex deduplication, and spatial chunking.

## Goals

1. **Single draw call per chunk** - Combine all tiles into unified geometry with shared texture bindings
2. **Texture atlases** - Pack diffuse and bump textures into two atlases
3. **Dual UV coordinates** - Maintain separate UVs for diffuse and bump maps (atlas remapped)
4. **Vertex deduplication** - Share vertices between adjacent tiles
5. **Spatial chunking** - Subdivide by XZ extents for frustum culling

---

## Phase 1: Texture Atlas Generation

### 1.1 Atlas Builder Data Structures

```cpp
// Atlas slot for one texture
struct AtlasSlot {
    int originalIndex;      // Original texture index from textures.txt
    int atlasX, atlasY;     // Position in atlas (pixels)
    int width, height;      // Texture dimensions
    float u0, v0, u1, v1;   // Normalized UV bounds in atlas [0,1]
};

// Complete atlas
struct TextureAtlas {
    Texture2D texture;              // GPU texture
    int width, height;              // Atlas dimensions
    std::vector<AtlasSlot> slots;   // All packed textures
    std::unordered_map<int, size_t> indexToSlot;  // originalIndex -> slot index
};

// Atlas pair (diffuse + bump)
struct TileAtlasPair {
    TextureAtlas diffuse;
    TextureAtlas bump;
    bool valid;
};
```

### 1.2 Atlas Packing Algorithm

Use a simple shelf-packing or binary tree rect packing algorithm:

1. **Collect unique textures** - Scan all tiles for unique `textureIndex1` (diffuse) and `textureIndex2` (bump)
2. **Load source images** - Load each unique texture as `Image` (CPU-side)
3. **Sort by height** - Descending order for better shelf packing
4. **Pack into atlas** - Place textures in rows (shelves), advance Y when row full
5. **Generate atlas texture** - Create `Image` of required size, blit all textures, upload to GPU
6. **Record UV mappings** - Store normalized coordinates for each slot

```cpp
// Proposed API
TileAtlasPair buildTileAtlases(const TextureLookup& lookup,
                                const std::vector<int>& diffuseIndices,
                                const std::vector<int>& bumpIndices,
                                int maxAtlasSize = 4096);

// UV remapping helper
Vector4 getAtlasUVBounds(const TextureAtlas& atlas, int originalIndex);
// Returns (u0, v0, u1, v1) for the texture in atlas coordinates
```

### 1.3 Atlas Size Considerations

| Scenario | Estimated Textures | Recommended Atlas |
|----------|-------------------|-------------------|
| Single domain | 20-50 | 2048x2048 |
| Full ship | 100-200 | 4096x4096 |
| With mipmaps | Any | Power-of-two, max 4096 |

If textures don't fit in single atlas, create multiple atlases and group tiles by atlas assignment (still better than per-texture batching).

### 1.4 Flat Bump Map Handling

- Texture index 0 maps to `flat_normal.png`
- Include flat normal in bump atlas at known location
- Tiles with `textureIndex2 == 0` use this slot's UV coordinates

---

## Phase 2: Dual UV Coordinate System

### 2.1 Vertex Structure

```cpp
struct BatchedTileVertex {
    Vector3 position;       // 12 bytes
    Vector3 normal;         // 12 bytes (hardcoded UP for floors)
    Vector4 tangent;        // 16 bytes (handedness in w)
    Vector2 uvDiffuse;      // 8 bytes - remapped to diffuse atlas
    Vector2 uvBump;         // 8 bytes - remapped to bump atlas
    Color color;            // 4 bytes - vertex color
};
// Total: 60 bytes per vertex
```

### 2.2 UV Remapping

When building mesh, transform original tile UVs to atlas coordinates:

```cpp
Vector2 remapUV(Vector2 originalUV, const AtlasSlot& slot) {
    // originalUV is [0,1] within original texture
    // slot.u0/v0/u1/v1 are atlas coordinates
    return {
        slot.u0 + originalUV.x * (slot.u1 - slot.u0),
        slot.v0 + originalUV.y * (slot.v1 - slot.v0)
    };
}
```

### 2.3 Shader Modifications

Update `lighting.fs` to sample from atlas:

```glsl
// Already uses texture0 for diffuse, texture2 for normal
// No shader changes needed if UVs are pre-remapped

// If using separate UV attributes:
in vec2 fragTexCoordDiffuse;  // For diffuse atlas
in vec2 fragTexCoordBump;     // For bump atlas

// Sample using respective UVs
vec4 texelColor = texture(texture0, fragTexCoordDiffuse);
vec3 normalMapSample = texture(texture2, fragTexCoordBump).rgb;
```

**Decision**: Pre-remap UVs in vertex data (simpler, no shader changes) vs. pass atlas bounds as uniforms (more flexible). Recommend pre-remapping.

---

## Phase 3: Vertex Deduplication

### 3.1 Spatial Hashing

Adjacent tiles sharing edges can share vertices. Use spatial hash to find duplicates:

```cpp
struct VertexKey {
    int32_t px, py, pz;  // Quantized position (multiply by 1000, round)
    int32_t nx, ny, nz;  // Quantized normal

    bool operator==(const VertexKey& other) const;
    size_t hash() const;
};

std::unordered_map<VertexKey, uint32_t> vertexMap;  // key -> index
```

### 3.2 Deduplication Process

```cpp
uint32_t addOrGetVertex(const BatchedTileVertex& vertex,
                        std::vector<BatchedTileVertex>& vertices,
                        std::unordered_map<VertexKey, uint32_t>& vertexMap) {
    VertexKey key = makeKey(vertex);

    auto it = vertexMap.find(key);
    if (it != vertexMap.end()) {
        return it->second;  // Existing vertex
    }

    uint32_t index = static_cast<uint32_t>(vertices.size());
    vertices.push_back(vertex);
    vertexMap[key] = index;
    return index;
}
```

### 3.3 UV Compatibility Check

Vertices can only be shared if UVs match. For atlas-remapped UVs, adjacent tiles with same texture will have compatible UVs at shared edges.

**Limitation**: Tiles with different textures cannot share vertices (different atlas UVs). This is acceptable - most deduplication comes from same-texture adjacency.

### 3.4 Expected Savings

| Scenario | Without Dedup | With Dedup | Savings |
|----------|---------------|------------|---------|
| Grid of same-texture tiles | 4 verts/tile | ~1 vert/tile | 75% |
| Mixed textures | 4 verts/tile | ~2-3 verts/tile | 25-50% |
| Sparse tiles | 4 verts/tile | 4 verts/tile | 0% |

---

## Phase 4: Spatial Chunking

### 4.1 Chunk Structure

```cpp
struct TileChunk {
    // Spatial bounds
    Vector2 gridCoord;      // Chunk grid position
    BoundingBox bounds;     // World-space AABB

    // Geometry
    Mesh mesh;              // Combined tile mesh
    Model model;            // Raylib model wrapper
    int tileCount;
    int vertexCount;
    int indexCount;

    // State
    bool visible;           // Frustum cull flag
    bool dirty;             // Needs rebuild
};

struct ChunkedTileMesh {
    std::vector<TileChunk> chunks;
    TileAtlasPair atlases;
    Vector2 chunkSize;      // World units per chunk
    int totalTiles;
    int totalVertices;
};
```

### 4.2 Chunk Size Determination

Balance between culling granularity and overhead:

```cpp
Vector2 calculateChunkSize(const std::vector<Tile>& tiles,
                           const BoundingBox& totalBounds) {
    // Parameters
    const int TARGET_TILES_PER_CHUNK = 500;
    const int MIN_CHUNKS = 4;
    const int MAX_CHUNKS = 64;
    const float MIN_CHUNK_SIZE = 5.0f;   // meters
    const float MAX_CHUNK_SIZE = 50.0f;  // meters

    // Calculate bounds extent
    float extentX = totalBounds.max.x - totalBounds.min.x;
    float extentZ = totalBounds.max.z - totalBounds.min.z;

    // Estimate chunks needed based on tile count
    int estimatedChunks = std::max(1, (int)tiles.size() / TARGET_TILES_PER_CHUNK);
    estimatedChunks = std::clamp(estimatedChunks, MIN_CHUNKS, MAX_CHUNKS);

    // Calculate chunk dimensions (prefer square-ish chunks)
    float aspect = extentX / std::max(extentZ, 0.001f);
    int chunksX = std::max(1, (int)std::sqrt(estimatedChunks * aspect));
    int chunksZ = std::max(1, estimatedChunks / chunksX);

    Vector2 chunkSize = {
        std::clamp(extentX / chunksX, MIN_CHUNK_SIZE, MAX_CHUNK_SIZE),
        std::clamp(extentZ / chunksZ, MIN_CHUNK_SIZE, MAX_CHUNK_SIZE)
    };

    return chunkSize;
}
```

### 4.3 Tile-to-Chunk Assignment

```cpp
int getTileChunkIndex(const Tile& tile, Vector2 chunkSize,
                      Vector2 boundsMin, int chunksX) {
    // Use tile center for assignment
    Vector3 center = getTileCenter(tile);
    int cx = (int)((center.x - boundsMin.x) / chunkSize.x);
    int cz = (int)((center.z - boundsMin.y) / chunkSize.y);
    return cz * chunksX + cx;
}
```

### 4.4 Sparse Area Handling

For domains with few tiles spread over large areas:

```cpp
bool shouldMergeChunks(const std::vector<int>& tileCounts,
                       int targetMinTiles = 50) {
    // If most chunks have < targetMinTiles, consider larger chunks
    int sparseChunks = 0;
    for (int count : tileCounts) {
        if (count > 0 && count < targetMinTiles) sparseChunks++;
    }
    return sparseChunks > tileCounts.size() / 2;
}
```

---

## Phase 5: Integration

### 5.1 Complete Build Pipeline

```cpp
ChunkedTileMesh buildChunkedTileMesh(const Domain& domain,
                                      const TextureLookup& textureLookup) {
    ChunkedTileMesh result = {};

    // 1. Collect all tiles
    std::vector<Tile> allTiles;
    for (const auto& area : domain.areas) {
        allTiles.insert(allTiles.end(), area.tiles.begin(), area.tiles.end());
    }
    if (allTiles.empty()) return result;

    // 2. Collect unique texture indices
    std::set<int> diffuseIndices, bumpIndices;
    for (const auto& tile : allTiles) {
        diffuseIndices.insert(tile.textureIndex1);
        bumpIndices.insert(tile.textureIndex2);
    }

    // 3. Build texture atlases
    result.atlases = buildTileAtlases(textureLookup,
        std::vector<int>(diffuseIndices.begin(), diffuseIndices.end()),
        std::vector<int>(bumpIndices.begin(), bumpIndices.end()));

    // 4. Calculate bounds and chunk size
    BoundingBox totalBounds = calculateTileBounds(allTiles);
    result.chunkSize = calculateChunkSize(allTiles, totalBounds);

    // 5. Assign tiles to chunks
    int chunksX = (int)ceil((totalBounds.max.x - totalBounds.min.x) / result.chunkSize.x);
    int chunksZ = (int)ceil((totalBounds.max.z - totalBounds.min.z) / result.chunkSize.y);
    std::vector<std::vector<const Tile*>> chunkTiles(chunksX * chunksZ);

    for (const auto& tile : allTiles) {
        int chunkIdx = getTileChunkIndex(tile, result.chunkSize,
            {totalBounds.min.x, totalBounds.min.z}, chunksX);
        chunkTiles[chunkIdx].push_back(&tile);
    }

    // 6. Build mesh for each non-empty chunk
    for (int i = 0; i < chunkTiles.size(); i++) {
        if (chunkTiles[i].empty()) continue;

        TileChunk chunk = buildChunk(chunkTiles[i], result.atlases, i,
                                      chunksX, result.chunkSize, totalBounds.min);
        result.chunks.push_back(chunk);
        result.totalTiles += chunk.tileCount;
        result.totalVertices += chunk.vertexCount;
    }

    return result;
}
```

### 5.2 Chunk Mesh Building (with deduplication)

```cpp
TileChunk buildChunk(const std::vector<const Tile*>& tiles,
                     const TileAtlasPair& atlases,
                     int chunkIndex, int chunksX,
                     Vector2 chunkSize, Vector2 boundsMin) {
    TileChunk chunk = {};
    chunk.gridCoord = {(float)(chunkIndex % chunksX), (float)(chunkIndex / chunksX)};

    // Vertex deduplication map
    std::unordered_map<VertexKey, uint32_t> vertexMap;
    std::vector<BatchedTileVertex> vertices;
    std::vector<uint32_t> indices;

    // Process each tile
    for (const Tile* tile : tiles) {
        // Get atlas slots for this tile's textures
        AtlasSlot diffuseSlot = getAtlasSlot(atlases.diffuse, tile->textureIndex1);
        AtlasSlot bumpSlot = getAtlasSlot(atlases.bump, tile->textureIndex2);

        // Add 4 vertices (potentially deduplicated)
        uint32_t vi[4];
        for (int i = 0; i < 4; i++) {
            BatchedTileVertex v;
            v.position = tile->vertices[i].position;
            v.normal = UP_NORMAL;
            v.tangent = FLOOR_TANGENT;
            v.uvDiffuse = remapUV(tile->vertices[i].uv1, diffuseSlot);
            v.uvBump = remapUV(tile->vertices[i].uv2.x > 0 ? tile->vertices[i].uv2 : tile->vertices[i].uv1, bumpSlot);
            v.color = tileColorToRGBA(tile->properties.diffuseColour);

            vi[i] = addOrGetVertex(v, vertices, vertexMap);
        }

        // Add indices (2 triangles, CW from above)
        indices.push_back(vi[0]); indices.push_back(vi[2]); indices.push_back(vi[1]);
        indices.push_back(vi[0]); indices.push_back(vi[3]); indices.push_back(vi[2]);
    }

    // Create mesh
    chunk.mesh = createMeshFromBatchedVertices(vertices, indices);
    chunk.tileCount = tiles.size();
    chunk.vertexCount = vertices.size();
    chunk.indexCount = indices.size();

    // Calculate bounds
    chunk.bounds = calculateChunkBounds(chunk.gridCoord, chunkSize, boundsMin);

    // Create model and apply shader
    chunk.model = LoadModelFromMesh(chunk.mesh);
    // Textures and shader applied by caller

    return chunk;
}
```

### 5.3 Rendering with Frustum Culling

```cpp
void renderChunkedTiles(ChunkedTileMesh& tileMesh,
                        const Camera3D& camera,
                        const SceneRenderer& renderer) {
    // Calculate frustum from camera
    Frustum frustum = calculateFrustum(camera);

    int visibleChunks = 0;
    int culledChunks = 0;

    for (auto& chunk : tileMesh.chunks) {
        // Frustum test
        chunk.visible = frustumContainsBox(frustum, chunk.bounds);

        if (chunk.visible) {
            DrawModel(chunk.model, Vector3Zero(), 1.0f, WHITE);
            visibleChunks++;
        } else {
            culledChunks++;
        }
    }

    // Optional: log cull stats
    // TraceLog(LOG_DEBUG, "Chunks: %d visible, %d culled", visibleChunks, culledChunks);
}
```

---

## Implementation Order

### Step 1: Atlas Builder (texture_atlas.h/cpp)
- [ ] Define `AtlasSlot`, `TextureAtlas`, `TileAtlasPair` structs
- [ ] Implement shelf-packing algorithm
- [ ] Implement `buildTileAtlases()` function
- [ ] Test with sample textures, verify UV bounds

### Step 2: UV Remapping
- [ ] Implement `remapUV()` helper
- [ ] Update `BatchedTileVertex` struct with dual UVs
- [ ] Create mesh builder that applies atlas UV remapping

### Step 3: Vertex Deduplication
- [ ] Define `VertexKey` with hash function
- [ ] Implement `addOrGetVertex()` with spatial hashing
- [ ] Integrate into mesh builder
- [ ] Measure vertex count reduction

### Step 4: Spatial Chunking
- [ ] Define `TileChunk`, `ChunkedTileMesh` structs
- [ ] Implement `calculateChunkSize()` heuristic
- [ ] Implement tile-to-chunk assignment
- [ ] Build per-chunk meshes with deduplication

### Step 5: Integration
- [ ] Implement `buildChunkedTileMesh()` complete pipeline
- [ ] Add frustum culling in render loop
- [ ] Update viewer to use new system
- [ ] Verify texture sampling in shader

### Step 6: Optimization
- [ ] Profile with large domains
- [ ] Tune chunk size parameters
- [ ] Consider LOD for distant chunks
- [ ] Add statistics overlay (chunks, draw calls, vertices)

---

## File Structure

```
shared/rendering/
├── texture_atlas.h          # Atlas structures and API
├── texture_atlas.cpp        # Atlas packing implementation
├── chunked_tile_mesh.h      # Chunk structures and API
├── chunked_tile_mesh.cpp    # Chunking and mesh building
├── tile_mesh.h              # (existing, kept for compatibility)
└── tile_mesh.cpp            # (existing, kept for compatibility)
```

---

## Performance Expectations

| Metric | Current | After Implementation |
|--------|---------|---------------------|
| Draw calls per domain | 50-200 (one per texture pair) | 4-16 (one per visible chunk) |
| Texture binds | 100-400 | 2 (atlas pair) |
| Vertices per tile | 4 | ~1.5 (with dedup) |
| Frustum culling | None | Per-chunk AABB |

---

## Fallback Strategy

If atlas building fails (textures too large, incompatible formats):
1. Log warning with specific texture indices
2. Fall back to current per-texture batching for affected tiles
3. Continue with atlas for compatible textures

This ensures graceful degradation rather than complete failure.
