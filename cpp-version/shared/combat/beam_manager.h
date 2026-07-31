#ifndef BEAM_MANAGER_H
#define BEAM_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>
#include <cstddef>
#include <cstdint>

struct UnitInstance;

//------------------------------------------------------------------------------
// Beam weapons (simulation layer). A beam is an instantaneous hitscan line from a
// firing point along the firing section's facing: it damages every unit whose body the
// line passes through, up to `maxRange` or the first solid object (wall / closed door),
// whichever is nearer. Beams are transient — they exist only on the frame they are fired,
// so callers `beginFrame()` (clear), then `fire()` for each active beam this frame, then
// `update(dt)` (advance the shared animation cursor). Rendering reads `beams()` read-only
// and draws each as a tiled additive quad (see game_render_gameplay). Mirrors the sim/
// render split of ProjectileManager/EffectManager. See docs/weapons.md.
//------------------------------------------------------------------------------

// One active beam this frame — geometry only (the render layer turns it into a quad).
struct Beam {
    Vector2 origin = {0, 0};   // firing point (world)
    float angle = 0.0f;        // facing angle (radians); direction = {-sin, cos}
    float length = 0.0f;       // truncated length (world units) after wall/range clipping
    int weaponId = -1;         // selects the frame set (plasma vs lightning)
};

// Tunables.
inline constexpr float BEAM_HALF_WIDTH = 0.18f;   // half the beam's damage/visual width (world units)
inline constexpr float BEAM_HEIGHT = 0.45f;       // draw height (world Y), near unit mid-height
inline constexpr float BEAM_TILE_WORLD = 1.0f;    // one texture tile per this many world units
inline constexpr float BEAM_ANIM_FPS = 10.0f;     // frame set cycles at this rate
inline constexpr int BEAM_FRAME_COUNT = 3;

class BeamManager {
public:
    // Clear all beams registered last frame. Call once at the start of the sim block,
    // before any fire() this frame.
    void beginFrame() { beams_.clear(); }

    // Register and simulate one beam for this frame. Casts from `origin` along `angle` for
    // up to `maxRange`, clipping to the first wall/closed door. Continuously accumulates
    // `dps * dt` of damage onto every target whose body the beam passes through (excluding
    // `shooter`), via the same realtime-damage accumulator explosions use — so fireRate is
    // irrelevant and the beam damages while held. Records the clipped geometry for rendering.
    // Returns the clipped length.
    float fire(b2WorldId world, Vector2 origin, float angle, float maxRange,
               float dps, float dt, const UnitInstance* shooter,
               UnitInstance* const* targets, std::size_t targetCount, int weaponId);

    // Advance the shared animation cursor (frame index cycles at BEAM_ANIM_FPS).
    void update(float dt);

    const std::vector<Beam>& beams() const { return beams_; }
    int animFrame() const { return frame_; }   // 0 .. BEAM_FRAME_COUNT-1

    // Distance from `origin` along `angle` to the first solid wall/closed door, clamped to
    // `maxRange` (== maxRange if nothing is hit). Exposed for testing.
    static float castLength(b2WorldId world, Vector2 origin, float angle, float maxRange);

    // Does the beam segment (origin, angle, length) pass through `unit`'s body
    // (perpendicular distance within its collision radius + BEAM_HALF_WIDTH, and the
    // closest point within [0, length])? Exposed for testing.
    static bool hitsUnit(Vector2 origin, float angle, float length, const UnitInstance* unit);

private:
    std::vector<Beam> beams_;
    float animTimer_ = 0.0f;
    int frame_ = 0;
};

#endif // BEAM_MANAGER_H
