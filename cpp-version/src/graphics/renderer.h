#ifndef RENDERER_H
#define RENDERER_H

#include "raylib.h"
#include "rlgl.h"
#include "lighting/light.h"  // Shared light definitions

typedef struct Entity Entity;

typedef struct Renderer {
    Shader lighting_shader;
    Light lights[MAX_LIGHTS];
    int light_count;
    int ambient_loc;
    int debug_mode_loc;
    int debug_mode;
    Model cube_model;  // Shared cube model with lighting shader
} Renderer;

void renderer_init(Renderer* renderer);
void renderer_destroy(Renderer* renderer);

Model renderer_load_gltf(const char* path);
Model renderer_load_gltf_specular(Renderer* renderer, const char* path, float specular_power, float specular_intensity);
void renderer_draw_entity(Renderer* renderer, Entity* entity);
void renderer_begin_lighting(Renderer* renderer, Camera3D* camera);
void renderer_set_debug_mode(Renderer* renderer, int mode);

#endif
