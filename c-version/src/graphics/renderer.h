#ifndef RENDERER_H
#define RENDERER_H

#include "raylib.h"

typedef struct Entity Entity;

typedef struct Renderer {
    // Add render state here if needed
    int placeholder;
} Renderer;

void renderer_init(Renderer* renderer);
void renderer_destroy(Renderer* renderer);

Model renderer_load_gltf(const char* path);
void renderer_draw_entity(Renderer* renderer, Entity* entity);

#endif
