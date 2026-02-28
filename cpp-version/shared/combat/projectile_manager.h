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
    int32_t ownerId = 0;        // Collision group ID of the unit that fired
    bool active = true;
    b2BodyId bodyId = b2_nullBodyId;
    BodyUserData userData;
};

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

inline constexpr float PROJECTILE_RADIUS = 0.1f;

//------------------------------------------------------------------------------
// Projectile Manager
//------------------------------------------------------------------------------

class ProjectileManager {
public:
    // Spawn a projectile as a Box2D body.
    // Direction is normalised internally. Lifetime in seconds.
    void spawn(b2WorldId worldId, Vector2 position, Vector2 direction,
               float speed, float damage, float lifetime, int32_t ownerId);

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

    // Number of currently active projectiles.
    int activeCount() const;

private:
    std::vector<Projectile> m_projectiles;
};

#endif // PROJECTILE_MANAGER_H
