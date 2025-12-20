#include "light.h"
#include <cstdio>

Light create_light(int type, Vector3 position, Vector3 target, Color color, Shader shader, int index) {
    Light light = {0};
    light.enabled = true;
    light.type = type;
    light.position = position;
    light.target = target;
    light.color = color;

    char name[32];

    snprintf(name, sizeof(name), "light%d_enabled", index);
    light.enabledLoc = GetShaderLocation(shader, name);

    snprintf(name, sizeof(name), "light%d_type", index);
    light.typeLoc = GetShaderLocation(shader, name);

    snprintf(name, sizeof(name), "light%d_position", index);
    light.positionLoc = GetShaderLocation(shader, name);

    snprintf(name, sizeof(name), "light%d_target", index);
    light.targetLoc = GetShaderLocation(shader, name);

    snprintf(name, sizeof(name), "light%d_color", index);
    light.colorLoc = GetShaderLocation(shader, name);

    // Set initial values
    int enabled = light.enabled ? 1 : 0;
    SetShaderValue(shader, light.enabledLoc, &enabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, light.typeLoc, &light.type, SHADER_UNIFORM_INT);
    SetShaderValue(shader, light.positionLoc, &light.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light.targetLoc, &light.target, SHADER_UNIFORM_VEC3);

    float color_normalized[4] = {
        light.color.r / 255.0f,
        light.color.g / 255.0f,
        light.color.b / 255.0f,
        light.color.a / 255.0f
    };
    SetShaderValue(shader, light.colorLoc, color_normalized, SHADER_UNIFORM_VEC4);

    return light;
}
