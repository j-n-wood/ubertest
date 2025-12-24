#include "game.h"
#include "generation/procgen.h"
#include <cmath>
#include <cstring>

#define MAX_TORQUE 100.0f
#define PI 3.14159265358979323846f

// Normalize angle to [-PI, PI]
static float normalize_angle(float angle) {
    while (angle > PI) angle -= 2.0f * PI;
    while (angle < -PI) angle += 2.0f * PI;
    return angle;
}

// Apply torque to rotate entity towards desired_rotation
static void apply_rotation_torque(Entity* entity) {
    if (!entity || !entity->physics.valid) return;

    float current = entity->rotation;
    float target = entity->desired_rotation;

    // Calculate shortest angular difference
    float diff = normalize_angle(target - current);

    // Get current angular velocity for damping
    float angular_vel = physics_body_get_angular_velocity(&entity->physics);

    // PD controller: proportional to error, with velocity damping
    float torque = diff * 50.0f - angular_vel * 5.0f;

    // Clamp to max torque
    if (torque > MAX_TORQUE) torque = MAX_TORQUE;
    if (torque < -MAX_TORQUE) torque = -MAX_TORQUE;

    physics_body_apply_torque(&entity->physics, torque);
}

void game_init(Game* game, const char* assetPath) {
    // Store asset path
    strncpy(game->asset_path, assetPath, sizeof(game->asset_path) - 1);
    game->asset_path[sizeof(game->asset_path) - 1] = '\0';

    physics_world_init(&game->physics);
    renderer_init(&game->renderer, game->asset_path);
    input_init(&game->input);

    game->entity_count = 0;
    game->controlled_entity = nullptr;
    game->running = true;

    // Top-down camera setup
    game->camera.position = (Vector3){0, 50, 0};
    game->camera.target = (Vector3){0, 0, 0};
    game->camera.up = (Vector3){0, 0, -1};
    game->camera.fovy = 45.0f;
    game->camera.projection = CAMERA_PERSPECTIVE;

    // Initialize screen-to-world cache
    input_update_screen_cache(&game->input, &game->camera);

    // Generate initial level
    procgen_generate_level(game);
}

void game_update(Game* game, float dt) {
    input_update(&game->input);
    input_apply_to_entity(&game->input, game->controlled_entity);
    input_update_entity_rotation(&game->input, game->controlled_entity, &game->camera);

    // Apply torque to rotate controlled entity towards mouse
    apply_rotation_torque(game->controlled_entity);

    if (game->input.quit) {
        game->running = false;
    }

    // Debug mode toggle (keys 0-5)
    // 0=normal, 1=normals, 2=lightDir, 3=specular, 4=viewDir, 5=halfDir
    for (int i = 0; i <= 5; i++) {
        if (IsKeyPressed(KEY_ZERO + i)) {
            renderer_set_debug_mode(&game->renderer, i);
        }
    }

    physics_world_step(&game->physics, dt);

    // Sync physics to entities
    for (int i = 0; i < game->entity_count; i++) {
        entity_sync_from_physics(&game->entities[i]);
    }

    // Camera follows controlled entity
    if (game->controlled_entity) {
        Vector3 pos = game->controlled_entity->position;
        game->camera.target = pos;
        game->camera.position = (Vector3){pos.x, 50, pos.z};
    }
}

void game_render(Game* game) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    // Update lighting shader with camera position
    renderer_begin_lighting(&game->renderer, &game->camera);

    BeginMode3D(game->camera);

    for (int i = 0; i < game->entity_count; i++) {
        renderer_draw_entity(&game->renderer, &game->entities[i]);
    }

    EndMode3D();

    DrawFPS(10, 10);

    // Show debug mode info
    const char* debugModes[] = {"0:Normal", "1:Normals", "2:LightDir", "3:Specular", "4:ViewDir", "5:HalfDir"};
    DrawText(TextFormat("Debug: %s", debugModes[game->renderer.debug_mode]), 10, 30, 20, WHITE);

    EndDrawing();
}

void game_destroy(Game* game) {
    for (int i = 0; i < game->entity_count; i++) {
        entity_destroy(&game->entities[i]);
    }

    renderer_destroy(&game->renderer);
    physics_world_destroy(&game->physics);
}
