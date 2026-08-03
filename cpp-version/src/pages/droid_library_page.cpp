#include "pages/droid_library_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "units/unit_types.h"
#include "units/weapon.h"
#include "units/unit_instance.h"
#include "units/movement_tuning.h"   // TURRET_SLEW_RATE, DEFAULT_TURN_SPEED, facing_angle_to
#include "rendering/scene_renderer.h"
#include "units/unit_json.h"
#include "util/index_wrap.h"
#include "raylib.h"
#include "raygui.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
constexpr float LIB_SPIN_RATE = 0.9f;  // radians/second

// Shortest-way angle slew (radians), matching gameplay (game_update_player_turret).
float normalizeAngle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
float slewToward(float current, float target, float maxStep) {
    float diff = normalizeAngle(target - current);
    if (diff > maxStep) diff = maxStep;
    if (diff < -maxStep) diff = -maxStep;
    return normalizeAngle(current + diff);
}

int classNumOf(const std::string& id) {
    const char* prefix = "droid_class_";
    if (id.rfind(prefix, 0) == 0) return std::atoi(id.c_str() + std::strlen(prefix));
    return 0;
}

// Draw `text` word-wrapped to maxWidth pixels, breaking on spaces and honoring any
// explicit '\n' in the string (raylib's DrawText advances a line per newline). Returns
// the y just below the last line drawn.
int drawWrappedText(const std::string& text, int x, int y, int fontSize, int maxWidth, Color color) {
    const int lineStep = fontSize + 6;
    std::string line;
    auto flush = [&]() {
        if (!line.empty()) DrawText(line.c_str(), x, y, fontSize, color);
        y += lineStep;
        line.clear();
    };
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') { flush(); ++i; continue; }
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
        std::string word = text.substr(i, j - i);
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && MeasureText(candidate.c_str(), fontSize) > maxWidth) {
            flush();               // current word won't fit — wrap first
            line = word;
        } else {
            line = candidate;
        }
        i = j;
        if (i < text.size() && text[i] == ' ') ++i;  // consume one separator space
    }
    flush();
    return y;
}
}  // namespace

DroidLibraryPage::~DroidLibraryPage() {
    teardown();
}

void DroidLibraryPage::activate() {
    // Definition list from the already-preloaded game manager, sorted by class number.
    ids_ = game_->unitManager.getDefinitionIds();
    std::sort(ids_.begin(), ids_.end(),
              [](const std::string& a, const std::string& b) { return classNumOf(a) < classNumOf(b); });

    // Private zero-gravity world + manager for the display droid.
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {0.0f, 0.0f};
    world_ = b2CreateWorld(&wd);
    std::string modelsPath = game_->assetPath + "/models/";
    units_.init(world_, modelsPath.c_str());
    units_.setModelCache(&game_->modelCache);  // reuse the game's shared models

    // 3/4 orbit camera (matches the unit_test display pedestal). Distance is zoomable.
    camera_.target = {0.0f, 0.3f, 0.0f};
    camera_.up = {0.0f, 1.0f, 0.0f};
    camera_.fovy = 45.0f;
    camera_.projection = CAMERA_PERSPECTIVE;
    camDist_ = kCamDistDefault;
    applyCameraDistance();

    // Open on the type the player currently controls (falls back to the first entry).
    index_ = 0;
    for (int i = 0; i < (int)ids_.size(); ++i) {
        if (ids_[i] == game_->playerUnitId) { index_ = i; break; }
    }
    spin_ = 0.0f;
    rebuildDisplay();
}

void DroidLibraryPage::deactivate() {
    teardown();
}

void DroidLibraryPage::teardown() {
    if (display_) { units_.destroyInstance(display_); display_ = nullptr; }
    units_.destroy();                                  // destroy unit bodies while world valid
    if (!B2_IS_NULL(world_)) { b2DestroyWorld(world_); world_ = b2_nullWorldId; }
}

void DroidLibraryPage::rebuildDisplay() {
    if (display_) { units_.destroyInstance(display_); display_ = nullptr; }
    if (ids_.empty()) return;
    const UnitDefinition* def = game_->unitManager.getDefinition(ids_[index_]);
    if (!def) return;
    display_ = units_.createInstance(def, {0.0f, 0.0f}, 0.0f);
    if (display_) {
        units_.applyShaderToModels(sceneRendererGetShader(&game_->sceneRenderer));
    }
}

void DroidLibraryPage::applyCameraDistance() {
    constexpr float pitch = 45.0f * DEG2RAD, yaw = -45.0f * DEG2RAD;
    camera_.position = {camDist_ * std::cos(pitch) * std::sin(yaw),
                        camDist_ * std::sin(pitch),
                        camDist_ * std::cos(pitch) * std::cos(yaw)};
}

void DroidLibraryPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) { pages_->pop(); return; }
    // Toggle the debug editor with the same key as the gameplay AI-debug overlay (V);
    // gameplay input doesn't run while this page is on top, so mirror the toggle here.
    if (IsKeyPressed(KEY_V)) game_->showAIDebug = !game_->showAIDebug;

    // Facing test (SPACE): stop the auto-spin and aim the turret/head (and the body) at the
    // mouse to see the independent heading + different rate. Toggle off to resume spinning.
    if (IsKeyPressed(KEY_SPACE)) facingTest_ = !facingTest_;

    // Zoom the orbit camera: '=' (plus) closer, '-' farther. Held for continuous zoom.
    if (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_MINUS)) {
        if (IsKeyDown(KEY_EQUAL)) camDist_ -= kCamZoomStep;
        if (IsKeyDown(KEY_MINUS)) camDist_ += kCamZoomStep;
        camDist_ = std::clamp(camDist_, kCamDistMin, kCamDistMax);
        applyCameraDistance();
    }

    if (ids_.empty()) return;
    int n = (int)ids_.size();
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_W)) {
        index_ = wrapIndex(index_, +1, n);
        rebuildDisplay();
    } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_S)) {
        index_ = wrapIndex(index_, -1, n);
        rebuildDisplay();
    }
}

bool DroidLibraryPage::mouseTargetHeading(float* outAngle) const {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), camera_);
    if (std::fabs(ray.direction.y) < 1e-4f) return false;   // parallel to the ground
    float t = -ray.position.y / ray.direction.y;
    if (t <= 0.0f) return false;                            // ground is behind the camera
    float hx = ray.position.x + ray.direction.x * t;
    float hz = ray.position.z + ray.direction.z * t;
    // Droid sits at the origin; facing convention matches the body/section angles.
    *outAngle = facing_angle_to(hx, hz);
    return true;
}

void DroidLibraryPage::update(float dt) {
    if (saveMsgTimer_ > 0.0f) saveMsgTimer_ -= dt;
    if (!display_ || !b2Body_IsValid(display_->bodyId)) return;

    if (facingTest_) {
        // Aim the body (main section) and the turret/head at the mouse, each at its own rate,
        // so the different heading + different slew rate are both visible.
        float target;
        if (mouseTargetHeading(&target)) facingTarget_ = target;
        const UnitDefinition* def = display_->definition;
        float bodyRate = (def && def->turnSpeed > 0.0f) ? def->turnSpeed : DEFAULT_TURN_SPEED;
        float turretRate = (def && def->properties.turretTurnSpeed > 0.0f)
                               ? def->properties.turretTurnSpeed : TURRET_SLEW_RATE;
        float headRate = (def && def->properties.headTurnSpeed > 0.0f)
                             ? def->properties.headTurnSpeed : TURRET_SLEW_RATE;
        spin_ = slewToward(spin_, facingTarget_, bodyRate * dt);
        if (SectionInstance* t = unit_find_section_by_role(display_, SectionRole::Turret))
            t->facingAngle = slewToward(t->facingAngle, facingTarget_, turretRate * dt);
        if (SectionInstance* hd = unit_find_section_by_role(display_, SectionRole::Head))
            hd->facingAngle = slewToward(hd->facingAngle, facingTarget_, headRate * dt);
    } else {
        spin_ += dt * LIB_SPIN_RATE;  // idle auto-spin about the vertical axis
        // Turret/head are FollowFacing (absolute world angle), so pin them to the body angle
        // here — otherwise they'd stay fixed in world space while the body spins under them.
        if (SectionInstance* t = unit_find_section_by_role(display_, SectionRole::Turret))
            t->facingAngle = spin_;
        if (SectionInstance* hd = unit_find_section_by_role(display_, SectionRole::Head))
            hd->facingAngle = spin_;
    }

    // No physics step in this world, so set the body transform directly (as the unit_test does).
    b2Body_SetTransform(display_->bodyId, (b2Vec2){0.0f, 0.0f}, b2MakeRot(spin_));
    unit_set_move_target(display_, {0.0f, 0.0f}, spin_);
    units_.update(dt);  // sync render transforms (incl. FollowFacing sections) + animation
}

void DroidLibraryPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // Spinning droid (3D), lit by the shared scene shader.
    sceneRendererUpdateCamera(&game_->sceneRenderer, camera_.position);
    BeginMode3D(camera_);
    DrawGrid(10, 0.3f);
    units_.renderAll();
    // Debug: draw the collision radius as a ground-level ring (physics origin = world
    // origin here). Reads the definition's live collisionRadius, so the editor slider
    // updates it immediately. Drawn at the floor so it doesn't clip through the model;
    // if it's still hard to read we can disable the depth test.
    if (game_->showAIDebug && !ids_.empty()) {
        const UnitDefinition* d = game_->unitManager.getDefinition(ids_[index_]);
        if (d) {
            Vector3 c = {0.0f, 0.02f, 0.0f};
            // Draw the ring with depth testing OFF so it's always visible over the model.
            // raylib batches geometry, so flush the batch around the state toggle for it
            // to take effect on just these draws.
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            // Collision radius (green) — the physical footprint.
            DrawCircle3D(c, d->collisionRadius, (Vector3){1, 0, 0}, 90.0f, GREEN);
            DrawCircle3D(c, d->collisionRadius * 0.98f, (Vector3){1, 0, 0}, 90.0f, LIME);
            // Detection radius (orange) — range at which an armed droid turns hostile.
            if (d->proximityRadius > 0.0f) {
                DrawCircle3D(c, d->proximityRadius, (Vector3){1, 0, 0}, 90.0f, ORANGE);
            }
            // Fire offset marker (red) — where projectiles spawn: body centre + fireOffset
            // (x = lateral, y = forward, z = height), rotated by the TURRET facing if the unit
            // has one (that's where the player's shots leave), else the body angle. For a twin
            // weapon the second (lateral-mirrored) barrel is marked too. See docs/weapons.md.
            {
                const Vector3& fo = d->properties.fireOffset;
                float fireAngle = spin_;
                if (display_) {
                    if (SectionInstance* t = unit_find_section_by_role(display_, SectionRole::Turret))
                        fireAngle = t->facingAngle;
                }
                float cs = std::cos(fireAngle), sn = std::sin(fireAngle);
                auto fireMarker = [&](float lateral) {
                    Vector3 mp = {lateral * cs - fo.y * sn, fo.z, lateral * sn + fo.y * cs};
                    DrawSphere(mp, 0.05f, RED);
                    DrawLine3D({0.0f, fo.z, 0.0f}, mp, (Color){255, 80, 80, 160});
                };
                fireMarker(fo.x);
                if (d->properties.weapon >= 0 && getWeaponDefinition(d->properties.weapon).twin)
                    fireMarker(-fo.x);
            }
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
    }
    // Facing test: the mouse target marker + body/turret/head facing lines, so the independent
    // heading and different slew rate are visible. Depth test off (same pattern as the rings).
    if (facingTest_ && display_) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        const float h = kFacingLineHeight;
        Vector3 origin = {0.0f, h, 0.0f};
        auto lineTo = [&](float ang, float len, Color col) {   // forward = {-sin a, cos a}
            DrawLine3D(origin, {-std::sin(ang) * len, h, std::cos(ang) * len}, col);
        };
        // Target (orbiting widget) marker + a faint line to it.
        Vector3 marker = {-std::sin(facingTarget_) * kFacingMarkerRadius, h,
                          std::cos(facingTarget_) * kFacingMarkerRadius};
        DrawSphere(marker, 0.08f, YELLOW);
        lineTo(facingTarget_, kFacingMarkerRadius, (Color){255, 255, 0, 110});
        // Body forward (main section), then turret/head current facing if present.
        lineTo(spin_, kFacingLineLen, SKYBLUE);
        if (SectionInstance* t = unit_find_section_by_role(display_, SectionRole::Turret))
            lineTo(t->facingAngle, kFacingLineLen, ORANGE);
        if (SectionInstance* hd = unit_find_section_by_role(display_, SectionRole::Head))
            lineTo(hd->facingAngle, kFacingLineLen, LIME);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }
    EndMode3D();

    // Header.
    DrawText("DROID LIBRARY", 30, 24, 30, RAYWHITE);

    // Stats panel (right side).
    if (!ids_.empty()) {
        const UnitDefinition* def = game_->unitManager.getDefinition(ids_[index_]);
        int x = sw - 440, y = 80;
        auto line = [&](const char* s) { DrawText(s, x, y, 18, RAYWHITE); y += 24; };
        if (def) {
            line(TextFormat("%s  (%d/%d)", def->name.c_str(), index_ + 1, (int)ids_.size()));
            y += 6;
            line(TextFormat("Class %d   Type %d   Drive %d   Brain %d",
                            def->properties.classId, def->properties.droidType,
                            def->properties.driveType, def->properties.brainType));
            std::string weaponName = "None";
            if (def->properties.weapon >= 0) {
                WeaponDefinition w = getWeaponDefinition(def->properties.weapon);
                if (!w.name.empty()) weaponName = w.name;
            }
            line(TextFormat("Weapon: %s", weaponName.c_str()));
            line(TextFormat("Armour: %.0f   Energy: %d", def->properties.armour, def->properties.energy));
            line(TextFormat("Speed %.0f  Accel %.0f  Decel %.0f",
                            def->maxSpeed, def->acceleration, def->deceleration));
            line(TextFormat("Turn rate: %.1f rad/s%s", def->turnSpeed,
                            def->turnSpeed > 0.0f ? "" : " (default)"));
            if (def->coastDamping < 0.0f) line("Coast damp: off");
            else line(TextFormat("Coast damp: %.1f", def->coastDamping));
            line(TextFormat("Turret: %s   Omni: %s",
                            def->properties.hasTurret ? "yes" : "no",
                            def->properties.omnidirectional ? "yes" : "no"));
            y += 10;
            // Description: word-wrapped to the panel width, honoring any explicit '\n'.
            drawWrappedText(def->properties.description, x, y, 16, 410, LIGHTGRAY);
        }
    }

    // Debug editing: when the game's debug flag is on, expose the tunable numeric
    // fields as sliders bound to the in-memory definition, plus a Save-to-JSON button.
    // Movement/armour edits don't change the model's appearance, so we deliberately do
    // NOT rebuild the display droid here (that would reload models every dragged frame);
    // the edits take effect for future instances and persist on Save.
    if (game_->showAIDebug && !ids_.empty()) {
        UnitDefinition* mdef = game_->unitManager.getDefinitionMutable(ids_[index_]);
        if (mdef) {
            // Live-apply to any spawned unit of this type. Because those instances share
            // this exact definition object, accel/decel are already read live by the
            // motor each frame; only maxSpeed (baked into linear damping at spawn) needs
            // an explicit retune, done here so edits are in effect the moment we ESC back
            // to the game. The controlled player droid is the primary target.
            auto retune = [&](UnitInstance* u) {
                if (u && u->definition == mdef) unit_apply_movement_tuning(u);
            };
            retune(game_->playerUnit);
            for (UnitInstance* e : game_->enemyUnits) retune(e);
            // Live-apply the (possibly edited) detection radius to active AI components of
            // this type — their detectionRadius was cached from the definition at spawn.
            for (auto& c : game_->aiManager.components()) {
                if (c.unit && c.unit->definition == mdef) c.detectionRadius = mdef->proximityRadius;
            }

            int ex = 30, ey = 300, ew = 300;
            DrawText("DEBUG EDIT", ex, ey - 28, 18, YELLOW);
            auto slider = [&](const char* label, float* v, float lo, float hi) {
                GuiSlider((Rectangle){(float)ex + 90, (float)ey, (float)ew, 20}, label,
                          TextFormat("%.0f", *v), v, lo, hi);
                ey += 30;
            };
            slider("maxSpeed", &mdef->maxSpeed, 0.0f, 500.0f);
            slider("accel", &mdef->acceleration, 0.0f, 1500.0f);
            slider("decel", &mdef->deceleration, 0.0f, 1500.0f);
            slider("turn (rad/s)", &mdef->turnSpeed, 0.0f, 15.0f);      // body turn rate (0 = default)
            slider("turret rate", &mdef->properties.turretTurnSpeed, 0.0f, 15.0f);  // 0 = TURRET_SLEW_RATE
            slider("head rate", &mdef->properties.headTurnSpeed, 0.0f, 15.0f);      // 0 = TURRET_SLEW_RATE
            slider("coastDamp", &mdef->coastDamping, -1.0f, 8.0f);  // <0 = off (crisp stop)
            slider("armour", &mdef->properties.armour, 0.0f, 1000.0f);
            // Collision radius: fine format, capped at the tile-fit limit (0.425) so the
            // saved value isn't re-clamped on reload. Drives the green ground ring.
            GuiSlider((Rectangle){(float)ex + 90, (float)ey, (float)ew, 20}, "collRadius",
                      TextFormat("%.3f", mdef->collisionRadius),
                      &mdef->collisionRadius, 0.05f, 0.425f);
            ey += 30;
            // Detection radius (proximityRadius): range at which an armed droid goes
            // hostile. Drives the orange ring; live-applied to active AI above.
            GuiSlider((Rectangle){(float)ex + 90, (float)ey, (float)ew, 20}, "detectRadius",
                      TextFormat("%.2f", mdef->proximityRadius),
                      &mdef->proximityRadius, 0.0f, 12.0f);
            ey += 30;

            std::string path = game_->assetPath + "/units/" + ids_[index_] + ".json";
            if (GuiButton((Rectangle){(float)ex, (float)ey + 8, 120, 32}, "Save to JSON")) {
                bool ok = saveUnitDefinitionToFile(path, *mdef);
                saveMsg_ = ok ? "Saved " + ids_[index_] + ".json" : "Save FAILED";
                saveMsgTimer_ = 2.5f;
            }
            // Load re-reads the file (for rarely-edited properties changed by hand). Swaps in a
            // fresh definition — retiring the old one so live gameplay instances stay valid — then
            // rebuilds the display droid. NOTE: mdef is stale after this (points at the retired
            // object); it isn't used again this frame.
            if (GuiButton((Rectangle){(float)ex + 130, (float)ey + 8, 120, 32}, "Load from JSON")) {
                bool ok = game_->unitManager.reloadDefinition(ids_[index_], path) != nullptr;
                if (ok) rebuildDisplay();
                saveMsg_ = ok ? "Loaded " + ids_[index_] + ".json" : "Load FAILED";
                saveMsgTimer_ = 2.5f;
            }
            if (saveMsgTimer_ > 0.0f) {
                DrawText(saveMsg_.c_str(), ex + 262, ey + 16, 16,
                         (saveMsg_.rfind("Save FAILED", 0) == 0 ||
                          saveMsg_.rfind("Load FAILED", 0) == 0) ? RED : GREEN);
            }
        }
    }

    const char* hint = facingTest_
        ? "FACING TEST: move mouse to aim   SPACE: resume spin   -/=: zoom   V: debug edit   ESC: back"
        : (game_->showAIDebug
               ? "W/S or UP/DOWN: browse   SPACE: facing test   -/=: zoom   V: debug edit (on)   ESC: back"
               : "W/S or UP/DOWN: browse   SPACE: facing test   -/=: zoom   V: debug edit   ESC: back");
    DrawText(hint, 30, sh - 30, 16, GRAY);
    EndDrawing();
}
