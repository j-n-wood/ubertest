#include "pages/ship_data_page.h"
#include "pages/page_manager.h"
#include "game.h"
#include "score/scoring.h"
#include "raylib.h"
#include "raygui.h"

void ShipDataPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) pages_->pop();  // back to console menu
}

void ShipDataPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawText("SHIP DATA", sw / 2 - MeasureText("SHIP DATA", 36) / 2, 50, 36, RAYWHITE);

    const std::string& shipName = game_->shipMap.name();
    const char* name = shipName.empty() ? "(unnamed)" : shipName.c_str();
    const int total = game_->shipDroidsTotal;
    const int remaining = game_->shipDroidsRemaining;

    // Alert band -> label + colour (mirrors the ship alert beacon; see scoring.h).
    const char* alertLabel = "GREEN";
    Color alertCol = GREEN;
    switch (alert_band(game_->alertLevel)) {
        case AlertBand::Red:    alertLabel = "RED";    alertCol = RED;    break;
        case AlertBand::Amber:  alertLabel = "AMBER";  alertCol = ORANGE; break;
        case AlertBand::Yellow: alertLabel = "YELLOW"; alertCol = YELLOW; break;
        case AlertBand::Green:  default: break;
    }

    const int x = 80, y = 150;
    DrawText(TextFormat("Ship:          %s", name), x, y, 24, RAYWHITE);
    DrawText(TextFormat("Droids left:   %d / %d", remaining, total), x, y + 44, 24, RAYWHITE);
    DrawText("Alert:", x, y + 88, 24, RAYWHITE);
    DrawText(alertLabel, x + 150, y + 88, 24, alertCol);

    if (game_ship_is_clear(game_)) {
        const char* clear = "SHIP CLEAR OF DROIDS";
        DrawText(clear, sw / 2 - MeasureText(clear, 26) / 2, y + 160, 26, GREEN);
    }

    if (GuiButton((Rectangle){(float)sw / 2 - 80, (float)sh - 90, 160, 40}, "Back")) {
        pages_->pop();
    }
    DrawText("ESC: back", 20, sh - 30, 16, GRAY);
    EndDrawing();
}
