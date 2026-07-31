#include "beam_manager.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "physics/body_user_data.h"
#include <cmath>

namespace {
// Forward direction for a facing angle (matches facing_angle_to elsewhere: dir = {-sin, cos}).
Vector2 forwardOf(float angle) { return {-std::sin(angle), std::cos(angle)}; }
}  // namespace

BeamHit BeamManager::castRay(b2WorldId world, Vector2 origin, float angle, float maxRange) {
    Vector2 dir = forwardOf(angle);
    float range = maxRange > 0.0f ? maxRange : 0.0f;
    BeamHit out;
    out.length = range;
    out.point = {origin.x + dir.x * range, origin.y + dir.y * range};  // range end if no hit
    if (B2_IS_NULL(world) || range <= 0.0f) return out;

    b2Vec2 o = {origin.x, origin.y};
    b2Vec2 translation = {dir.x * range, dir.y * range};
    // Probe against walls and CLOSED doors (an open door clears its filter and is skipped).
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_PROJECTILE;
    filter.maskBits = CATEGORY_STATIC | CATEGORY_DOOR;
    b2RayResult r = b2World_CastRayClosest(world, o, translation, filter);
    if (r.hit) {
        out.length = r.fraction * range;
        out.hitWall = true;
        out.point = {r.point.x, r.point.y};
        out.normal = {r.normal.x, r.normal.y};
    }
    return out;
}

float BeamManager::castLength(b2WorldId world, Vector2 origin, float angle, float maxRange) {
    return castRay(world, origin, angle, maxRange).length;
}

bool BeamManager::hitsUnit(Vector2 origin, float angle, float length, const UnitInstance* unit) {
    if (!unit || !b2Body_IsValid(unit->bodyId)) return false;
    b2Vec2 up = b2Body_GetPosition(unit->bodyId);
    Vector2 dir = forwardOf(angle);
    Vector2 w = {up.x - origin.x, up.y - origin.y};
    float along = w.x * dir.x + w.y * dir.y;      // projection onto the beam direction
    if (along < 0.0f || along > length) return false;  // behind the muzzle or past the end
    // Perpendicular distance from the beam centre-line.
    float cx = origin.x + dir.x * along;
    float cy = origin.y + dir.y * along;
    float dx = up.x - cx, dy = up.y - cy;
    float perp = std::sqrt(dx * dx + dy * dy);
    float radius = unit->definition ? unit->definition->collisionRadius : 0.3f;
    return perp <= radius + BEAM_HALF_WIDTH;
}

float BeamManager::fire(b2WorldId world, Vector2 origin, float angle, float maxRange,
                        float dps, float dt, const UnitInstance* shooter,
                        UnitInstance* const* targets, std::size_t targetCount, int weaponId) {
    BeamHit hit = castRay(world, origin, angle, maxRange);

    // Continuous damage: accumulate dps*dt onto every unit the beam passes through, flushed
    // on the shared realtime-damage tick (like explosion damage). No fireRate gating.
    if (dps > 0.0f && dt > 0.0f && targets) {
        float raw = dps * dt;
        for (std::size_t i = 0; i < targetCount; ++i) {
            UnitInstance* t = targets[i];
            if (!t || t == shooter || !t->active) continue;
            if (hitsUnit(origin, angle, hit.length, t)) {
                accumulateRealtimeDamage(t->combatState, raw);
            }
        }
    }

    Beam b;
    b.origin = origin;
    b.angle = angle;
    b.length = hit.length;
    b.weaponId = weaponId;
    b.hitWall = hit.hitWall;
    b.hitPoint = hit.point;
    b.hitNormal = hit.normal;
    beams_.push_back(b);
    return hit.length;
}

void BeamManager::update(float dt) {
    if (dt <= 0.0f) return;
    animTimer_ += dt;
    const float frameTime = 1.0f / BEAM_ANIM_FPS;
    while (animTimer_ >= frameTime) {
        animTimer_ -= frameTime;
        frame_ = (frame_ + 1) % BEAM_FRAME_COUNT;
    }
}
