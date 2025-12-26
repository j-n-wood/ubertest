#ifndef TILE_MESH_H
#define TILE_MESH_H

#include "raylib.h"
#include "scene_convert/scene_types.h"
#include <vector>
#include <map>

//------------------------------------------------------------------------------
// Tile Mesh Generation - Creates Raylib meshes from loaded tile data
//
// Design:
// - JSON stores coordinates already transformed to render space (Y-up, scaled)
// - Tiles are axis-aligned quads with 4 vertices in CCW order: BL, BR, TR, TL
// - Uses index buffers for efficient rendering (4 verts, 6 indices per tile)
// - Normals are hardcoded to (0, 1, 0) since tiles face up
// - Tiles are batched by texture indices for efficient rendering
//------------------------------------------------------------------------------

// Batch of tiles sharing the same texture indices
struct TileBatch {
    Mesh mesh;
    int textureIndex1;
    int textureIndex2;
    int tileCount;
    int triangleCount;
    bool valid;
};

// Collection of batched tile meshes
struct TileBatchCollection {
    std::vector<TileBatch> batches;
    int totalTiles;
    int totalTriangles;
    Vector3 boundsMin;
    Vector3 boundsMax;
    bool success;
    const char* error;
};

// Batch key for sorting tiles by texture
struct TileBatchKey {
    int textureIndex1;
    int textureIndex2;

    bool operator<(const TileBatchKey& other) const {
        if (textureIndex1 != other.textureIndex1) return textureIndex1 < other.textureIndex1;
        return textureIndex2 < other.textureIndex2;
    }
};

//------------------------------------------------------------------------------
// Main API
//------------------------------------------------------------------------------

// Create batched meshes from tiles, grouped by texture indices
// Tiles are expected to have positions already in render space (Y-up, scaled)
TileBatchCollection createBatchedTileMeshes(const std::vector<Tile>& tiles);

// Create batched meshes from a domain (all areas)
TileBatchCollection createDomainBatchedMeshes(const Domain& domain);

// Free all batched meshes
void freeTileBatchCollection(TileBatchCollection* collection);

//------------------------------------------------------------------------------
// Legacy API (for compatibility, creates single unbatched mesh)
//------------------------------------------------------------------------------

struct TileMeshResult {
    Mesh mesh;
    int triangleCount;
    int vertexCount;
    bool success;
    const char* error;
};

// Create a single mesh from all tiles (no batching)
TileMeshResult createTileMesh(const std::vector<Tile>& tiles);
TileMeshResult createDomainMesh(const Domain& domain);

void freeTileMesh(Mesh* mesh);

//------------------------------------------------------------------------------
// Coordinate transform helpers (for conversion phase only)
// These are used when writing to JSON, not when reading
//------------------------------------------------------------------------------

// Scale factor for converting original game units to meters
constexpr float SCALE_UNITS_TO_METERS = 0.0254f;

// Transform from game space to render space (for use during conversion)
// Game: X horizontal, Y horizontal (forward), Z vertical (height)
// Render: X horizontal, Y vertical (up), Z horizontal (depth)
inline Vector3 gameToRenderCoords(float gameX, float gameY, float gameZ, float scale) {
    return {
        gameX * scale,      // X unchanged
        gameZ * scale,      // Game Z (height) -> Render Y (up)
        gameY * scale       // Game Y (forward) -> Render Z (depth)
    };
}

inline Vector3 gameToRenderCoords(const Vector3& gamePos, float scale) {
    return gameToRenderCoords(gamePos.x, gamePos.y, gamePos.z, scale);
}

//------------------------------------------------------------------------------
// Debug helpers
//------------------------------------------------------------------------------

// Get bounds of tile vertices (assumes coordinates are already in render space)
void getTileBounds(const std::vector<Tile>& tiles, Vector3* outMin, Vector3* outMax);

#endif // TILE_MESH_H
