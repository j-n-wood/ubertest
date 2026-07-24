#ifndef MOVEMENT_TUNING_H
#define MOVEMENT_TUNING_H

#include <cmath>

//------------------------------------------------------------------------------
// Shared movement / control tuning.
//
// Design invariant: AI-driven units and the player-controlled unit are simulated
// IDENTICALLY. The player gains remote control of a unit by replacing the AI's
// target with player-generated input; the physical drive is the same. Therefore
// there is exactly ONE parameter set here, used by every movable unit regardless
// of who is driving it. Do not add a player-specific override — tune globally.
//
// These are starting values; expect to tune them in-game (see the plan's
// verification section). Terminal cruise speed ≈ UNIT_MOTOR_MAX_FORCE saturated
// against UNIT_LINEAR_DAMPING, eased near the target by the carrot lookahead.
//------------------------------------------------------------------------------

// b2MotorJoint drive: each movable unit is anchored to the static world-origin
// body by a motor joint whose linearOffset == desired world position and
// angularOffset == desired world facing. maxForce/maxTorque bound the authority
// so collisions negotiate with the drive inside the solver instead of overpowering
// it open-loop, and correctionFactor sets how aggressively per-step position error
// is corrected (post-collision recovery rate).
inline constexpr float UNIT_MOTOR_MAX_FORCE         = 40.0f;  // newtons
inline constexpr float UNIT_MOTOR_MAX_TORQUE        = 120.0f; // newton-metres
inline constexpr float UNIT_MOTOR_CORRECTION_FACTOR = 0.2f;   // [0,1]

// Body damping: secondary speed cap and general stability. These are the FALLBACK
// values used when a unit definition has no per-type movement data (maxSpeed == 0).
// When per-type data is present, linear damping is derived per unit as
// acceleration / maxSpeed so terminal velocity equals maxSpeed (see unit_manager).
inline constexpr float UNIT_LINEAR_DAMPING  = 4.0f;
inline constexpr float UNIT_ANGULAR_DAMPING = 8.0f;

// Per-type facing turn rate. The motor's angularOffset is the desired facing; left
// unbounded (the old fixed UNIT_MOTOR_MAX_TORQUE against a unit's tiny rotational
// inertia gives thousands of rad/s², so facing snapped instantly to the mouse/AI
// angle). Instead the motor torque is derived per unit from a terminal turn rate:
// with angular damping d, terminal angular velocity = maxTorque / (I * d), so choosing
// maxTorque = I * turnSpeed * d makes the unit turn at up to `turnSpeed` rad/s and
// bounds per-tick rotation. UnitDefinition::turnSpeed overrides this default when > 0.
inline constexpr float DEFAULT_TURN_SPEED = 6.0f;  // rad/s (~1 revolution/second)

// Motor max-torque for a unit with the given rotational inertia and per-type turn rate
// (turnSpeed <= 0 falls back to DEFAULT_TURN_SPEED). Keep in sync with the terminal-rate
// reasoning above. Used at motor creation and by unit_apply_movement_tuning for retune.
inline float unit_motor_max_torque(float rotationalInertia, float turnSpeed) {
    float rate = (turnSpeed > 0.0f) ? turnSpeed : DEFAULT_TURN_SPEED;
    return rotationalInertia * rate * UNIT_ANGULAR_DAMPING;
}

// Converts the original droidclasses.txt speed/acceleration/deceleration numbers
// into world units (u/s and u/s²). Chosen to match the ÷20 convention used for
// projectile speeds; e.g. class 0 speed 200 -> 10 u/s. Tune globally.
inline constexpr float MOVEMENT_UNIT_SCALE = 0.05f;

// Force-authority multiplier that DECOUPLES motor force from top speed. The raw
// per-type acceleration (once scaled) yields only a few newtons of motor force —
// too weak to overcome contact friction or another body, so units jammed and
// froze on any collision. Multiplying force by this factor gives units real
// authority to push through/recover from contacts. Terminal speed is unaffected:
// linear damping is scaled by the same factor (terminal = force / (mass*damping)),
// so top speed stays maxSpeed*MOVEMENT_UNIT_SCALE while the motor pushes hard.
inline constexpr float UNIT_MOTOR_AUTHORITY = 10.0f;

// Distance (world units) below which a unit's move target is treated as "holding" its
// current position — i.e. drive input was released or the unit parked. The coast
// behaviour engages only in this case, not while actively driving to a target.
inline constexpr float UNIT_HOLD_THRESHOLD = 0.05f;

// Base driving linear damping: sized so terminal velocity == maxSpeed*MOVEMENT_UNIT_SCALE
// (damping = acceleration*UNIT_MOTOR_AUTHORITY/maxSpeed). Units without per-type movement
// data (maxSpeed == 0) fall back to the global constant. Shared by createInstance,
// unit_apply_movement_tuning, and the driving branch of unit_set_move_target.
inline float unit_base_linear_damping(float maxSpeed, float acceleration) {
    if (maxSpeed > 0.0f && acceleration > 0.0f) {
        return acceleration * UNIT_MOTOR_AUTHORITY / maxSpeed;
    }
    return UNIT_LINEAR_DAMPING;
}

// Coast model: when a unit is holding (drive released, dist < UNIT_HOLD_THRESHOLD) and its
// per-type coastDamping >= 0, the body's linear damping is set to coastDamping and the
// motor's drive force is dropped to zero, so the unit coasts to a stop under that damping
// alone (lower = longer float/drift). coastDamping < 0 disables coasting: holding uses the
// normal deceleration braking force + base driving damping (crisp stop, the default).

// Facing angle (a physics body angle θ) that makes a unit visually face the world
// direction (dx, dz). The renderer draws a unit facing (-sinθ, cosθ) (model +Z
// forward, drawn at -worldRotation about +Y — see unit_manager renderSection), so
// to face (dx, dz) we need θ = atan2(-dx, dz). Physics coords: x = world X, y =
// world Z, so callers pass (dir.x, dir.y). Used identically by the player, AI body
// facing, turret head tracking, and fire-alignment so all conventions agree.
inline float facing_angle_to(float dx, float dz) { return std::atan2(-dx, dz); }

// Locomotion "carrot": the linear target is placed at most this far ahead of the
// unit along its heading. Bounding the position error bounds the applied force
// (natural speed cap) and gives smooth arrival easing as the final target nears.
inline constexpr float UNIT_MOVE_LOOKAHEAD = 0.75f;

// Turret / head facing slew rate (radians per second). Facing is a render-only
// scalar (no physics body); it rotates the shortest way toward its target angle.
inline constexpr float TURRET_SLEW_RATE = 8.0f;

// Contact friction for unit shapes. Kept ~0 so units slide along walls (and past
// each other) instead of sticking — the physics-native way to let a unit escape
// when its path to a waypoint grazes an obstacle. Motor force + linear damping
// govern speed, so friction has no role in movement feel here.
inline constexpr float UNIT_CONTACT_FRICTION = 0.0f;

#endif // MOVEMENT_TUNING_H
