#include "console_manager.h"
#include "raymath.h"

void ConsoleManager::init(const std::vector<ConsoleSpec>& specs) {
    consoles_ = specs;
    playerInRange_ = false;
}

void ConsoleManager::destroy() {
    consoles_.clear();
    playerInRange_ = false;
}

void ConsoleManager::update(Vector2 playerPos) {
    playerInRange_ = false;
    for (const ConsoleSpec& c : consoles_) {
        if (Vector2Distance(playerPos, c.physicsCenter) <= CONSOLE_USE_RADIUS) {
            playerInRange_ = true;
            break;
        }
    }
}
