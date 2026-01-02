#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"

typedef struct ScreenToWorldCache {
    float screen_width;
    float screen_height;
    float half_visible_width;
    float half_visible_height;
    bool valid;
} ScreenToWorldCache;

typedef struct Input {
    Vector2 movement;  // Movement direction
    Vector2 mouse_pos; // Screen mouse position
    ScreenToWorldCache screen_cache;
    bool quit;
} Input;

void input_init(Input* input);
void input_update(Input* input);
void input_update_screen_cache(Input* input, Camera3D* camera);

#endif
