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

    // Motor joint anchoring this unit to the static world-origin body. Its
    // linearOffset is the desired world position and angularOffset the desired
    // world facing. Driven identically by the AI or the player via
    // unit_set_move_target(). b2_nullJointId until unit_attach_motor_joint().
    b2JointId motorJoint = b2_nullJointId;

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

    // Rendering-only: set false when the player has no line of sight to this unit.
    // Does not affect simulation (AI/physics/combat continue) — only viewport draw.
    bool visible = true;

    // Rendering-only vertical (world Y) lift added to every section, used to draw the
    // player's influence device on top of the unit it is piloting (physics is 2D, so
    // "on top" is a render offset). 0 for normal units. See docs/transfer.md.
    float renderHeightOffset = 0.0f;
};

//------------------------------------------------------------------------------
// Motor-joint movement control (shared by AI and player — see movement_tuning.h)
//------------------------------------------------------------------------------

// Create the static, shapeless anchor body at the world origin (identity
// transform). It is the bodyA for every unit's motor joint, so a joint's
// linearOffset equals the target world position and angularOffset the target
// world facing. One per Box2D world; caller owns it (destroyed with the world).
b2BodyId unit_create_origin_body(b2WorldId world);

// Attach a motor joint anchoring `unit` to `originBody`, using the shared tuning
// parameters. The joint's initial target is the unit body's current transform, so
// an undriven unit holds station. Safe to call once per unit after its body exists.
void unit_attach_motor_joint(UnitInstance* unit, b2WorldId world, b2BodyId originBody);

// Set the desired world position and facing for a unit's motor joint. This is the
// single control entry point both the AI state machine and the player-input code
// call — the enforcement point for the identical-simulation invariant. No-op if
// the unit has no (valid) motor joint.
void unit_set_move_target(UnitInstance* unit, Vector2 targetPos, float targetFacing);

// Re-apply the unit's per-type movement tuning (the linear-damping terminal-speed cap)
// from its current definition to the live body. acceleration/deceleration are already
// read live by unit_set_move_target every frame, but maxSpeed is baked into linear
// damping at creation — call this after editing a definition's maxSpeed to retune an
// existing instance without re-creating it. No-op if the body is invalid.
void unit_apply_movement_tuning(UnitInstance* unit);

// Enable or disable a unit's collisions by swapping its shape filter: enabled restores
// the normal unit filter {CATEGORY_UNIT, MASK_UNIT, collisionGroupId}; disabled sets
// {0, 0, groupIndex} so it collides with nothing and cannot be hit by projectiles (there
// is no damage-side guard — hittability is purely the filter). Used by the transfer
// mechanic to make the piloting device invulnerable. No-op if the body is invalid.
void unit_set_collision_enabled(UnitInstance* unit, bool enabled);

#endif // UNIT_INSTANCE_H
