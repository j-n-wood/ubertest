#include "pages/deck_info_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "raylib.h"
#include "raygui.h"
#include <cmath>

void DeckInfoPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) pages_->pop();  // back to console menu
}

void DeckInfoPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int L = game_->currentLevel;

    // --- Map: a zoomed-out top-down render of the level GEOMETRY only. -------------------------
    // Deliberately just the baked tile mesh — no units, particles, doors or scenery objects — which
    // is enough to read as a deck map. Orthographic camera looking straight down, framed to the
    // level's bounds. Fills the screen behind the info bars.
    const bool haveMap = L >= 0 && L < (int)game_->levelRenderData.size() &&
                         game_->levelRenderData[L].meshValid;
    if (haveMap) {
        LevelRenderData& data = game_->levelRenderData[L];
        const Vector3 mn = data.boundsMin, mx = data.boundsMax;
        const Vector3 ctr = {(mn.x + mx.x) * 0.5f, 0.0f, (mn.z + mx.z) * 0.5f};
        const float w = fmaxf(mx.x - mn.x, 0.1f);
        const float d = fmaxf(mx.z - mn.z, 0.1f);
        const float aspect = (float)sw / (float)sh;
        // Orthographic fovy = vertical world extent; size it to fit BOTH dimensions (+10% margin).
        const float fovy = fmaxf(d, w / aspect) * 1.1f;

        Camera3D cam = {0};
        cam.position = {ctr.x, fmaxf(mx.y, 1.0f) + 5.0f, ctr.z};  // straight above the deck
        cam.target = ctr;
        cam.up = {0.0f, 0.0f, -1.0f};                            // look axis is -Y; up must differ
        cam.fovy = fovy;
        cam.projection = CAMERA_ORTHOGRAPHIC;

        // Flat, evenly-lit map look: point the scene shader's eye at the map camera, and turn off the
        // lights-out dimming + shadow term for this pass (gameplay re-sets them next frame).
        Shader shader = sceneRendererGetShader(&game_->sceneRenderer);
        sceneRendererUpdateCamera(&game_->sceneRenderer, cam.position);
        sceneRendererSetDarkness(&game_->sceneRenderer, 0.0f);
        ShadowMap::disable(shader);

        BeginMode3D(cam);
        DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
        EndMode3D();
    } else {
        const char* nomap = "(no map available for this deck)";
        DrawText(nomap, sw / 2 - MeasureText(nomap, 20) / 2, sh / 2, 20, GRAY);
    }

    // --- Overlay: title bar + deck info, on translucent bands so they read over the map. ---------
    DrawRectangle(0, 0, sw, 88, (Color){10, 15, 25, 210});
    DrawText("DECK INFO", sw / 2 - MeasureText("DECK INFO", 36) / 2, 28, 36, RAYWHITE);

    const char* deck = "-";
    if (L >= 0 && L < (int)game_->levels.size()) deck = game_->levels[L].name.c_str();

    // Live hostile count on the active deck (exclude the player device + a captured droid), matching
    // the "level cleared" condition.
    int live = 0;
    for (const UnitInstance* e : game_->enemyUnits) {
        if (!e || e == game_->playerUnit || e == game_->transfer.captured) continue;
        if (e->active && b2Body_IsValid(e->bodyId)) live++;
    }

    DrawRectangle(0, sh - 116, sw, 116, (Color){10, 15, 25, 210});
    DrawText(TextFormat("Deck:        %s", deck), 40, sh - 98, 24, RAYWHITE);
    DrawText(TextFormat("Live droids: %d", live), 40, sh - 62, 24, RAYWHITE);

    if (GuiButton((Rectangle){(float)sw - 180, (float)sh - 66, 140, 40}, "Back")) {
        pages_->pop();
    }
    DrawText("ESC: back", 20, sh - 28, 16, GRAY);
    EndDrawing();
}
