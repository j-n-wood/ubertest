#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"

typedef struct Input {
    Vector2 movement;  // Normalized movement direction
    bool quit;
} Input;

void input_init(Input* input);
void input_update(Input* input);
void input_apply_to_camera(Input* input, Camera3D* camera, float dt);

#endif
