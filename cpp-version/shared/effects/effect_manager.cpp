#include "effects/effect_manager.h"

#include "units/unit_instance.h"
#include "physics/body_user_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Damage/second at distance r from an explosion centre: full EXPLOSION_DPS within the core,
// falling as ~1/r beyond it, and 0 past EXPLOSION_RADIUS.
float explosionDps(float r) {
    if (r > EXPLOSION_RADIUS) return 0.0f;
    float rr = (r < EXPLOSION_CORE) ? EXPLOSION_CORE : r;
    float f = EXPLOSION_CORE / rr;   // 1 at/inside the core, 1/r outside it
    if (f > 1.0f) f = 1.0f;
    return EXPLOSION_DPS * f;
}

// b2World_OverlapAABB context: accumulate this frame's damage onto every unit in range.
struct DamageCtx {
    Vector2 center;
    int32_t ownerGroup;
    float dt;
};

bool damageOverlapCallback(b2ShapeId shapeId, void* ctxPtr) {
    auto* ctx = static_cast<DamageCtx*>(ctxPtr);
    b2BodyId body = b2Shape_GetBody(shapeId);
    auto* ud = static_cast<BodyUserData*>(b2Body_GetUserData(body));
    if (!ud || ud->tag != BodyTag::Unit || !ud->owner) return true;  // keep scanning
    auto* unit = static_cast<UnitInstance*>(ud->owner);
    if (unit->collisionGroupId == ctx->ownerGroup) return true;      // same unit — immune
    b2Vec2 p = b2Body_GetPosition(body);
    float dx = p.x - ctx->center.x;
    float dy = p.y - ctx->center.y;
    float dps = explosionDps(sqrtf(dx * dx + dy * dy));
    if (dps > 0.0f) accumulateRealtimeDamage(unit->combatState, dps * ctx->dt);
    return true;  // continue: damage every unit in range, not just the first
}

}  // namespace

EffectManager::~EffectManager() {
    destroy();
}

void EffectManager::init(b2WorldId world) {
    destroy();
    world_ = world;
}

void EffectManager::spawnExplosion(Vector2 pos, int32_t ownerGroup) {
    Effect e;
    e.type = EffectType::Explosion;
    e.pos = pos;
    e.age = 0.0f;
    e.rotationDeg = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 360.0f;
    e.ownerGroup = ownerGroup;
    e.active = true;
    effects_.push_back(e);
}

void EffectManager::update(float dt) {
    for (Effect& e : effects_) {
        if (!e.active) continue;
        e.age += dt;
        if (e.age >= EXPLOSION_LIFETIME) { e.active = false; continue; }

        if (e.type == EffectType::Explosion && !B2_IS_NULL(world_)) {
            const float R = EXPLOSION_RADIUS;
            b2AABB aabb;
            aabb.lowerBound = {e.pos.x - R, e.pos.y - R};
            aabb.upperBound = {e.pos.x + R, e.pos.y + R};
            b2QueryFilter filter;
            filter.categoryBits = CATEGORY_STATIC;  // query identity is irrelevant
            filter.maskBits = CATEGORY_UNIT;         // only hit units
            DamageCtx ctx{e.pos, e.ownerGroup, dt};
            b2World_OverlapAABB(world_, aabb, filter, damageOverlapCallback, &ctx);
        }
    }

    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const Effect& e) { return !e.active; }),
                   effects_.end());
}

void EffectManager::destroy() {
    effects_.clear();
    world_ = b2_nullWorldId;
}
