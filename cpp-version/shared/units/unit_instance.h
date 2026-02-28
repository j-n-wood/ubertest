#ifndef UNIT_INSTANCE_H
#define UNIT_INSTANCE_H

#include "unit_types.h"
#include "combat_state.h"
#include "physics/body_user_data.h"
#include "box2d/box2d.h"
#include <memory>
#include <vector>

//------------------------------------------------------------------------------
// Section Instance (runtime section - rendering only, no per-section physics)
//------------------------------------------------------------------------------

class SectionInstance {
public:
    SectionInstance() = default;
    ~SectionInstance();

    // Non-copyable (owns model resources)
    SectionInstance(const SectionInstance&) = delete;
    SectionInstance& operator=(const SectionInstance&) = delete;

    // Movable
    SectionInstance(SectionInstance&&) = default;
    SectionInstance& operator=(SectionInstance&&) = default;

    const SectionDefinition* definition = nullptr;

    // Rendering
    Model model = {};
    bool hasModel = false;

    // Animation
    ModelAnimation* animations = nullptr;
    int animCount = 0;
    int currentAnim = 0;
    int currentFrame = 0;
    bool animPlaying = false;

    // Hierarchy
    SectionInstance* parent = nullptr;
    std::vector<std::unique_ptr<SectionInstance>> children;

    // Cached world transform (updated each frame from unit physics + offsets)
    Vector2 worldPosition = {0, 0};
    float worldRotation = 0.0f;

    // Facing angle override (used when rotationMode == FollowFacing)
    float facingAngle = 0.0f;
};

//------------------------------------------------------------------------------
// Debris Object (created when unit is dismantled)
//------------------------------------------------------------------------------

struct DebrisObject {
    b2BodyId bodyId = b2_nullBodyId;
    BodyUserData bodyUserData;
    Model model = {};
    bool hasModel = false;
    float height = 0.0f;
    int32_t collisionGroup = 0;
};

//------------------------------------------------------------------------------
// Unit Instance (runtime unit with single physics body)
//------------------------------------------------------------------------------

struct UnitInstance {
    const UnitDefinition* definition = nullptr;

    // Single physics body for the entire unit
    b2BodyId bodyId = b2_nullBodyId;

    // Section hierarchy (rendering only)
    std::unique_ptr<SectionInstance> rootSection;

    // All section instances flattened for iteration
    std::vector<SectionInstance*> allSections;

    // Collision filtering - negative group index prevents self-collision
    int32_t collisionGroupId = 0;

    // Body identification for contact events
    BodyUserData bodyUserData;

    // Combat
    UnitCombatState combatState;

    // State
    bool active = true;
};

#endif // UNIT_INSTANCE_H
