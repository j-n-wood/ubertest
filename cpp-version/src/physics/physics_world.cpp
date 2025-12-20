#include "physics_world.h"

void physics_world_init(PhysicsWorld* world) {
    b2WorldDef world_def = b2DefaultWorldDef();
    world_def.gravity = (b2Vec2){0.0f, 0.0f};  // Zero gravity for top-down
    world->world_id = b2CreateWorld(&world_def);
}

void physics_world_step(PhysicsWorld* world, float dt) {
    b2World_Step(world->world_id, dt, 4);
}

void physics_world_destroy(PhysicsWorld* world) {
    b2DestroyWorld(world->world_id);
}

PhysicsBody physics_create_dynamic_circle(PhysicsWorld* world, Vector2 pos, float radius) {
    PhysicsBody body = {0};

    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_dynamicBody;
    body_def.position = (b2Vec2){pos.x, pos.y};
    body_def.linearDamping = 4.0f;   // Simulate top-down friction
    body_def.angularDamping = 8.0f;  // Damping for rotation

    body.body_id = b2CreateBody(world->world_id, &body_def);

    b2Circle circle = {.center = {0, 0}, .radius = radius};
    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.density = 1.0f;
    shape_def.friction = 0.3f;

    b2CreateCircleShape(body.body_id, &shape_def, &circle);
    body.valid = true;

    return body;
}

PhysicsBody physics_create_static_box(PhysicsWorld* world, Vector2 pos, float w, float h) {
    PhysicsBody body = {0};

    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = (b2Vec2){pos.x, pos.y};

    body.body_id = b2CreateBody(world->world_id, &body_def);

    b2Polygon box = b2MakeBox(w / 2.0f, h / 2.0f);
    b2ShapeDef shape_def = b2DefaultShapeDef();

    b2CreatePolygonShape(body.body_id, &shape_def, &box);
    body.valid = true;

    return body;
}

void physics_body_apply_force(PhysicsBody* body, Vector2 force) {
    if (!body->valid) return;
    b2Body_ApplyForceToCenter(body->body_id, (b2Vec2){force.x, force.y}, true);
}

void physics_body_apply_torque(PhysicsBody* body, float torque) {
    if (!body->valid) return;
    b2Body_ApplyTorque(body->body_id, torque, true);
}

float physics_body_get_angular_velocity(PhysicsBody* body) {
    if (!body->valid) return 0.0f;
    return b2Body_GetAngularVelocity(body->body_id);
}

Vector2 physics_body_get_position(PhysicsBody* body) {
    if (!body->valid) return (Vector2){0, 0};
    b2Vec2 pos = b2Body_GetPosition(body->body_id);
    return (Vector2){pos.x, pos.y};
}

float physics_body_get_angle(PhysicsBody* body) {
    if (!body->valid) return 0.0f;
    b2Rot rot = b2Body_GetRotation(body->body_id);
    return b2Rot_GetAngle(rot);
}
