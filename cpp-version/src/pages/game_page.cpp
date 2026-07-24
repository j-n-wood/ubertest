#include "pages/game_page.h"
#include "pages/page_manager.h"
#include "pages/console_menu_page.h"
#include "game.h"
#include "raylib.h"
#include <memory>

void GamePage::update(float dt) {
    game_update_gameplay(game_, dt);

    // Open the console when standing on a console tile and pressing SPACE.
    if (game_->consoleManager.playerInRange() && IsKeyPressed(KEY_SPACE)) {
        pages_->push(std::make_unique<ConsoleMenuPage>(game_, pages_));
    }
}

void GamePage::render() {
    game_render_gameplay(game_);
}
