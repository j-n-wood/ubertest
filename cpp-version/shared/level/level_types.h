#ifndef LEVEL_TYPES_H
#define LEVEL_TYPES_H

#include "raylib.h"
#include <string>
#include <vector>
#include <map>

//------------------------------------------------------------------------------
// TMX Level Types
//
// Data structures for parsed TMX level files and TSX tileset definitions.
// Used by both level_viewer tool and game code.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Waypoint (from TMX object layer)
//------------------------------------------------------------------------------

struct TmxWaypoint {
    int id = 0;
    float x = 0.0f;           // Pixel coordinates (TMX space)
    float y = 0.0f;
    std::vector<int> links;   // Connected waypoint IDs
};

//------------------------------------------------------------------------------
// Tile Properties (per-tile metadata from TSX)
//------------------------------------------------------------------------------

struct TmxTileProperties {
    // Physics properties (STUB for future)
    bool solid = true;        // Blocks movement
    bool floor = true;        // Walkable surface
    int collisionShape = 0;   // 0=full, 1=half, etc.

    // Bump mapping override (future)
    int bumpTileIndex = -1;   // -1 = use default flat normal

    // 3D object replacement (future)
    std::string modelPath;    // Empty = render as tile
};

//------------------------------------------------------------------------------
// Tileset Definition (from TSX file)
//------------------------------------------------------------------------------

struct TmxTileset {
    std::string name;
    std::string imageSource;  // e.g., "map_blocks.png"
    int imageWidth = 0;
    int imageHeight = 0;
    int tileWidth = 64;
    int tileHeight = 64;
    int spacing = 2;
    int tileCount = 0;
    int columns = 0;
    int firstGid = 1;         // First global tile ID (from TMX reference)

    // Per-tile properties (indexed by local tile ID, 0-based)
    std::map<int, TmxTileProperties> tileProperties;
};

//------------------------------------------------------------------------------
// Level Definition (from TMX file)
//------------------------------------------------------------------------------

struct TmxLevel {
    std::string name;
    std::string filePath;     // Source TMX file path
    int width = 0;            // Grid width in tiles
    int height = 0;           // Grid height in tiles
    int tileWidth = 64;       // Tile size in pixels
    int tileHeight = 64;
    std::vector<int> tiles;   // Row-major tile IDs (0 = empty, 1+ = tile)
    std::vector<TmxWaypoint> waypoints;
    std::string tilesetSource; // Reference to TSX file (e.g., "default.tsx")
};

//------------------------------------------------------------------------------
// Render Mode
//------------------------------------------------------------------------------

enum class LevelRenderMode {
    TilesetAtlas,    // Mode 1: Direct tileset atlas rendering
    ExtendedBump,    // Mode 2: Per-tile bump texture coordinates (future)
    Objects3D        // Mode 3: 3D object replacement for some tiles (future)
};

//------------------------------------------------------------------------------
// Level Render Data (generated from TMX + Tileset)
//------------------------------------------------------------------------------

struct LevelRenderTile {
    Vector3 position;         // World position (center of tile)
    Vector2 uv0;              // Top-left UV
    Vector2 uv1;              // Bottom-right UV
    int tileId;               // Original tile ID for reference
};

struct LevelRenderData {
    // Tile geometry
    std::vector<LevelRenderTile> tiles;
    Mesh tileMesh;
    Model tileModel;
    bool meshValid = false;

    // Waypoint visualization data
    std::vector<Vector3> waypointPositions;
    std::vector<std::pair<int, int>> waypointLinks; // pairs of waypoint indices

    // Level metadata
    std::string levelName;
    int gridWidth = 0;
    int gridHeight = 0;
    int tileCount = 0;
    Vector3 boundsMin = {0, 0, 0};
    Vector3 boundsMax = {0, 0, 0};
};

//------------------------------------------------------------------------------
// Load Results
//------------------------------------------------------------------------------

struct TmxLoadResult {
    TmxLevel level;
    bool success = false;
    std::string errorMsg;
};

struct TsxLoadResult {
    TmxTileset tileset;
    bool success = false;
    std::string errorMsg;
};

#endif // LEVEL_TYPES_H
