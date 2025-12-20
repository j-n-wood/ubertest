#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "physics/physics_world.h"
#include "graphics/renderer.h"
#include "entities/entity.h"
#include "input/input.h"

#define MAX_ENTITIES 1024

typedef struct Game {
    PhysicsWorld physics;
    Renderer renderer;
    Input input;
    Entity entities[MAX_ENTITIES];
    int entity_count;
    Camera3D camera;
    bool running;
} Game;

void game_init(Game* game);
void game_update(Game* game, float dt);
void game_render(Game* game);
void game_destroy(Game* game);

#endif
