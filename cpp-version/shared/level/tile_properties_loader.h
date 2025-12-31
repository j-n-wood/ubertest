#ifndef TILE_PROPERTIES_LOADER_H
#define TILE_PROPERTIES_LOADER_H

#include "level_types.h"
#include <string>

//------------------------------------------------------------------------------
// Tile Properties Loader
//
// Loads tiles.json configuration files that define per-tile rendering
// properties for the custom tiles rendering mode.
//------------------------------------------------------------------------------

// Load tile properties from a tiles.json file
// Returns a TilePropertiesConfig with valid=true on success
TilePropertiesConfig loadTileProperties(const std::string& filePath);

// Save tile properties to a tiles.json file
bool saveTileProperties(const TilePropertiesConfig& config, const std::string& filePath);

#endif // TILE_PROPERTIES_LOADER_H
