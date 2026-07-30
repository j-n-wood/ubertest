#include "pages/weapon_editor_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "units/weapon.h"
#include "util/index_wrap.h"
#include "raylib.h"
#include "raygui.h"
#include <cstdio>

namespace {
// The tunable numeric fields, in display order, each bound to a WeaponDefinition member
// via pointer-to-member so the slider and value box write straight into the table entry.
struct FieldSpec {
    const char* label;
    float WeaponDefinition::* mem;
    float lo, hi;
};
constexpr FieldSpec kFields[] = {
    {"damage",       &WeaponDefinition::damage,       0.0f, 100.0f},
    {"speed",        &WeaponDefinition::speed,        0.0f,  40.0f},
    {"fireRate",     &WeaponDefinition::fireRate,     0.0f,   3.0f},
    {"maxRange",     &WeaponDefinition::maxRange,     0.0f,  40.0f},
    {"optimumRange", &WeaponDefinition::optimumRange, 0.0f,  40.0f},
};
static_assert(sizeof(kFields) / sizeof(kFields[0]) == WeaponEditorPage::kFieldCount,
              "kFields must match kFieldCount");

const char* weaponTypeName(WeaponType t) {
    switch (t) {
        case WeaponType::Beam:    return "beam";
        case WeaponType::Instant: return "instant";
        case WeaponType::Area:    return "area";
        default:                  return "projectile";
    }
}
const char* damageTypeName(DamageType t) {
    switch (t) {
        case DamageType::Flame:      return "flame";
        case DamageType::Cutter:     return "cutter";
        case DamageType::Laser:      return "laser";
        case DamageType::Projectile: return "projectile";
        case DamageType::Disruptor:  return "disruptor";
        case DamageType::Impact:     return "impact";
        default:                     return "plasma";
    }
}
}  // namespace

void WeaponEditorPage::activate() {
    index_ = 0;
    shownWeaponId_ = -1;   // force a buffer refresh on the first render
    for (int i = 0; i < kFieldCount; ++i) editMode_[i] = false;
    saveMsgTimer_ = 0.0f;
}

void WeaponEditorPage::refreshBuffers() {
    const WeaponDefinition* w = getWeaponByIndex(index_);
    if (!w) return;
    for (int i = 0; i < kFieldCount; ++i) {
        std::snprintf(textBuf_[i], sizeof(textBuf_[i]), "%.3f", w->*kFields[i].mem);
        editMode_[i] = false;
    }
    shownWeaponId_ = w->id;
}

void WeaponEditorPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_F4)) { pages_->pop(); return; }
    int n = weaponCount();
    if (n <= 0) return;
    // Browse with arrows only (letter keys would collide with numeric text entry).
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_RIGHT)) {
        index_ = wrapIndex(index_, +1, n);
    } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_LEFT)) {
        index_ = wrapIndex(index_, -1, n);
    }
}

void WeaponEditorPage::update(float dt) {
    if (saveMsgTimer_ > 0.0f) saveMsgTimer_ -= dt;
    syncLiveWeaponStates();
}

void WeaponEditorPage::syncLiveWeaponStates() {
    // playerWeapon and each AI component cache a WeaponDefinition *copy* keyed by id, only
    // re-fetched when the id changes. Re-fetch here (preserving cooldown) so edits to the
    // active weapon's stats are live the instant we return to gameplay.
    int pid = game_->playerWeapon.definition.id;
    if (pid >= 0) game_->playerWeapon.definition = getWeaponDefinition(pid);
    for (auto& c : game_->aiManager.components()) {
        int id = c.weaponState.definition.id;
        if (id >= 0) c.weaponState.definition = getWeaponDefinition(id);
    }
}

void WeaponEditorPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawText("WEAPON EDITOR", 30, 24, 30, RAYWHITE);
    DrawText("Gameplay paused", 30, 60, 16, (Color){150, 170, 200, 255});

    WeaponDefinition* w = getWeaponByIndex(index_);
    if (!w) {
        DrawText("No weapons loaded.", 30, 120, 20, RED);
        DrawText("ESC / F4: back", 30, sh - 30, 16, GRAY);
        EndDrawing();
        return;
    }

    // Re-fill the text boxes whenever the shown weapon changes.
    if (w->id != shownWeaponId_) refreshBuffers();

    // Weapon name + position in the table.
    DrawText(TextFormat("%s   (id %d,  %d/%d)", w->name.c_str(), w->id, index_ + 1, weaponCount()),
             30, 100, 22, YELLOW);

    // Read-only descriptors (not editable here: they change firing behaviour, not balance).
    DrawText(TextFormat("type: %s     damageType: %s     twin: %s     radius: %.2f",
                        weaponTypeName(w->type), damageTypeName(w->damageType),
                        w->twin ? "yes" : "no", w->radius),
             30, 132, 16, LIGHTGRAY);

    // Editable numeric fields: a slider and a value box per field, both bound to the same
    // definition member. GuiValueBoxFloat only writes *value while in edit mode and never
    // reads it back, so we keep the text buffer synced from the (slider-driven) value each
    // frame the box isn't being typed into.
    const int labelX  = 40;
    const int sliderX = 170;
    const int sliderW = 300;
    const int boxX    = sliderX + sliderW + 24;
    const int boxW    = 96;
    const int rowH    = 40;
    int y = 190;

    for (int i = 0; i < kFieldCount; ++i) {
        float* v = &(w->*kFields[i].mem);
        const FieldSpec& f = kFields[i];

        DrawText(f.label, labelX, y + 3, 18, RAYWHITE);
        GuiSlider((Rectangle){(float)sliderX, (float)y, (float)sliderW, 22}, "", "",
                  v, f.lo, f.hi);

        if (!editMode_[i]) std::snprintf(textBuf_[i], sizeof(textBuf_[i]), "%.3f", *v);
        if (GuiValueBoxFloat((Rectangle){(float)boxX, (float)y, (float)boxW, 22}, "",
                             textBuf_[i], v, editMode_[i])) {
            editMode_[i] = !editMode_[i];
            // Only one box edits at a time — leaving the others to re-sync from *value.
            if (editMode_[i]) for (int j = 0; j < kFieldCount; ++j) if (j != i) editMode_[j] = false;
        }
        // Keep the edited value within the slider's range so the two controls agree.
        if (*v < f.lo) *v = f.lo;
        if (*v > f.hi) *v = f.hi;
        y += rowH;
    }

    // Save button: overwrites the in-use weapons.json with the current table.
    y += 12;
    if (GuiButton((Rectangle){(float)labelX, (float)y, 180, 34}, "Save to weapons.json")) {
        std::string path = game_->assetPath + "/data/weapons.json";
        bool ok = saveWeaponsToFile(path);
        saveMsg_ = ok ? "Saved weapons.json" : "Save FAILED";
        saveMsgTimer_ = 2.5f;
    }
    if (saveMsgTimer_ > 0.0f) {
        DrawText(saveMsg_.c_str(), labelX + 196, y + 8, 18,
                 saveMsg_.rfind("Save FAILED", 0) == 0 ? RED : GREEN);
    }

    DrawText("UP/DOWN: browse weapons   drag slider or type a value   ESC / F4: back",
             30, sh - 30, 16, GRAY);
    EndDrawing();
}
