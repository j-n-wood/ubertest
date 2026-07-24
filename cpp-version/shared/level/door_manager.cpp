#include "door_manager.h"

#include <algorithm>
#include <cstdint>

namespace {
// Overlap-query sink: any hit means a unit is within the door's proximity region.
struct UnitOverlapCtx { bool found = false; };
bool unitOverlapCallback(b2ShapeId, void* ctx) {
    static_cast<UnitOverlapCtx*>(ctx)->found = true;
    return false;  // stop at the first unit
}
}  // namespace

//------------------------------------------------------------------------------
// State machine (pure — no physics, unit-testable)
//------------------------------------------------------------------------------

void door_advance(DoorSim& s, bool unitInRange, float dt) {
    switch (s.state) {
        case DoorState::Closed:
            if (unitInRange) s.state = DoorState::Opening;
            break;

        case DoorState::Opening:
            s.openFraction += dt / DOOR_OPEN_TIME;
            if (s.openFraction >= 1.0f) {
                s.openFraction = 1.0f;
                s.state = DoorState::Open;
                s.closeHold = 0.0f;
            }
            break;

        case DoorState::Open:
            if (unitInRange) {
                s.closeHold = 0.0f;
            } else {
                s.closeHold += dt;
                if (s.closeHold >= DOOR_CLOSE_DELAY) s.state = DoorState::Closing;
            }
            break;

        case DoorState::Closing:
            if (unitInRange) {              // a unit returned — reopen
                s.state = DoorState::Opening;
                break;
            }
            s.openFraction -= dt / DOOR_OPEN_TIME;
            if (s.openFraction <= 0.0f) {
                s.openFraction = 0.0f;
                s.state = DoorState::Closed;
            }
            break;
    }
}

//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

DoorManager::~DoorManager() {
    destroy();
}

void DoorManager::init(b2WorldId world, const std::vector<DoorSpec>& specs) {
    destroy();
    world_ = world;
    if (B2_IS_NULL(world_)) return;

    doors_.reserve(specs.size());  // stable addresses for BodyUserData pointers
    for (size_t i = 0; i < specs.size(); i++) {
        doors_.push_back(Door{});
        Door& d = doors_.back();
        d.spec = specs[i];
        d.userData.tag = BodyTag::Door;
        d.userData.owner = reinterpret_cast<void*>(static_cast<uintptr_t>(i));

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {d.spec.physicsCenter.x, d.spec.physicsCenter.y};
        bodyDef.userData = &d.userData;
        d.body = b2CreateBody(world_, &bodyDef);

        // Solid collision shape (closed to start): blocks units + projectiles.
        b2Polygon box = b2MakeBox(d.spec.size.x * 0.5f, d.spec.size.y * 0.5f);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.filter.categoryBits = CATEGORY_DOOR;
        shapeDef.filter.maskBits = MASK_DOOR_SOLID;
        d.collisionShape = b2CreatePolygonShape(d.body, &shapeDef, &box);
        d.solid = true;
    }

    rebuildViews();  // views() valid immediately (all doors start Closed)
}

void DoorManager::destroy() {
    if (!B2_IS_NULL(world_)) {
        for (Door& d : doors_) {
            if (b2Body_IsValid(d.body)) b2DestroyBody(d.body);
        }
    }
    doors_.clear();
    views_.clear();
    world_ = b2_nullWorldId;
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

bool DoorManager::unitInSensorRange(const Door& d) const {
    // Poll the proximity region (door box + margin) for any unit. Projectiles are
    // excluded by the filter, so they never open a door.
    float hx = d.spec.size.x * 0.5f + DOOR_SENSOR_MARGIN;
    float hy = d.spec.size.y * 0.5f + DOOR_SENSOR_MARGIN;
    b2AABB aabb;
    aabb.lowerBound = {d.spec.physicsCenter.x - hx, d.spec.physicsCenter.y - hy};
    aabb.upperBound = {d.spec.physicsCenter.x + hx, d.spec.physicsCenter.y + hy};
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_DOOR;
    filter.maskBits = MASK_DOOR_SENSOR;  // units only
    UnitOverlapCtx ctx;
    b2World_OverlapAABB(world_, aabb, filter, unitOverlapCallback, &ctx);
    return ctx.found;
}

void DoorManager::applyCollisionFilter(Door& d) {
    bool solid = (d.sim.state != DoorState::Open);
    if (solid == d.solid) return;  // only touch the filter on a change
    d.solid = solid;
    b2Filter f;
    f.categoryBits = CATEGORY_DOOR;
    f.maskBits = solid ? MASK_DOOR_SOLID : 0;  // 0 = collides with nothing (open)
    f.groupIndex = 0;
    b2Shape_SetFilter(d.collisionShape, f);
}

void DoorManager::update(float dt) {
    if (B2_IS_NULL(world_)) return;

    // Poll each door's proximity region for units, advance its state machine, toggle
    // collision, and publish render-state.
    for (Door& d : doors_) {
        door_advance(d.sim, unitInSensorRange(d), dt);
        applyCollisionFilter(d);
    }
    rebuildViews();
}

void DoorManager::rebuildViews() {
    views_.clear();
    views_.reserve(doors_.size());
    for (const Door& d : doors_) {
        views_.push_back(DoorView{
            d.spec.orientation, d.spec.physicsCenter, d.spec.size,
            d.sim.state, d.sim.openFraction, d.spec.col, d.spec.row});
    }
}
