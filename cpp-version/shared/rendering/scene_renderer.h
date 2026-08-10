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
    int effectiveEyeHeightLoc;
    int useEnvMapLoc;       // Per-material env-map toggle (set during unit draw loop)
    int envIntensityLoc;    // Per-material env-map additive strength
    int darknessLoc;        // Lights-out scene dimming (0 = full brightness, 1 = black)

    // Current settings
    int debugMode;
    float ambient[4];
    float specularPower;
    float specularIntensity;
    bool useNormalMap;
    float bumpIntensity;
    float effectiveEyeHeight;  // Height above ground for specular calc (-1 = use camera Y)

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

// Set the lights-out dimming applied to the whole lit scene (0 = full brightness, 1 = black).
void sceneRendererSetDarkness(SceneRenderer* renderer, float darkness);

// Set specular properties
void sceneRendererSetSpecular(SceneRenderer* renderer, float power, float intensity);

// Set debug visualization mode (0 = normal, 1-6 = debug modes)
// Mode 6 shows the raw normal map texture
void sceneRendererSetDebugMode(SceneRenderer* renderer, int mode);

// Enable/disable normal/bump mapping
void sceneRendererSetNormalMapEnabled(SceneRenderer* renderer, bool enabled);

// Set bump intensity (default 1.0, higher = stronger bump effect)
void sceneRendererSetBumpIntensity(SceneRenderer* renderer, float intensity);

// Set effective eye height for specular calculations
// height >= 0: Use this height above ground plane instead of camera Y
// height < 0: Use actual camera Y position (default behavior)
void sceneRendererSetEffectiveEyeHeight(SceneRenderer* renderer, float height);

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

// Env-map uniform locations (set per-material during the unit draw loop). -1 if unavailable.
int sceneRendererGetUseEnvMapLoc(SceneRenderer* renderer);
int sceneRendererGetEnvIntensityLoc(SceneRenderer* renderer);

#endif // SCENE_RENDERER_H
