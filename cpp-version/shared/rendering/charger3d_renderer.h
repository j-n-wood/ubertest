#ifndef CHARGER3D_RENDERER_H
#define CHARGER3D_RENDERER_H

#include "raylib.h"
#include "level/charger_manager.h"   // ChargerView, ChargerState
#include <vector>

//------------------------------------------------------------------------------
// Charger presentation (3D particles). Equivalent of the uber ChargerParts /
// IdleChargerParts additive particle systems, driven by ChargerManager::views():
//   Idle     -> a soft light-blue glow ring slowly orbiting the pad.
//   Charging -> particles spawning on the ring and falling inward (infall).
// Each charger owns a FIXED pool of particles that are recycled on expiry (no
// per-frame allocation; steady count = spawn_rate * lifetime). Drawn as additive
// flare billboards. Used by the game's Objects3D mode; the 2D tile modes keep the
// animated-tile ChargerRenderer.
//------------------------------------------------------------------------------

class Charger3DRenderer {
public:
    Charger3DRenderer() = default;

    // Advance/recycle each charger's particle pool. Sizes pools to the view count.
    void update(float dt, const std::vector<ChargerView>& views);

    // Draw all charger particles as additive billboards (flare texture supplied by caller).
    void render(const Camera3D& camera, Texture2D flare) const;

    void destroy();

private:
    struct Particle {
        Vector2 offset;     // world XZ offset from the charger centre
        Vector2 vel;        // world XZ velocity (m/s) — charging (infall) only
        float   age;        // seconds — charging only
        float   life;       // seconds — charging only
        float   baseSize;   // billboard size at full brightness (m)
        float   bright;     // 0..1, recomputed each frame; drives size + additive alpha
    };
    struct Pool {
        Vector2 center{0, 0};
        float   orbit = 0.0f;                     // ring rotation phase (radians)
        ChargerState state = ChargerState::Idle;
        std::vector<Particle> parts;
        bool seeded = false;
    };

    void recycleActive(Particle& p) const;   // (re)spawn an infalling particle on the ring
    void seedForState(Pool& pool) const;     // (re)initialise a pool when its state changes

    std::vector<Pool> pools_;
};

#endif // CHARGER3D_RENDERER_H
