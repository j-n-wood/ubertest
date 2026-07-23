#ifndef AI_MANAGER_H
#define AI_MANAGER_H

#include "ai_component.h"
#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>

struct UnitInstance;
struct SpawnEntry;
struct ProjectileManager;

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

    // Tick all AI components.
    void update(float dt, Vector2 playerPos, b2WorldId worldId,
                ProjectileManager* projectiles);

    // Notify that a unit took damage — triggers Chase (armed) or Flee (unarmed).
    void onDamageTaken(UnitInstance* unit);

    // Access (mainly for testing)
    const std::vector<AIComponent>& components() const { return components_; }
    std::vector<AIComponent>& components() { return components_; }
    int componentCount() const { return static_cast<int>(components_.size()); }

private:
    void updatePatrol(AIComponent& ai, float dt, Vector2 playerPos);
    void updateChase(AIComponent& ai, float dt, Vector2 playerPos,
                     b2WorldId worldId, ProjectileManager* projectiles);
    void updateFlee(AIComponent& ai, float dt, Vector2 playerPos);

    // Waypoint selection
    int selectPatrolTarget(const AIComponent& ai) const;
    int selectChaseTarget(const AIComponent& ai, Vector2 playerPos) const;
    int selectFleeTarget(const AIComponent& ai, Vector2 playerPos) const;

    // Movement and rotation helpers.
    // setMotion drives the unit toward `moveTarget` (via a bounded carrot) while
    // facing `facing`, by setting its motor-joint target. holdPosition parks the
    // unit at its current transform (used when dwelling / halting / no target).
    void setMotion(AIComponent& ai, Vector2 moveTarget, float facing) const;
    void holdPosition(AIComponent& ai) const;
    bool isAtWaypoint(const AIComponent& ai, Vector2 waypointPos) const;
    Vector2 getUnitPosition(const AIComponent& ai) const;
    Vector2 waypointPos2D(int index) const;

    // Firing
    bool canFire(const AIComponent& ai, Vector2 playerPos) const;
    void tryFireAtPlayer(AIComponent& ai, Vector2 playerPos,
                         b2WorldId worldId, ProjectileManager* projectiles);

    std::vector<AIComponent> components_;
    std::vector<Vector3> waypointPositions_;
    std::vector<std::vector<int>> adjacency_;
};

#endif // AI_MANAGER_H
