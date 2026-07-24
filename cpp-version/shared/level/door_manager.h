#ifndef DOOR_MANAGER_H
#define DOOR_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include "physics/body_user_data.h"
#include <vector>

//------------------------------------------------------------------------------
// Doors — a simulation entity, kept independent of any renderer.
//
// The DoorManager owns the sensing (a per-frame Box2D overlap query over each
// door's proximity region — polled rather than edge-triggered, since v3.0.0 sensor
// end-events are unreliable on teleport), the physics (a collision body toggled by
// state), and the open/close state machine. It exposes a read-only render-state
// (DoorView) that any presentation layer consumes — the interim 2D tile renderer
// now, a future 3D renderer or debug overlay later. See docs/doors.md.
//------------------------------------------------------------------------------

enum class DoorOrientation { Horizontal, Vertical };
enum class DoorState { Closed, Opening, Open, Closing };

// Tuning (world units / seconds).
inline constexpr float DOOR_OPEN_TIME     = 0.2f;  // time to fully open
inline constexpr float DOOR_CLOSE_DELAY   = 2.0f;  // delay after last unit leaves before closing
inline constexpr float DOOR_SENSOR_MARGIN = 1.5f;  // extra size around the door for the open sensor

// Source-agnostic spawn descriptor. Produced by a TMX detector today and (later) a
// scene_convert::Door adapter. Coordinates are world units (worldScale = 1).
struct DoorSpec {
    DoorOrientation orientation = DoorOrientation::Horizontal;
    Vector2 physicsCenter = {0, 0};   // door centre in physics/world coords
    Vector2 size = {1.0f, 0.5f};      // collision box size (H: 1.0x0.5, V: 0.5x1.0)
    int col = 0;                       // source grid cell (for the 2D tile renderer)
    int row = 0;
};

// Read-only render-state — the single seam a renderer reads.
struct DoorView {
    DoorOrientation orientation;
    Vector2 worldPos;
    Vector2 size;
    DoorState state;
    float openFraction;   // 0 = closed, 1 = fully open
    int col, row;
};

// Pure state-machine step (no physics) — unit-testable in isolation.
struct DoorSim {
    DoorState state = DoorState::Closed;
    float openFraction = 0.0f;
    float closeHold = 0.0f;   // time since a unit was last in range while open
};
void door_advance(DoorSim& sim, bool unitInRange, float dt);

class DoorManager {
public:
    DoorManager() = default;
    ~DoorManager();
    DoorManager(const DoorManager&) = delete;
    DoorManager& operator=(const DoorManager&) = delete;

    // Create door bodies (collision + sensor) for the given specs. Replaces any
    // existing doors. `reserve` keeps door storage addresses stable for Box2D
    // user-data pointers.
    void init(b2WorldId world, const std::vector<DoorSpec>& specs);

    // Destroy all door bodies (call before the world is destroyed / on level switch).
    void destroy();

    // Process sensor events, advance state machines, toggle collision. Call after
    // b2World_Step so this step's sensor events are available.
    void update(float dt);

    const std::vector<DoorView>& views() const { return views_; }

private:
    struct Door {
        DoorSpec spec;
        DoorSim sim;
        bool solid = true;                       // current collision-filter state
        b2BodyId body = b2_nullBodyId;
        b2ShapeId collisionShape = b2_nullShapeId;
        BodyUserData userData;                   // tag = Door (so units skip rerouting on contact)
    };

    bool unitInSensorRange(const Door& d) const;  // poll the proximity region for units
    void applyCollisionFilter(Door& d);
    void rebuildViews();  // refresh the read-only render-state from door sim state

    b2WorldId world_ = b2_nullWorldId;
    std::vector<Door> doors_;
    std::vector<DoorView> views_;
};

#endif // DOOR_MANAGER_H
