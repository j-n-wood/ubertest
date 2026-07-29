#include "ai_manager.h"
#include "units/unit_instance.h"
#include "units/movement_tuning.h"
#include "units/weapon.h"
#include "combat/projectile_manager.h"
#include "level/spawn_config.h"
#include "physics/body_user_data.h"
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

// Circle-cast hit sink: records whether the cast hit anything and stops at the
// first (closest) hit.
struct WallCastCtx { bool hit = false; };
float wallCastCallback(b2ShapeId, b2Vec2, b2Vec2, float, void* ctx) {
    static_cast<WallCastCtx*>(ctx)->hit = true;
    return 0.0f;  // terminate — we only need to know that something was hit
}

// Step `current` toward `target` by at most maxStep radians, taking the shortest
// way around the circle (so an angle near 0/2π slews the short way to a nearby
// target instead of spinning almost all the way around).
float slewToward(float current, float target, float maxStep) {
    float diff = normalizeAngle(target - current);
    if (diff > maxStep) diff = maxStep;
    if (diff < -maxStep) diff = -maxStep;
    return normalizeAngle(current + diff);
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
    m_worldId = worldId;  // cache for movement helpers (raycasts)
    for (auto& ai : components_) {
        if (!ai.unit || !ai.unit->active) continue;
        if (ai.controlled) continue;  // player is piloting this unit — no AI drive

        // Tick the collision-redirect decision cooldown
        if (ai.collideCooldown > 0.0f) ai.collideCooldown -= dt;

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
// Collision response
//------------------------------------------------------------------------------

AIComponent* AIManager::findComponent(UnitInstance* unit) {
    for (auto& ai : components_) {
        if (ai.unit == unit) return &ai;
    }
    return nullptr;
}

void AIManager::setControlled(UnitInstance* unit, bool controlled) {
    if (AIComponent* ai = findComponent(unit)) ai->controlled = controlled;
}

void AIManager::forgetUnit(UnitInstance* unit) {
    if (AIComponent* ai = findComponent(unit)) {
        ai->unit = nullptr;      // update()/collisions skip null-unit components
        ai->controlled = false;
    }
}

void AIManager::processCollisions(b2WorldId worldId) {
    b2ContactEvents events = b2World_GetContactEvents(worldId);

    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& ev = events.beginEvents[i];
        auto* udA = static_cast<BodyUserData*>(b2Body_GetUserData(b2Shape_GetBody(ev.shapeIdA)));
        auto* udB = static_cast<BodyUserData*>(b2Body_GetUserData(b2Shape_GetBody(ev.shapeIdB)));

        // Ignore projectile contacts — those are damage, handled by ProjectileManager.
        if ((udA && udA->tag == BodyTag::Projectile) ||
            (udB && udB->tag == BodyTag::Projectile)) {
            continue;
        }

        // Ignore door contacts — a unit must not reroute off a door; the door opens
        // on proximity. (Doors are transparent to pathClear already: it casts vs
        // CATEGORY_STATIC only.)
        if ((udA && udA->tag == BodyTag::Door) ||
            (udB && udB->tag == BodyTag::Door)) {
            continue;
        }

        bool aUnit = udA && udA->tag == BodyTag::Unit;
        bool bUnit = udB && udB->tag == BodyTag::Unit;
        auto* unitA = aUnit ? static_cast<UnitInstance*>(udA->owner) : nullptr;
        auto* unitB = bUnit ? static_cast<UnitInstance*>(udB->owner) : nullptr;

        onCollision(unitA, unitB);
    }
}

void AIManager::onCollision(UnitInstance* a, UnitInstance* b) {
    // Each unit involved reacts; the "other" is the partner unit or nullptr
    // (wall/debris). The player has no AIComponent, so findComponent skips it.
    if (a) {
        if (AIComponent* ai = findComponent(a); ai && !ai->controlled) handleCollision(*ai, b);
    }
    if (b) {
        if (AIComponent* ai = findComponent(b); ai && !ai->controlled) handleCollision(*ai, a);
    }
}

void AIManager::handleCollision(AIComponent& ai, UnitInstance* other) {
    (void)other;
    // Hostile units are pursuing/attacking — they push through, don't back off.
    if (ai.hostile) return;

    // Debounce the decision (not movement): while cooling down, keep moving toward
    // the redirect target already chosen instead of re-deciding every frame.
    if (ai.collideCooldown > 0.0f) return;
    ai.collideCooldown = AI_COLLIDE_COOLDOWN;

    // Redirect away from the collision point. Normally head back toward the prior
    // waypoint; if we're already doing that (or have none), drop the target so the
    // next tick re-selects — back-avoidance then biases us off the blocked route.
    if (ai.previousWaypoint >= 0 && ai.targetWaypoint != ai.previousWaypoint) {
        ai.targetWaypoint = ai.previousWaypoint;
    } else {
        ai.targetWaypoint = -1;
    }
}

//------------------------------------------------------------------------------
// Off-course recovery / path validation
//------------------------------------------------------------------------------
//
// LIMITATION (flagged, not yet solved): reachability here is a single STRAIGHT-LINE
// cast. In open rooms with interior geometry — pillars, tables, stub walls — a
// waypoint can be trivially reachable by a short detour around the obstacle yet
// fail this cast because the direct line clips it. nearestReachableWaypoint() will
// then skip such waypoints and, if a whole cluster is occluded, fall back to the
// plain nearest (possibly itself occluded), so a unit can sit re-targeting a
// waypoint it can't straight-line reach.
//
// Proper fix (future): route over the waypoint graph instead of line-of-sight —
// e.g. BFS/A* over `adjacency_`, picking the nearest waypoint the unit can reach
// AND whose next hop is clear, or inserting intermediate steering points. Until
// then the near-frictionless wall SLIDING (see UNIT_CONTACT_FRICTION) is the
// mitigation: a unit whose path grazes an obstacle slides around it rather than
// pinning, which covers most pillar/table/stub-wall cases in practice.

bool AIManager::pathClear(Vector2 from, Vector2 to, float radius) const {
    if (B2_IS_NULL(m_worldId)) return true;
    Vector2 d = Vector2Subtract(to, from);
    float len = Vector2Length(d);
    if (len < 0.001f) return true;

    b2Circle circle;
    circle.center = {0.0f, 0.0f};
    circle.radius = (radius > 0.01f) ? radius : 0.1f;  // spherical width ~ unit collision radius
    b2Transform xf;
    xf.p = {from.x, from.y};
    xf.q = b2MakeRot(0.0f);
    b2Vec2 translation = {d.x, d.y};
    // Cast as a unit against walls only — other units are not treated as blockers.
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_UNIT;
    filter.maskBits = CATEGORY_STATIC;

    WallCastCtx ctx;
    b2World_CastCircle(m_worldId, &circle, xf, translation, filter, wallCastCallback, &ctx);
    return !ctx.hit;
}

int AIManager::nearestWaypoint(Vector2 pos) const {
    int best = -1;
    float bestDist = 1e30f;
    for (int i = 0; i < static_cast<int>(waypointPositions_.size()); i++) {
        float d = Vector2Distance(pos, waypointPos2D(i));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

int AIManager::nearestReachableWaypoint(Vector2 pos, float radius) const {
    // Consider waypoints nearest-first; return the first with a wall-clear path.
    std::vector<std::pair<float, int>> order;
    order.reserve(waypointPositions_.size());
    for (int i = 0; i < static_cast<int>(waypointPositions_.size()); i++) {
        order.push_back({Vector2Distance(pos, waypointPos2D(i)), i});
    }
    std::sort(order.begin(), order.end());
    for (const auto& [dist, idx] : order) {
        if (pathClear(pos, waypointPos2D(idx), radius)) return idx;
    }
    return order.empty() ? -1 : order.front().second;  // fallback: plain nearest
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
        holdPosition(ai);
        return;
    }

    // Need a target?
    if (ai.targetWaypoint < 0) {
        ai.targetWaypoint = selectPatrolTarget(ai);
        if (ai.targetWaypoint < 0) { holdPosition(ai); return; } // dead end
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
        holdPosition(ai);
        return;
    }

    // Stuck detection: if we're trying to move but barely moving, the route is
    // blocked (pinned on a wall, or a deadlock). Abandon it and reselect, biased
    // away from the blocked direction. Handles the case where a wall contact only
    // fired one begin-touch event and left the unit pinned. The timer resets when
    // the target changes so each fresh route gets a full window.
    if (ai.targetWaypoint != ai.stuckWaypoint) {
        ai.stuckWaypoint = ai.targetWaypoint;
        ai.stuckTimer = 0.0f;
    }
    b2Vec2 vel = b2Body_GetLinearVelocity(ai.unit->bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    if (speed < AI_STUCK_SPEED) {
        ai.stuckTimer += dt;
        if (ai.stuckTimer > AI_STUCK_TIME) {
            float radius = ai.unit->definition ? ai.unit->definition->collisionRadius : 0.2f;
            if (!pathClear(unitPos, targetPos, radius)) {
                // A wall lies between us and the target — we've been knocked off the
                // graph. Re-acquire by heading to the nearest waypoint we can
                // actually reach on a wall-clear path.
                int n = nearestReachableWaypoint(unitPos, radius);
                ai.previousWaypoint = -1;
                ai.currentWaypoint = -1;
                ai.targetWaypoint = n;
            } else {
                // Path to the target is wall-clear (blocked by a unit/deadlock) —
                // abandon it and reselect, biased away from the blocked direction.
                ai.previousWaypoint = ai.targetWaypoint;
                ai.targetWaypoint = -1;
            }
            ai.stuckTimer = 0.0f;
            return;
        }
    } else {
        ai.stuckTimer = 0.0f;
    }

    // Move toward target, facing the movement direction.
    Vector2 dir = Vector2Subtract(targetPos, unitPos);
    float targetAngle = facing_angle_to(dir.x, dir.y);
    setMotion(ai, targetPos, targetAngle);
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
        // Continue waypoint navigation toward the player, always choosing the linked
        // waypoint nearest the player (selectChaseTarget). Facing rules: an omnidirectional
        // unit always faces the player; a unit that is stopped (blocked) faces the player so
        // it aims at its target even while pinned; otherwise it faces its movement direction.
        // (Turret units additionally slew their head to the player below.)
        Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
        float facePlayer = facing_angle_to(toPlayer.x, toPlayer.y);

        if (ai.targetWaypoint < 0) {
            ai.targetWaypoint = selectChaseTarget(ai, playerPos);
        }

        if (ai.targetWaypoint < 0) {
            // No route available — hold position but keep facing the player.
            unit_set_move_target(ai.unit, unitPos, facePlayer);
        } else {
            Vector2 targetPos = waypointPos2D(ai.targetWaypoint);
            if (isAtWaypoint(ai, targetPos)) {
                ai.previousWaypoint = ai.currentWaypoint;
                ai.currentWaypoint = ai.targetWaypoint;
                ai.targetWaypoint = selectChaseTarget(ai, playerPos);
            }

            // Stuck detection (mirrors Patrol): if pinned below AI_STUCK_SPEED for
            // AI_STUCK_TIME AND a wall blocks the route, re-acquire via a reachable
            // waypoint. If the path is wall-clear (blocked by a unit — usually the player
            // it's chasing), keep pressing rather than abandoning the pursuit.
            if (ai.targetWaypoint != ai.stuckWaypoint) {
                ai.stuckWaypoint = ai.targetWaypoint;
                ai.stuckTimer = 0.0f;
            }
            b2Vec2 vel = b2Body_GetLinearVelocity(ai.unit->bodyId);
            float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
            bool stopped = speed < AI_STUCK_SPEED;
            float radius = ai.unit->definition ? ai.unit->definition->collisionRadius : 0.2f;
            if (stopped) {
                ai.stuckTimer += dt;
                if (ai.stuckTimer > AI_STUCK_TIME && ai.targetWaypoint >= 0 &&
                    !pathClear(unitPos, waypointPos2D(ai.targetWaypoint), radius)) {
                    ai.currentWaypoint = nearestReachableWaypoint(unitPos, radius);
                    ai.previousWaypoint = -1;
                    ai.targetWaypoint = -1;
                    ai.stuckTimer = 0.0f;
                }
            } else {
                ai.stuckTimer = 0.0f;
            }

            if (ai.targetWaypoint >= 0) {
                Vector2 wpPos = waypointPos2D(ai.targetWaypoint);
                float bodyAngle;
                if (ai.omnidirectional || stopped) {
                    bodyAngle = facePlayer;  // aim at the target when it doesn't need to, or can't, move
                } else {
                    Vector2 dir = Vector2Subtract(wpPos, unitPos);
                    bodyAngle = facing_angle_to(dir.x, dir.y);
                }
                setMotion(ai, wpPos, bodyAngle);
            } else {
                unit_set_move_target(ai.unit, unitPos, facePlayer);  // rerouting — hold + face player
            }
        }
    } else {
        // Halting — hold station and turn body to face the player. Holding the
        // position via the motor joint also resists being shoved off the firing spot.
        Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
        float angle = facing_angle_to(toPlayer.x, toPlayer.y);
        unit_set_move_target(ai.unit, unitPos, angle);
    }

    // Head tracking — all hostile droids slew the head toward the player. Facing
    // is a render-only scalar; slew the shortest way at the shared turret rate.
    if (ai.hasTurret && ai.unit->rootSection) {
        Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
        float headAngle = facing_angle_to(toPlayer.x, toPlayer.y);
        // Find head section (first child with FollowFacing mode)
        for (auto* section : ai.unit->allSections) {
            if (section->definition &&
                section->definition->rotationMode == SectionRotationMode::FollowFacing) {
                section->facingAngle =
                    slewToward(section->facingAngle, headAngle, TURRET_SLEW_RATE * dt);
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
    (void)dt;
    if (ai.targetWaypoint < 0) {
        ai.targetWaypoint = selectFleeTarget(ai, playerPos);
        if (ai.targetWaypoint < 0) { holdPosition(ai); return; }
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

        // Move toward the flee waypoint, facing the movement direction.
        Vector2 dir = Vector2Subtract(wpPos, unitPos);
        float angle = facing_angle_to(dir.x, dir.y);
        setMotion(ai, wpPos, angle);
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

void AIManager::setMotion(AIComponent& ai, Vector2 moveTarget, float facing) const {
    Vector2 unitPos = getUnitPosition(ai);
    Vector2 dir = Vector2Subtract(moveTarget, unitPos);
    float len = Vector2Length(dir);

    // Place the linear target a bounded distance ahead along the heading (a
    // "carrot"). This caps the position error — and thus the motor force and
    // speed — and eases arrival as the final target comes within lookahead.
    Vector2 carrot = unitPos;
    if (len > 0.001f) {
        float reach = fminf(UNIT_MOVE_LOOKAHEAD, len);
        carrot = Vector2Add(unitPos, Vector2Scale(dir, reach / len));
    }

    unit_set_move_target(ai.unit, carrot, facing);
}

void AIManager::holdPosition(AIComponent& ai) const {
    Vector2 unitPos = getUnitPosition(ai);
    float facing = b2Rot_GetAngle(b2Body_GetRotation(ai.unit->bodyId));
    unit_set_move_target(ai.unit, unitPos, facing);
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

    // Line of sight: a hostile only fires when no wall lies between it and the
    // player (so it holds fire around corners). Reuses the static-geometry cast.
    float losRadius = ai.unit->definition ? ai.unit->definition->collisionRadius : 0.2f;
    if (!pathClear(unitPos, playerPos, losRadius)) return false;

    // Disruptor (area weapon) ignores facing
    if (ai.weaponState.definition.type == WeaponType::Area) return true;

    // Check facing alignment
    float bodyAngle = b2Rot_GetAngle(b2Body_GetRotation(ai.unit->bodyId));
    Vector2 toPlayer = Vector2Subtract(playerPos, unitPos);
    float angleToPlayer = facing_angle_to(toPlayer.x, toPlayer.y);

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
    // Only projectile weapons fire this phase (beam/area/instant deferred); don't
    // consume the cooldown for a weapon type we can't yet spawn.
    if (ai.weaponState.definition.type != WeaponType::Projectile) return;
    if (!canFire(ai, playerPos)) return;

    if (!tryFire(ai.weaponState)) return;

    Vector2 unitPos = getUnitPosition(ai);
    Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, unitPos));
    const auto& wdef = ai.weaponState.definition;
    float lifetime = (wdef.speed > 0.0f) ? (wdef.maxRange / wdef.speed) : 1.0f;

    // Apply fire offset (facing-relative: x = lateral, y = forward), clamped to the unit's
    // collision radius so a stray/old-data offset can't spawn the bolt inside a wall.
    const auto& props = ai.unit->definition->properties;
    Vector2 off2d = {props.fireOffset.x, props.fireOffset.y};
    float offLen = sqrtf(off2d.x * off2d.x + off2d.y * off2d.y);
    float maxOff = ai.unit->definition->collisionRadius;
    if (offLen > maxOff && offLen > 1e-5f) {
        off2d.x *= maxOff / offLen;
        off2d.y *= maxOff / offLen;
    }
    float angle = b2Rot_GetAngle(b2Body_GetRotation(ai.unit->bodyId));
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    auto spawnFrom = [&](Vector2 o) {
        return Vector2{unitPos.x + o.x * cosA - o.y * sinA,
                       unitPos.y + o.x * sinA + o.y * cosA};
    };

    projectiles->spawn(worldId, spawnFrom(off2d), dir,
                       wdef.speed, wdef.damage, lifetime,
                       ai.unit->collisionGroupId, wdef.id, wdef.radius);

    // Twin: second barrel is the offset mirrored across the facing axis (negate lateral x),
    // so the two shots straddle the centreline instead of stacking.
    if (wdef.twin) {
        projectiles->spawn(worldId, spawnFrom({-off2d.x, off2d.y}), dir,
                           wdef.speed, wdef.damage, lifetime,
                           ai.unit->collisionGroupId, wdef.id, wdef.radius);
    }
}
