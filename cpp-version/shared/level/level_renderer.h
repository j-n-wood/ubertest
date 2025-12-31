#ifndef LEVEL_RENDERER_H
#define LEVEL_RENDERER_H

#include "level_types.h"
#include "rendering/scene_renderer.h"

//------------------------------------------------------------------------------
// Level Renderer
//
// Transforms TMX level data into renderable meshes using the shared
// rendering pipeline. Supports multiple rendering modes.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Render Data Generation
//------------------------------------------------------------------------------

// Create render data from a TMX level and tileset
// mode: Rendering mode (Tilemap, CustomTiles, Objects3D)
// worldScale: World units per tile (default 1.0 = 1 tile = 1 unit)
LevelRenderData createLevelRenderData(
    const TmxLevel& level,
    const TmxTileset& tileset,
    LevelRenderMode mode = LevelRenderMode::Tilemap,
    float worldScale = 1.0f
);

// Free render data resources (mesh, model)
void freeLevelRenderData(LevelRenderData* data);

//------------------------------------------------------------------------------
// Mesh Generation
//------------------------------------------------------------------------------

// Create a mesh for all tiles in the level (Tilemap mode)
// Uses tileset UV coordinates for texture mapping
// Both diffuse and bump textures use the same UVs
Mesh createLevelTileMesh(
    const TmxLevel& level,
    const TmxTileset& tileset,
    float worldScale = 1.0f
);

// Create a mesh for all tiles with custom bump UVs (CustomTiles mode)
// Diffuse UVs from tileset atlas, bump UVs from tiles.json bumpTileIndex
// Uses mesh.texcoords2 for bump atlas coordinates
// bumpAtlasWidth/Height: actual texture dimensions for UV calculation
Mesh createLevelTileMeshCustom(
    const TmxLevel& level,
    const TmxTileset& tileset,
    const TilePropertiesConfig& tileProps,
    int bumpAtlasWidth,
    int bumpAtlasHeight,
    float worldScale = 1.0f
);

// Create a Model from the tile mesh with materials set up
Model createLevelTileModel(
    const Mesh& mesh,
    Texture2D atlasTexture,
    Texture2D bumpTexture,
    SceneRenderer* renderer
);

//------------------------------------------------------------------------------
// Coordinate Transforms
//------------------------------------------------------------------------------

// Convert TMX grid position (column, row) to world position
// Origin is centered on the level, Y=0 plane
Vector3 tmxGridToWorld(int col, int row, const TmxLevel& level, float worldScale);

// Convert TMX pixel position to world position
Vector3 tmxPixelToWorld(float x, float y, const TmxLevel& level, float worldScale);

//------------------------------------------------------------------------------
// Physics Data (STUB)
//------------------------------------------------------------------------------

// STUB: Generate collision data from tile properties
// Returns empty collision data for now
// CollisionData generateLevelCollision(const TmxLevel& level, const TmxTileset& tileset);

//------------------------------------------------------------------------------
// 3D Objects (STUB)
//------------------------------------------------------------------------------

// STUB: Generate 3D object placements from tile properties
// Returns empty vector for now
// std::vector<ObjectInstance> generateLevelObjects(const TmxLevel& level, const TmxTileset& tileset);

#endif // LEVEL_RENDERER_H
