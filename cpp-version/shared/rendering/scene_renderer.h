#ifndef SCENE_RENDERER_H
#define SCENE_RENDERER_H

#include "raylib.h"
#include "lighting/light.h"

//------------------------------------------------------------------------------
// Scene Renderer - Standard lighting and rendering setup for all tools
//------------------------------------------------------------------------------

// Scene renderer state
struct SceneRenderer {
    Shader shader;
    Light lights[MAX_LIGHTS];
    int lightCount;

    // Shader uniform locations
    int ambientLoc;
    int debugModeLoc;
    int specPowerLoc;
    int specIntensityLoc;
    int useNormalMapLoc;
    int bumpIntensityLoc;

    // Current settings
    int debugMode;
    float ambient[4];
    float specularPower;
    float specularIntensity;
    bool useNormalMap;
    float bumpIntensity;

    // Default normal map for materials without bump textures
    Texture2D defaultNormalMap;
    bool defaultNormalMapLoaded;

    // State
    bool initialized;
};

// Initialize the scene renderer with standard lighting shader
// shaderPath: path to shaders directory (e.g., "shaders/" or "../shaders/")
// Returns true on success
bool sceneRendererInit(SceneRenderer* renderer, const char* shaderPath);

// Destroy the scene renderer and unload shader
void sceneRendererDestroy(SceneRenderer* renderer);

// Update camera position for specular calculations (call before rendering)
void sceneRendererUpdateCamera(SceneRenderer* renderer, Vector3 cameraPos);

// Apply the lighting shader to a model's materials
void sceneRendererApplyShader(SceneRenderer* renderer, Model* model);

// Set ambient light level (0.0 - 1.0)
void sceneRendererSetAmbient(SceneRenderer* renderer, float r, float g, float b, float a);

// Set specular properties
void sceneRendererSetSpecular(SceneRenderer* renderer, float power, float intensity);

// Set debug visualization mode (0 = normal, 1-6 = debug modes)
// Mode 6 shows the raw normal map texture
void sceneRendererSetDebugMode(SceneRenderer* renderer, int mode);

// Enable/disable normal/bump mapping
void sceneRendererSetNormalMapEnabled(SceneRenderer* renderer, bool enabled);

// Set bump intensity (default 1.0, higher = stronger bump effect)
void sceneRendererSetBumpIntensity(SceneRenderer* renderer, float intensity);

// Add a directional light (shining from position toward target)
// Returns light index or -1 if max lights reached
int sceneRendererAddDirectionalLight(SceneRenderer* renderer, Vector3 position, Vector3 target, Color color);

// Add a point light at position
// Returns light index or -1 if max lights reached
int sceneRendererAddPointLight(SceneRenderer* renderer, Vector3 position, Color color);

// Enable/disable a light by index
void sceneRendererSetLightEnabled(SceneRenderer* renderer, int index, bool enabled);

// Get the shader (for direct access if needed)
Shader sceneRendererGetShader(SceneRenderer* renderer);

#endif // SCENE_RENDERER_H
