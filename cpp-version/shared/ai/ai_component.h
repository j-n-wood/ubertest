#ifndef AI_COMPONENT_H
#define AI_COMPONENT_H

#include "raylib.h"
#include "units/weapon.h"

struct UnitInstance;
class SectionInstance;

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
    bool hasTurret = false;       // has a turret-role section (derived at spawn)
    bool hasHead = false;         // has a head-role section (derived at spawn)
    bool omnidirectional = false; // never orient: hold body angle 0
    bool fireWhileMoving = false; // aim body at target, don't halt to fire, no LOS facing gate
    float turretTurnSpeed = 0.0f; // per-unit turret-section slew rate (rad/s); 0 = global default
    float headTurnSpeed = 0.0f;   // per-unit head-section slew rate (rad/s); 0 = global default

    // Per-unit patrol dwell range (s) at a waypoint, derived from typeCode at init (see
    // dwellRangeForType). Defaults to the global AI_DWELL_MIN/MAX; typeCode 300-399 → 0/0
    // (never pause). A random value in [dwellMin, dwellMax] is taken on each arrival.
    float dwellMin = 0.5f;
    float dwellMax = 1.2f;

    // Cached aiming sections (nullptr if the unit has none). Slewed toward an aim angle
    // each frame; the turret's facing sets the firing angle, the head's the visibility cone.
    SectionInstance* turretSection = nullptr;
    SectionInstance* headSection = nullptr;

    // Weapon state for cooldown tracking
    WeaponState weaponState;

    // Time (s) the unit has gone without sight of the player while chasing. Reset to 0
    // whenever it can see the player; once it reaches AI_LOSE_SIGHT_TIME the unit gives up
    // and reverts to Patrol. See updateChase.
    float loseSightTimer = 0.0f;

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
inline constexpr float AI_DWELL_MIN = 0.5f;  // default patrol pause range at a waypoint
inline constexpr float AI_DWELL_MAX = 1.2f;  // (per-unit override derived from typeCode)
inline constexpr float AI_BACK_AVOIDANCE_WEIGHT = 0.2f;  // Reduced probability for previous waypoint
inline constexpr float AI_COLINEAR_THRESHOLD = 0.7f;     // Dot product threshold to skip dwell
inline constexpr float AI_FACING_THRESHOLD = 0.25f;      // Radians (~14 degrees) for fire alignment

// Head vision cone: a unit with a head-role section can only see (detect / keep line of
// sight on) a target within this forward cone of the head's current facing. Dot product
// of the head-forward unit vector and the unit-to-target direction; >= this passes.
// 0.3 ≈ a 145°-wide cone (72.5° either side of where the head points).
inline constexpr float AI_HEAD_VISION_DOT = 0.3f;

// A hostile (chasing) unit gives up and reverts to Patrol after this long without a clear
// line of sight to the player (broke LOS around a corner / behind a closed door, left the
// visual range, or — for head units — left the vision cone).
inline constexpr float AI_LOSE_SIGHT_TIME = 2.0f;  // seconds

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
