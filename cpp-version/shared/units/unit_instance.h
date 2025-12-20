#ifndef UNIT_INSTANCE_H
#define UNIT_INSTANCE_H

#include "unit_types.h"
#include "box2d/box2d.h"
#include <memory>
#include <vector>

//------------------------------------------------------------------------------
// Section Instance (runtime section with physics body)
//------------------------------------------------------------------------------

class SectionInstance {
public:
    SectionInstance() = default;
    ~SectionInstance();

    // Non-copyable (owns physics/model resources)
    SectionInstance(const SectionInstance&) = delete;
    SectionInstance& operator=(const SectionInstance&) = delete;

    // Movable
    SectionInstance(SectionInstance&&) = default;
    SectionInstance& operator=(SectionInstance&&) = default;

    const SectionDefinition* definition = nullptr;

    // Physics
    b2BodyId bodyId = b2_nullBodyId;
    bool hasPhysics = false;

    // Joint to parent (null for root or after deconstruction)
    b2JointId parentJoint = b2_nullJointId;

    // Rendering
    Model model = {};
    bool hasModel = false;

    // Hierarchy
    SectionInstance* parent = nullptr;
    std::vector<std::unique_ptr<SectionInstance>> children;

    // State
    bool attached = true;                     // False after joint break

    // Cached world transform (updated each frame)
    Vector2 worldPosition = {0, 0};
    float worldRotation = 0.0f;
};

//------------------------------------------------------------------------------
// Unit Instance (runtime unit with all sections)
//------------------------------------------------------------------------------

struct UnitInstance {
    const UnitDefinition* definition = nullptr;
    std::unique_ptr<SectionInstance> rootSection;

    // All section instances flattened for iteration
    std::vector<SectionInstance*> allSections;

    // All joints for quick access during deconstruction
    std::vector<b2JointId> allJoints;

    // Collision filtering - negative group index prevents self-collision
    int32_t collisionGroupId = 0;

    // State
    bool active = true;
};

#endif // UNIT_INSTANCE_H
