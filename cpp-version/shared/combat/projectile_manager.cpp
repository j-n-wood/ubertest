#include "projectile_manager.h"
#include "units/unit_instance.h"
#include <algorithm>

//------------------------------------------------------------------------------
// Spawn — create a Box2D bullet body
//------------------------------------------------------------------------------

void ProjectileManager::spawn(b2WorldId worldId, Vector2 position, Vector2 direction,
                              float speed, float damage, float lifetime,
                              int32_t ownerId, int weaponId, float radius) {
    float len = Vector2Length(direction);
    if (len < 1e-6f) return;

    Vector2 dir = Vector2Normalize(direction);
    Vector2 vel = Vector2Scale(dir, speed);

    Projectile p;
    p.position = position;
    p.velocity = vel;
    p.damage = damage;
    p.remainingLifetime = lifetime;
    p.age = 0.0f;
    p.radius = (radius > 0.0f) ? radius : PROJECTILE_RADIUS;
    p.ownerId = ownerId;
    p.weaponId = weaponId;
    p.active = true;
    p.userData.tag = BodyTag::Projectile;

    // Each Box2D body stores &Projectile::userData. push_back can reallocate the vector,
    // which would leave every prior body pointing at freed memory (then contact events can
    // no longer identify the projectile and it never deactivates — it bounces / lodges in
    // corners). Detect the move and re-bind all existing bodies to their new addresses.
    const Projectile* dataBefore = m_projectiles.data();
    m_projectiles.push_back(p);
    if (m_projectiles.data() != dataBefore) {
        for (size_t i = 0; i + 1 < m_projectiles.size(); ++i) {
            if (b2Body_IsValid(m_projectiles[i].bodyId)) {
                b2Body_SetUserData(m_projectiles[i].bodyId, &m_projectiles[i].userData);
            }
        }
    }

    size_t index = m_projectiles.size() - 1;
    Projectile& stored = m_projectiles[index];
    stored.userData.owner = reinterpret_cast<void*>(index);

    // Create Box2D body
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {position.x, position.y};
    bodyDef.isBullet = true;
    bodyDef.linearDamping = 0.0f;
    bodyDef.angularDamping = 0.0f;
    bodyDef.gravityScale = 0.0f;
    bodyDef.userData = &stored.userData;

    stored.bodyId = b2CreateBody(worldId, &bodyDef);

    // Create circle shape
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 0.1f;
    shapeDef.friction = 0.0f;
    shapeDef.restitution = 0.0f;
    shapeDef.enableContactEvents = true;
    shapeDef.filter.categoryBits = CATEGORY_PROJECTILE;
    shapeDef.filter.maskBits = MASK_PROJECTILE;
    shapeDef.filter.groupIndex = ownerId;

    b2Circle circle;
    circle.center = {0, 0};
    circle.radius = stored.radius;
    b2CreateCircleShape(stored.bodyId, &shapeDef, &circle);

    // Set velocity
    b2Body_SetLinearVelocity(stored.bodyId, {vel.x, vel.y});
}

//------------------------------------------------------------------------------
// Update — decrement lifetime, deactivate expired
//------------------------------------------------------------------------------

void ProjectileManager::update(float dt) {
    for (auto& p : m_projectiles) {
        if (!p.active) continue;

        p.age += dt;   // drives per-instance sprite animation
        p.remainingLifetime -= dt;
        if (p.remainingLifetime <= 0.0f) {
            p.active = false;
        }
    }
}

//------------------------------------------------------------------------------
// Sync positions from Box2D
//------------------------------------------------------------------------------

void ProjectileManager::syncFromPhysics() {
    for (auto& p : m_projectiles) {
        if (!p.active) continue;
        if (!b2Body_IsValid(p.bodyId)) continue;

        b2Vec2 pos = b2Body_GetPosition(p.bodyId);
        p.position = {pos.x, pos.y};
    }
}

//------------------------------------------------------------------------------
// Process contact events — apply damage and deactivate
//------------------------------------------------------------------------------

void ProjectileManager::processContactEvents(b2WorldId worldId) {
    b2ContactEvents events = b2World_GetContactEvents(worldId);

    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& event = events.beginEvents[i];

        b2BodyId bodyA = b2Shape_GetBody(event.shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(event.shapeIdB);

        auto* udA = static_cast<BodyUserData*>(b2Body_GetUserData(bodyA));
        auto* udB = static_cast<BodyUserData*>(b2Body_GetUserData(bodyB));

        // Identify which is the projectile and which is the other
        BodyUserData* projectileUD = nullptr;
        BodyUserData* otherUD = nullptr;

        if (udA && udA->tag == BodyTag::Projectile) {
            projectileUD = udA;
            otherUD = udB;
        } else if (udB && udB->tag == BodyTag::Projectile) {
            projectileUD = udB;
            otherUD = udA;
        } else {
            continue;
        }

        // Find the projectile by index
        size_t idx = reinterpret_cast<size_t>(projectileUD->owner);
        if (idx >= m_projectiles.size()) continue;

        Projectile& proj = m_projectiles[idx];
        if (!proj.active) continue;

        // If the other body is a unit, apply damage
        if (otherUD && otherUD->tag == BodyTag::Unit) {
            auto* unit = static_cast<UnitInstance*>(otherUD->owner);
            if (unit) {
                applyDamage(unit->combatState, proj.damage);
            }
        }

        // Deactivate projectile on any contact
        proj.active = false;
    }
}

//------------------------------------------------------------------------------
// Cleanup — destroy Box2D bodies for inactive, compact list
//------------------------------------------------------------------------------

void ProjectileManager::cleanup() {
    for (auto& p : m_projectiles) {
        if (!p.active && b2Body_IsValid(p.bodyId)) {
            b2DestroyBody(p.bodyId);
            p.bodyId = b2_nullBodyId;
        }
    }

    m_projectiles.erase(
        std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                        [](const Projectile& p) { return !p.active; }),
        m_projectiles.end());

    // Compaction moved the surviving projectiles, so both the owner index AND each body's
    // stored userData pointer are now stale — re-index and re-bind every survivor.
    for (size_t i = 0; i < m_projectiles.size(); ++i) {
        m_projectiles[i].userData.owner = reinterpret_cast<void*>(i);
        if (b2Body_IsValid(m_projectiles[i].bodyId)) {
            b2Body_SetUserData(m_projectiles[i].bodyId, &m_projectiles[i].userData);
        }
    }
}

//------------------------------------------------------------------------------
// Active count
//------------------------------------------------------------------------------

int ProjectileManager::activeCount() const {
    return static_cast<int>(std::count_if(
        m_projectiles.begin(), m_projectiles.end(),
        [](const Projectile& p) { return p.active; }));
}
