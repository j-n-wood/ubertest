#ifndef CONSOLE_MANAGER_H
#define CONSOLE_MANAGER_H

#include "raylib.h"
#include <array>
#include <vector>

//------------------------------------------------------------------------------
// Consoles — static, usable map tiles (type="console"). Simplest of the tile
// objects: no collision, no animation, no Box2D. The manager just tracks whether
// the player is standing near the centre of any console tile, so gameplay can
// offer a "use" (SPACE) action. See docs/console.md.
//------------------------------------------------------------------------------

// console.gltf footprint half-extents (metres), from its bounding box (X ±0.305, Z ±0.813). Used for
// both the 3D console's collision box and the "use" zone in front of it (see consoleFacingAngle).
inline constexpr float CONSOLE_HALF_X = 0.305f;
inline constexpr float CONSOLE_HALF_Z = 0.813f;

struct ConsoleSpec {
    Vector2 physicsCenter = {0, 0};
    int col = 0;
    int row = 0;
    float facingRad = 0.0f;   // authored facing (rotation about up); used by the 3D console model
};

// The console model's facing about the up axis (radians), shared by the 3D renderer and its collision
// footprint so they stay aligned: the authored rotation.z is negated for the game->render depth flip,
// plus a 90° offset for the model's default orientation.
inline float consoleFacingAngle(float facingRad) { return facingRad - PI * 0.5f; }

// The "use" zone is a box the size of the console footprint placed IN FRONT of the console (the
// accessible/room side; the console body blocks its own centre). Single-sourced here so the proximity
// check and the V-debug outline agree. Corners are the box's 4 world-space corners in the XZ plane.
Vector2 consoleUseZoneCenter(const ConsoleSpec& c);
std::array<Vector2, 4> consoleUseZoneCorners(const ConsoleSpec& c);

class ConsoleManager {
public:
    ~ConsoleManager();   // RAII safety net; destroy() is idempotent (see game_destroy for order)
    void init(const std::vector<ConsoleSpec>& specs);
    void update(Vector2 playerPos);
    void destroy();

    bool playerInRange() const { return playerInRange_; }
    const std::vector<ConsoleSpec>& consoles() const { return consoles_; }

private:
    std::vector<ConsoleSpec> consoles_;
    bool playerInRange_ = false;
};

#endif // CONSOLE_MANAGER_H
