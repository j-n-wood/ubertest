#include "level_renderer.h"
#include "tileset_loader.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include <set>

//------------------------------------------------------------------------------
// Coordinate Transforms
//------------------------------------------------------------------------------

Vector3 tmxGridToWorld(int col, int row, const TmxLevel& level, float worldScale) {
    // Center the level around origin
    // TMX: origin top-left, X right, Y down
    // World: origin center, X right, Y up (height), Z forward (into screen)
    //
    // Convention: TMX row 0 (top) -> +Z, TMX row N (bottom) -> -Z
    // This way, when viewing top-down from +Y looking at -Y,
    // the level appears as it does in Tiled (row 0 at top of screen)
    float halfWidth = level.width * worldScale * 0.5f;
    float halfHeight = level.height * worldScale * 0.5f;

    float worldX = (col + 0.5f) * worldScale - halfWidth;
    float worldY = 0.0f;  // Tiles on floor plane
    float worldZ = (row + 0.5f) * worldScale - halfHeight;  // TMX Y down -> World Z increases

    return {worldX, worldY, worldZ};
}

Vector3 tmxPixelToWorld(float x, float y, const TmxLevel& level, float worldScale) {
    // Convert pixel coords to grid coords, then to world
    float col = x / level.tileWidth;
    float row = y / level.tileHeight;

    float halfWidth = level.width * worldScale * 0.5f;
    float halfHeight = level.height * worldScale * 0.5f;

    float worldX = col * worldScale - halfWidth;
    float worldY = 0.0f;
    float worldZ = row * worldScale - halfHeight;  // TMX Y down -> World Z increases

    return {worldX, worldY, worldZ};
}

//------------------------------------------------------------------------------
// Mesh Generation
//------------------------------------------------------------------------------

Mesh createLevelTileMesh(
    const TmxLevel& level,
    const TmxTileset& tileset,
    float worldScale
) {
    Mesh mesh = {0};

    // Count non-empty tiles
    int tileCount = 0;
    for (int id : level.tiles) {
        if (id > 0) tileCount++;
    }

    if (tileCount == 0) {
        TraceLog(LOG_WARNING, "Level has no tiles to render");
        return mesh;
    }

    // Each tile = 4 vertices, 6 indices (2 triangles)
    int vertexCount = tileCount * 4;
    int triangleCount = tileCount * 2;

    // Allocate mesh data
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;

    mesh.vertices = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
    mesh.tangents = (float*)RL_MALLOC(vertexCount * 4 * sizeof(float));
    mesh.indices = (unsigned short*)RL_MALLOC(triangleCount * 3 * sizeof(unsigned short));

    // Half tile size in world units
    float halfTile = worldScale * 0.5f;

    // Fill mesh data
    int vi = 0;  // Vertex index
    int ii = 0;  // Index index

    for (int row = 0; row < level.height; row++) {
        for (int col = 0; col < level.width; col++) {
            int tileIdx = row * level.width + col;
            int tileId = level.tiles[tileIdx];

            if (tileId <= 0) continue;  // Skip empty tiles

            // Get tile center in world space
            Vector3 center = tmxGridToWorld(col, row, level, worldScale);

            // Get UV coordinates for this tile
            float u0, v0, u1, v1;
            getTileUV(tileset, tileId, &u0, &v0, &u1, &v1);

            // Vertex base index for this tile
            int baseVert = vi;

            // Generate 4 vertices (CCW from above when looking down +Y to -Y)
            // BL, BR, TR, TL
            float positions[4][3] = {
                {center.x - halfTile, 0.0f, center.z - halfTile},  // BL (0)
                {center.x + halfTile, 0.0f, center.z - halfTile},  // BR (1)
                {center.x + halfTile, 0.0f, center.z + halfTile},  // TR (2)
                {center.x - halfTile, 0.0f, center.z + halfTile},  // TL (3)
            };

            // UV coordinates (matching vertex order)
            // TMX row 0 is at top of texture, maps to +Z in world
            // So tile at +Z needs top of texture (v0), tile at -Z needs bottom (v1)
            float uvs[4][2] = {
                {u0, v0},  // BL (world -Z side = TMX top = texture top)
                {u1, v0},  // BR
                {u1, v1},  // TR (world +Z side = TMX bottom = texture bottom)
                {u0, v1},  // TL
            };

            // All floor tiles face up
            float normal[3] = {0.0f, 1.0f, 0.0f};
            float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};

            for (int v = 0; v < 4; v++) {
                int idx = vi * 3;
                mesh.vertices[idx + 0] = positions[v][0];
                mesh.vertices[idx + 1] = positions[v][1];
                mesh.vertices[idx + 2] = positions[v][2];

                idx = vi * 2;
                mesh.texcoords[idx + 0] = uvs[v][0];
                mesh.texcoords[idx + 1] = uvs[v][1];

                idx = vi * 3;
                mesh.normals[idx + 0] = normal[0];
                mesh.normals[idx + 1] = normal[1];
                mesh.normals[idx + 2] = normal[2];

                idx = vi * 4;
                mesh.tangents[idx + 0] = tangent[0];
                mesh.tangents[idx + 1] = tangent[1];
                mesh.tangents[idx + 2] = tangent[2];
                mesh.tangents[idx + 3] = tangent[3];

                vi++;
            }

            // Triangle indices (CW when viewed from above for correct backface culling)
            // Triangle 0: BL(0), TR(2), BR(1)
            // Triangle 1: BL(0), TL(3), TR(2)
            mesh.indices[ii++] = baseVert + 0;
            mesh.indices[ii++] = baseVert + 2;
            mesh.indices[ii++] = baseVert + 1;

            mesh.indices[ii++] = baseVert + 0;
            mesh.indices[ii++] = baseVert + 3;
            mesh.indices[ii++] = baseVert + 2;
        }
    }

    // Upload to GPU
    UploadMesh(&mesh, false);

    TraceLog(LOG_INFO, "Created level mesh: %d tiles, %d vertices, %d triangles",
             tileCount, vertexCount, triangleCount);

    return mesh;
}

//------------------------------------------------------------------------------
// Custom Tiles Mesh Generation (with separate bump atlas UVs)
//------------------------------------------------------------------------------

namespace {

// Calculate UV coordinates for a tile in the bump atlas
// atlasWidth/atlasHeight are the actual texture dimensions
void getBumpAtlasUV(const BumpAtlasConfig& atlas, int atlasWidth, int atlasHeight,
                    int bumpTileIndex, float* u0, float* v0, float* u1, float* v1) {
    // Calculate tile position in atlas
    int col = bumpTileIndex % atlas.columns;
    int row = bumpTileIndex / atlas.columns;

    // Calculate normalized UV coordinates with 0.5px inset to sample at pixel centers
    float pixelU0 = col * atlas.tileWidth + 0.5f;
    float pixelV0 = row * atlas.tileHeight + 0.5f;
    float pixelU1 = pixelU0 + atlas.tileWidth - 1.0f;
    float pixelV1 = pixelV0 + atlas.tileHeight - 1.0f;

    *u0 = pixelU0 / atlasWidth;
    *u1 = pixelU1 / atlasWidth;

    // Invert V coordinates (OpenGL texture origin is bottom-left, image origin is top-left)
    // Keep the same tile, just flip the V range within that tile
    *v1 = (pixelV0 / atlasHeight);
    *v0 = (pixelV1 / atlasHeight);

    /*
    //debug - sample center of first tile to avoid edge/corner artifacts
    float halfTileU = (atlas.tileWidth * 0.5f) / atlasWidth;
    float halfTileV = (atlas.tileHeight * 0.5f) / atlasHeight;
    *u0 = halfTileU;
    *u1 = halfTileU;
    *v0 = halfTileV;
    *v1 = halfTileV;
    */
}

} // namespace

Mesh createLevelTileMeshCustom(
    const TmxLevel& level,
    const TmxTileset& tileset,
    const TilePropertiesConfig& tileProps,
    int bumpAtlasWidth,
    int bumpAtlasHeight,
    float worldScale
) {
    Mesh mesh = {0};

    // Count non-empty tiles and track override statistics
    int tileCount = 0;
    int tilesWithOverrides = 0;
    int tilesUsingDefaults = 0;

    for (int id : level.tiles) {
        if (id > 0) {
            tileCount++;
            if (tileProps.tiles.count(id) > 0) {
                tilesWithOverrides++;
            } else {
                tilesUsingDefaults++;
            }
        }
    }

    if (tileCount == 0) {
        TraceLog(LOG_WARNING, "Level has no tiles to render");
        return mesh;
    }

    TraceLog(LOG_INFO, "CustomTiles: %d tiles total, %d with overrides, %d using defaults (bumpIdx=%d)",
             tileCount, tilesWithOverrides, tilesUsingDefaults, tileProps.defaults.bumpTileIndex);
    TraceLog(LOG_INFO, "CustomTiles: bump atlas %dx%d, tile size %dx%d, columns=%d",
             bumpAtlasWidth, bumpAtlasHeight,
             tileProps.bumpAtlas.tileWidth, tileProps.bumpAtlas.tileHeight,
             tileProps.bumpAtlas.columns);

    // Each tile = 4 vertices, 6 indices (2 triangles)
    int vertexCount = tileCount * 4;
    int triangleCount = tileCount * 2;

    // Allocate mesh data (including texcoords2 for bump atlas UVs)
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;

    mesh.vertices = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertexCount * 2 * sizeof(float));
    mesh.texcoords2 = (float*)RL_MALLOC(vertexCount * 2 * sizeof(float));  // Bump atlas UVs
    mesh.normals = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
    mesh.tangents = (float*)RL_MALLOC(vertexCount * 4 * sizeof(float));
    mesh.indices = (unsigned short*)RL_MALLOC(triangleCount * 3 * sizeof(unsigned short));

    // Half tile size in world units
    float halfTile = worldScale * 0.5f;

    // Fill mesh data
    int vi = 0;  // Vertex index
    int ii = 0;  // Index index

    for (int row = 0; row < level.height; row++) {
        for (int col = 0; col < level.width; col++) {
            int tileIdx = row * level.width + col;
            int tileId = level.tiles[tileIdx];

            if (tileId <= 0) continue;  // Skip empty tiles

            // Get tile center in world space
            Vector3 center = tmxGridToWorld(col, row, level, worldScale);

            // Get diffuse UV coordinates from tileset atlas
            float u0, v0, u1, v1;
            getTileUV(tileset, tileId, &u0, &v0, &u1, &v1);

            // Get bump atlas UV coordinates based on tile properties
            const TileRenderProperties& props = tileProps.getProperties(tileId);
            float bu0, bv0, bu1, bv1;
            getBumpAtlasUV(tileProps.bumpAtlas, bumpAtlasWidth, bumpAtlasHeight,
                           props.bumpTileIndex, &bu0, &bv0, &bu1, &bv1);

            // Vertex base index for this tile
            int baseVert = vi;

            // Generate 4 vertices (CCW from above when looking down +Y to -Y)
            // BL, BR, TR, TL
            float positions[4][3] = {
                {center.x - halfTile, 0.0f, center.z - halfTile},  // BL (0)
                {center.x + halfTile, 0.0f, center.z - halfTile},  // BR (1)
                {center.x + halfTile, 0.0f, center.z + halfTile},  // TR (2)
                {center.x - halfTile, 0.0f, center.z + halfTile},  // TL (3)
            };

            // Diffuse UV coordinates (matching vertex order)
            float diffuseUVs[4][2] = {
                {u0, v0},  // BL
                {u1, v0},  // BR
                {u1, v1},  // TR
                {u0, v1},  // TL
            };

            // Bump atlas UV coordinates (matching vertex order)
            float bumpUVs[4][2] = {
                {bu0, bv0},  // BL
                {bu1, bv0},  // BR
                {bu1, bv1},  // TR
                {bu0, bv1},  // TL
            };

            // All floor tiles face up
            float normal[3] = {0.0f, 1.0f, 0.0f};
            float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};

            for (int v = 0; v < 4; v++) {
                int idx = vi * 3;
                mesh.vertices[idx + 0] = positions[v][0];
                mesh.vertices[idx + 1] = positions[v][1];
                mesh.vertices[idx + 2] = positions[v][2];

                // Diffuse UVs (texcoords)
                idx = vi * 2;
                mesh.texcoords[idx + 0] = diffuseUVs[v][0];
                mesh.texcoords[idx + 1] = diffuseUVs[v][1];

                // Bump atlas UVs (texcoords2)
                mesh.texcoords2[idx + 0] = bumpUVs[v][0];
                mesh.texcoords2[idx + 1] = bumpUVs[v][1];

                idx = vi * 3;
                mesh.normals[idx + 0] = normal[0];
                mesh.normals[idx + 1] = normal[1];
                mesh.normals[idx + 2] = normal[2];

                idx = vi * 4;
                mesh.tangents[idx + 0] = tangent[0];
                mesh.tangents[idx + 1] = tangent[1];
                mesh.tangents[idx + 2] = tangent[2];
                mesh.tangents[idx + 3] = tangent[3];

                vi++;
            }

            // Triangle indices (CW when viewed from above for correct backface culling)
            // Triangle 0: BL(0), TR(2), BR(1)
            // Triangle 1: BL(0), TL(3), TR(2)
            mesh.indices[ii++] = baseVert + 0;
            mesh.indices[ii++] = baseVert + 2;
            mesh.indices[ii++] = baseVert + 1;

            mesh.indices[ii++] = baseVert + 0;
            mesh.indices[ii++] = baseVert + 3;
            mesh.indices[ii++] = baseVert + 2;
        }
    }

    // Upload to GPU
    UploadMesh(&mesh, false);

    TraceLog(LOG_INFO, "Created custom tiles mesh: %d tiles, %d vertices, %d triangles (with texcoords2)",
             tileCount, vertexCount, triangleCount);

    return mesh;
}

//------------------------------------------------------------------------------
// Model Creation
//------------------------------------------------------------------------------

Model createLevelTileModel(
    const Mesh& mesh,
    Texture2D atlasTexture,
    Texture2D bumpTexture,
    SceneRenderer* renderer
) {
    Model model = LoadModelFromMesh(mesh);

    // Set diffuse texture
    if (atlasTexture.id > 0) {
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlasTexture;
    }

    // Set bump/normal texture
    if (bumpTexture.id > 0) {
        model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = bumpTexture;
    }

    // Apply scene renderer shader
    if (renderer) {
        sceneRendererApplyShader(renderer, &model);
    }

    return model;
}

//------------------------------------------------------------------------------
// Render Data Generation
//------------------------------------------------------------------------------

LevelRenderData createLevelRenderData(
    const TmxLevel& level,
    const TmxTileset& tileset,
    LevelRenderMode mode,
    float worldScale
) {
    LevelRenderData data;

    data.levelName = level.name;
    data.gridWidth = level.width;
    data.gridHeight = level.height;

    // Count tiles and compute bounds
    int tileCount = 0;
    for (int id : level.tiles) {
        if (id > 0) tileCount++;
    }
    data.tileCount = tileCount;

    // Compute level bounds
    float halfWidth = level.width * worldScale * 0.5f;
    float halfHeight = level.height * worldScale * 0.5f;
    data.boundsMin = {-halfWidth, 0.0f, -halfHeight};
    data.boundsMax = {halfWidth, 0.0f, halfHeight};

    // Generate waypoint positions
    for (const auto& wp : level.waypoints) {
        Vector3 worldPos = tmxPixelToWorld(wp.x, wp.y, level, worldScale);
        data.waypointPositions.push_back(worldPos);
    }

    // Generate waypoint links
    // Build ID -> index map
    std::map<int, int> idToIndex;
    for (size_t i = 0; i < level.waypoints.size(); i++) {
        idToIndex[level.waypoints[i].id] = static_cast<int>(i);
    }

    // Build adjacency list (bidirectional) and deduplicated link pairs (for visualization)
    data.waypointAdjacency.resize(level.waypoints.size());
    for (size_t i = 0; i < level.waypoints.size(); i++) {
        for (int linkId : level.waypoints[i].links) {
            auto it = idToIndex.find(linkId);
            if (it != idToIndex.end()) {
                int j = it->second;
                data.waypointAdjacency[i].push_back(j);
                // Add visualization link (only in one direction to avoid duplicates)
                if (i < (size_t)j) {
                    data.waypointLinks.push_back({static_cast<int>(i), j});
                }
            }
        }
    }

    // Store tile render data for debug/future use
    for (int row = 0; row < level.height; row++) {
        for (int col = 0; col < level.width; col++) {
            int tileIdx = row * level.width + col;
            int tileId = level.tiles[tileIdx];

            if (tileId <= 0) continue;

            LevelRenderTile rt;
            rt.position = tmxGridToWorld(col, row, level, worldScale);
            getTileUV(tileset, tileId, &rt.uv0.x, &rt.uv0.y, &rt.uv1.x, &rt.uv1.y);
            rt.tileId = tileId;
            data.tiles.push_back(rt);
        }
    }

    data.meshValid = false;
    return data;
}

//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------

void freeLevelRenderData(LevelRenderData* data) {
    if (!data) return;

    if (data->meshValid) {
        UnloadModel(data->tileModel);
        data->meshValid = false;
    }

    data->tiles.clear();
    data->waypointPositions.clear();
    data->waypointLinks.clear();
}

//------------------------------------------------------------------------------
// Collision Data Generation
//------------------------------------------------------------------------------

namespace {

// Helper struct for tracking collision rectangles during merging
struct WorkRect {
    float minX, minZ, maxX, maxZ;
    bool merged = false;

    float width() const { return maxX - minX; }
    float height() const { return maxZ - minZ; }
    float centerX() const { return (minX + maxX) * 0.5f; }
    float centerZ() const { return (minZ + maxZ) * 0.5f; }

    bool canMergeX(const WorkRect& other, float epsilon = 0.001f) const {
        // Can merge horizontally if same height and adjacent
        return std::abs(minZ - other.minZ) < epsilon &&
               std::abs(maxZ - other.maxZ) < epsilon &&
               (std::abs(maxX - other.minX) < epsilon || std::abs(other.maxX - minX) < epsilon);
    }

    bool canMergeZ(const WorkRect& other, float epsilon = 0.001f) const {
        // Can merge vertically if same width and adjacent
        return std::abs(minX - other.minX) < epsilon &&
               std::abs(maxX - other.maxX) < epsilon &&
               (std::abs(maxZ - other.minZ) < epsilon || std::abs(other.maxZ - minZ) < epsilon);
    }

    void mergeWith(const WorkRect& other) {
        minX = std::min(minX, other.minX);
        minZ = std::min(minZ, other.minZ);
        maxX = std::max(maxX, other.maxX);
        maxZ = std::max(maxZ, other.maxZ);
    }
};

// Merge adjacent rectangles into larger shapes
std::vector<WorkRect> mergeRectangles(std::vector<WorkRect>& rects) {
    if (rects.empty()) return {};

    bool merged = true;
    while (merged) {
        merged = false;

        // Try to merge along X axis first (horizontal merging)
        for (size_t i = 0; i < rects.size() && !merged; i++) {
            if (rects[i].merged) continue;

            for (size_t j = i + 1; j < rects.size() && !merged; j++) {
                if (rects[j].merged) continue;

                if (rects[i].canMergeX(rects[j])) {
                    rects[i].mergeWith(rects[j]);
                    rects[j].merged = true;
                    merged = true;
                }
            }
        }

        // Then try Z axis (vertical merging)
        for (size_t i = 0; i < rects.size() && !merged; i++) {
            if (rects[i].merged) continue;

            for (size_t j = i + 1; j < rects.size() && !merged; j++) {
                if (rects[j].merged) continue;

                if (rects[i].canMergeZ(rects[j])) {
                    rects[i].mergeWith(rects[j]);
                    rects[j].merged = true;
                    merged = true;
                }
            }
        }
    }

    // Collect non-merged rectangles
    std::vector<WorkRect> result;
    for (const auto& r : rects) {
        if (!r.merged) {
            result.push_back(r);
        }
    }

    return result;
}

} // namespace

LevelCollisionData generateLevelCollision(
    const TmxLevel& level,
    const TmxTileset& tileset,
    float worldScale
) {
    LevelCollisionData data;

    // Collect all collision rectangles from tiles
    std::vector<WorkRect> workRects;

    float tileSize = static_cast<float>(tileset.tileWidth);  // Assume square tiles
    float halfLevelWidth = level.width * worldScale * 0.5f;
    float halfLevelHeight = level.height * worldScale * 0.5f;

    int tilesWithCollision = 0;

    for (int row = 0; row < level.height; row++) {
        for (int col = 0; col < level.width; col++) {
            int tileIdx = row * level.width + col;
            int tileId = level.tiles[tileIdx];

            if (tileId <= 0) continue;

            // Get tile properties (local ID is tileId - firstGid, but properties are stored by local ID)
            int localId = tileId - tileset.firstGid;
            auto it = tileset.tileProperties.find(localId);
            if (it == tileset.tileProperties.end() || !it->second.hasCollision()) {
                continue;
            }

            tilesWithCollision++;

            // Get tile origin in world space (top-left corner in TMX coords -> world coords)
            // TMX: origin top-left, X right, Y down
            // World: X right, Z increases with TMX Y
            float tileOriginX = col * worldScale - halfLevelWidth;
            float tileOriginZ = row * worldScale - halfLevelHeight;

            // Process each collision rect in this tile
            for (const auto& rect : it->second.collisionRects) {
                // Convert from TMX pixel coords (within tile) to world coords
                // rect.x, rect.y are offsets from tile top-left in pixels
                float rectMinX = tileOriginX + (rect.x / tileSize) * worldScale;
                float rectMinZ = tileOriginZ + (rect.y / tileSize) * worldScale;
                float rectMaxX = rectMinX + (rect.width / tileSize) * worldScale;
                float rectMaxZ = rectMinZ + (rect.height / tileSize) * worldScale;

                WorkRect wr;
                wr.minX = rectMinX;
                wr.minZ = rectMinZ;
                wr.maxX = rectMaxX;
                wr.maxZ = rectMaxZ;
                workRects.push_back(wr);
            }
        }
    }

    TraceLog(LOG_INFO, "Collision: Found %d tiles with collision shapes, %d raw rectangles",
             tilesWithCollision, (int)workRects.size());

    // Merge adjacent rectangles
    std::vector<WorkRect> mergedRects = mergeRectangles(workRects);

    TraceLog(LOG_INFO, "Collision: Merged to %d rectangles", (int)mergedRects.size());

    // Convert to output format
    for (const auto& wr : mergedRects) {
        CollisionRect cr;
        cr.x = wr.centerX();
        cr.z = wr.centerZ();
        cr.halfWidth = wr.width() * 0.5f;
        cr.halfHeight = wr.height() * 0.5f;
        data.rects.push_back(cr);
    }

    // Calculate bounds
    if (!mergedRects.empty()) {
        data.boundsMin = {mergedRects[0].minX, 0.0f, mergedRects[0].minZ};
        data.boundsMax = {mergedRects[0].maxX, 0.0f, mergedRects[0].maxZ};

        for (const auto& wr : mergedRects) {
            data.boundsMin.x = std::min(data.boundsMin.x, wr.minX);
            data.boundsMin.z = std::min(data.boundsMin.z, wr.minZ);
            data.boundsMax.x = std::max(data.boundsMax.x, wr.maxX);
            data.boundsMax.z = std::max(data.boundsMax.z, wr.maxZ);
        }
    }

    // Generate debug vertices (4 corners per rectangle, as line loop)
    for (const auto& wr : mergedRects) {
        // 4 lines per rectangle = 8 vertices (pairs for DrawLine3D)
        data.debugVertices.push_back({wr.minX, 0.0f, wr.minZ});
        data.debugVertices.push_back({wr.maxX, 0.0f, wr.minZ});

        data.debugVertices.push_back({wr.maxX, 0.0f, wr.minZ});
        data.debugVertices.push_back({wr.maxX, 0.0f, wr.maxZ});

        data.debugVertices.push_back({wr.maxX, 0.0f, wr.maxZ});
        data.debugVertices.push_back({wr.minX, 0.0f, wr.maxZ});

        data.debugVertices.push_back({wr.minX, 0.0f, wr.maxZ});
        data.debugVertices.push_back({wr.minX, 0.0f, wr.minZ});
    }
    data.debugVertexCount = static_cast<int>(data.debugVertices.size());

    return data;
}

void freeLevelCollisionData(LevelCollisionData* data) {
    if (!data) return;
    data->rects.clear();
    data->debugVertices.clear();
    data->debugVertexCount = 0;
}

//------------------------------------------------------------------------------
// Debug Visualization
//------------------------------------------------------------------------------

void drawCollisionDebug(const LevelCollisionData& collision, Color color, float yOffset) {
    if (collision.debugVertices.empty()) return;

    // Draw lines in pairs
    for (size_t i = 0; i + 1 < collision.debugVertices.size(); i += 2) {
        Vector3 start = collision.debugVertices[i];
        Vector3 end = collision.debugVertices[i + 1];

        // Apply Y offset
        start.y += yOffset;
        end.y += yOffset;

        DrawLine3D(start, end, color);
    }
}
