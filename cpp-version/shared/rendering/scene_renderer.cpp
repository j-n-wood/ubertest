#include "scene_renderer.h"
#include <cstdio>
#include <cstring>

//------------------------------------------------------------------------------
// Initialize the scene renderer with standard lighting shader
//------------------------------------------------------------------------------
bool sceneRendererInit(SceneRenderer* renderer, const char* shaderPath) {
    if (!renderer) return false;

    // Reset state
    memset(renderer, 0, sizeof(SceneRenderer));

    // Build shader paths
    char vsPath[256];
    char fsPath[256];
    snprintf(vsPath, sizeof(vsPath), "%slighting.vs", shaderPath);
    snprintf(fsPath, sizeof(fsPath), "%slighting.fs", shaderPath);

    // Load lighting shader
    renderer->shader = LoadShader(vsPath, fsPath);

    if (!IsShaderValid(renderer->shader)) {
        TraceLog(LOG_ERROR, "SceneRenderer: Failed to load shaders from %s", shaderPath);
        return false;
    }

    // Get shader locations
    renderer->shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(renderer->shader, "viewPos");
    renderer->shader.locs[SHADER_LOC_COLOR_SPECULAR] = GetShaderLocation(renderer->shader, "colSpecular");
    renderer->ambientLoc = GetShaderLocation(renderer->shader, "ambient");
    renderer->debugModeLoc = GetShaderLocation(renderer->shader, "debugMode");
    renderer->specPowerLoc = GetShaderLocation(renderer->shader, "specularPower");
    renderer->specIntensityLoc = GetShaderLocation(renderer->shader, "specularIntensity");

    // Set default values
    renderer->debugMode = 0;
    renderer->ambient[0] = 0.1f;
    renderer->ambient[1] = 0.1f;
    renderer->ambient[2] = 0.1f;
    renderer->ambient[3] = 1.0f;
    renderer->specularPower = 32.0f;
    renderer->specularIntensity = 0.5f;
    renderer->lightCount = 0;

    // Apply defaults to shader
    SetShaderValue(renderer->shader, renderer->ambientLoc, renderer->ambient, SHADER_UNIFORM_VEC4);
    SetShaderValue(renderer->shader, renderer->debugModeLoc, &renderer->debugMode, SHADER_UNIFORM_INT);
    SetShaderValue(renderer->shader, renderer->specPowerLoc, &renderer->specularPower, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->shader, renderer->specIntensityLoc, &renderer->specularIntensity, SHADER_UNIFORM_FLOAT);

    renderer->initialized = true;
    return true;
}

//------------------------------------------------------------------------------
// Destroy the scene renderer and unload shader
//------------------------------------------------------------------------------
void sceneRendererDestroy(SceneRenderer* renderer) {
    if (!renderer || !renderer->initialized) return;

    UnloadShader(renderer->shader);
    renderer->initialized = false;
}

//------------------------------------------------------------------------------
// Update camera position for specular calculations
//------------------------------------------------------------------------------
void sceneRendererUpdateCamera(SceneRenderer* renderer, Vector3 cameraPos) {
    if (!renderer || !renderer->initialized) return;

    float pos[3] = {cameraPos.x, cameraPos.y, cameraPos.z};
    SetShaderValue(renderer->shader, renderer->shader.locs[SHADER_LOC_VECTOR_VIEW], pos, SHADER_UNIFORM_VEC3);
}

//------------------------------------------------------------------------------
// Apply the lighting shader to a model's materials
//------------------------------------------------------------------------------
void sceneRendererApplyShader(SceneRenderer* renderer, Model* model) {
    if (!renderer || !renderer->initialized || !model) return;

    for (int i = 0; i < model->materialCount; i++) {
        model->materials[i].shader = renderer->shader;
    }
}

//------------------------------------------------------------------------------
// Set ambient light level
//------------------------------------------------------------------------------
void sceneRendererSetAmbient(SceneRenderer* renderer, float r, float g, float b, float a) {
    if (!renderer || !renderer->initialized) return;

    renderer->ambient[0] = r;
    renderer->ambient[1] = g;
    renderer->ambient[2] = b;
    renderer->ambient[3] = a;
    SetShaderValue(renderer->shader, renderer->ambientLoc, renderer->ambient, SHADER_UNIFORM_VEC4);
}

//------------------------------------------------------------------------------
// Set specular properties
//------------------------------------------------------------------------------
void sceneRendererSetSpecular(SceneRenderer* renderer, float power, float intensity) {
    if (!renderer || !renderer->initialized) return;

    renderer->specularPower = power;
    renderer->specularIntensity = intensity;
    SetShaderValue(renderer->shader, renderer->specPowerLoc, &renderer->specularPower, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->shader, renderer->specIntensityLoc, &renderer->specularIntensity, SHADER_UNIFORM_FLOAT);
}

//------------------------------------------------------------------------------
// Set debug visualization mode
//------------------------------------------------------------------------------
void sceneRendererSetDebugMode(SceneRenderer* renderer, int mode) {
    if (!renderer || !renderer->initialized) return;

    renderer->debugMode = mode;
    SetShaderValue(renderer->shader, renderer->debugModeLoc, &renderer->debugMode, SHADER_UNIFORM_INT);
}

//------------------------------------------------------------------------------
// Add a directional light
// For directional lights: lightDir = normalize(target - position)
// So position should be where light shines FROM and target where it shines TO
// Example: position={0,0,0}, target={0,50,0} gives lightDir pointing UP (light from above)
//------------------------------------------------------------------------------
int sceneRendererAddDirectionalLight(SceneRenderer* renderer, Vector3 position, Vector3 target, Color color) {
    if (!renderer || !renderer->initialized) return -1;
    if (renderer->lightCount >= MAX_LIGHTS) return -1;

    int index = renderer->lightCount;
    renderer->lights[index] = create_light(LIGHT_DIRECTIONAL, position, target, color, renderer->shader, index);
    renderer->lightCount++;
    return index;
}

//------------------------------------------------------------------------------
// Add a point light
//------------------------------------------------------------------------------
int sceneRendererAddPointLight(SceneRenderer* renderer, Vector3 position, Color color) {
    if (!renderer || !renderer->initialized) return -1;
    if (renderer->lightCount >= MAX_LIGHTS) return -1;

    int index = renderer->lightCount;
    // For point lights, target is not used (position is the light location)
    renderer->lights[index] = create_light(LIGHT_POINT, position, position, color, renderer->shader, index);
    renderer->lightCount++;
    return index;
}

//------------------------------------------------------------------------------
// Enable/disable a light by index
//------------------------------------------------------------------------------
void sceneRendererSetLightEnabled(SceneRenderer* renderer, int index, bool enabled) {
    if (!renderer || !renderer->initialized) return;
    if (index < 0 || index >= renderer->lightCount) return;

    renderer->lights[index].enabled = enabled;
    int val = enabled ? 1 : 0;
    SetShaderValue(renderer->shader, renderer->lights[index].enabledLoc, &val, SHADER_UNIFORM_INT);
}

//------------------------------------------------------------------------------
// Get the shader
//------------------------------------------------------------------------------
Shader sceneRendererGetShader(SceneRenderer* renderer) {
    if (!renderer || !renderer->initialized) {
        return (Shader){0};
    }
    return renderer->shader;
}
