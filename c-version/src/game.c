#include "game.h"
#include "generation/procgen.h"

void game_init(Game* game) {
    physics_world_init(&game->physics);
    renderer_init(&game->renderer);
    input_init(&game->input);

    game->entity_count = 0;
    game->running = true;

    // Top-down camera setup
    game->camera.position = (Vector3){0, 50, 0};
    game->camera.target = (Vector3){0, 0, 0};
    game->camera.up = (Vector3){0, 0, -1};
    game->camera.fovy = 45.0f;
    game->camera.projection = CAMERA_PERSPECTIVE;

    // Generate initial level
    procgen_generate_level(game);
}

void game_update(Game* game, float dt) {
    input_update(&game->input);
    input_apply_to_camera(&game->input, &game->camera, dt);

    if (game->input.quit) {
        game->running = false;
    }

    physics_world_step(&game->physics, dt);

    // Sync physics to entities
    for (int i = 0; i < game->entity_count; i++) {
        entity_sync_from_physics(&game->entities[i]);
    }
}

void game_render(Game* game) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    BeginMode3D(game->camera);

    for (int i = 0; i < game->entity_count; i++) {
        renderer_draw_entity(&game->renderer, &game->entities[i]);
    }

    EndMode3D();

    DrawFPS(10, 10);
    EndDrawing();
}

void game_destroy(Game* game) {
    for (int i = 0; i < game->entity_count; i++) {
        entity_destroy(&game->entities[i]);
    }

    renderer_destroy(&game->renderer);
    physics_world_destroy(&game->physics);
}
