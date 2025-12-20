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
// Section Definition (loaded from JSON, immutable template)
//------------------------------------------------------------------------------

struct SectionDefinition {
    std::string name;
    std::string modelPath;                    // Relative path to GLTF file

    // Transform relative to parent
    Vector2 localOffset = {0, 0};             // 2D offset (physics coords)
    float localRotation = 0.0f;               // Radians
    float height = 0.0f;                      // Y offset for 3D rendering
    Vector3 scale = {1, 1, 1};                // Model scale

    // Physics (optional)
    std::optional<PhysicsProperties> physics;

    // Joint properties for connection to parent
    float jointBreakForce = 0.0f;             // 0 = unbreakable
    float jointBreakTorque = 0.0f;            // 0 = unbreakable

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
    SectionDefinition rootSection;
    PropertyMap properties;                   // Unit-level properties
};

#endif // UNIT_TYPES_H
