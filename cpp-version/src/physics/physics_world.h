#ifndef PHYSICS_WORLD_H
#define PHYSICS_WORLD_H

#include "box2d/box2d.h"
#include "raylib.h"

typedef struct PhysicsBody {
    b2BodyId body_id;
    bool valid;
} PhysicsBody;

typedef struct PhysicsWorld {
    b2WorldId world_id;
} PhysicsWorld;

void physics_world_init(PhysicsWorld* world);
void physics_world_step(PhysicsWorld* world, float dt);
void physics_world_destroy(PhysicsWorld* world);

PhysicsBody physics_create_dynamic_circle(PhysicsWorld* world, Vector2 pos, float radius);
PhysicsBody physics_create_static_box(PhysicsWorld* world, Vector2 pos, float w, float h);

void physics_body_apply_force(PhysicsBody* body, Vector2 force);
void physics_body_apply_torque(PhysicsBody* body, float torque);
float physics_body_get_angular_velocity(PhysicsBody* body);
Vector2 physics_body_get_position(PhysicsBody* body);
float physics_body_get_angle(PhysicsBody* body);

#endif
