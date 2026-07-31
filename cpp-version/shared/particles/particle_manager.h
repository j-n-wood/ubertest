#ifndef PARTICLE_MANAGER_H
#define PARTICLE_MANAGER_H

#include "raylib.h"
#include "rendering/texture_manager.h"
#include <vector>
#include <cstddef>

//------------------------------------------------------------------------------
// ParticleManager — CPU-simulated particles rendered as additive billboards.
//
// Storage is struct-of-arrays (SoA): one contiguous array per field. The per-frame
// update touches only the "hot" numeric arrays (pos/vel/age/rot), so it is a set of
// unit-stride, cache-dense, vectorizable loops. Particles are unordered, so removal
// is O(1) swap-and-pop across all arrays. This is the one pool that can reach
// thousands of instances; the AoS Projectile/Effect pools stay AoS.
//
// The manager is data-only (no raylib draw calls): the game renders from renderData()
// as one additive DrawBillboardPro batch (all particles of a system share a texture,
// so rlgl collapses them into ~1 draw call). See docs/effects.md.
//------------------------------------------------------------------------------

// One-shot radial burst configuration.
struct ParticleBurst {
    int       count       = 16;
    float     speedMin    = 1.5f;
    float     speedMax    = 3.0f;
    float     lifeMin     = 0.3f;
    float     lifeMax     = 0.6f;
    float     startSize   = 0.15f;
    float     endSize     = 0.0f;
    Color     startColor  = {255, 200, 120, 255};
    Color     endColor    = {255, 80, 0, 0};    // fades to transparent (additive → out)
    float     angularVelMax = 180.0f;           // deg/s, random sign
    TextureId texture     = TEX_FLARE;
    // Emission direction. Velocity angle = atan2(vy, vx). `spreadRad` is the half-cone about
    // `dirAngle`; the default (>= PI) is a full 360° radial burst (back-compatible). A small
    // spread emits a directional jet — e.g. sparks reflected off a surface.
    float     dirAngle    = 0.0f;               // radians
    float     spreadRad   = PI;                 // half-cone; >= PI = full radial
};

// Read-only view of the particle arrays for rendering (pointers valid until the next
// burst/update/clear).
struct ParticleSpan {
    std::size_t n = 0;
    const float* posX = nullptr;
    const float* posY = nullptr;
    const float* age = nullptr;
    const float* lifetime = nullptr;
    const float* rot = nullptr;
    const float* startSize = nullptr;
    const float* endSize = nullptr;
    const Color* startColor = nullptr;
    const Color* endColor = nullptr;
    const int*   texture = nullptr;
};

class ParticleManager {
public:
    // Spawn cfg.count particles radiating from `pos` (randomised speed/life/spin).
    void burst(const ParticleBurst& cfg, Vector2 pos);

    // Advance all particles and remove expired ones (swap-and-pop).
    void update(float dt);

    void clear();
    std::size_t count() const { return posX_.size(); }
    ParticleSpan renderData() const;

private:
    void swapPop(std::size_t i);  // remove element i from every array

    // Hot (written every frame)
    std::vector<float> posX_, posY_, velX_, velY_, age_, lifetime_, rot_, angVel_;
    // Cold (set at spawn, read at render)
    std::vector<float> startSize_, endSize_;
    std::vector<Color> startColor_, endColor_;
    std::vector<int>   texture_;
};

// Shared render height for particles (world Y), matching projectiles/explosions.
inline constexpr float PARTICLE_HEIGHT = 0.5f;

#endif // PARTICLE_MANAGER_H
