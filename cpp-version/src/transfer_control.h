#ifndef TRANSFER_CONTROL_H
#define TRANSFER_CONTROL_H

#include "raylib.h"
#include "box2d/box2d.h"

struct Game;
struct UnitInstance;

//------------------------------------------------------------------------------
// Transfer mechanic: the player is a fixed type-0 "influence device" that pilots
// AI units. This module owns the control state machine — driving the controlled
// unit, overlaying the device, invulnerability, ram-to-capture, the fly-over
// animation, and detach-on-death. See docs/transfer.md.
//------------------------------------------------------------------------------

enum class ControlMode {
    Free,          // device driven directly, collides normally, vulnerable
    Controlling,   // piloting `captured`; device overlaid, non-colliding, invulnerable
    Transferring   // 1.5s fly-over to `transferTarget`; input locked, invulnerable
};

inline constexpr float TRANSFER_DURATION = 1.0f;   // seconds for the capture fly-over
inline constexpr float CAPTURE_MARGIN = 0.1f;      // extra reach beyond summed radii for ram capture
inline constexpr float DEVICE_ATTACH_HEIGHT = 1.2f; // render Y lift so the device sits on top

struct TransferState {
    ControlMode mode = ControlMode::Free;
    UnitInstance* captured = nullptr;        // piloted unit (Controlling)
    UnitInstance* transferTarget = nullptr;  // incoming unit (Transferring)
    Vector2 transferFrom = {0, 0};           // device start position for the fly-over
    float transferTimer = 0.0f;              // 0..TRANSFER_DURATION
    // Rigid weld linking the device to the captured unit while Controlling, so it tracks
    // without a frame's lag; destroyed when there is no captured unit.
    b2JointId weldJoint = b2_nullJointId;
};

// Fraction of the capture animation elapsed (0..1), clamped. Pure — unit-tested.
inline float transfer_progress(float timer, float duration) {
    if (duration <= 0.0f) return 1.0f;
    float f = timer / duration;
    if (f < 0.0f) return 0.0f;
    if (f > 1.0f) return 1.0f;
    return f;
}

// Per-frame control update (call each gameplay frame, not while paused): routes input
// to the controlled unit, manages the device overlay/invulnerability, ram-capture in
// transfer mode, the fly-over animation, and detach when the captured unit dies.
void transfer_update(Game* game, float dt);

// Reset to Free and re-enable the device's collision. Call before despawning units
// (e.g. on level switch) so the device isn't left pointing at a destroyed unit.
void transfer_reset(Game* game);

// Debug (F1/F2): create/cycle the captured unit's type by `direction` (+1/-1),
// skipping class 0; the device itself stays type 0.
void transfer_debug_cycle(Game* game, int direction);

// Cross-level carry: `transfer_captured_class` returns the piloted unit's class number
// (or -1 if not piloting), captured BEFORE the old level is despawned;
// `transfer_recapture_class` respawns that class at the device's position on the new
// level and resumes piloting it. Call the latter after the player has been placed.
int transfer_captured_class(Game* game);
float transfer_captured_health(Game* game);  // current health of the piloted unit, or -1
void transfer_recapture_class(Game* game, int classId, float health = -1.0f);

#endif // TRANSFER_CONTROL_H
