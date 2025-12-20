#include "entity.h"

void entity_init(Entity* entity, EntityType type, Vector3 position) {
    entity->type = type;
    entity->position = position;
    entity->rotation = 0.0f;
    entity->desired_rotation = 0.0f;
    entity->has_model = false;
    entity->physics.valid = false;
    entity->active = true;
}

void entity_destroy(Entity* entity) {
    if (entity->has_model) {
        UnloadModel(entity->model);
        entity->has_model = false;
    }
    entity->active = false;
}

void entity_sync_from_physics(Entity* entity) {
    if (!entity->physics.valid) return;

    // Map 2D physics (X,Y) to 3D rendering (X,Z)
    Vector2 physics_pos = physics_body_get_position(&entity->physics);
    entity->position.x = physics_pos.x;
    entity->position.z = physics_pos.y;  // Physics Y -> Render Z
    // Y (height) remains unchanged

    entity->rotation = physics_body_get_angle(&entity->physics);
}
