#ifndef DECAL_MANAGER_H
#define DECAL_MANAGER_H

#include "raylib.h"
#include "rendering/texture_manager.h"
#include <vector>

//------------------------------------------------------------------------------
// DecalManager — runtime floor "dirty marks": alpha-textured quads laid flat on the floor
// (blastmarks where destructible objects explode, drips from damaged moving droids). Render-only in
// the sense of the sim/render split — it holds decal state + is stepped in the sim block; the game
// draws the quads (see game_render_gameplay). Decals PERSIST PER DECK (indexed by level) so marks a
// player leaves behind are still there on return, mirroring the eager per-deck rosters. There is no
// lifetime: a decal lives until a cleaner droid fades its alpha to 0 (see game/AI). `cleanable`
// distinguishes these runtime marks (true) from future level-authored decals (false, never faded).
// See docs/decals.md.
//------------------------------------------------------------------------------

// Alpha removed per second while a cleaner is on a mark (a "slow" fade — ~2 s from full).
inline constexpr float DECAL_CLEAN_RATE = 0.5f;
// Per-deck cap so drips can't grow without bound; the oldest mark is dropped past this.
inline constexpr int   DECAL_MAX_PER_LEVEL = 256;

struct Decal {
    Vector2   pos = {0, 0};     // world XZ (physics coords)
    float     size = 0.3f;      // half-extent, world units
    float     rotation = 0.0f;  // yaw about up (radians) — random so marks don't visibly tile
    float     alpha = 1.0f;     // 1 = full, fades to 0 when cleaned (then removed)
    TextureId texture = TEX_DECAL_BLASTMARK;
    bool      cleanable = true; // runtime marks yes; level-authored decals (future) no
};

class DecalManager {
public:
    void build(int levelCount);          // size the per-deck storage (once, at ship load)
    void setActiveLevel(int level);      // which deck's decals are live (spawn/update/render/clean)

    void spawnBlastmark(Vector2 pos, float size);  // scorch where a destructible exploded
    void spawnDrip(Vector2 pos, float size);       // fluid mark from a damaged moving droid

    void update(float dt);               // remove fully-faded decals on the active deck

    // The active deck's decals — the game's render pass reads this.
    const std::vector<Decal>& active() const;

    // Nearest cleanable, still-visible decal on the active deck within maxDist of `pos`, or -1.
    int  nearestCleanable(Vector2 pos, float maxDist) const;
    // Fade decal `idx` (active deck) by DECAL_CLEAN_RATE*dt; returns true once it's fully cleaned.
    bool cleanAt(int idx, float dt);

    void clear();                        // drop every deck's decals (teardown)

private:
    std::vector<std::vector<Decal>> byLevel_;   // decals per level index
    int active_ = 0;

    std::vector<Decal>* activeVec();
    const std::vector<Decal>* activeVec() const;
};

#endif // DECAL_MANAGER_H
