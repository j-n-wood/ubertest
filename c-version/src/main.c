#include "raylib.h"
#include "game.h"

int main(void) {
    InitWindow(1280, 720, "Top-Down Game");
    SetTargetFPS(60);

    Game game = {0};
    game_init(&game);

    while (!WindowShouldClose() && game.running) {
        float dt = GetFrameTime();
        game_update(&game, dt);
        game_render(&game);
    }

    game_destroy(&game);
    CloseWindow();

    return 0;
}
