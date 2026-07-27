#include "pages/ship_view_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "rendering/texture_manager.h"
#include "raylib.h"

namespace {
// Map a fractional rect (0..1 of the image) into the on-screen destination rect the
// ship image was drawn into.
Rectangle fracToScreen(const Rectangle& f, const Rectangle& dst) {
    return {dst.x + f.x * dst.width, dst.y + f.y * dst.height,
            f.width * dst.width, f.height * dst.height};
}

// Level name for a runtime level index (safe).
const char* levelName(const Game* g, int level) {
    if (level >= 0 && level < (int)g->levels.size()) return g->levels[level].name.c_str();
    return "-";
}
}  // namespace

void ShipViewPage::activate() {
    origin_ = game_->liftManager.currentStop();
    selected_ = origin_;
    // Ship images are preloaded into the TextureManager at startup (see game_init).
}

void ShipViewPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) { pages_->pop(); return; }
    if (!selected_) return;

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        const LiftStop* next = game_->liftManager.stepStop(selected_, +1);
        if (next) selected_ = next;
    } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        const LiftStop* next = game_->liftManager.stepStop(selected_, -1);
        if (next) selected_ = next;
    } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        // Travel to the selected stop (no-op switch if it's the current deck), then close.
        if (selected_->level != game_->currentLevel) {
            game_switch_to_stop(game_, *selected_);
        }
        pages_->pop();
        return;
    }
}

void ShipViewPage::render() {
    // Ship images owned by the TextureManager (loaded once at startup).
    const Texture2D& shipTex = gTextures().get(TEX_SHIP_MAP);
    const Texture2D& litTex = gTextures().get(TEX_SHIP_MAP_LIT);
    const bool texLoaded = gTextures().loaded(TEX_SHIP_MAP);
    const bool litLoaded = gTextures().loaded(TEX_SHIP_MAP_LIT);

    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawText("SHIP LIFT", sw / 2 - MeasureText("SHIP LIFT", 30) / 2, 24, 30, RAYWHITE);

    // Fit the ship image into a centred area, preserving its aspect ratio.
    Rectangle dst{};
    if (texLoaded) {
        float areaW = sw * 0.9f;
        float areaH = sh * 0.6f;
        float ax = sw * 0.05f;
        float ay = 80.0f;
        float scale = fminf(areaW / shipTex.width, areaH / shipTex.height);
        float dw = shipTex.width * scale;
        float dh = shipTex.height * scale;
        dst = {ax + (areaW - dw) * 0.5f, ay + (areaH - dh) * 0.5f, dw, dh};
        DrawTexturePro(shipTex,
                       (Rectangle){0, 0, (float)shipTex.width, (float)shipTex.height},
                       dst, (Vector2){0, 0}, 0.0f, WHITE);
    } else {
        DrawText("(ship image unavailable)", sw / 2 - 120, sh / 2, 20, GRAY);
    }

    if (origin_ && texLoaded) {
        // "Light up" a fractional region by blitting the corresponding sub-rect of the
        // lit image (ship_on) over the dim base; falls back to a translucent fill if the
        // lit image is unavailable. Then an optional tint/outline marks its role.
        auto light = [&](const Rectangle& f, Color tint, Color outline) {
            Rectangle d = fracToScreen(f, dst);
            if (litLoaded) {
                Rectangle src = {f.x * litTex.width, f.y * litTex.height,
                                 f.width * litTex.width, f.height * litTex.height};
                DrawTexturePro(litTex, src, d, (Vector2){0, 0}, 0.0f, WHITE);
            }
            if (tint.a) DrawRectangleRec(d, tint);
            if (outline.a) DrawRectangleLinesEx(d, 1.0f, outline);
        };

        // Accessed elevator shaft: light it, faint blue tint.
        if (const Rectangle* er = game_->shipMap.elevatorRect(origin_->elevator)) {
            light(*er, (Color){80, 170, 255, 60}, (Color){120, 200, 255, 200});
        }
        // Player's current deck (green outline = "you are here").
        if (const auto* rects = game_->shipMap.deckRects(origin_->levelNumber)) {
            for (const Rectangle& r : *rects) {
                light(r, (Color){60, 220, 120, 50}, (Color){80, 240, 140, 230});
            }
        }
        // Selected destination deck (yellow), when different from the current deck.
        if (selected_ && selected_->levelNumber != origin_->levelNumber) {
            if (const auto* rects = game_->shipMap.deckRects(selected_->levelNumber)) {
                for (const Rectangle& r : *rects) {
                    light(r, (Color){255, 230, 60, 90}, (Color){255, 240, 120, 240});
                }
            }
        }
    }

    // Info line.
    if (selected_) {
        DrawText(TextFormat("Elevator %d    Deck: %s", origin_ ? origin_->elevator : -1,
                            levelName(game_, selected_->level)),
                 80, sh - 80, 22, RAYWHITE);
    } else {
        DrawText("No lift here.", 80, sh - 80, 22, GRAY);
    }
    DrawText("W/S or UP/DOWN: choose deck   ENTER: travel   ESC: cancel",
             80, sh - 40, 18, GRAY);

    EndDrawing();
}
