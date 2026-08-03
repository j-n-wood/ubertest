#include "beam_manager.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "physics/body_user_data.h"
#include <cmath>

namespace {
// Forward direction for a facing angle (matches facing_angle_to elsewhere: dir = {-sin, cos}).
Vector2 forwardOf(float angle) { return {-std::sin(angle), std::cos(angle)}; }

// Ray-cast sink: keeps the closest non-shooter hit. The shooter's own body is ignored (the
// muzzle sits on it). Returning the hit fraction makes Box2D narrow the search to nearer
// shapes, so the last recorded hit is the closest.
struct BeamCastCtx {
    const UnitInstance* shooter = nullptr;
    bool hit = false;
    float fraction = 1.0f;
    b2Vec2 point{};
    b2Vec2 normal{};
    bool isUnit = false;
    UnitInstance* unit = nullptr;
};
float beamCastCallback(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* ctx) {
    auto* c = static_cast<BeamCastCtx*>(ctx);
    auto* ud = static_cast<BodyUserData*>(b2Body_GetUserData(b2Shape_GetBody(shape)));
    bool isUnit = ud && ud->tag == BodyTag::Unit;
    if (isUnit && ud->owner == static_cast<const void*>(c->shooter)) {
        return -1.0f;  // ignore the shooter's own body, keep casting
    }
    c->hit = true;
    c->fraction = fraction;
    c->point = point;
    c->normal = normal;
    c->isUnit = isUnit;
    c->unit = isUnit ? static_cast<UnitInstance*>(ud->owner) : nullptr;
    return fraction;   // clip the ray to this hit (only nearer shapes are reported after)
}
}  // namespace

BeamHit BeamManager::castRay(b2WorldId world, Vector2 origin, float angle, float maxRange,
                             const UnitInstance* shooter) {
    Vector2 dir = forwardOf(angle);
    float range = maxRange > 0.0f ? maxRange : 0.0f;
    BeamHit out;
    out.length = range;
    out.point = {origin.x + dir.x * range, origin.y + dir.y * range};  // range end if no hit
    if (B2_IS_NULL(world) || range <= 0.0f) return out;

    b2Vec2 o = {origin.x, origin.y};
    b2Vec2 translation = {dir.x * range, dir.y * range};
    // Stop at walls, CLOSED doors (an open door clears its filter), and units.
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_PROJECTILE;
    filter.maskBits = CATEGORY_STATIC | CATEGORY_DOOR | CATEGORY_UNIT;
    BeamCastCtx ctx;
    ctx.shooter = shooter;
    b2World_CastRay(world, o, translation, filter, beamCastCallback, &ctx);
    if (ctx.hit) {
        out.length = ctx.fraction * range;
        out.point = {ctx.point.x, ctx.point.y};
        out.normal = {ctx.normal.x, ctx.normal.y};
        out.hitWall = !ctx.isUnit;   // geometry → sparks; a unit absorbs the beam
        out.unit = ctx.unit;
    }
    return out;
}

float BeamManager::castLength(b2WorldId world, Vector2 origin, float angle, float maxRange) {
    return castRay(world, origin, angle, maxRange).length;
}

float BeamManager::fire(b2WorldId world, Vector2 origin, float angle, float maxRange,
                        float dps, float dt, const UnitInstance* shooter, int weaponId) {
    BeamHit hit = castRay(world, origin, angle, maxRange, shooter);

    // The beam stops at (and damages) the first unit it reaches — continuous dps*dt fed
    // through the shared realtime-damage accumulator, like explosion damage. No fireRate gate.
    if (hit.unit && hit.unit->active && dps > 0.0f && dt > 0.0f) {
        accumulateRealtimeDamage(hit.unit->combatState, dps * dt);
    }

    Beam b;
    b.origin = origin;
    b.angle = angle;
    b.length = hit.length;
    b.weaponId = weaponId;
    b.hit = hit.hitWall || hit.unit != nullptr;   // any collision (geometry or unit) sparks
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
