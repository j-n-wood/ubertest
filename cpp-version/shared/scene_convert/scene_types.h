#ifndef SCENE_TYPES_H
#define SCENE_TYPES_H

#include "raylib.h"
#include <string>
#include <vector>
#include <optional>
#include <array>

//------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------

struct Ship;
struct Domain;
struct Area;
struct Tile;
struct Feature;
struct PathGeometry;
struct Waypoint;
struct Transporter;
struct Decks;

//------------------------------------------------------------------------------
// Basic Types
//------------------------------------------------------------------------------

struct Bounds {
    Vector3 min = {0, 0, 0};
    Vector3 max = {0, 0, 0};
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

//------------------------------------------------------------------------------
// Collision Types (for Box2D)
//------------------------------------------------------------------------------

struct CollisionPolygon {
    std::vector<Vector2> vertices;  // CCW order, max 8 for Box2D
};

struct CollisionChain {
    std::vector<Vector2> vertices;
    bool loop = false;
};

struct CollisionData {
    std::vector<CollisionPolygon> polygons;
    std::vector<CollisionChain> chains;
};

// Feature collision (box or circle)
struct FeatureCollision {
    enum class Type { Box, Circle };
    Type type = Type::Box;
    Vector2 halfExtents = {0.5f, 0.5f};  // for box
    float radius = 0.5f;                  // for circle
};

//------------------------------------------------------------------------------
// Tile Definition
//------------------------------------------------------------------------------

struct TileVertex {
    Vector3 position = {0, 0, 0};
    Vector2 uv1 = {0, 0};
    Vector2 uv2 = {0, 0};
};

struct TileTextures {
    std::string diffuse;
    std::string bump;
    std::string effect;
};

struct TileProperties {
    Vector3 diffuseColour = {1, 1, 1};
    Vector3 specularColour = {0, 0, 0};
    int tileType = 0;
    bool additiveBlend = false;
    bool alphaBlend = false;
    int effectTexture = -1;
    int effectRenderMode = -1;
    int effectBlendSource = -1;
    int effectBlendDest = -1;
    bool texRotate = false;
};

struct Tile {
    std::vector<TileVertex> vertices;
    TileTextures textures;
    TileProperties properties;

    // Original texture indices (for debugging/reference)
    int textureIndex1 = 0;
    int textureIndex2 = 0;
};

//------------------------------------------------------------------------------
// Archetile Definition (from tiles.txt)
//------------------------------------------------------------------------------

struct Archetile {
    std::string name;
    int index = 0;
    Tile tile;  // The template tile data
};

//------------------------------------------------------------------------------
// Feature Definition
//------------------------------------------------------------------------------

struct FeatureFlags {
    bool indestructible = false;
    bool solid = true;
    bool fullbright = false;
};

struct Feature {
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};  // radians
    FeatureFlags flags;
    int renderIndex = 0;           // Original index
    std::string model;             // Resolved model path
    std::optional<FeatureCollision> collision;
};

//------------------------------------------------------------------------------
// Path Geometry (from XML)
//------------------------------------------------------------------------------

struct PathNode {
    int id = 0;
    Vector3 position = {0, 0, 0};
};

struct ControlPoint {
    Vector3 position = {0, 0, 0};
};

struct PathLink {
    int id = 0;
    int start = 0;
    int finish = 0;
    std::optional<ControlPoint> control;
    std::vector<int> profiles;
    // When `profiles` is empty: if true (default), the link uses the geometry's default profile set
    // (the ids in <Profiles>) — this is how interior walls are declared. XML `defaultProfiles="0"`
    // sets this false (a wall-less structural link).
    bool useDefaultProfiles = true;
};

struct PathProfile {
    int id = 0;
    std::vector<Vector2> points;
};

struct PathArea {
    int id = 0;
    int materialId = 0;
    std::vector<int> links;
};

struct PathGeometry {
    std::vector<PathNode> nodes;
    std::vector<PathLink> links;
    std::vector<PathProfile> profiles;
    std::vector<PathArea> areas;
    std::string sourceFile;  // Original XML file path
};

//------------------------------------------------------------------------------
// Waypoint Definition
//------------------------------------------------------------------------------

struct WaypointFlags {
    bool start = false;      // Valid droid spawn point
    bool console = false;    // Near a console
    bool recharge = false;   // Near a charger
    bool lift = false;       // Near a lift/transporter
    bool transmat = false;   // Transmat beam destination
};

struct Waypoint {
    int id = 0;
    Vector3 position = {0, 0, 0};
    WaypointFlags flags;
    std::array<int, 6> neighbors = {0, 0, 0, 0, 0, 0};
};

//------------------------------------------------------------------------------
// Object Definitions
//------------------------------------------------------------------------------

struct DoorProperties {
    float mass = 1.0f;
    bool alwaysRender = false;
    Vector3 spin = {0, 0, 0};
};

struct Door {
    int id = 0;
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};  // radians
    Vector2 size = {10, 25};
    int state = 0;                 // 0=closed, 1=open
    std::array<int, 2> waypoints = {0, 0};
    DoorProperties properties;
    FeatureCollision collision;
};

struct Console {
    int id = 0;
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};
    int waypointId = 0;
    int targetObjectId = 0;
    // Console also has size/mass/etc from parsing but typically fixed
    Vector2 size = {10, 25};
    float mass = 0.0f;
    bool alwaysRender = false;
    bool fixed = false;
};

struct Charger {
    int id = 0;
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};
};

struct DestructibleProperties {
    bool fixed = false;
    struct Spin {
        char axis = 'z';
        float speed = 0.0f;
    };
    std::optional<Spin> spin;
};

struct Destructible {
    int id = 0;
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};
    int renderIndex = 0;
    std::string model;
    int hitPoints = 50;
    DestructibleProperties properties;
    std::optional<FeatureCollision> collision;
};

// Generic object (for unknown types)
struct GenericObject {
    int id = 0;
    Vector3 position = {0, 0, 0};
    Vector3 rotation = {0, 0, 0};
    Vector2 size = {0, 0};
    int typeId = 0;
    float mass = 0.0f;
    bool alwaysRender = false;
    Vector3 spin = {0, 0, 0};
};

struct Objects {
    std::vector<Door> doors;
    std::vector<Console> consoles;
    std::vector<Charger> chargers;
    std::vector<Destructible> destructibles;
    std::vector<GenericObject> generic;
};

//------------------------------------------------------------------------------
// Spawn Definition
//------------------------------------------------------------------------------

struct Spawn {
    int droidClass = 0;
    int waypointIndex = 0;
    float angle = 0.0f;  // radians
};

//------------------------------------------------------------------------------
// Area Definition
//------------------------------------------------------------------------------

struct Area {
    Bounds bounds;
    std::vector<Tile> tiles;
    std::vector<Feature> features;
    std::vector<PathGeometry> geometry;
    CollisionData collision;
};

//------------------------------------------------------------------------------
// Domain Definition (one level/deck)
//------------------------------------------------------------------------------

struct DomainMetadata {
    std::string sourceFile;
    std::string conversionDate;
};

struct Domain {
    std::string version = "1.0";
    int levelNumber = 0;
    std::string name;
    int ambience = -1;
    std::array<int, 9> profile = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    Bounds bounds;
    std::vector<Area> areas;
    std::vector<Waypoint> waypoints;
    Objects objects;
    std::vector<Spawn> spawns;
    DomainMetadata metadata;
};

//------------------------------------------------------------------------------
// Transporter Definition (lift connections between domains)
//------------------------------------------------------------------------------

struct Transporter {
    int id = 0;
    int domainIndex = 0;
    Vector3 position = {0, 0, 0};
    int levelUp = -1;
    int levelDown = -1;
    int liftRow = 0;
};

//------------------------------------------------------------------------------
// Decks Definition (deck plan UI layout)
//------------------------------------------------------------------------------

struct Elevator {
    int id = 0;
    Rect rect;
};

struct DomainRect {
    int domainIndex = 0;
    int rectNumber = 0;
    Rect rect;
};

struct Decks {
    std::vector<Elevator> elevators;
    std::vector<DomainRect> domainRects;
};

//------------------------------------------------------------------------------
// Ship Definition (top-level container)
//------------------------------------------------------------------------------

struct ShipMetadata {
    std::string sourceFile;
    std::string conversionDate;
    std::string toolVersion = "1.0.0";
};

struct Ship {
    std::string version = "1.0";
    std::string name;
    int crew = 0;
    int capacity = 0;
    std::vector<std::string> description;
    std::vector<std::string> domainPaths;  // Relative paths to domain JSON files
    std::vector<Transporter> transporters;
    Decks decks;
    ShipMetadata metadata;

    // Loaded domains (populated when viewing, not stored in JSON)
    std::vector<Domain> domains;
};

//------------------------------------------------------------------------------
// Texture/Asset Mapping
//------------------------------------------------------------------------------

struct TextureMapping {
    int index = 0;
    std::string path;
};

struct RenderObjectMapping {
    int index = 0;
    std::string modelPath;
};

#endif // SCENE_TYPES_H
