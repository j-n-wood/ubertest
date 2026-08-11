#ifndef BEAM_MANAGER_H
#define BEAM_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>
#include <cstddef>
#include <cstdint>

struct UnitInstance;
struct ObjectInstance;

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
    // Impact: true when the beam terminated on ANY collision — wall, closed door, or unit
    // (false when it reached maxRange with nothing in the way). Drives reflected impact sparks.
    // hitNormal is the surface normal at the impact (points back toward the muzzle).
    bool hit = false;
    Vector2 hitPoint = {0, 0};
    Vector2 hitNormal = {0, 0};
};

// Result of a beam ray-cast. The ray stops at the first solid thing (wall, closed door, or
// unit) other than the shooter. `hitWall` is true only for geometry (spawns sparks); `unit`
// is the unit it stopped on (nullptr otherwise) and takes the beam's damage.
struct BeamHit {
    float length = 0.0f;
    bool hitWall = false;      // stopped on geometry — wall/closed door/non-destructible object (→ sparks)
    Vector2 point = {0, 0};
    Vector2 normal = {0, 0};
    UnitInstance* unit = nullptr;      // unit the beam stopped on (absorbs the beam), if any
    ObjectInstance* object = nullptr;  // destructible object the beam stopped on, if any
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

    // Register and simulate one beam for this frame. Casts from `origin` along `angle` for up
    // to `maxRange`, stopping at the first wall, closed door, OR unit (other than `shooter`).
    // If it stops on a unit, that unit continuously takes `dps * dt` damage via the same
    // realtime-damage accumulator explosions use — so fireRate is irrelevant and the beam
    // damages while held. A unit blocks the beam, so anything behind it is shielded. Records
    // the clipped geometry (and any wall impact) for rendering. Returns the clipped length.
    float fire(b2WorldId world, Vector2 origin, float angle, float maxRange,
               float dps, float dt, const UnitInstance* shooter, int weaponId);

    // Advance the shared animation cursor (frame index cycles at BEAM_ANIM_FPS).
    void update(float dt);

    const std::vector<Beam>& beams() const { return beams_; }
    int animFrame() const { return frame_; }   // 0 .. BEAM_FRAME_COUNT-1

    // Ray-cast the beam: clipped length + impact point/normal + the unit stopped on. Stops at
    // the first wall, closed door, or unit other than `shooter` (pass nullptr for no shooter).
    // hitWall=false / unit=nullptr with point at the range end if nothing is hit. For testing.
    static BeamHit castRay(b2WorldId world, Vector2 origin, float angle, float maxRange,
                           const UnitInstance* shooter = nullptr);

    // Distance-only convenience wrapper over castRay. Exposed for testing.
    static float castLength(b2WorldId world, Vector2 origin, float angle, float maxRange);

private:
    std::vector<Beam> beams_;
    float animTimer_ = 0.0f;
    int frame_ = 0;
};

#endif // BEAM_MANAGER_H
