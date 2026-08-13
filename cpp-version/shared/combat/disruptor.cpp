#include "combat/disruptor.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "units/unit_types.h"
#include "units/weapon.h"
#include "physics/body_user_data.h"
#include <cmath>

namespace {

struct LosCtx { bool blocked = false; };

float losCastCallback(b2ShapeId, b2Vec2, b2Vec2, float fraction, void* ctx) {
    static_cast<LosCtx*>(ctx)->blocked = true;
    return fraction;   // clip to the nearest hit; any hit means the sightline is blocked
}

// Clear line-of-sight between a and b: no wall or CLOSED door on the segment. Masks out CATEGORY_UNIT
// so units (including the target and firer) never block — matches uber's LOS trace which ignores
// droids/objects. Mirrors BeamManager::castRay's filter, minus the unit category.
bool lineClear(b2WorldId world, Vector2 a, Vector2 b) {
    if (B2_IS_NULL(world)) return true;
    b2Vec2 o = {a.x, a.y};
    b2Vec2 translation = {b.x - a.x, b.y - a.y};
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_PROJECTILE;
    // Walls, CLOSED doors, and glass all block the disruptor (the user's rule: glass stops the blast
    // too — only pure sight passes through glass). Open doors clear their filter and never block.
    filter.maskBits = CATEGORY_STATIC | CATEGORY_DOOR | CATEGORY_GLASS;
    LosCtx ctx;
    b2World_CastRay(world, o, translation, filter, losCastCallback, &ctx);
    return !ctx.blocked;
}

}  // namespace

int disruptorBlast(b2WorldId world, Vector2 firePos, const UnitInstance* firer,
                   const WeaponDefinition& weapon, const std::vector<UnitInstance*>& units) {
    const float maxR2 = weapon.maxRange * weapon.maxRange;
    int hits = 0;
    for (UnitInstance* u : units) {
        if (!u || u == firer || !u->active || !u->definition) continue;
        if (u->definition->properties.disruptorShielded) continue;          // immune classes
        if (u->combatState.currentHealth <= 0.0f || !b2Body_IsValid(u->bodyId)) continue;
        b2Vec2 up = b2Body_GetPosition(u->bodyId);
        float dx = firePos.x - up.x, dy = firePos.y - up.y;
        if (dx * dx + dy * dy > maxR2) continue;                            // out of range
        if (!lineClear(world, firePos, {up.x, up.y})) continue;             // wall/closed door blocks
        applyDamage(u->combatState, weapon.damage, /*ignoreArmour=*/true);
        u->damageAlert = true;                                              // survivors turn to the source
        float len = std::sqrt(dx * dx + dy * dy);
        u->damageFromDir = (len > 1e-5f) ? Vector2{dx / len, dy / len} : Vector2{0, 0};
        ++hits;
    }
    return hits;
}
