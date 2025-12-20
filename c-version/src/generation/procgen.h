#ifndef PROCGEN_H
#define PROCGEN_H

#include "raylib.h"
#include "entities/entity.h"

typedef struct Game Game;

void procgen_generate_level(Game* game);
Entity* procgen_spawn_entity(Game* game, EntityType type, Vector3 pos, const char* model_path);

#endif
