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
    // Per-type facing turn rate: bound the torque so the unit turns at up to turnSpeed
    // rad/s instead of snapping (see unit_motor_max_torque / DEFAULT_TURN_SPEED).
    def.maxTorque = unit_motor_max_torque(b2Body_GetInertiaTensor(unit->bodyId),
                                          d ? d->turnSpeed : 0.0f);
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

        // "Holding": the target is (approximately) the current position — drive input was
        // released or the unit parked. Distinct from a course-correction brake (moving
        // away from a still-distant target), which stays a normal deceleration.
        bool holding = (dist < UNIT_HOLD_THRESHOLD);
        bool coasting = holding && (def->coastDamping >= 0.0f);

        if (coasting) {
            // Coast: no drive force, glide to a stop under coastDamping alone (see the
            // coast-model comment in movement_tuning.h). Facing (maxTorque) is untouched.
            b2Body_SetLinearDamping(unit->bodyId, def->coastDamping);
            b2MotorJoint_SetMaxForce(unit->motorJoint, 0.0f);
        } else {
            // Normal drive/brake: restore the base driving damping (terminal-speed cap)
            // and set the force authority from acceleration / deceleration.
            b2Body_SetLinearDamping(unit->bodyId,
                                    unit_base_linear_damping(def->maxSpeed, def->acceleration));
            float speedToward = (dist > 1e-4f) ? (vel.x * dx + vel.y * dy) / dist : 0.0f;
            bool braking = holding || (speedToward < 0.0f);
            float rate = braking ? def->deceleration : def->acceleration;
            if (rate <= 0.0f) rate = def->acceleration;  // decel unspecified -> use accel
            float mass = b2Body_GetMass(unit->bodyId);
            b2MotorJoint_SetMaxForce(unit->motorJoint,
                                     mass * rate * MOVEMENT_UNIT_SCALE * UNIT_MOTOR_AUTHORITY);
        }
    }
}

void unit_apply_movement_tuning(UnitInstance* unit) {
    if (!unit || !b2Body_IsValid(unit->bodyId)) {
        return;
    }
    // Base driving damping (terminal-speed cap). unit_set_move_target overrides this per
    // frame while coasting; this sets the resting value for the driving case.
    const UnitDefinition* def = unit->definition;
    b2Body_SetLinearDamping(unit->bodyId,
        unit_base_linear_damping(def ? def->maxSpeed : 0.0f,
                                 def ? def->acceleration : 0.0f));

    // Turn rate: re-derive the motor's max torque from the (possibly edited) turnSpeed.
    if (b2Joint_IsValid(unit->motorJoint)) {
        b2MotorJoint_SetMaxTorque(unit->motorJoint,
            unit_motor_max_torque(b2Body_GetInertiaTensor(unit->bodyId),
                                  def ? def->turnSpeed : 0.0f));
    }
}

void unit_set_collision_enabled(UnitInstance* unit, bool enabled) {
    if (!unit || !b2Body_IsValid(unit->bodyId)) {
        return;
    }
    // A unit has exactly one shape; fetch it (UnitInstance doesn't cache the shape id).
    b2ShapeId shape = b2_nullShapeId;
    if (b2Body_GetShapes(unit->bodyId, &shape, 1) < 1 || !b2Shape_IsValid(shape)) {
        return;
    }
    b2Filter f;
    if (enabled) {
        f.categoryBits = CATEGORY_UNIT;
        f.maskBits = MASK_UNIT;
        f.groupIndex = unit->collisionGroupId;  // negative -> never self-collide
    } else {
        f.categoryBits = 0;   // no category -> projectiles' mask can't match it
        f.maskBits = 0;       // collides with nothing
        f.groupIndex = unit->collisionGroupId;
    }
    b2Shape_SetFilter(shape, f);
}
