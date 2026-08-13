#ifndef PHYSICS_WORLD_H
#define PHYSICS_WORLD_H

#include "box2d/box2d.h"
#include "raylib.h"
#include "physics/body_user_data.h"   // CATEGORY_STATIC / CATEGORY_GLASS (default arg below)

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

// Static collision from a converted 3D level's collision.json (Objects3D). Vertices are absolute
// world coords in the 2D physics plane (render X, render Z), already metric. The body sits at the
// origin so the vertices are used as-is.
//   polygon: CCW convex, 3..8 verts (a solid wall block). Invalid/degenerate -> {valid=false}.
//   chain:   an open or looped wall outline; Box2D requires >= 4 points, so shorter chains and
//            degenerate loops are rejected with {valid=false}.
// `category` selects the collision category: CATEGORY_STATIC (default) for normal walls, or
// CATEGORY_GLASS for LOS-transparent glass walls (see body_user_data.h / game_create_level_collision).
PhysicsBody physics_create_static_polygon(PhysicsWorld* world, const Vector2* verts, int count,
                                          uint16_t category = CATEGORY_STATIC);
PhysicsBody physics_create_static_chain(PhysicsWorld* world, const Vector2* verts, int count, bool loop);

// A single static wall segment (a..b) in the 2D physics plane. Zero-length segments -> {valid=false}.
// Used for the converted level's walls, which are individual profiled edges (2-3 points each).
PhysicsBody physics_create_static_segment(PhysicsWorld* world, Vector2 a, Vector2 b);

void physics_body_apply_force(PhysicsBody* body, Vector2 force);
void physics_body_apply_torque(PhysicsBody* body, float torque);
float physics_body_get_angular_velocity(PhysicsBody* body);
Vector2 physics_body_get_position(PhysicsBody* body);
float physics_body_get_angle(PhysicsBody* body);

#endif
