#include "procgen.h"
#include "game.h"
#include <stddef.h>
#include <cstdio>

void procgen_generate_level(Game* game) {
    // Spawn Suzanne as player using asset path
    char model_path[512];
    snprintf(model_path, sizeof(model_path), "%s/models/Suzanne.glb", game->asset_path);
    game->controlled_entity = procgen_spawn_entity_specular(game, ENTITY_PLAYER, (Vector3){0, 0, 0}, model_path, 64.0f, 5.0f);

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

Entity* procgen_spawn_entity_specular(Game* game, EntityType type, Vector3 pos, const char* model_path, float specular_power, float specular_intensity) {
    if (game->entity_count >= MAX_ENTITIES) {
        return NULL;
    }

    Entity* entity = &game->entities[game->entity_count++];
    entity_init(entity, type, pos);

    // Load model with specular material
    if (model_path) {
        entity->model = renderer_load_gltf_specular(&game->renderer, model_path, specular_power, specular_intensity);
        entity->has_model = true;
    }

    // Create physics body based on type
    Vector2 physics_pos = {pos.x, pos.z};

    switch (type) {
        case ENTITY_PLAYER:
        case ENTITY_ENEMY:
            entity->physics = physics_create_dynamic_circle(&game->physics, physics_pos, 0.5f);
            break;
        case ENTITY_OBSTACLE:
            entity->physics = physics_create_static_box(&game->physics, physics_pos, 2.0f, 2.0f);
            break;
        case ENTITY_PROP:
            break;
    }

    return entity;
}
