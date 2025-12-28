#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H

#include "level_types.h"
#include <string>

//------------------------------------------------------------------------------
// Tileset Loader
//
// Parses Tiled TSX tileset files and computes UV coordinates for tile IDs.
//------------------------------------------------------------------------------

// Load a TSX tileset file
// basePath is the directory containing the TSX file (for resolving image paths)
TsxLoadResult loadTsxTileset(const std::string& filePath);

// Compute UV coordinates for a tile ID
// Returns UV rect as (u0, v0, u1, v1) - top-left and bottom-right corners
// tileId is 1-based (0 = empty tile, returns zero rect)
void getTileUV(const TmxTileset& tileset, int tileId,
               float* u0, float* v0, float* u1, float* v1);

// Load tileset image as raylib texture
// Returns texture with id=0 on failure
Texture2D loadTilesetTexture(const TmxTileset& tileset, const std::string& basePath);

#endif // TILESET_LOADER_H
