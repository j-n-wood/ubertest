#include "pages/console_menu_page.h"
#include "pages/page_manager.h"
#include "pages/droid_library_page.h"
#include "pages/status_page.h"
#include "pages/ship_data_page.h"
#include "game.h"
#include "raylib.h"
#include "raygui.h"
#include <memory>

void ConsoleMenuPage::handleInput() {
    if (IsKeyPressed(KEY_ESCAPE)) pages_->pop();  // back to gameplay
}

void ConsoleMenuPage::render() {
    BeginDrawing();
    ClearBackground((Color){10, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawText("CONSOLE", sw / 2 - MeasureText("CONSOLE", 40) / 2, 60, 40, RAYWHITE);

    const float bw = 260.0f, bh = 44.0f;
    const float bx = sw / 2.0f - bw / 2.0f;
    float by = 180.0f;

    if (GuiButton((Rectangle){bx, by, bw, bh}, "Droid Library")) {
        pages_->push(std::make_unique<DroidLibraryPage>(game_, pages_));
    }
    if (GuiButton((Rectangle){bx, by + 60, bw, bh}, "Status")) {
        pages_->push(std::make_unique<StatusPage>(game_, pages_));
    }
    if (GuiButton((Rectangle){bx, by + 120, bw, bh}, "Ship Data")) {
        pages_->push(std::make_unique<ShipDataPage>(game_, pages_));
    }
    if (GuiButton((Rectangle){bx, by + 180, bw, bh}, "Exit")) {
        pages_->pop();
    }

    DrawText("ESC: back to game", 20, sh - 30, 16, GRAY);
    EndDrawing();
}
