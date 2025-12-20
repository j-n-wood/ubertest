#ifndef ENTITY_H
#define ENTITY_H

#include "raylib.h"
#include "physics/physics_world.h"

typedef enum EntityType {
    ENTITY_PLAYER,
    ENTITY_ENEMY,
    ENTITY_OBSTACLE,
    ENTITY_PROP
} EntityType;

typedef struct Entity {
    EntityType type;
    Vector3 position;
    float rotation;          // Y-axis rotation in radians (from physics)
    float desired_rotation;  // Target rotation to turn towards
    Model model;
    bool has_model;
    PhysicsBody physics;
    bool active;
} Entity;

void entity_init(Entity* entity, EntityType type, Vector3 position);
void entity_destroy(Entity* entity);
void entity_sync_from_physics(Entity* entity);

#endif
