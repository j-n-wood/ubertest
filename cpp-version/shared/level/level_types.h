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
// Lift marker (from a TMX object-layer point object with elevator/stop_index
// custom properties). Identifies a lift tile as stop `stopIndex` on elevator
// `elevator`; the LiftManager groups these across levels into elevator chains.
//------------------------------------------------------------------------------

struct TmxLift {
    int col = 0;              // Tile column (object pixel x / tileWidth)
    int row = 0;              // Tile row    (object pixel y / tileHeight)
    int elevator = 0;         // Which elevator/shaft this lift accesses
    int stopIndex = 0;        // Position on that elevator (0-based, ascending)
};

//------------------------------------------------------------------------------
// Tile Collision Shape (from TSX objectgroup)
//------------------------------------------------------------------------------

struct TileCollisionRect {
    float x = 0.0f;           // Offset from tile origin (top-left in TMX coords)
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

//------------------------------------------------------------------------------
// Tile Properties (per-tile metadata from TSX)
//------------------------------------------------------------------------------

struct TmxTileProperties {
    // Physics properties
    bool solid = true;        // Blocks movement
    bool floor = true;        // Walkable surface
    int collisionShape = 0;   // 0=full, 1=half, etc. (legacy)

    // Collision rectangles from TSX objectgroup
    std::vector<TileCollisionRect> collisionRects;

    // Bump mapping override (future)
    int bumpTileIndex = -1;   // -1 = use default flat normal

    // 3D object replacement (future)
    std::string modelPath;    // Empty = render as tile

    // Semantic tile tagging (custom TSX properties)
    std::string type;         // e.g. "door" (empty = plain tile)
    std::string orientation;  // e.g. "horizontal" / "vertical" (for doors)
    float closed = 1.0f;      // door frame openness: 1 = closed .. 0 = fully open

    bool hasCollision() const { return !collisionRects.empty(); }
    bool isDoor() const { return type == "door"; }
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
    int number = -1;          // Stable deck number parsed from "level_<N>_name.tmx"
                              // (array index != N because files sort lexicographically)
    int width = 0;            // Grid width in tiles
    int height = 0;           // Grid height in tiles
    int tileWidth = 64;       // Tile size in pixels
    int tileHeight = 64;
    std::vector<int> tiles;   // Row-major tile IDs (0 = empty, 1+ = tile)
    std::vector<TmxWaypoint> waypoints;
    std::vector<TmxLift> lifts; // Lift markers from the object layer
    std::string tilesetSource; // Reference to TSX file (e.g., "default.tsx")
    int tileRow = 0;           // Base tileset colour row (map property "tileRow"); the tileset
                               // atlas stacks colour-variant rows of the same tiles.
};

//------------------------------------------------------------------------------
// Render Mode
//------------------------------------------------------------------------------

enum class LevelRenderMode {
    Tilemap,         // Mode 1: Direct tileset atlas rendering (flat normal map)
    CustomTiles,     // Mode 2: Per-tile bump maps and material properties
    Objects3D        // Mode 3: 3D object replacement for some tiles (future)
};

// Legacy alias for backwards compatibility
constexpr LevelRenderMode TilesetAtlas = LevelRenderMode::Tilemap;

//------------------------------------------------------------------------------
// Custom Tile Rendering Properties (from tiles.json)
//------------------------------------------------------------------------------

struct TileRenderProperties {
    int bumpTileIndex = 0;              // Index into bump atlas (0 = flat normal)
    float specularIntensity = 0.5f;     // Specular highlight strength (0.0-1.0)
    float albedoMultiplier[3] = {1.0f, 1.0f, 1.0f};  // RGB multiplier for diffuse
};

struct BumpAtlasConfig {
    std::string texture;                // Filename (relative to assets/textures/)
    int tileWidth = 128;
    int tileHeight = 128;
    int columns = 8;
};

struct TilePropertiesConfig {
    int version = 1;
    BumpAtlasConfig bumpAtlas;
    std::map<int, TileRenderProperties> tiles;  // Keyed by tile ID
    TileRenderProperties defaults;
    bool valid = false;                 // True if successfully loaded

    // Get properties for a tile ID, falling back to defaults
    const TileRenderProperties& getProperties(int tileId) const {
        auto it = tiles.find(tileId);
        if (it != tiles.end()) {
            return it->second;
        }
        return defaults;
    }
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
    // Objects3D: tileModel mesh indices tagged glass (drawtype 5) in the bundle manifest — drawn in a
    // separate transparent env-mapped pass and skipped in the opaque pass.
    std::vector<int> glassMeshIndices;

    // Waypoint visualization data
    std::vector<Vector3> waypointPositions;
    std::vector<std::pair<int, int>> waypointLinks; // pairs of waypoint indices

    // Per-waypoint "droid start" flag (uber's waypoint `start`): only these are valid AI-spawn
    // points. Parallel to waypointPositions. Empty if the source didn't provide flags (TMX path),
    // in which case spawn resolution falls back to all waypoints. See resolveSpawns.
    std::vector<uint8_t> waypointIsStart;

    // Waypoint adjacency list — waypointAdjacency[i] = indices of waypoints linked to i
    std::vector<std::vector<int>> waypointAdjacency;

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

//------------------------------------------------------------------------------
// Level Collision Data (generated from tiles with collision shapes)
//------------------------------------------------------------------------------

struct CollisionRect {
    float x = 0.0f;           // World X (center)
    float z = 0.0f;           // World Z (center)
    float halfWidth = 0.0f;   // Half extent in X
    float halfHeight = 0.0f;  // Half extent in Z
};

struct LevelCollisionData {
    std::vector<CollisionRect> rects;  // Optimized collision rectangles

    // Debug visualization data
    std::vector<Vector3> debugVertices;  // Line vertices for outline rendering
    int debugVertexCount = 0;

    // Bounds
    Vector3 boundsMin = {0, 0, 0};
    Vector3 boundsMax = {0, 0, 0};
};

#endif // LEVEL_TYPES_H
