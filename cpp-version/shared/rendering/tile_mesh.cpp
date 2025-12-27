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
// Calculate number of triangles for a tile based on vertex count and type
static int calcTriangleCount(int vertexCount, int tileType) {
    if (vertexCount < 3) return 0;

    // tileType: 0=fan, 1=strip, 2=triangles
    if (tileType == 2) {
        // Triangles: 3 vertices per triangle
        return vertexCount / 3;
    } else {
        // Fan or strip: n-2 triangles for n vertices
        return vertexCount - 2;
    }
}

static Mesh createMeshFromTiles(const std::vector<const Tile*>& tiles) {
    if (tiles.empty()) {
        return {0};
    }

    // First pass: count total vertices and triangles
    int totalVertices = 0;
    int totalTriangles = 0;

    for (const Tile* tile : tiles) {
        int n = static_cast<int>(tile->vertices.size());
        if (n < 3) continue;

        int tileType = tile->properties.tileType;
        // Values >= 3 are material indices from archetiles, treat as fan
        if (tileType > 2) tileType = 0;

        totalVertices += n;
        totalTriangles += calcTriangleCount(n, tileType);
    }

    if (totalVertices == 0 || totalTriangles == 0) {
        return {0};
    }

    int indexCount = totalTriangles * 3;

    Mesh mesh = {0};
    mesh.vertexCount = totalVertices;
    mesh.triangleCount = totalTriangles;

    // Allocate arrays
    mesh.vertices = (float*)MemAlloc(totalVertices * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(totalVertices * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(totalVertices * 3 * sizeof(float));
    mesh.tangents = (float*)MemAlloc(totalVertices * 4 * sizeof(float));  // vec4: xyz + handedness
    mesh.colors = (unsigned char*)MemAlloc(totalVertices * 4 * sizeof(unsigned char));
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

    int vi = 0;  // vertex write index
    int ii = 0;  // index write index

    for (const Tile* tile : tiles) {
        int n = static_cast<int>(tile->vertices.size());
        if (n < 3) continue;

        // Get diffuse color from tile properties
        unsigned char r = (unsigned char)(tile->properties.diffuseColour.x * 255);
        unsigned char g = (unsigned char)(tile->properties.diffuseColour.y * 255);
        unsigned char b = (unsigned char)(tile->properties.diffuseColour.z * 255);
        unsigned char a = 255;

        int baseVertex = vi;  // Remember start vertex for this tile

        // Store all vertices (positions already in render space from JSON)
        for (int i = 0; i < n; ++i) {
            const TileVertex& tv = tile->vertices[i];

            mesh.vertices[vi * 3 + 0] = tv.position.x;
            mesh.vertices[vi * 3 + 1] = tv.position.y;
            mesh.vertices[vi * 3 + 2] = tv.position.z;

            mesh.texcoords[vi * 2 + 0] = tv.uv1.x;
            mesh.texcoords[vi * 2 + 1] = tv.uv1.y;

            // All floor tiles face up
            mesh.normals[vi * 3 + 0] = UP_NORMAL.x;
            mesh.normals[vi * 3 + 1] = UP_NORMAL.y;
            mesh.normals[vi * 3 + 2] = UP_NORMAL.z;

            // Tangent for bump mapping (aligned with +X / U coordinate)
            mesh.tangents[vi * 4 + 0] = FLOOR_TANGENT[0];
            mesh.tangents[vi * 4 + 1] = FLOOR_TANGENT[1];
            mesh.tangents[vi * 4 + 2] = FLOOR_TANGENT[2];
            mesh.tangents[vi * 4 + 3] = FLOOR_TANGENT[3];  // handedness

            mesh.colors[vi * 4 + 0] = r;
            mesh.colors[vi * 4 + 1] = g;
            mesh.colors[vi * 4 + 2] = b;
            mesh.colors[vi * 4 + 3] = a;

            vi++;
        }

        // Generate indices based on tile type
        // tileType: 0=fan, 1=strip, 2=triangles (from original game)
        // Values >= 3 are material indices from archetiles, treat as fan
        int tileType = tile->properties.tileType;
        if (tileType > 2) tileType = 0;

        int triCount = calcTriangleCount(n, tileType);

        // NOTE: Winding order is reversed (CW in source data) to produce CCW
        // when viewed from +Y looking down (camera at +Y, looking toward Y=0)

        if (tileType == 1) {
            // Triangle strip: alternating winding, reversed for CCW from above
            for (int t = 0; t < triCount; ++t) {
                if (t % 2 == 0) {
                    // Even: (i, i+2, i+1) - reversed from (i, i+1, i+2)
                    mesh.indices[ii + 0] = static_cast<unsigned short>(baseVertex + t);
                    mesh.indices[ii + 1] = static_cast<unsigned short>(baseVertex + t + 2);
                    mesh.indices[ii + 2] = static_cast<unsigned short>(baseVertex + t + 1);
                } else {
                    // Odd: (i+1, i+2, i) - reversed from (i+1, i, i+2)
                    mesh.indices[ii + 0] = static_cast<unsigned short>(baseVertex + t + 1);
                    mesh.indices[ii + 1] = static_cast<unsigned short>(baseVertex + t + 2);
                    mesh.indices[ii + 2] = static_cast<unsigned short>(baseVertex + t);
                }
                ii += 3;
            }
        } else if (tileType == 2) {
            // Triangles: every 3 vertices, reversed winding
            for (int t = 0; t < triCount; ++t) {
                mesh.indices[ii + 0] = static_cast<unsigned short>(baseVertex + t * 3);
                mesh.indices[ii + 1] = static_cast<unsigned short>(baseVertex + t * 3 + 2);
                mesh.indices[ii + 2] = static_cast<unsigned short>(baseVertex + t * 3 + 1);
                ii += 3;
            }
        } else {
            // Triangle fan (default): v0 is shared, reversed winding
            // tri[i] = (0, i+2, i+1) - reversed from (0, i+1, i+2)
            for (int t = 0; t < triCount; ++t) {
                mesh.indices[ii + 0] = static_cast<unsigned short>(baseVertex);
                mesh.indices[ii + 1] = static_cast<unsigned short>(baseVertex + t + 2);
                mesh.indices[ii + 2] = static_cast<unsigned short>(baseVertex + t + 1);
                ii += 3;
            }
        }
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

    TraceLog(LOG_INFO, "TILE_MESH: Processing %zu tiles for batching", tiles.size());

    // Group tiles by texture indices
    std::map<TileBatchKey, std::vector<const Tile*>> groups;

    int skippedCount = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        // Skip tiles with fewer than 3 vertices
        if (tile.vertices.size() < 3) {
            TraceLog(LOG_INFO, "TILE_MESH: Skipping tile %zu with %zu vertices (< 3)",
                     i, tile.vertices.size());
            skippedCount++;
            continue;
        }

        // Log first few tiles for debugging
        if (i < 3) {
            int tt = tile.properties.tileType;
            TraceLog(LOG_INFO, "TILE_MESH: Tile %zu: %zu verts, tileType=%d",
                     i, tile.vertices.size(), tt);
        }

        TileBatchKey key{tile.textureIndex1, tile.textureIndex2};
        groups[key].push_back(&tile);
    }

    TraceLog(LOG_INFO, "TILE_MESH: Grouped into %zu batches, skipped %d degenerate tiles",
             groups.size(), skippedCount);

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

        batch.mesh = createMeshFromTiles(groupTiles);
        batch.valid = (batch.mesh.vertexCount > 0);
        batch.triangleCount = batch.mesh.triangleCount;  // Get actual triangle count from mesh

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
