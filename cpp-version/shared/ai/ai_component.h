#ifndef AI_COMPONENT_H
#define AI_COMPONENT_H

#include "raylib.h"
#include "units/weapon.h"

struct UnitInstance;

//------------------------------------------------------------------------------
// AI State
//------------------------------------------------------------------------------

enum class AIState {
    Patrol,     // Move between linked waypoints randomly
    Chase,      // Armed hostile: pursue player via waypoints, fire when able
    Flee        // Unarmed damaged: move away from player via waypoints
};

//------------------------------------------------------------------------------
// AI Component — per-enemy AI state attached to a UnitInstance
//------------------------------------------------------------------------------

struct AIComponent {
    AIState state = AIState::Patrol;

    // Waypoint navigation
    int currentWaypoint = -1;     // Waypoint index the droid is at/near
    int targetWaypoint = -1;      // Waypoint index moving toward
    int previousWaypoint = -1;    // For back-avoidance bias
    float dwellTimer = 0.0f;      // Pause duration remaining at waypoint

    // Hostility
    bool hostile = false;

    // Detection
    float detectionRadius = 0.0f; // proximityRadius — triggers Chase
    float visualRange = 0.0f;     // visualRadius — disengage range

    // Capabilities (cached from DroidProperties)
    bool armed = false;           // weapon >= 0
    bool hasTurret = false;
    bool omnidirectional = false;

    // Weapon state for cooldown tracking
    WeaponState weaponState;

    // Reference back to unit
    UnitInstance* unit = nullptr;
};

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

inline constexpr float AI_MOVEMENT_FORCE = 7.0f;        // Same as player
inline constexpr float AI_WAYPOINT_ARRIVAL_DIST = 0.5f;  // Distance to consider "arrived"
inline constexpr float AI_DWELL_MIN = 0.5f;
inline constexpr float AI_DWELL_MAX = 2.0f;
inline constexpr float AI_BACK_AVOIDANCE_WEIGHT = 0.2f;  // Reduced probability for previous waypoint
inline constexpr float AI_COLINEAR_THRESHOLD = 0.7f;     // Dot product threshold to skip dwell
inline constexpr float AI_FACING_THRESHOLD = 0.25f;      // Radians (~14 degrees) for fire alignment
inline constexpr float AI_MAX_TORQUE = 100.0f;
inline constexpr float AI_ROTATION_KP = 30.0f;
inline constexpr float AI_ROTATION_KD = 5.0f;

#endif // AI_COMPONENT_H
