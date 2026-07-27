#include "charger_manager.h"
#include "physics/body_user_data.h"

namespace {
struct UnitOverlapCtx { bool found = false; };
bool unitOverlapCallback(b2ShapeId, void* ctx) {
    static_cast<UnitOverlapCtx*>(ctx)->found = true;
    return false;  // stop at first unit
}
}  // namespace

void ChargerManager::init(b2WorldId world, const std::vector<ChargerSpec>& specs) {
    destroy();
    world_ = world;
    chargers_.reserve(specs.size());
    for (const ChargerSpec& s : specs) {
        chargers_.push_back(Charger{s, ChargerState::Idle});
    }
    // Publish initial views so views() is valid before the first update.
    views_.clear();
    views_.reserve(chargers_.size());
    for (const Charger& c : chargers_) {
        views_.push_back(ChargerView{c.spec.physicsCenter, c.spec.size,
                                     c.spec.col, c.spec.row, c.state});
    }
}

ChargerManager::~ChargerManager() {
    destroy();
}

void ChargerManager::destroy() {
    // No physics bodies to free — chargers are non-colliding.
    chargers_.clear();
    views_.clear();
    world_ = b2_nullWorldId;
}

bool ChargerManager::unitInRange(const ChargerSpec& s) const {
    float hx = s.size.x * 0.5f + CHARGER_SENSOR_MARGIN;
    float hy = s.size.y * 0.5f + CHARGER_SENSOR_MARGIN;
    b2AABB aabb;
    aabb.lowerBound = {s.physicsCenter.x - hx, s.physicsCenter.y - hy};
    aabb.upperBound = {s.physicsCenter.x + hx, s.physicsCenter.y + hy};
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_STATIC;   // query identity is irrelevant here
    filter.maskBits = CATEGORY_UNIT;         // only units count
    UnitOverlapCtx ctx;
    b2World_OverlapAABB(world_, aabb, filter, unitOverlapCallback, &ctx);
    return ctx.found;
}

void ChargerManager::update(float dt) {
    (void)dt;  // state is instantaneous proximity for now (charge accumulation later)
    if (B2_IS_NULL(world_)) return;

    views_.clear();
    views_.reserve(chargers_.size());
    for (Charger& c : chargers_) {
        c.state = unitInRange(c.spec) ? ChargerState::Charging : ChargerState::Idle;
        views_.push_back(ChargerView{c.spec.physicsCenter, c.spec.size,
                                     c.spec.col, c.spec.row, c.state});
    }
}
