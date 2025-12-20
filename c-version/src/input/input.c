#include "input.h"

#define CAMERA_SPEED 20.0f

void input_init(Input* input) {
    input->movement = (Vector2){0, 0};
    input->quit = false;
}

void input_update(Input* input) {
    input->movement = (Vector2){0, 0};

    if (IsKeyDown(KEY_RIGHT)) input->movement.x += 1.0f;
    if (IsKeyDown(KEY_LEFT))  input->movement.x -= 1.0f;
    if (IsKeyDown(KEY_UP))    input->movement.y -= 1.0f;
    if (IsKeyDown(KEY_DOWN))  input->movement.y += 1.0f;

    if (IsKeyPressed(KEY_ESCAPE)) input->quit = true;
}

void input_apply_to_camera(Input* input, Camera3D* camera, float dt) {
    float dx = input->movement.x * CAMERA_SPEED * dt;
    float dz = input->movement.y * CAMERA_SPEED * dt;

    camera->position.x += dx;
    camera->position.z += dz;
    camera->target.x += dx;
    camera->target.z += dz;
}
