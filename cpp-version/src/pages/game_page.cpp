#include "pages/game_page.h"
#include "pages/page_manager.h"
#include "pages/console_menu_page.h"
#include "pages/droid_library_page.h"
#include "game.h"
#include "raylib.h"
#include <memory>

void GamePage::update(float dt) {
    game_update_gameplay(game_, dt);

    // Open the console when standing on a console tile and pressing SPACE.
    if (game_->consoleManager.playerInRange() && IsKeyPressed(KEY_SPACE)) {
        pages_->push(std::make_unique<ConsoleMenuPage>(game_, pages_));
    }

    // Debug shortcut (F3, only in debug mode): jump straight to the droid library —
    // pushed directly onto gameplay (skipping the console menu) so its editor is up
    // immediately for tweaking accel/decel, and ESC drops right back to the game to
    // test. Debug mode (showAIDebug, toggled by V) also gates the library's editors.
    if (game_->showAIDebug && IsKeyPressed(KEY_F3)) {
        pages_->push(std::make_unique<DroidLibraryPage>(game_, pages_));
    }
}

void GamePage::render() {
    game_render_gameplay(game_);
}
