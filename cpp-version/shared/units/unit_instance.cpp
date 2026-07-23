#include "unit_instance.h"
#include "movement_tuning.h"
#include "unit_types.h"
#include <cmath>

//------------------------------------------------------------------------------
// SectionInstance destructor - RAII cleanup of rendering resources
//------------------------------------------------------------------------------

SectionInstance::~SectionInstance() {
    // Children are destroyed automatically via unique_ptr

    // Unload animations
    if (animations && animCount > 0) {
        UnloadModelAnimations(animations, animCount);
        animations = nullptr;
        animCount = 0;
    }

    // Unload model
    if (hasModel) {
        UnloadModel(model);
        hasModel = false;
    }
}

//------------------------------------------------------------------------------
// Motor-joint movement control
//------------------------------------------------------------------------------

b2BodyId unit_create_origin_body(b2WorldId world) {
    b2BodyDef def = b2DefaultBodyDef();
    def.type = b2_staticBody;
    def.position = {0.0f, 0.0f};
    // rotation defaults to identity; no shape — pure transform anchor.
    return b2CreateBody(world, &def);
}

void unit_attach_motor_joint(UnitInstance* unit, b2WorldId world, b2BodyId originBody) {
    if (!unit || !b2Body_IsValid(unit->bodyId) || !b2Body_IsValid(originBody)) {
        return;
    }

    b2MotorJointDef def = b2DefaultMotorJointDef();
    def.bodyIdA = originBody;           // static anchor at world origin (identity)
    def.bodyIdB = unit->bodyId;
    // Initial target = current transform so an undriven unit holds station.
    def.linearOffset = b2Body_GetPosition(unit->bodyId);
    def.angularOffset = b2Rot_GetAngle(b2Body_GetRotation(unit->bodyId));
    // Initial force authority: per-type acceleration if the definition provides it,
    // otherwise the global default. unit_set_move_target() refines this each frame
    // (acceleration vs. deceleration depending on motion).
    float maxForce = UNIT_MOTOR_MAX_FORCE;
    const UnitDefinition* d = unit->definition;
    if (d && d->maxSpeed > 0.0f && d->acceleration > 0.0f) {
        maxForce = b2Body_GetMass(unit->bodyId) * d->acceleration
                 * MOVEMENT_UNIT_SCALE * UNIT_MOTOR_AUTHORITY;
    }
    def.maxForce = maxForce;
    def.maxTorque = UNIT_MOTOR_MAX_TORQUE;
    def.correctionFactor = UNIT_MOTOR_CORRECTION_FACTOR;
    def.collideConnected = false;

    unit->motorJoint = b2CreateMotorJoint(world, &def);
}

void unit_set_move_target(UnitInstance* unit, Vector2 targetPos, float targetFacing) {
    if (!unit || !b2Joint_IsValid(unit->motorJoint)) {
        return;
    }
    // Setting a joint offset does not wake a sleeping body; make sure the body is
    // awake so it always responds to a new command (belt-and-suspenders alongside
    // enableSleep=false on the body).
    if (b2Body_IsValid(unit->bodyId)) {
        b2Body_SetAwake(unit->bodyId, true);
    }
    b2MotorJoint_SetLinearOffset(unit->motorJoint, {targetPos.x, targetPos.y});
    b2MotorJoint_SetAngularOffset(unit->motorJoint, targetFacing);

    // Per-type force authority: allow up to (mass * acceleration) while speeding up
    // toward the target, and (mass * deceleration) while braking. This honours the
    // distinct accel/decel values from the original data. Units without movement
    // data keep the maxForce the joint was created with (global tuning).
    const UnitDefinition* def = unit->definition;
    if (def && def->maxSpeed > 0.0f && def->acceleration > 0.0f) {
        b2Vec2 pos = b2Body_GetPosition(unit->bodyId);
        b2Vec2 vel = b2Body_GetLinearVelocity(unit->bodyId);
        float dx = targetPos.x - pos.x;
        float dy = targetPos.y - pos.y;
        float dist = sqrtf(dx * dx + dy * dy);

        // Braking when holding position (target ~= here) or when current motion
        // opposes the target direction; otherwise accelerating.
        float speedToward = (dist > 1e-4f) ? (vel.x * dx + vel.y * dy) / dist : 0.0f;
        bool braking = (dist < 0.05f) || (speedToward < 0.0f);

        float rate = braking ? def->deceleration : def->acceleration;
        if (rate <= 0.0f) rate = def->acceleration;  // decel unspecified -> use accel
        float mass = b2Body_GetMass(unit->bodyId);
        b2MotorJoint_SetMaxForce(unit->motorJoint,
                                 mass * rate * MOVEMENT_UNIT_SCALE * UNIT_MOTOR_AUTHORITY);
    }
}
