#ifndef CONSOLE_MANAGER_H
#define CONSOLE_MANAGER_H

#include "raylib.h"
#include <vector>

//------------------------------------------------------------------------------
// Consoles — static, usable map tiles (type="console"). Simplest of the tile
// objects: no collision, no animation, no Box2D. The manager just tracks whether
// the player is standing near the centre of any console tile, so gameplay can
// offer a "use" (SPACE) action. See docs/console.md.
//------------------------------------------------------------------------------

inline constexpr float CONSOLE_USE_RADIUS = 0.5f;  // player must be near the tile centre

struct ConsoleSpec {
    Vector2 physicsCenter = {0, 0};
    int col = 0;
    int row = 0;
};

class ConsoleManager {
public:
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
