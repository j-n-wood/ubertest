#ifndef SHARED_LIGHT_H
#define SHARED_LIGHT_H

#include "raylib.h"

// Light type constants
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1

// Maximum lights supported
#define MAX_LIGHTS 4

// Light structure compatible with standard lighting shader
typedef struct Light {
    int type;
    bool enabled;
    Vector3 position;
    Vector3 target;
    Color color;
    int enabledLoc;
    int typeLoc;
    int positionLoc;
    int targetLoc;
    int colorLoc;
} Light;

// Create and initialize a light, binding it to shader uniforms
Light create_light(int type, Vector3 position, Vector3 target, Color color, Shader shader, int index);

#endif // SHARED_LIGHT_H
