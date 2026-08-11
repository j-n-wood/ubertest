#ifndef AI_MANAGER_H
#define AI_MANAGER_H

#include "ai_component.h"
#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>

struct UnitInstance;
struct SpawnEntry;
struct ProjectileManager;
class BeamManager;

//------------------------------------------------------------------------------
// AI Manager — updates all enemy AI components each frame
//------------------------------------------------------------------------------

class AIManager {
public:
    // Initialise AI components for spawned enemies.
    // waypointPositions: world-space Vector3 (x,z used as 2D).
    // adjacency: adjacency[i] = vector of connected waypoint indices.
    // enemies: pointers to spawned UnitInstance objects (parallel with spawns).
    void init(const std::vector<SpawnEntry>& spawns,
              const std::vector<Vector3>& waypointPositions,
              const std::vector<std::vector<int>>& adjacency,
              const std::vector<UnitInstance*>& enemies);

    // Tick all AI components. `beams`/`playerUnit` are optional — supplied by the game so
    // beam-weapon units can hitscan-damage the player; nullptr (e.g. in tests) disables beams.
    void update(float dt, Vector2 playerPos, b2WorldId worldId,
                ProjectileManager* projectiles,
                BeamManager* beams = nullptr, UnitInstance* playerUnit = nullptr);

    // Notify that a unit took damage — triggers Chase (armed) or Flee (unarmed).
    void onDamageTaken(UnitInstance* unit);

    // Process unit↔unit and unit↔wall contacts from the last physics step. A
    // non-hostile unit pauses on contact and retreats to its prior waypoint after
    // repeated hits (see AIComponent collision constants). Call after b2World_Step.
    void processCollisions(b2WorldId worldId);

    // React to a single contact pair (either party may be null for a wall/other).
    // Public so it can be driven directly in tests; processCollisions calls it.
    void onCollision(UnitInstance* a, UnitInstance* b);

    // Transfer support: mark a unit's AI component as player-controlled (skipped by
    // update/collisions) or clear it. `forgetUnit` nulls the component for a unit that
    // is about to be destroyed so nothing dereferences it. No-ops if the unit has no
    // component (e.g. the player device). See docs/transfer.md.
    void setControlled(UnitInstance* unit, bool controlled);
    void forgetUnit(UnitInstance* unit);

    // Access (mainly for testing)
    const std::vector<AIComponent>& components() const { return components_; }
    std::vector<AIComponent>& components() { return components_; }
    int componentCount() const { return static_cast<int>(components_.size()); }

private:
    void updatePatrol(AIComponent& ai, float dt, Vector2 playerPos);
    void updateChase(AIComponent& ai, float dt, Vector2 playerPos,
                     b2WorldId worldId, ProjectileManager* projectiles,
                     BeamManager* beams, UnitInstance* playerUnit);
    void updateFlee(AIComponent& ai, float dt, Vector2 playerPos);

    // Waypoint selection
    int selectPatrolTarget(const AIComponent& ai) const;
    int selectChaseTarget(const AIComponent& ai, Vector2 playerPos) const;
    int selectFleeTarget(const AIComponent& ai, Vector2 playerPos) const;

    // Collision / avoidance helpers
    AIComponent* findComponent(UnitInstance* unit);
    void handleCollision(AIComponent& ai, UnitInstance* other);

    // Off-course recovery / path validation
    // pathClear: is a straight move of the given (spherical) radius from `from` to
    // `to` free of solid walls? Casts against CATEGORY_STATIC only, so other units
    // do not count as blockers. nearestWaypoint: closest node by distance.
    // nearestReachableWaypoint: closest node whose path from `pos` is wall-clear
    // (falls back to the plain nearest if none qualify).
    // includeDoors: when true the cast also blocks on CLOSED doors (CATEGORY_DOOR) — used
    // for firing line-of-sight. Pathfinding leaves it false so units don't reroute around
    // doors (which open on proximity). Open doors clear their filter and never block either way.
    bool pathClear(Vector2 from, Vector2 to, float radius, bool includeDoors = false) const;
    int  nearestWaypoint(Vector2 pos) const;
    int  nearestReachableWaypoint(Vector2 pos, float radius) const;

    // Movement and rotation helpers.
    // setMotion drives the unit toward `moveTarget` (via a bounded carrot) while
    // facing `facing`, by setting its motor-joint target. holdPosition parks the
    // unit at its current transform (used when dwelling / halting / no target).
    void setMotion(AIComponent& ai, Vector2 moveTarget, float facing) const;
    void holdPosition(AIComponent& ai) const;

    // Slew the unit's turret/head sections toward `aimAngle` (radians) at the unit's
    // turret rate. Render-only: sets each aiming section's facingAngle. No-op if the
    // unit has neither. Called with the target angle when engaging, else the body angle
    // so an idle turret settles facing forward instead of freezing at its last angle.
    void updateAimingSections(AIComponent& ai, float aimAngle, float dt) const;
    bool isAtWaypoint(const AIComponent& ai, Vector2 waypointPos) const;
    Vector2 getUnitPosition(const AIComponent& ai) const;
    Vector2 waypointPos2D(int index) const;

    // Sight cone: true if the unit has neither a head nor a turret, or `targetPos` lies within
    // the forward cone (AI_HEAD_VISION_DOT) of its head — or, if it has no head, its turret (in
    // that order). Gates detection and firing so oriented units "only see what they face".
    bool sightConeSeesTarget(const AIComponent& ai, Vector2 targetPos) const;

    // True if the unit currently has sight of the player: within visual range, a clear
    // line of sight (walls + closed doors block it), and — for head units — inside the head
    // vision cone. Drives the lose-sight timeout in updateChase.
    bool hasSightOfPlayer(const AIComponent& ai, Vector2 playerPos) const;

    // Firing
    bool canFire(const AIComponent& ai, Vector2 playerPos) const;
    void tryFireAtPlayer(AIComponent& ai, Vector2 playerPos,
                         b2WorldId worldId, ProjectileManager* projectiles);

    // Beam weapons: active while armed, the player is within maxRange, and there's a clear
    // sightline (walls + closed doors block it, head cone respected) — no fire-rate gate. The
    // beam sweeps with the firing section as it aims; damage lands via the hitscan geometry.
    bool beamActive(const AIComponent& ai, Vector2 playerPos) const;
    void fireBeamAtPlayer(AIComponent& ai, float dt, Vector2 playerPos,
                          b2WorldId worldId, BeamManager* beams, UnitInstance* playerUnit);

    std::vector<AIComponent> components_;
    std::vector<Vector3> waypointPositions_;
    std::vector<std::vector<int>> adjacency_;

    // Cached each update() so movement helpers can raycast against the world.
    b2WorldId m_worldId = b2_nullWorldId;
};

#endif // AI_MANAGER_H
