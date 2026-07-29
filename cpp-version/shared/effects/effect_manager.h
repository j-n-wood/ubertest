#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>
#include <cstdint>

//------------------------------------------------------------------------------
// EffectManager — transient world effects, managed like doors/chargers but
// spawned dynamically (not from tiles). The first kind is Explosion: an animated
// additive billboard (rendered by the game) that does NOT collide and deals area
// damage over time. More effect kinds (some non-damaging) will be added as new
// EffectType values with their own update/render rules. See docs/effects.md.
//------------------------------------------------------------------------------

enum class EffectType { Explosion };

// Explosion tuning (see docs/effects.md).
inline constexpr float EXPLOSION_DPS      = 10.0f;   // energy/s at the core
inline constexpr float EXPLOSION_RADIUS   = 0.75f;   // damage radius (visual diameter = 2x)
inline constexpr float EXPLOSION_CORE     = 0.25f;   // dps is capped to EXPLOSION_DPS within this
inline constexpr float EXPLOSION_FPS      = 10.0f;   // animation frames/second
inline constexpr int   EXPLOSION_FRAMES   = 8;       // rlboom sheet is 8x1
inline constexpr float EXPLOSION_LIFETIME = EXPLOSION_FRAMES / EXPLOSION_FPS;  // plays once (0.8s)

struct Effect {
    EffectType type = EffectType::Explosion;
    Vector2 pos = {0, 0};
    float age = 0.0f;           // seconds alive; drives animation + lifetime
    float rotationDeg = 0.0f;   // fixed random screen-space rotation
    int32_t ownerGroup = 0;     // collisionGroupId of the source unit (immune to its own blast)
    bool active = true;
};

class EffectManager {
public:
    ~EffectManager();

    void init(b2WorldId world);   // bind the active level world; clears existing effects
    void spawnExplosion(Vector2 pos, int32_t ownerGroup);
    void update(float dt);        // advance age, apply area damage, expire + compact
    void destroy();               // clear (no bodies to free — effects don't collide)

    const std::vector<Effect>& getEffects() const { return effects_; }

private:
    b2WorldId world_ = b2_nullWorldId;
    std::vector<Effect> effects_;
};

#endif // EFFECT_MANAGER_H
