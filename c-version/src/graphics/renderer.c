#include "renderer.h"
#include "entities/entity.h"

void renderer_init(Renderer* renderer) {
    renderer->placeholder = 0;
}

void renderer_destroy(Renderer* renderer) {
    (void)renderer;
}

Model renderer_load_gltf(const char* path) {
    return LoadModel(path);
}

void renderer_draw_entity(Renderer* renderer, Entity* entity) {
    (void)renderer;

    if (!entity->active) return;

    if (entity->has_model) {
        Vector3 axis = {0, 1, 0};
        float angle_deg = entity->rotation * RAD2DEG;
        DrawModelEx(entity->model, entity->position, axis, angle_deg,
                    (Vector3){1, 1, 1}, WHITE);
    } else {
        // Draw placeholder cube if no model
        DrawCubeV(entity->position, (Vector3){1, 1, 1}, RED);
    }
}
