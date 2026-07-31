#include "beam_manager.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "physics/body_user_data.h"
#include <cmath>

namespace {
// Forward direction for a facing angle (matches facing_angle_to elsewhere: dir = {-sin, cos}).
Vector2 forwardOf(float angle) { return {-std::sin(angle), std::cos(angle)}; }
}  // namespace

float BeamManager::castLength(b2WorldId world, Vector2 origin, float angle, float maxRange) {
    if (B2_IS_NULL(world) || maxRange <= 0.0f) return maxRange > 0.0f ? maxRange : 0.0f;
    Vector2 dir = forwardOf(angle);
    b2Vec2 o = {origin.x, origin.y};
    b2Vec2 translation = {dir.x * maxRange, dir.y * maxRange};
    // Probe against walls and CLOSED doors (an open door clears its filter and is skipped).
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_PROJECTILE;
    filter.maskBits = CATEGORY_STATIC | CATEGORY_DOOR;
    b2RayResult r = b2World_CastRayClosest(world, o, translation, filter);
    return r.hit ? r.fraction * maxRange : maxRange;
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
    float length = castLength(world, origin, angle, maxRange);

    // Continuous damage: accumulate dps*dt onto every unit the beam passes through, flushed
    // on the shared realtime-damage tick (like explosion damage). No fireRate gating.
    if (dps > 0.0f && dt > 0.0f && targets) {
        float raw = dps * dt;
        for (std::size_t i = 0; i < targetCount; ++i) {
            UnitInstance* t = targets[i];
            if (!t || t == shooter || !t->active) continue;
            if (hitsUnit(origin, angle, length, t)) {
                accumulateRealtimeDamage(t->combatState, raw);
            }
        }
    }

    beams_.push_back(Beam{origin, angle, length, weaponId});
    return length;
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
