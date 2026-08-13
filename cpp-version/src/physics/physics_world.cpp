#include "physics_world.h"
#include "physics/body_user_data.h"

#include <cmath>
#include <vector>

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
    shape_def.filter.categoryBits = CATEGORY_STATIC;
    shape_def.filter.maskBits = MASK_STATIC;

    b2CreatePolygonShape(body.body_id, &shape_def, &box);
    body.valid = true;

    return body;
}

PhysicsBody physics_create_static_polygon(PhysicsWorld* world, const Vector2* verts, int count,
                                          uint16_t category) {
    PhysicsBody body = {0};
    if (count < 3 || count > 8) return body;

    b2Vec2 points[8];
    for (int i = 0; i < count; i++) points[i] = (b2Vec2){verts[i].x, verts[i].y};
    b2Hull hull = b2ComputeHull(points, count);
    if (hull.count < 3) return body;  // degenerate / collinear input

    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = (b2Vec2){0.0f, 0.0f};  // vertices are already absolute world coords
    body.body_id = b2CreateBody(world->world_id, &body_def);

    b2Polygon poly = b2MakePolygon(&hull, 0.0f);
    b2ShapeDef shape_def = b2DefaultShapeDef();
    // `category` is CATEGORY_STATIC for normal walls; glass walls pass CATEGORY_GLASS so sight
    // raycasts (which mask STATIC only) pass through while physical collision still applies.
    shape_def.filter.categoryBits = category;
    shape_def.filter.maskBits = MASK_STATIC;
    b2CreatePolygonShape(body.body_id, &shape_def, &poly);
    body.valid = true;
    return body;
}

PhysicsBody physics_create_static_chain(PhysicsWorld* world, const Vector2* verts, int count, bool loop) {
    PhysicsBody body = {0};

    // A looped outline repeats its first point at the end; Box2D closes the loop implicitly, so
    // drop the duplicate before counting.
    int n = count;
    if (loop && n >= 2 &&
        std::fabs(verts[0].x - verts[n - 1].x) < 1e-4f &&
        std::fabs(verts[0].y - verts[n - 1].y) < 1e-4f) {
        n -= 1;
    }
    if (n < 4) return body;  // Box2D chains require >= 4 points

    std::vector<b2Vec2> pts(n);
    for (int i = 0; i < n; i++) pts[i] = (b2Vec2){verts[i].x, verts[i].y};

    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = (b2Vec2){0.0f, 0.0f};
    body.body_id = b2CreateBody(world->world_id, &body_def);

    b2ChainDef chain_def = b2DefaultChainDef();
    chain_def.points = pts.data();
    chain_def.count = n;
    chain_def.isLoop = loop;
    chain_def.filter.categoryBits = CATEGORY_STATIC;
    chain_def.filter.maskBits = MASK_STATIC;
    b2CreateChain(body.body_id, &chain_def);
    body.valid = true;
    return body;
}

PhysicsBody physics_create_static_segment(PhysicsWorld* world, Vector2 a, Vector2 b) {
    PhysicsBody body = {0};
    if (std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f) return body;  // zero length

    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = (b2Vec2){0.0f, 0.0f};  // endpoints are absolute world coords
    body.body_id = b2CreateBody(world->world_id, &body_def);

    b2Segment seg = {{a.x, a.y}, {b.x, b.y}};
    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.filter.categoryBits = CATEGORY_STATIC;
    shape_def.filter.maskBits = MASK_STATIC;
    b2CreateSegmentShape(body.body_id, &shape_def, &seg);
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
