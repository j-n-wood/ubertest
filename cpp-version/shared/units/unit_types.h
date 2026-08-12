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
// Droid Properties (typed fields for unit-level gameplay data)
//------------------------------------------------------------------------------

struct DroidProperties {
    int classId = -1;
    int typeCode = 0;
    int energy = 0;
    float armour = 0.0f;
    int weapon = -1;           // Weapon ID, -1 = unarmed
    int droidType = 0;
    int driveType = 0;
    int brainType = 0;
    bool hasTurret = false;        // Has a turret-role section (may be authored or derived)
    bool omnidirectional = false;  // Never orient: body facing is held at angle 0
    bool fireWhileMoving = false;  // Body aims at target, unit doesn't halt to fire, no LOS facing gate
    float turretTurnSpeed = 0.0f;  // Per-unit TURRET section slew rate (rad/s); 0 = global default
    float headTurnSpeed = 0.0f;    // Per-unit HEAD section slew rate (rad/s); 0 = global default
    float visualRadius = 0.0f;    // Visual detection / disengage range
    // Drip decals: while damaged BELOW this many health points and moving, the droid leaks fluid
    // marks onto the floor (see game_update_drips / DecalManager). 0 = never drips.
    float dripThreshold = 0.0f;
    Vector3 fireOffset = {0, 0, 0}; // Projectile spawn offset from unit centre
    std::string description;
};

//------------------------------------------------------------------------------
// Section Rotation Mode
//------------------------------------------------------------------------------

enum class SectionRotationMode {
    FollowUnit,         // Section rotates with unit physics rotation (default)
    FollowFacing,       // Section rotates to face a target angle (e.g., turret)
    Fixed               // Section maintains fixed world rotation
};

//------------------------------------------------------------------------------
// Section Role — what an independently-aiming section is FOR. A non-None role
// implies FollowFacing rotation (the section slews toward an aim angle). See
// docs/unit_animation.md.
//------------------------------------------------------------------------------

enum class SectionRole {
    None,               // Ordinary section: rotates per rotationMode (default)
    Turret,             // Aiming section that DETERMINES the unit's firing angle
    Head                // Aiming section used ONLY for the visibility/facing cone
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

    // Role: turret (drives firing angle) or head (drives visibility cone). A non-None
    // role forces FollowFacing at load. Default None = ordinary section.
    SectionRole role = SectionRole::None;

    // GLTF animation: when true this section plays skeletal clip 1 while the unit is
    // moving and clip 0 while idle (the legacy "anim_moving" marker). See
    // docs/unit_animation.md.
    bool animMoving = false;

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

    // Per-type movement limits, in the ORIGINAL droidclasses.txt units
    // (speed/acceleration/deceleration from droid_class.cpp). The movement layer
    // scales these to world units via MOVEMENT_UNIT_SCALE (movement_tuning.h) and
    // maps them onto the motor joint: acceleration/deceleration -> max force,
    // maxSpeed + acceleration -> linear damping (terminal velocity). A value of 0
    // means "unspecified" — the unit falls back to the global motor tuning.
    float maxSpeed = 0.0f;                     // Top speed
    float acceleration = 0.0f;                 // Force authority while speeding up
    float deceleration = 0.0f;                 // Force authority while braking
    float turnSpeed = 0.0f;                    // Max facing turn rate (rad/s); 0 = global default
    float coastDamping = -1.0f;                // Linear damping while coasting (drive released);
                                               // <0 = disabled (crisp stop), lower = floatier drift

    SectionDefinition rootSection;
    DroidProperties properties;               // Droid gameplay properties
};

#endif // UNIT_TYPES_H
