#ifndef PROJECTILE_MANAGER_H
#define PROJECTILE_MANAGER_H

#include "units/combat_state.h"
#include "physics/body_user_data.h"
#include "raymath.h"
#include "box2d/box2d.h"
#include <vector>
#include <cstdint>

//------------------------------------------------------------------------------
// Projectile (a single in-flight projectile as a Box2D body)
//------------------------------------------------------------------------------

struct Projectile {
    Vector2 position = {0, 0};
    Vector2 velocity = {0, 0};
    float damage = 0.0f;
    float remainingLifetime = 0.0f;
    float age = 0.0f;           // Time alive (drives per-instance sprite animation)
    float radius = 0.1f;        // Physics (collision) radius (default == PROJECTILE_RADIUS)
    int32_t ownerId = 0;        // Collision group ID of the unit that fired
    int weaponId = -1;          // Firing weapon (for per-weapon rendering; -1 = unknown)
    bool active = true;
    b2BodyId bodyId = b2_nullBodyId;
    BodyUserData userData;
};

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

inline constexpr float PROJECTILE_RADIUS = 0.1f;

//------------------------------------------------------------------------------
// Projectile impact — one recorded per projectile that hit something this step, for the
// render layer to spawn impact sparks (render-only; the sim just records the geometry).
//------------------------------------------------------------------------------

struct ProjectileImpact {
    Vector2 point = {0, 0};    // world impact point
    Vector2 normal = {0, 0};   // surface normal at impact (from a hit event; else −incident)
    Vector2 incident = {0, 0}; // unit travel direction of the projectile
    int weaponId = -1;
};

//------------------------------------------------------------------------------
// Projectile Manager
//------------------------------------------------------------------------------

class ProjectileManager {
public:
    // Spawn a projectile as a Box2D body.
    // Direction is normalised internally. Lifetime in seconds. weaponId is carried on the
    // projectile for per-weapon rendering (it does not affect simulation).
    void spawn(b2WorldId worldId, Vector2 position, Vector2 direction,
               float speed, float damage, float lifetime, int32_t ownerId,
               int weaponId = -1, float radius = PROJECTILE_RADIUS);

    // Decrement lifetime for active projectiles. Deactivate expired ones.
    void update(float dt);

    // Copy Box2D body positions to Projectile::position for all active projectiles.
    void syncFromPhysics();

    // Read Box2D contact events. Apply damage to hit units. Deactivate projectiles on any contact.
    void processContactEvents(b2WorldId worldId);

    // Destroy Box2D bodies for inactive projectiles. Compact the list.
    void cleanup();

    // Read access for rendering or testing.
    const std::vector<Projectile>& getProjectiles() const { return m_projectiles; }

    // Impacts recorded by the most recent processContactEvents (one per projectile that hit
    // something). Consumed by the render layer to spawn impact sparks; valid until the next call.
    const std::vector<ProjectileImpact>& impacts() const { return m_impacts; }

    // Number of currently active projectiles.
    int activeCount() const;

private:
    std::vector<Projectile> m_projectiles;
    std::vector<ProjectileImpact> m_impacts;  // rebuilt each processContactEvents
};

#endif // PROJECTILE_MANAGER_H
