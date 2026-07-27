#ifndef CHARGER_MANAGER_H
#define CHARGER_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>

//------------------------------------------------------------------------------
// Chargers — an animated, walkable map object (like doors, but simpler): no
// collision, and a free-running tile animation. The simulation only tracks an
// IDLE/CHARGING state from unit proximity (polled overlap, same as doors) so the
// energy-recharge interaction can be added later; it exposes a read-only ChargerView
// for the presentation layer. The tile animation itself is a render concern (the
// renderer cycles frames by time), independent of state. See docs/charger.md.
//------------------------------------------------------------------------------

enum class ChargerState { Idle, Charging };

// Tuning.
inline constexpr float CHARGER_SENSOR_MARGIN = 0.2f;   // world units around the tile for proximity
inline constexpr float CHARGER_FRAME_TIME    = 0.1f;   // seconds per animation frame

// Source-agnostic spawn descriptor (world units; worldScale = 1).
struct ChargerSpec {
    Vector2 physicsCenter = {0, 0};
    Vector2 size = {1.0f, 1.0f};   // tile footprint
    int col = 0;
    int row = 0;
};

// Read-only render-state.
struct ChargerView {
    Vector2 worldPos;
    Vector2 size;
    int col, row;
    ChargerState state;
};

class ChargerManager {
public:
    ~ChargerManager();   // RAII safety net; destroy() is idempotent (see game_destroy for order)
    void init(b2WorldId world, const std::vector<ChargerSpec>& specs);
    void update(float dt);   // poll proximity -> state
    void destroy();
    const std::vector<ChargerView>& views() const { return views_; }

private:
    bool unitInRange(const ChargerSpec& s) const;

    struct Charger {
        ChargerSpec spec;
        ChargerState state = ChargerState::Idle;
    };

    b2WorldId world_ = b2_nullWorldId;
    std::vector<Charger> chargers_;
    std::vector<ChargerView> views_;
};

#endif // CHARGER_MANAGER_H
