#include "input.h"
#include <cmath>

void input_init(Input* input) {
    input->movement = (Vector2){0, 0};
    input->mouse_pos = (Vector2){0, 0};
    input->screen_cache.valid = false;
    input->quit = false;
}

void input_update(Input* input) {
    input->movement = (Vector2){0, 0};

    // Arrow keys
    if (IsKeyDown(KEY_RIGHT)) input->movement.x += 1.0f;
    if (IsKeyDown(KEY_LEFT))  input->movement.x -= 1.0f;
    if (IsKeyDown(KEY_UP))    input->movement.y -= 1.0f;
    if (IsKeyDown(KEY_DOWN))  input->movement.y += 1.0f;

    // WASD keys
    if (IsKeyDown(KEY_D)) input->movement.x += 1.0f;
    if (IsKeyDown(KEY_A)) input->movement.x -= 1.0f;
    if (IsKeyDown(KEY_W)) input->movement.y -= 1.0f;
    if (IsKeyDown(KEY_S)) input->movement.y += 1.0f;

    // Normalize if diagonal
    float len = std::sqrt(input->movement.x * input->movement.x +
                          input->movement.y * input->movement.y);
    if (len > 1.0f) {
        input->movement.x /= len;
        input->movement.y /= len;
    }

    input->mouse_pos = GetMousePosition();

    if (IsKeyPressed(KEY_ESCAPE)) input->quit = true;
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
