#pragma once

#include "paradroid_parser.h"
#include <string>

struct TmxWriterConfig {
    std::string tilesetSource = "default.tsx";
    int tileWidth = 64;
    int tileHeight = 64;
    int firstGid = 1;  // TMX uses 1-based tile IDs, 0 = empty
};

// Write a single level to a TMX file
// Returns true on success, false on error (with errorMsg set)
bool writeTmxFile(const ParadroidLevel& level,
                  const std::string& outputPath,
                  const TmxWriterConfig& config,
                  std::string& errorMsg);

// Generate a TMX filename for a level (e.g., "level_0_maintenance.tmx")
std::string getTmxFilename(const ParadroidLevel& level);
