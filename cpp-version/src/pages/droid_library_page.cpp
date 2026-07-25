#include "pages/droid_library_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "units/unit_types.h"
#include "units/weapon.h"
#include "units/unit_instance.h"
#include "rendering/scene_renderer.h"
#include "units/unit_json.h"
#include "util/index_wrap.h"
#include "raylib.h"
#include "raygui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
constexpr float LIB_SPIN_RATE = 0.9f;  // radians/second

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

    // 3/4 orbit camera (matches the unit_test display pedestal).
    float dist = 3.0f, pitch = 45.0f * DEG2RAD, yaw = -45.0f * DEG2RAD;
    camera_.position = {dist * std::cos(pitch) * std::sin(yaw),
                        dist * std::sin(pitch),
                        dist * std::cos(pitch) * std::cos(yaw)};
    camera_.target = {0.0f, 0.3f, 0.0f};
    camera_.up = {0.0f, 1.0f, 0.0f};
    camera_.fovy = 45.0f;
    camera_.projection = CAMERA_PERSPECTIVE;

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

void DroidLibraryPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) { pages_->pop(); return; }
    // Toggle the debug editor with the same key as the gameplay AI-debug overlay (V);
    // gameplay input doesn't run while this page is on top, so mirror the toggle here.
    if (IsKeyPressed(KEY_V)) game_->showAIDebug = !game_->showAIDebug;
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

void DroidLibraryPage::update(float dt) {
    if (saveMsgTimer_ > 0.0f) saveMsgTimer_ -= dt;
    spin_ += dt * LIB_SPIN_RATE;
    if (display_ && b2Body_IsValid(display_->bodyId)) {
        // Spin about the vertical axis. No physics step in this world, so set the
        // transform directly and match the motor-joint target (as the unit_test does).
        b2Body_SetTransform(display_->bodyId, (b2Vec2){0.0f, 0.0f}, b2MakeRot(spin_));
        unit_set_move_target(display_, {0.0f, 0.0f}, spin_);
        units_.update(dt);  // sync render transform + advance model animation
    }
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
            slider("turn (rad/s)", &mdef->turnSpeed, 0.0f, 15.0f);
            slider("coastDamp", &mdef->coastDamping, -1.0f, 8.0f);  // <0 = off (crisp stop)
            slider("armour", &mdef->properties.armour, 0.0f, 1000.0f);

            if (GuiButton((Rectangle){(float)ex, (float)ey + 8, 160, 32}, "Save to JSON")) {
                std::string path = game_->assetPath + "/units/" + ids_[index_] + ".json";
                bool ok = saveUnitDefinitionToFile(path, *mdef);
                saveMsg_ = ok ? "Saved " + ids_[index_] + ".json" : "Save FAILED";
                saveMsgTimer_ = 2.5f;
            }
            if (saveMsgTimer_ > 0.0f) {
                DrawText(saveMsg_.c_str(), ex + 172, ey + 16, 16,
                         saveMsg_.rfind("Save FAILED", 0) == 0 ? RED : GREEN);
            }
        }
    }

    DrawText(game_->showAIDebug ? "W/S or UP/DOWN: browse   V: debug edit (on)   ESC: back"
                                : "W/S or UP/DOWN: browse   V: debug edit   ESC: back",
             30, sh - 30, 16, GRAY);
    EndDrawing();
}
