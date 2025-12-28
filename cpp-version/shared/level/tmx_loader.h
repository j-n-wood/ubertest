#ifndef TMX_LOADER_H
#define TMX_LOADER_H

#include "level_types.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// TMX Loader
//
// Parses Tiled TMX map files into TmxLevel structures.
// Supports CSV-encoded tile layers and object layers for waypoints.
//------------------------------------------------------------------------------

// Load a single TMX level file
TmxLoadResult loadTmxLevel(const std::string& filePath);

// Load all TMX files from a directory
// Returns levels sorted by filename
std::vector<TmxLoadResult> loadTmxLevelsFromDirectory(const std::string& directoryPath);

// Get level name from filename (e.g., "level_0_maintenance.tmx" -> "Maintenance")
std::string extractLevelName(const std::string& filename);

#endif // TMX_LOADER_H
