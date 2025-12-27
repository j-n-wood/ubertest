#include "tile_mesh.h"
#include "raymath.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

//------------------------------------------------------------------------------
// Tile Mesh Generation with Index Buffers
//
// Tiles are axis-aligned quads with 4 vertices in CCW order: BL, BR, TR, TL
// (Bottom-Left, Bottom-Right, Top-Right, Top-Left when viewed from above)
//
// Each tile uses 4 unique vertices and 6 indices (2 triangles):
//   Triangle 0: BL(0), BR(1), TR(2) - lower-right triangle
//   Triangle 1: BL(0), TR(2), TL(3) - upper-left triangle
//
// This gives correct CCW winding when viewed from above (Y+ direction)
//------------------------------------------------------------------------------

// Up normal for all floor tiles
static constexpr Vector3 UP_NORMAL = {0.0f, 1.0f, 0.0f};

// Tangent for floor tiles (aligned with +X / U texture coordinate)
// vec4 format: xyz = tangent direction, w = handedness (+1 for right-handed)
static constexpr float FLOOR_TANGENT[4] = {1.0f, 0.0f, 0.0f, 1.0f};

//------------------------------------------------------------------------------
// Create a single mesh from a batch of tiles using index buffer
//------------------------------------------------------------------------------
static Mesh createMeshFromTiles(const std::vector<const Tile*>& tiles) {
    if (tiles.empty()) {
        return {0};
    }

    int tileCount = static_cast<int>(tiles.size());
    int vertexCount = tileCount * 4;     // 4 vertices per tile
    int indexCount = tileCount * 6;      // 6 indices per tile (2 triangles)

    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = tileCount * 2;

    // Allocate arrays
    mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.tangents = (float*)MemAlloc(vertexCount * 4 * sizeof(float));  // vec4: xyz + handedness
    mesh.colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));
    mesh.indices = (unsigned short*)MemAlloc(indexCount * sizeof(unsigned short));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.tangents || !mesh.colors || !mesh.indices) {
        if (mesh.vertices) MemFree(mesh.vertices);
        if (mesh.texcoords) MemFree(mesh.texcoords);
        if (mesh.normals) MemFree(mesh.normals);
        if (mesh.tangents) MemFree(mesh.tangents);
        if (mesh.colors) MemFree(mesh.colors);
        if (mesh.indices) MemFree(mesh.indices);
        return {0};
    }

    int vi = 0;  // vertex index
    int ii = 0;  // index index

    for (const Tile* tile : tiles) {
        // Skip tiles without exactly 4 vertices
        if (tile->vertices.size() != 4) continue;

        // Get diffuse color from tile properties
        unsigned char r = (unsigned char)(tile->properties.diffuseColour.x * 255);
        unsigned char g = (unsigned char)(tile->properties.diffuseColour.y * 255);
        unsigned char b = (unsigned char)(tile->properties.diffuseColour.z * 255);
        unsigned char a = 255;

        // Store 4 vertices (positions already in render space from JSON)
        // Vertex order: BL(0), BR(1), TL(2), TR(3)
        for (int i = 0; i < 4; ++i) {
            const TileVertex& tv = tile->vertices[i];

            mesh.vertices[(vi + i) * 3 + 0] = tv.position.x;
            mesh.vertices[(vi + i) * 3 + 1] = tv.position.y;
            mesh.vertices[(vi + i) * 3 + 2] = tv.position.z;

            mesh.texcoords[(vi + i) * 2 + 0] = tv.uv1.x;
            mesh.texcoords[(vi + i) * 2 + 1] = tv.uv1.y;

            // All floor tiles face up
            mesh.normals[(vi + i) * 3 + 0] = UP_NORMAL.x;
            mesh.normals[(vi + i) * 3 + 1] = UP_NORMAL.y;
            mesh.normals[(vi + i) * 3 + 2] = UP_NORMAL.z;

            // Tangent for bump mapping (aligned with +X / U coordinate)
            mesh.tangents[(vi + i) * 4 + 0] = FLOOR_TANGENT[0];
            mesh.tangents[(vi + i) * 4 + 1] = FLOOR_TANGENT[1];
            mesh.tangents[(vi + i) * 4 + 2] = FLOOR_TANGENT[2];
            mesh.tangents[(vi + i) * 4 + 3] = FLOOR_TANGENT[3];  // handedness

            mesh.colors[(vi + i) * 4 + 0] = r;
            mesh.colors[(vi + i) * 4 + 1] = g;
            mesh.colors[(vi + i) * 4 + 2] = b;
            mesh.colors[(vi + i) * 4 + 3] = a;
        }

        // Store indices for 2 triangles (CW winding viewed from above for raylib)
        // Vertex order in data: BL(0), BR(1), TR(2), TL(3) - CCW in X-Z plane
        // Raylib/OpenGL expects CCW when viewed from front (normal direction)
        // Since normal is UP (+Y), and camera looks DOWN (-Y), we need CW in X-Z plane
        // Triangle 0: BL(0), TR(2), BR(1) - lower-right triangle (CW from above)
        mesh.indices[ii + 0] = static_cast<unsigned short>(vi + 0);  // BL
        mesh.indices[ii + 1] = static_cast<unsigned short>(vi + 2);  // TR
        mesh.indices[ii + 2] = static_cast<unsigned short>(vi + 1);  // BR

        // Triangle 1: BL(0), TL(3), TR(2) - upper-left triangle (CW from above)
        mesh.indices[ii + 3] = static_cast<unsigned short>(vi + 0);  // BL
        mesh.indices[ii + 4] = static_cast<unsigned short>(vi + 3);  // TL
        mesh.indices[ii + 5] = static_cast<unsigned short>(vi + 2);  // TR

        vi += 4;
        ii += 6;
    }

    // Upload to GPU
    UploadMesh(&mesh, false);

    return mesh;
}

//------------------------------------------------------------------------------
// Batched mesh creation
//------------------------------------------------------------------------------

TileBatchCollection createBatchedTileMeshes(const std::vector<Tile>& tiles) {
    TileBatchCollection result = {};

    if (tiles.empty()) {
        result.success = true;
        return result;
    }

    // Group tiles by texture indices
    std::map<TileBatchKey, std::vector<const Tile*>> groups;

    for (const auto& tile : tiles) {
        // Only process tiles with exactly 4 vertices
        if (tile.vertices.size() != 4) continue;

        TileBatchKey key{tile.textureIndex1, tile.textureIndex2};
        groups[key].push_back(&tile);
    }

    // Calculate bounds while iterating
    bool firstVertex = true;
    Vector3 bmin = {0, 0, 0};
    Vector3 bmax = {0, 0, 0};

    // Create a batch for each texture group
    for (const auto& [key, groupTiles] : groups) {
        TileBatch batch = {};
        batch.textureIndex1 = key.textureIndex1;
        batch.textureIndex2 = key.textureIndex2;
        batch.tileCount = static_cast<int>(groupTiles.size());
        batch.triangleCount = batch.tileCount * 2;

        batch.mesh = createMeshFromTiles(groupTiles);
        batch.valid = (batch.mesh.vertexCount > 0);

        if (batch.valid) {
            result.batches.push_back(batch);
            result.totalTiles += batch.tileCount;
            result.totalTriangles += batch.triangleCount;

            // Update bounds
            for (const Tile* tile : groupTiles) {
                for (const auto& vertex : tile->vertices) {
                    if (firstVertex) {
                        bmin = bmax = vertex.position;
                        firstVertex = false;
                    } else {
                        bmin.x = std::min(bmin.x, vertex.position.x);
                        bmin.y = std::min(bmin.y, vertex.position.y);
                        bmin.z = std::min(bmin.z, vertex.position.z);
                        bmax.x = std::max(bmax.x, vertex.position.x);
                        bmax.y = std::max(bmax.y, vertex.position.y);
                        bmax.z = std::max(bmax.z, vertex.position.z);
                    }
                }
            }
        }
    }

    result.boundsMin = bmin;
    result.boundsMax = bmax;
    result.success = true;

    TraceLog(LOG_INFO, "TILE_MESH: Created %zu batches, %d tiles, %d triangles",
             result.batches.size(), result.totalTiles, result.totalTriangles);

    return result;
}

TileBatchCollection createDomainBatchedMeshes(const Domain& domain) {
    // Collect all tiles from all areas
    std::vector<Tile> allTiles;
    for (const auto& area : domain.areas) {
        allTiles.insert(allTiles.end(), area.tiles.begin(), area.tiles.end());
    }
    return createBatchedTileMeshes(allTiles);
}

void freeTileBatchCollection(TileBatchCollection* collection) {
    if (!collection) return;

    for (auto& batch : collection->batches) {
        if (batch.valid) {
            UnloadMesh(batch.mesh);
            batch.valid = false;
        }
    }
    collection->batches.clear();
    collection->totalTiles = 0;
    collection->totalTriangles = 0;
}

//------------------------------------------------------------------------------
// Legacy API (single mesh, no batching)
//------------------------------------------------------------------------------

TileMeshResult createTileMesh(const std::vector<Tile>& tiles) {
    TileMeshResult result = {};

    if (tiles.empty()) {
        result.success = true;
        return result;
    }

    // Collect pointers to tiles with exactly 4 vertices
    std::vector<const Tile*> validTiles;
    for (const auto& tile : tiles) {
        if (tile.vertices.size() == 4) {
            validTiles.push_back(&tile);
        }
    }

    if (validTiles.empty()) {
        result.success = true;
        return result;
    }

    result.mesh = createMeshFromTiles(validTiles);
    result.vertexCount = result.mesh.vertexCount;
    result.triangleCount = result.mesh.triangleCount;
    result.success = (result.mesh.vertexCount > 0);

    if (!result.success) {
        result.error = "Failed to create mesh";
    }

    return result;
}

TileMeshResult createDomainMesh(const Domain& domain) {
    std::vector<Tile> allTiles;
    for (const auto& area : domain.areas) {
        allTiles.insert(allTiles.end(), area.tiles.begin(), area.tiles.end());
    }
    return createTileMesh(allTiles);
}

void freeTileMesh(Mesh* mesh) {
    if (mesh) {
        UnloadMesh(*mesh);
        *mesh = {0};
    }
}

//------------------------------------------------------------------------------
// Bounds calculation
//------------------------------------------------------------------------------

void getTileBounds(const std::vector<Tile>& tiles, Vector3* outMin, Vector3* outMax) {
    if (!outMin || !outMax) return;

    bool first = true;
    Vector3 bmin = {0, 0, 0};
    Vector3 bmax = {0, 0, 0};

    for (const auto& tile : tiles) {
        for (const auto& vertex : tile.vertices) {
            // Positions are already in render space
            Vector3 p = vertex.position;

            if (first) {
                bmin = bmax = p;
                first = false;
            } else {
                bmin.x = std::min(bmin.x, p.x);
                bmin.y = std::min(bmin.y, p.y);
                bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x);
                bmax.y = std::max(bmax.y, p.y);
                bmax.z = std::max(bmax.z, p.z);
            }
        }
    }

    *outMin = bmin;
    *outMax = bmax;
}
