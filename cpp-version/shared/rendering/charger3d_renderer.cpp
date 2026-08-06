#include "rendering/charger3d_renderer.h"
#include "rendering/render_scope.h"   // DisableDepthMaskScope
#include "raymath.h"
#include <cstdlib>
#include <cmath>

namespace {
// Fixed pool per charger (recycled — no per-frame allocation). Idle splits into two antipodal
// arcs, so this is even; each arc gets COUNT/2 particles.
constexpr int   PARTICLE_COUNT = 64;
constexpr float RING_RADIUS    = 0.55f;  // m — fits within a 1-tile charger pad
constexpr float PARTICLE_Y     = 0.25f;  // m above the floor (uber z=10 units)
constexpr float INFALL_SPEED   = 0.6f;   // m/s inward for the charging state
constexpr float ORBIT_SPEED    = 2.0f;   // rad/s ring rotation (both states)
constexpr float BASE_SIZE      = 0.15f;  // m billboard size at full brightness
constexpr float IDLE_ARC_SPAN  = 2.0f;   // rad each idle arc trails behind its leading point
constexpr Color GLOW_COLOR     = {153, 204, 255, 255};  // uber (0.6, 0.8, 1.0)

inline float frand() { return (float)std::rand() / (float)RAND_MAX; }
}

void Charger3DRenderer::recycleActive(Particle& p) const {
    // Spawn on the ring at a random angle, moving inward toward the centre (infall), reaching it as
    // it expires. Fades + shrinks via `bright` over its life.
    float a = frand() * 2.0f * PI;
    p.offset = {std::cos(a) * RING_RADIUS, std::sin(a) * RING_RADIUS};
    p.vel = {-std::cos(a) * INFALL_SPEED, -std::sin(a) * INFALL_SPEED};
    p.age = 0.0f;
    p.life = RING_RADIUS / INFALL_SPEED;
    p.baseSize = BASE_SIZE * (0.8f + 0.4f * frand());
}

void Charger3DRenderer::seedForState(Pool& pool) const {
    pool.parts.resize(PARTICLE_COUNT);
    if (pool.state == ChargerState::Charging) {
        for (Particle& p : pool.parts) {
            recycleActive(p);
            p.age = frand() * p.life;   // stagger so the infall stream is continuous
        }
    } else {
        // Idle is positional (see update): just fix a uniform base size.
        for (Particle& p : pool.parts) p.baseSize = BASE_SIZE;
    }
}

void Charger3DRenderer::update(float dt, const std::vector<ChargerView>& views) {
    if (pools_.size() != views.size()) pools_.assign(views.size(), Pool{});

    const int perArc = PARTICLE_COUNT / 2;
    const float dphi = IDLE_ARC_SPAN / (float)perArc;

    for (size_t i = 0; i < views.size(); i++) {
        Pool& pool = pools_[i];
        pool.center = views[i].worldPos;
        pool.orbit += ORBIT_SPEED * dt;

        bool changed = (!pool.seeded) || (views[i].state != pool.state);
        pool.state = views[i].state;
        if (changed) { seedForState(pool); pool.seeded = true; }

        if (pool.state == ChargerState::Charging) {
            for (Particle& p : pool.parts) {
                p.age += dt;
                p.offset.x += p.vel.x * dt;
                p.offset.y += p.vel.y * dt;
                if (p.age >= p.life) recycleActive(p);
                float t = (p.life > 0.0f) ? (p.age / p.life) : 1.0f;
                p.bright = 1.0f - (t > 1.0f ? 1.0f : t);
            }
        } else {
            // Idle: two antipodal arcs of evenly-spaced particles that trail (fade) behind their
            // leading point; the whole pattern rotates with `orbit`. Deterministic -> no gaps.
            for (int k = 0; k < (int)pool.parts.size(); k++) {
                Particle& p = pool.parts[k];
                int arc = k & 1;             // 0 / 1 -> the two antipodal arcs
                int idx = k >> 1;            // position along the arc (0 = leading)
                float phi = pool.orbit + (arc ? PI : 0.0f) - idx * dphi;
                p.offset = {std::cos(phi) * RING_RADIUS, std::sin(phi) * RING_RADIUS};
                p.bright = 1.0f - (float)idx / (float)(perArc - 1);  // leading bright -> trailing dim
            }
        }
    }
}

void Charger3DRenderer::render(const Camera3D& camera, Texture2D flare) const {
    if (pools_.empty() || flare.id == 0) return;

    Rectangle src = {0, 0, (float)flare.width, (float)flare.height};
    BeginBlendMode(BLEND_ADDITIVE);
    DisableDepthMaskScope depthGuard;   // additive: test depth vs opaque, but don't occlude
    for (const Pool& pool : pools_) {
        for (const Particle& p : pool.parts) {
            if (p.bright <= 0.0f) continue;
            float size = p.baseSize * p.bright;
            if (size <= 0.0f) continue;
            Color c = GLOW_COLOR;
            c.a = (unsigned char)(255.0f * p.bright);
            Vector3 pos = {pool.center.x + p.offset.x, PARTICLE_Y, pool.center.y + p.offset.y};
            DrawBillboardPro(camera, flare, src, pos, camera.up, (Vector2){size, size},
                             (Vector2){size * 0.5f, size * 0.5f}, 0.0f, c);
        }
    }
    EndBlendMode();
}

void Charger3DRenderer::destroy() {
    pools_.clear();
}
