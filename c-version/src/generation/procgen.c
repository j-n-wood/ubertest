#include "procgen.h"
#include "game.h"
#include <stddef.h>

void procgen_generate_level(Game* game) {
    // Spawn a test player entity at origin
    procgen_spawn_entity(game, ENTITY_PLAYER, (Vector3){0, 0, 0}, NULL);

    // Spawn ellipsoid prop
    procgen_spawn_entity(game, ENTITY_PROP, (Vector3){3, 0, -2}, "assets/models/ellipsoid.gltf");

    // Spawn some static obstacles
    procgen_spawn_entity(game, ENTITY_OBSTACLE, (Vector3){5, 0, 5}, NULL);
    procgen_spawn_entity(game, ENTITY_OBSTACLE, (Vector3){-5, 0, -5}, NULL);
    procgen_spawn_entity(game, ENTITY_OBSTACLE, (Vector3){5, 0, -5}, NULL);
    procgen_spawn_entity(game, ENTITY_OBSTACLE, (Vector3){-5, 0, 5}, NULL);
}

Entity* procgen_spawn_entity(Game* game, EntityType type, Vector3 pos, const char* model_path) {
    if (game->entity_count >= MAX_ENTITIES) {
        return NULL;
    }

    Entity* entity = &game->entities[game->entity_count++];
    entity_init(entity, type, pos);

    // Load model if path provided
    if (model_path) {
        entity->model = renderer_load_gltf(model_path);
        entity->has_model = true;
    }

    // Create physics body based on type
    Vector2 physics_pos = {pos.x, pos.z};  // Map 3D (X,Z) to 2D (X,Y)

    switch (type) {
        case ENTITY_PLAYER:
        case ENTITY_ENEMY:
            entity->physics = physics_create_dynamic_circle(&game->physics, physics_pos, 0.5f);
            break;
        case ENTITY_OBSTACLE:
            entity->physics = physics_create_static_box(&game->physics, physics_pos, 2.0f, 2.0f);
            break;
        case ENTITY_PROP:
            // Props may or may not have physics
            break;
    }

    return entity;
}
