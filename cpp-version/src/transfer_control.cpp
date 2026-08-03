#include "transfer_control.h"
#include "game.h"
#include "units/unit_instance.h"
#include "units/unit_types.h"
#include "units/movement_tuning.h"
#include "rendering/scene_renderer.h"
#include "raymath.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
namespace {

UnitInstance* controlledUnit(Game* g) {
    return g->transfer.captured ? g->transfer.captured : g->playerUnit;
}

bool unitAlive(const UnitInstance* u) {
    return u && u->active && b2Body_IsValid(u->bodyId) && u->combatState.alive;
}

Vector2 bodyPos(const UnitInstance* u) {
    b2Vec2 p = b2Body_GetPosition(u->bodyId);
    return {p.x, p.y};
}

float unitRadius(const UnitInstance* u) {
    return (u && u->definition) ? u->definition->collisionRadius : 0.3f;
}

// Drive a unit from movement input + desired facing, using the same bounded-carrot
// scheme the free player and AI use (see movement_tuning.h / unit_set_move_target).
void driveUnit(UnitInstance* u, Vector2 movement, float desiredRot) {
    if (!u || !b2Body_IsValid(u->bodyId)) return;
    b2Vec2 bp = b2Body_GetPosition(u->bodyId);
    Vector2 target = {bp.x, bp.y};  // default: hold position
    float mag = sqrtf(movement.x * movement.x + movement.y * movement.y);
    if (mag > 0.001f) {
        target.x = bp.x + (movement.x / mag) * UNIT_MOVE_LOOKAHEAD;
        target.y = bp.y + (movement.y / mag) * UNIT_MOVE_LOOKAHEAD;
    }
    unit_set_move_target(u, target, desiredRot);
}

// Hard-teleport the device to `pos` facing the player's aim (used for the fly-over and
// the detach snap; the device motor is zeroed while overlaid so it won't fight this).
void teleportDevice(Game* g, Vector2 pos) {
    UnitInstance* dev = g->playerUnit;
    if (!dev || !b2Body_IsValid(dev->bodyId)) return;
    b2Body_SetTransform(dev->bodyId, (b2Vec2){pos.x, pos.y}, b2MakeRot(g->playerDesiredRotation));
    b2Body_SetLinearVelocity(dev->bodyId, (b2Vec2){0, 0});
    b2Body_SetAngularVelocity(dev->bodyId, 0.0f);
}

void destroyWeld(Game* g) {
    if (b2Joint_IsValid(g->transfer.weldJoint)) b2DestroyJoint(g->transfer.weldJoint);
    g->transfer.weldJoint = b2_nullJointId;
}

// Enter "overlay" mode: the device is invulnerable (no collision), its motor is neutered
// (position comes from the weld or teleport), and it renders lifted onto the unit.
void deviceEnterOverlay(Game* g) {
    UnitInstance* dev = g->playerUnit;
    if (!dev) return;
    unit_set_collision_enabled(dev, false);
    if (b2Joint_IsValid(dev->motorJoint)) {
        b2MotorJoint_SetMaxForce(dev->motorJoint, 0.0f);
        b2MotorJoint_SetMaxTorque(dev->motorJoint, 0.0f);
    }
    dev->renderHeightOffset = DEVICE_ATTACH_HEIGHT;
}

// Leave overlay mode back to a free, directly-controlled device.
void deviceExitOverlay(Game* g) {
    destroyWeld(g);
    UnitInstance* dev = g->playerUnit;
    if (!dev) return;
    unit_set_collision_enabled(dev, true);
    unit_apply_movement_tuning(dev);  // restore motor torque + damping (maxForce set by drive)
    dev->renderHeightOffset = 0.0f;
}

// Rigidly weld the device onto the captured unit so it tracks with no frame lag.
void createWeld(Game* g) {
    UnitInstance* dev = g->playerUnit;
    UnitInstance* cap = g->transfer.captured;
    if (!dev || !cap || !b2Body_IsValid(dev->bodyId) || !b2Body_IsValid(cap->bodyId)) return;
    destroyWeld(g);
    // Snap the device exactly onto the captured unit before welding (avoids a jolt).
    b2Vec2 cp = b2Body_GetPosition(cap->bodyId);
    b2Body_SetTransform(dev->bodyId, cp, b2Body_GetRotation(cap->bodyId));
    b2WeldJointDef def = b2DefaultWeldJointDef();
    def.bodyIdA = cap->bodyId;
    def.bodyIdB = dev->bodyId;
    def.localAnchorA = (b2Vec2){0, 0};
    def.localAnchorB = (b2Vec2){0, 0};
    def.referenceAngle = 0.0f;  // same facing as the captured unit (which faces the aim)
    def.collideConnected = false;
    g->transfer.weldJoint = b2CreateWeldJoint(g->physics.world_id, &def);
}

// Remove a unit: release any weld, detach its AI component, drop it from the enemy list,
// destroy it. `explode` spawns the death explosion (gameplay death); pass false for a silent
// removal (e.g. carrying a captured droid across a level change — it's re-created on arrival).
void destroyUnit(Game* g, UnitInstance* u, bool explode = true) {
    if (!u) return;
    // A destroyed captured/created unit leaves an explosion + sparks at its position (owner =
    // its own group, so it doesn't self-damage; the overlay device is non-colliding hence
    // unaffected). See docs/effects.md. Only gameplay death routes here — teardown uses other
    // paths.
    if (explode && b2Body_IsValid(u->bodyId)) {
        game_spawn_explosion(g, bodyPos(u), u->collisionGroupId);
    }
    if (u == g->transfer.captured) destroyWeld(g);
    g->aiManager.forgetUnit(u);
    auto drop = [](std::vector<UnitInstance*>& v, UnitInstance* x) {
        v.erase(std::remove(v.begin(), v.end(), x), v.end());
    };
    drop(g->enemyUnits, u);
    if (u->levelIndex >= 0 && u->levelIndex < (int)g->levelRuntime.size()) {
        drop(g->levelRuntime[u->levelIndex].units, u);
    }
    g->unitManager.destroyInstance(u);
}

// Begin a capture: release the old captured unit, freeze the target's AI, put the device
// into overlay mode, and start the fly-over from the device's current position.
void beginTransfer(Game* g, UnitInstance* target) {
    TransferState& st = g->transfer;
    st.transferFrom = bodyPos(g->playerUnit);
    if (st.captured) { destroyUnit(g, st.captured); st.captured = nullptr; }
    st.transferTarget = target;
    st.transferTimer = 0.0f;
    st.mode = ControlMode::Transferring;
    g->aiManager.setControlled(target, true);  // hold station during the flight
    deviceEnterOverlay(g);                      // invulnerable + lifted for the fly-over
}

int classNumOf(const std::string& id) {
    const char* prefix = "droid_class_";
    if (id.rfind(prefix, 0) == 0) return std::atoi(id.c_str() + std::strlen(prefix));
    return -1;
}

// Create a fresh unit of `defId` at (pos, angle), track it, and pilot it (overlay +
// weld). Shared by the debug cycle and cross-level carry. Returns false on failure.
bool enterControllingNewUnit(Game* g, const std::string& defId, Vector2 pos, float angle) {
    // Create the captured unit in the ACTIVE level's world so it shares physics with the
    // player device (which lives in that world).
    const int L = g->currentLevel;
    bool validL = (L >= 0 && L < (int)g->levelRuntime.size());
    b2WorldId world = validL ? g->levelRuntime[L].world : b2_nullWorldId;
    b2BodyId origin = validL ? g->levelRuntime[L].origin : b2_nullBodyId;
    UnitInstance* u = g->unitManager.createInstance(defId, pos, angle, world, origin);
    if (!u) {
        g->transfer.mode = ControlMode::Free;
        deviceExitOverlay(g);
        return false;
    }
    u->levelIndex = L;
    g->unitManager.applyShaderToModels(sceneRendererGetShader(&g->sceneRenderer));
    g->enemyUnits.push_back(u);
    if (validL) g->levelRuntime[L].units.push_back(u);  // roster member
    g->transfer.captured = u;
    g->transfer.mode = ControlMode::Controlling;
    deviceEnterOverlay(g);
    createWeld(g);
    return true;
}

}  // namespace

//------------------------------------------------------------------------------
// Public
//------------------------------------------------------------------------------

void transfer_update(Game* game, float dt) {
    TransferState& st = game->transfer;
    UnitInstance* dev = game->playerUnit;
    if (!dev || !b2Body_IsValid(dev->bodyId)) return;

    // --- Transferring: play the invulnerable fly-over; input is locked. ---
    if (st.mode == ControlMode::Transferring) {
        if (!unitAlive(st.transferTarget)) {
            // Target died mid-flight — drop back to Free where the device is.
            st.transferTarget = nullptr;
            st.mode = ControlMode::Free;
            deviceExitOverlay(game);
            return;
        }
        st.transferTimer += dt;
        float f = transfer_progress(st.transferTimer, TRANSFER_DURATION);
        Vector2 tp = bodyPos(st.transferTarget);
        teleportDevice(game, {Lerp(st.transferFrom.x, tp.x, f), Lerp(st.transferFrom.y, tp.y, f)});
        if (f >= 1.0f) {
            st.captured = st.transferTarget;
            st.transferTarget = nullptr;
            st.transferTimer = 0.0f;
            st.mode = ControlMode::Controlling;
            game->aiManager.setControlled(st.captured, true);
            createWeld(game);  // link device to captured — from here the weld tracks it
            game_award_points(game, st.captured);  // capturing a droid scores like a kill
        }
        return;
    }

    // --- Controlling: detach if the captured unit was destroyed. ---
    if (st.mode == ControlMode::Controlling && !unitAlive(st.captured)) {
        Vector2 last = (st.captured && b2Body_IsValid(st.captured->bodyId))
                           ? bodyPos(st.captured)
                           : bodyPos(dev);
        destroyUnit(game, st.captured);  // releases the weld (gameplay death → explosion)
        st.captured = nullptr;
        st.mode = ControlMode::Free;
        deviceExitOverlay(game);
        teleportDevice(game, last);
        // Gameplay rule: losing your captured droid restores the device to full health.
        dev->combatState.currentHealth = dev->combatState.maxHealth;
        dev->combatState.alive = true;
    }

    UnitInstance* ctl = controlledUnit(game);
    if (!ctl || !b2Body_IsValid(ctl->bodyId)) return;

    // --- Ram-to-capture while transfer mode is armed. ---
    bool transferMode = game->input.transferMode && !game->testConfig.enabled;
    if (transferMode) {
        Vector2 ap = bodyPos(ctl);
        float ar = unitRadius(ctl);
        for (UnitInstance* e : game->enemyUnits) {
            if (!unitAlive(e) || e == st.captured) continue;
            if (Vector2Distance(ap, bodyPos(e)) <= ar + unitRadius(e) + CAPTURE_MARGIN) {
                beginTransfer(game, e);
                return;
            }
        }
    }

    // Drive the controlled unit. When Controlling, the device follows via the weld (no
    // per-frame device move needed — that's what removes the frame lag).
    driveUnit(ctl, game->input.movement, game->playerDesiredRotation);
}

void transfer_reset(Game* game) {
    TransferState& st = game->transfer;
    if (st.mode != ControlMode::Free) {
        deviceExitOverlay(game);
    }
    // Called on a level change while carrying a captured droid: remove that droid from the
    // level being left (silently — no explosion/score; a fresh copy is re-created on arrival by
    // transfer_recapture_class). Without this the captured droid is left behind and reappears
    // on re-entry — a duplicate.
    if (st.captured) {
        destroyUnit(game, st.captured, /*explode=*/false);
    }
    st.mode = ControlMode::Free;
    st.captured = nullptr;
    st.transferTarget = nullptr;
    st.transferTimer = 0.0f;
}

void transfer_debug_cycle(Game* game, int direction) {
    if (!game->playerUnit || !b2Body_IsValid(game->playerUnit->bodyId)) return;
    if (game->transfer.mode == ControlMode::Transferring) return;  // don't interrupt a fly-over

    // Loaded definitions, class-numbered, excluding class 0 (the device itself).
    std::vector<std::string> ids;
    for (const std::string& id : game->unitManager.getDefinitionIds()) {
        if (classNumOf(id) > 0) ids.push_back(id);
    }
    if (ids.empty()) return;
    std::sort(ids.begin(), ids.end(),
              [](const std::string& a, const std::string& b) { return classNumOf(a) < classNumOf(b); });

    int count = static_cast<int>(ids.size());
    int cur = -1;
    if (game->transfer.captured && game->transfer.captured->definition) {
        const std::string& cid = game->transfer.captured->definition->id;
        for (int i = 0; i < count; ++i) if (ids[i] == cid) { cur = i; break; }
    }
    int next = (cur < 0) ? (direction >= 0 ? 0 : count - 1)
                         : ((cur + direction) % count + count) % count;

    Vector2 pos = bodyPos(controlledUnit(game));
    float angle = game->playerDesiredRotation;

    if (game->transfer.captured) { destroyUnit(game, game->transfer.captured); game->transfer.captured = nullptr; }

    if (enterControllingNewUnit(game, ids[next], pos, angle)) {
        TraceLog(LOG_INFO, "Transfer debug: now piloting %s", ids[next].c_str());
    }
}

int transfer_captured_class(Game* game) {
    const TransferState& st = game->transfer;
    if (st.mode == ControlMode::Controlling && st.captured && st.captured->definition) {
        return classNumOf(st.captured->definition->id);
    }
    return -1;
}

float transfer_captured_health(Game* game) {
    const TransferState& st = game->transfer;
    if (st.mode == ControlMode::Controlling && st.captured) {
        return st.captured->combatState.currentHealth;
    }
    return -1.0f;
}

void transfer_recapture_class(Game* game, int classId, float health) {
    if (classId < 0 || !game->playerUnit || !b2Body_IsValid(game->playerUnit->bodyId)) return;
    // Respawn the carried class at the device's (post-migration) position and resume
    // piloting it, restoring its carried health so a damaged droid stays damaged.
    Vector2 pos = bodyPos(game->playerUnit);
    if (enterControllingNewUnit(game, "droid_class_" + std::to_string(classId), pos,
                                game->playerDesiredRotation) &&
        health >= 0.0f && game->transfer.captured) {
        UnitInstance* u = game->transfer.captured;
        float maxH = u->combatState.maxHealth;
        u->combatState.currentHealth = (maxH > 0.0f && health > maxH) ? maxH : health;
        u->combatState.alive = (u->combatState.currentHealth > 0.0f);
    }
}
