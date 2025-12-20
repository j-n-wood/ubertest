#include "input.h"
#include "physics/physics_world.h"
#include <cmath>

#define MOVEMENT_FORCE 50.0f

void input_init(Input* input) {
    input->movement = (Vector2){0, 0};
    input->mouse_pos = (Vector2){0, 0};
    input->screen_cache.valid = false;
    input->quit = false;
}

void input_update(Input* input) {
    input->movement = (Vector2){0, 0};

    if (IsKeyDown(KEY_RIGHT)) input->movement.x += 1.0f;
    if (IsKeyDown(KEY_LEFT))  input->movement.x -= 1.0f;
    if (IsKeyDown(KEY_UP))    input->movement.y -= 1.0f;
    if (IsKeyDown(KEY_DOWN))  input->movement.y += 1.0f;

    input->mouse_pos = GetMousePosition();

    if (IsKeyPressed(KEY_ESCAPE)) input->quit = true;
}

void input_apply_to_entity(Input* input, Entity* entity) {
    if (!entity || !entity->physics.valid) return;

    Vector2 force = {
        input->movement.x * MOVEMENT_FORCE,
        input->movement.y * MOVEMENT_FORCE
    };
    physics_body_apply_force(&entity->physics, force);
}

void input_update_screen_cache(Input* input, Camera3D* camera) {
    ScreenToWorldCache* cache = &input->screen_cache;

    cache->screen_width = (float)GetScreenWidth();
    cache->screen_height = (float)GetScreenHeight();

    // Calculate world extent visible at ground plane
    // For perspective camera: visible_height = 2 * distance * tan(fovy/2)
    float camera_height = camera->position.y;
    float half_fov = camera->fovy * 0.5f * DEG2RAD;
    float visible_height = 2.0f * camera_height * std::tan(half_fov);
    float aspect = cache->screen_width / cache->screen_height;
    float visible_width = visible_height * aspect;

    cache->half_visible_width = visible_width * 0.5f;
    cache->half_visible_height = visible_height * 0.5f;
    cache->valid = true;
}

void input_update_entity_rotation(Input* input, Entity* entity, Camera3D* camera) {
    if (!entity) return;

    // Ensure cache is valid
    if (!input->screen_cache.valid) {
        input_update_screen_cache(input, camera);
    }

    ScreenToWorldCache* cache = &input->screen_cache;

    // Normalize mouse to [-1, 1] range
    float norm_x = (input->mouse_pos.x / cache->screen_width) * 2.0f - 1.0f;
    float norm_y = (input->mouse_pos.y / cache->screen_height) * 2.0f - 1.0f;

    // Map normalized screen coords to world offset from camera target
    float world_x = camera->target.x + norm_x * cache->half_visible_width;
    float world_z = camera->target.z + norm_y * cache->half_visible_height;

    // Calculate angle from entity to mouse world position
    float dx = world_x - entity->position.x;
    float dz = world_z - entity->position.z;

    // Only update if mouse is far enough from entity to get stable angle
    if (dx * dx + dz * dz > 0.01f) {
        entity->desired_rotation = std::atan2(dx, dz);
    }
}
