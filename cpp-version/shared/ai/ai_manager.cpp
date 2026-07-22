#include "ai_manager.h"
#include "units/unit_instance.h"
#include "units/weapon.h"
#include "combat/projectile_manager.h"
#include "level/spawn_config.h"
#include "raymath.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

namespace {

float normalizeAngle(float angle) {
    while (angle > PI) angle -= 2.0f * PI;
    while (angle < -PI) angle += 2.0f * PI;
    return angle;
}

float randomFloat(float min, float max) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return min + t * (max - min);
}

} // namespace

//------------------------------------------------------------------------------
// Init
//------------------------------------------------------------------------------

void AIManager::init(const std::vector<SpawnEntry>& spawns,
                     const std::vector<Vector3>& waypointPositions,
                     const std::vector<std::vector<int>>& adjacency,
                     const std::vector<UnitInstance*>& enemies) {
    waypointPositions_ = waypointPositions;
    adjacency_ = adjacency;
    components_.clear();
    components_.reserve(enemies.size());

    for (size_t i = 0; i < enemies.size(); i++) {
        UnitInstance* unit = enemies[i];
        if (!unit || !unit->definition) continue;

        AIComponent ai;
        ai.unit = unit;
        ai.currentWaypoint = spawns[i].waypointIndex;
        ai.targetWaypoint = -1;
        ai.previousWaypoint = -1;

        const auto& def = *unit->definition;
        const auto& props = def.properties;
        ai.detectionRadius = def.proximityRadius;
        ai.visualRange = props.visualRadius;
        ai.armed = props.weapon >= 0;
        ai.hasTurret = props.hasTurret;
        ai.omnidirectional = props.omnidirectional;

        if (ai.armed) {
            ai.weaponState = initWeaponState(props);
        }

        components_.push_back(ai);
    }
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void AIManager::update(float dt, Vector2 playerPos, b2WorldId worldId,
                       ProjectileManager* projectiles) {
    for (auto& ai : components_) {
        if (!ai.unit || !ai.unit->active) continue;

        // Update weapon cooldown
        if (ai.armed) {
            updateWeaponCooldown(ai.weaponState, dt);
        }

        switch (ai.state) {
            case AIState::Patrol:
                updatePatrol(ai, dt, playerPos);
                break;
            case AIState::Chase:
                updateChase(ai, dt, playerPos, worldId, projectiles);
                break;
            case AIState::Flee:
                updateFlee(ai, dt, playerPos);
                break;
        }
    }
}

//------------------------------------------------------------------------------
// Damage callback
//------------------------------------------------------------------------------

void AIManager::onDamageTaken(UnitInstance* unit) {
    for (auto& ai : components_) {
        if (ai.unit == unit) {
            if (ai.armed) {
                ai.state = AIState::Chase;
                ai.hostile = true;
            } else {
                ai.state = AIState::Flee;
                ai.hostile = true;
            }
            return;
        }
    }
}

//------------------------------------------------------------------------------
// Patrol
//------------------------------------------------------------------------------

void AIManager::updatePatrol(AIComponent& ai, float dt, Vector2 playerPos) {
    // Detection check: armed droids detect player
    if (ai.armed && ai.detectionRadius > 0.0f) {
        Vector2 pos = getUnitPosition(ai);
        float dist = Vector2Distance(pos, playerPos);
        if (dist <= ai.detectionRadius) {
            ai.state = AIState::Chase;
            ai.hostile = true;
            return;
        }
    }

    // Dwelling at waypoint
    if (ai.dwellTimer > 0.0f) {
        ai.dwellTimer -= dt;
        return;
    }

    // Need a target?
    if (ai.targetWaypoint < 0) {
        ai.targetWaypoint = selectPatrolTarget(ai);
        if (ai.targetWaypoint < 0) return; // dead end
    }

    Vector2 targetPos = waypointPos2D(ai.targetWaypoint);
    Vector2 unitPos = getUnitPosition(ai);

    if (isAtWaypoint(ai, targetPos)) {
        // Arrived — check colinearity to decide whether to dwell
        Vector2 arrivalDir = {0, 0};
        if (ai.previousWaypoint >= 0) {
            Vector2 prevPos = waypointPos2D(ai.previousWaypoint);
            arrivalDir = Vector2Normalize(Vector2Subtract(unitPos, prevPos));
        }

        ai.previousWaypoint = ai.currentWaypoint;
        ai.currentWaypoint = ai.targetWaypoint;
        ai.targetWaypoint = -1;

        // Select next waypoint to test colinearity
        int next = selectPatrolTarget(ai);
        if (next >= 0) {
            Vector2 nextPos = waypointPos2D(next);
            Vector2 nextDir = Vector2Normalize(Vector2Subtract(nextPos, unitPos));
            float dot = Vector2DotProduct(arrivalDir, nextDir);

            if (ai.previousWaypoint >= 0 && dot > AI_COLINEAR_THRESHOLD) {
                // Colinear — continue without dwell
                ai.targetWaypoint = next;
            } else {
                // Not colinear or first waypoint — dwell
                ai.targetWaypoint = next;
                ai.dwellTimer = randomFloat(AI_DWELL_MIN, AI_DWELL_MAX);
            }
        }
        return;
    }

    // Move toward target
    moveTowardWaypoint(ai, targetPos);

    // Orient body to face movement direction
    Vector2 dir = Vector2Subtract(targetPos, unitPos);
    float targetAngle = atan2f(dir.y, dir.x);
    applyRotation(ai.unit->bodyId, targetAngle);
}

//------------------------------------------------------------------------------
// Chase
//------------------------------------------------------------------------------

void AIManager::updateChase(AIComponent& ai, float dt, Vector2 playerPos,
                            b2WorldId worldId, ProjectileManager* projectiles) {
    Vector2 unitPos = getUnitPosition(ai);
    float distToPlayer = Vector2Distance(unitPos, playerPos);

    // Disengage if player out of visual range
    if (ai.visualRange > 0.0f && distToPlayer > ai.visualRange) {
        ai.state = AIState::Patrol;
        ai.hostile = false;
        return;
    }

    bool isStandard = !ai.hasTurret && !ai.omnidirectional;
    float optimumRange = ai.weaponState.definition.optimumRange;

    // Standard droids halt within optimum range to turn and fire
    bool halting = isStandard && distToPlayer <= optimumRange;

    if (!halting) {
        // Continue waypoint navigation toward player
        if (ai.targetWaypoint < 0) {
            ai.targetWaypoint = selectChaseTarget(ai, playerPos);
            if (ai.targetWaypoint < 0) return;
        }

        Vector2 targetPos = waypointPos2D(ai.targetWaypoint);
        if (isAtWaypoint(ai, targetPos)) {
            ai.previousWaypoint = ai.currentWaypoint;
            ai.currentWaypoint = ai.targetWaypoint;
            ai.targetWaypoint = selectChaseTarget(ai, playerPos);
        }

        if (ai.targetWaypoint >= 0) {
            Vector2 wpPos = waypointPos2D(ai.targetWaypoint);
            moveTowardWaypoint(ai, wpPos);

            // Body orientation
            if (ai.omnidirectional) {
                // Body faces player while moving
                Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
                float angle = atan2f(toPlayer.y, toPlayer.x);
                applyRotation(ai.unit->bodyId, angle);
            } else {
                // Body faces movement direction (turret or standard)
                Vector2 dir = Vector2Subtract(wpPos, unitPos);
                float angle = atan2f(dir.y, dir.x);
                applyRotation(ai.unit->bodyId, angle);
            }
        }
    } else {
        // Halting — turn body to face player
        Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
        float angle = atan2f(toPlayer.y, toPlayer.x);
        applyRotation(ai.unit->bodyId, angle);
    }

    // Head tracking — all hostile droids turn head toward player
    if (ai.hasTurret && ai.unit->rootSection) {
        Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
        float headAngle = atan2f(toPlayer.y, toPlayer.x);
        // Find head section (first child with FollowFacing mode)
        for (auto* section : ai.unit->allSections) {
            if (section->definition &&
                section->definition->rotationMode == SectionRotationMode::FollowFacing) {
                section->facingAngle = headAngle;
                break;
            }
        }
    }

    // Try to fire
    tryFireAtPlayer(ai, playerPos, worldId, projectiles);
}

//------------------------------------------------------------------------------
// Flee
//------------------------------------------------------------------------------

void AIManager::updateFlee(AIComponent& ai, float dt, Vector2 playerPos) {
    if (ai.targetWaypoint < 0) {
        ai.targetWaypoint = selectFleeTarget(ai, playerPos);
        if (ai.targetWaypoint < 0) return;
    }

    Vector2 targetPos = waypointPos2D(ai.targetWaypoint);
    Vector2 unitPos = getUnitPosition(ai);

    if (isAtWaypoint(ai, targetPos)) {
        ai.previousWaypoint = ai.currentWaypoint;
        ai.currentWaypoint = ai.targetWaypoint;
        ai.targetWaypoint = selectFleeTarget(ai, playerPos);
    }

    if (ai.targetWaypoint >= 0) {
        Vector2 wpPos = waypointPos2D(ai.targetWaypoint);
        moveTowardWaypoint(ai, wpPos);

        // Orient body to face movement direction
        Vector2 dir = Vector2Subtract(wpPos, unitPos);
        float angle = atan2f(dir.y, dir.x);
        applyRotation(ai.unit->bodyId, angle);
    }
}

//------------------------------------------------------------------------------
// Waypoint Selection
//------------------------------------------------------------------------------

int AIManager::selectPatrolTarget(const AIComponent& ai) const {
    if (ai.currentWaypoint < 0 ||
        ai.currentWaypoint >= static_cast<int>(adjacency_.size())) {
        return -1;
    }

    const auto& neighbours = adjacency_[ai.currentWaypoint];
    if (neighbours.empty()) return -1;
    if (neighbours.size() == 1) return neighbours[0];

    // Weighted random selection with back-avoidance bias
    std::vector<float> weights;
    weights.reserve(neighbours.size());
    float totalWeight = 0.0f;

    for (int n : neighbours) {
        float w = (n == ai.previousWaypoint) ? AI_BACK_AVOIDANCE_WEIGHT : 1.0f;
        weights.push_back(w);
        totalWeight += w;
    }

    float r = randomFloat(0.0f, totalWeight);
    float cumulative = 0.0f;
    for (size_t i = 0; i < neighbours.size(); i++) {
        cumulative += weights[i];
        if (r <= cumulative) return neighbours[i];
    }

    return neighbours.back();
}

int AIManager::selectChaseTarget(const AIComponent& ai, Vector2 playerPos) const {
    if (ai.currentWaypoint < 0 ||
        ai.currentWaypoint >= static_cast<int>(adjacency_.size())) {
        return -1;
    }

    const auto& neighbours = adjacency_[ai.currentWaypoint];
    if (neighbours.empty()) return -1;

    // Pick the linked waypoint closest to the player
    int best = neighbours[0];
    float bestDist = Vector2Distance(waypointPos2D(neighbours[0]), playerPos);

    for (size_t i = 1; i < neighbours.size(); i++) {
        float d = Vector2Distance(waypointPos2D(neighbours[i]), playerPos);
        if (d < bestDist) {
            bestDist = d;
            best = neighbours[i];
        }
    }

    return best;
}

int AIManager::selectFleeTarget(const AIComponent& ai, Vector2 playerPos) const {
    if (ai.currentWaypoint < 0 ||
        ai.currentWaypoint >= static_cast<int>(adjacency_.size())) {
        return -1;
    }

    const auto& neighbours = adjacency_[ai.currentWaypoint];
    if (neighbours.empty()) return -1;

    // Pick the linked waypoint farthest from the player
    int best = neighbours[0];
    float bestDist = Vector2Distance(waypointPos2D(neighbours[0]), playerPos);

    for (size_t i = 1; i < neighbours.size(); i++) {
        float d = Vector2Distance(waypointPos2D(neighbours[i]), playerPos);
        if (d > bestDist) {
            bestDist = d;
            best = neighbours[i];
        }
    }

    return best;
}

//------------------------------------------------------------------------------
// Movement and Rotation
//------------------------------------------------------------------------------

void AIManager::moveTowardWaypoint(AIComponent& ai, Vector2 targetPos) const {
    Vector2 unitPos = getUnitPosition(ai);
    Vector2 dir = Vector2Subtract(targetPos, unitPos);
    float len = Vector2Length(dir);
    if (len < 0.001f) return;

    Vector2 force = Vector2Scale(Vector2Normalize(dir), AI_MOVEMENT_FORCE);
    b2Body_ApplyForceToCenter(ai.unit->bodyId, {force.x, force.y}, true);
}

void AIManager::applyRotation(b2BodyId bodyId, float targetAngle) const {
    float currentAngle = b2Rot_GetAngle(b2Body_GetRotation(bodyId));
    float angularVel = b2Body_GetAngularVelocity(bodyId);
    float inertia = b2Body_GetInertiaTensor(bodyId);
    float inertiaScale = inertia > 0.0f ? inertia : 1.0f;

    float diff = normalizeAngle(targetAngle - currentAngle);
    float torque = diff * AI_ROTATION_KP * inertiaScale - angularVel * AI_ROTATION_KD * inertiaScale;

    if (torque > AI_MAX_TORQUE) torque = AI_MAX_TORQUE;
    if (torque < -AI_MAX_TORQUE) torque = -AI_MAX_TORQUE;

    b2Body_ApplyTorque(bodyId, torque, true);
}

bool AIManager::isAtWaypoint(const AIComponent& ai, Vector2 waypointPos) const {
    Vector2 pos = getUnitPosition(ai);
    return Vector2Distance(pos, waypointPos) < AI_WAYPOINT_ARRIVAL_DIST;
}

Vector2 AIManager::getUnitPosition(const AIComponent& ai) const {
    b2Vec2 p = b2Body_GetPosition(ai.unit->bodyId);
    return {p.x, p.y};
}

Vector2 AIManager::waypointPos2D(int index) const {
    if (index < 0 || index >= static_cast<int>(waypointPositions_.size())) {
        return {0, 0};
    }
    // World coordinates: x is X, z is Y in 2D (top-down)
    return {waypointPositions_[index].x, waypointPositions_[index].z};
}

//------------------------------------------------------------------------------
// Firing
//------------------------------------------------------------------------------

bool AIManager::canFire(const AIComponent& ai, Vector2 playerPos) const {
    if (!ai.armed) return false;
    if (ai.weaponState.cooldownRemaining > 0.0f) return false;

    Vector2 unitPos = getUnitPosition(ai);
    float dist = Vector2Distance(unitPos, playerPos);

    // Must be within max range
    if (dist > ai.weaponState.definition.maxRange) return false;

    // Disruptor (area weapon) ignores facing
    if (ai.weaponState.definition.type == WeaponType::Area) return true;

    // Check facing alignment
    float bodyAngle = b2Rot_GetAngle(b2Body_GetRotation(ai.unit->bodyId));
    Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
    float angleToPlayer = atan2f(toPlayer.y, toPlayer.x);

    float facingAngle;
    if (ai.hasTurret) {
        // Turret droids fire from head — check head facing
        // Use the head's facingAngle if available
        facingAngle = bodyAngle; // fallback
        for (auto* section : ai.unit->allSections) {
            if (section->definition &&
                section->definition->rotationMode == SectionRotationMode::FollowFacing) {
                facingAngle = section->facingAngle;
                break;
            }
        }
    } else {
        // Standard and omnidirectional fire from body
        facingAngle = bodyAngle;
    }

    float angleDiff = fabsf(normalizeAngle(angleToPlayer - facingAngle));
    return angleDiff <= AI_FACING_THRESHOLD;
}

void AIManager::tryFireAtPlayer(AIComponent& ai, Vector2 playerPos,
                                b2WorldId worldId, ProjectileManager* projectiles) {
    if (!projectiles) return;
    if (!canFire(ai, playerPos)) return;

    if (!tryFire(ai.weaponState)) return;

    Vector2 unitPos = getUnitPosition(ai);
    Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, unitPos));
    const auto& wdef = ai.weaponState.definition;
    float lifetime = (wdef.speed > 0.0f) ? (wdef.maxRange / wdef.speed) : 1.0f;

    // Apply fire offset (rotated by firing section angle)
    Vector2 spawnPos = unitPos;
    const auto& props = ai.unit->definition->properties;
    if (props.fireOffset.x != 0 || props.fireOffset.y != 0 || props.fireOffset.z != 0) {
        float angle = b2Rot_GetAngle(b2Body_GetRotation(ai.unit->bodyId));
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        // fireOffset.x/y are 2D offset relative to unit facing
        spawnPos.x += props.fireOffset.x * cosA - props.fireOffset.y * sinA;
        spawnPos.y += props.fireOffset.x * sinA + props.fireOffset.y * cosA;
    }

    projectiles->spawn(worldId, spawnPos, dir,
                       wdef.speed, wdef.damage, lifetime,
                       ai.unit->collisionGroupId);

    // Twin weapons fire a second projectile
    if (wdef.twin) {
        projectiles->spawn(worldId, spawnPos, dir,
                           wdef.speed, wdef.damage, lifetime,
                           ai.unit->collisionGroupId);
    }
}
