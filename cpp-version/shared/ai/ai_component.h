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

    // Collision response: short decision cooldown after redirecting away from a
    // collision. This does NOT stop the unit — it keeps moving toward its new
    // target; the cooldown just debounces the redirect decision.
    float collideCooldown = 0.0f;

    // Stuck detection: time spent trying to move but barely moving (blocked by a
    // wall or wedged), used to abandon a blocked route and reselect. The timer is
    // reset whenever the pursued target changes, so each fresh route gets a full
    // window before being judged stuck.
    float stuckTimer = 0.0f;
    int   stuckWaypoint = -1;  // target the stuck timer is currently accruing against

    // Reference back to unit
    UnitInstance* unit = nullptr;

    // Set while the player has taken control of this unit (transfer mechanic). The
    // AIManager skips controlled components (no AI driving, no collision-redirect) so
    // player input alone moves the unit. See docs/transfer.md.
    bool controlled = false;
};

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Movement/rotation are driven by the shared motor-joint control layer; the drive
// parameters (max force/torque, correction factor, carrot lookahead, turret slew)
// live in movement_tuning.h and are identical for AI and player. The constants
// below are AI decision-logic only.
// Fine arrival tolerance so a unit reaches (near) the waypoint centre before it
// turns toward the next one. The map is tile-based with narrow doorways; turning
// early (the old 0.5) cut corners at an angle and clipped solid tiles. Reaching
// close to centre keeps trajectories aligned to the (grid-laid) waypoint links.
inline constexpr float AI_WAYPOINT_ARRIVAL_DIST = 0.15f; // Distance to consider "arrived"
inline constexpr float AI_DWELL_MIN = 0.5f;
inline constexpr float AI_DWELL_MAX = 2.0f;
inline constexpr float AI_BACK_AVOIDANCE_WEIGHT = 0.2f;  // Reduced probability for previous waypoint
inline constexpr float AI_COLINEAR_THRESHOLD = 0.7f;     // Dot product threshold to skip dwell
inline constexpr float AI_FACING_THRESHOLD = 0.25f;      // Radians (~14 degrees) for fire alignment

// Collision response: a non-hostile unit that bumps an obstacle REDIRECTS — it
// heads back toward its prior waypoint (and re-selects from there, with the
// existing back-avoidance bias steering it off the blocked route) — rather than
// stopping. Stopping/"stunning" was removed because in a cluster it locked units
// permanently. The cooldown debounces the redirect so a sustained contact doesn't
// re-decide every frame; the unit keeps moving the whole time.
inline constexpr float AI_COLLIDE_COOLDOWN = 0.6f;  // s between collision redirects

// Stuck detection: a unit that is trying to move toward a waypoint but stays below
// AI_STUCK_SPEED for AI_STUCK_TIME is blocked (pinned on a wall, or a deadlock).
// It abandons the blocked route and reselects, biased away from that direction.
// This is the general recovery for "just stops on a world collision": a wall
// contact only emits a begin-touch event once, so it can't be relied on to keep
// nudging a pinned unit — this speed check catches it regardless.
inline constexpr float AI_STUCK_SPEED = 0.5f;  // u/s: below this counts as "not moving"
inline constexpr float AI_STUCK_TIME  = 0.8f;  // s below the speed before giving up the route

#endif // AI_COMPONENT_H
