#ifndef UNIT_TYPES_H
#define UNIT_TYPES_H

#include "raylib.h"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <variant>

//------------------------------------------------------------------------------
// Physics Shape Definitions
//------------------------------------------------------------------------------

enum class PhysicsShapeType {
    None,
    Circle,
    Box,
    Polygon
};

struct CircleShapeDef {
    float radius = 0.5f;
    Vector2 offset = {0, 0};
};

struct BoxShapeDef {
    float width = 1.0f;
    float height = 1.0f;
    Vector2 offset = {0, 0};
};

struct PolygonShapeDef {
    std::vector<Vector2> vertices;  // Max 8 vertices for Box2D
};

//------------------------------------------------------------------------------
// Physics Properties
//------------------------------------------------------------------------------

struct PhysicsProperties {
    PhysicsShapeType shapeType = PhysicsShapeType::None;
    CircleShapeDef circle;
    BoxShapeDef box;
    PolygonShapeDef polygon;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    float linearDamping = 4.0f;
    float angularDamping = 8.0f;
    bool isSensor = false;
};

//------------------------------------------------------------------------------
// Custom Properties (arbitrary key-value data)
//------------------------------------------------------------------------------

// Property value can be bool, int, float, string, Vector2, or Vector3
using PropertyValue = std::variant<
    bool,
    int,
    float,
    std::string,
    Vector2,
    Vector3
>;

using PropertyMap = std::unordered_map<std::string, PropertyValue>;

//------------------------------------------------------------------------------
// Section Rotation Mode
//------------------------------------------------------------------------------

enum class SectionRotationMode {
    FollowUnit,         // Section rotates with unit physics rotation (default)
    FollowFacing,       // Section rotates to face a target angle (e.g., turret)
    Fixed               // Section maintains fixed world rotation
};

//------------------------------------------------------------------------------
// Section Definition (loaded from JSON, immutable template)
//------------------------------------------------------------------------------

struct SectionDefinition {
    std::string name;
    std::string modelPath;                    // Relative path to GLTF file

    // Transform relative to parent
    // offset: (x, y, z) where x/y are 2D physics coords, z is vertical height
    Vector3 offset = {0, 0, 0};
    float localRotation = 0.0f;               // Radians
    Vector3 scale = {1, 1, 1};                // Model scale

    // Rotation behavior
    SectionRotationMode rotationMode = SectionRotationMode::FollowUnit;

    // Physics (optional - used for debris when unit is dismantled)
    std::optional<PhysicsProperties> physics;

    // Custom game properties
    PropertyMap properties;

    // Child sections (recursive)
    std::vector<SectionDefinition> children;
};

//------------------------------------------------------------------------------
// Unit Definition (loaded from JSON, shared across instances)
//------------------------------------------------------------------------------

struct UnitDefinition {
    std::string name;
    std::string id;                           // Unique identifier for this unit type

    // Unit-level physics (single collision shape for entire unit)
    float collisionRadius = 0.5f;             // Collision shape radius (meters)
    float proximityRadius = 1.0f;             // Proximity detection radius for AI/sensing

    SectionDefinition rootSection;
    PropertyMap properties;                   // Unit-level properties
};

#endif // UNIT_TYPES_H
