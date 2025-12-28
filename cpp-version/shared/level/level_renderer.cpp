#include "level_renderer.h"
#include "tileset_loader.h"
#include "rlgl.h"
#include <cmath>

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

    for (size_t i = 0; i < level.waypoints.size(); i++) {
        for (int linkId : level.waypoints[i].links) {
            auto it = idToIndex.find(linkId);
            if (it != idToIndex.end()) {
                // Add link (only in one direction to avoid duplicates)
                int j = it->second;
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
