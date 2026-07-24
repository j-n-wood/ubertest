#include "pages/status_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "raylib.h"
#include "raygui.h"

void StatusPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) pages_->pop();  // back to console menu
}

void StatusPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawText("STATUS", sw / 2 - MeasureText("STATUS", 36) / 2, 50, 36, RAYWHITE);

    // Deck (level) name.
    const char* deck = "-";
    if (game_->currentLevel >= 0 && game_->currentLevel < (int)game_->levels.size()) {
        deck = game_->levels[game_->currentLevel].name.c_str();
    }

    // Live droid count.
    int live = 0;
    for (const UnitInstance* e : game_->enemyUnits) {
        if (e && e->active && b2Body_IsValid(e->bodyId)) live++;
    }

    int x = 80, y = 140;
    DrawText(TextFormat("Deck:        %s", deck), x, y, 24, RAYWHITE);
    DrawText(TextFormat("Live droids: %d", live), x, y + 40, 24, RAYWHITE);

    if (GuiButton((Rectangle){(float)sw / 2 - 80, (float)sh - 90, 160, 40}, "Back")) {
        pages_->pop();
    }
    DrawText("ESC: back", 20, sh - 30, 16, GRAY);
    EndDrawing();
}
