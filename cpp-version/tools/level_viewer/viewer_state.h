#ifndef VIEWER_STATE_H
#define VIEWER_STATE_H

#include "level/level_types.h"
#include "level/tmx_loader.h"
#include "level/tileset_loader.h"
#include "level/level_renderer.h"
#include "rendering/scene_renderer.h"
#include "raylib.h"
#include <vector>
#include <string>

//------------------------------------------------------------------------------
// Level Viewer State
//
// Manages loaded levels, rendering state, and UI configuration.
//------------------------------------------------------------------------------

struct LevelViewerState {
    // Loaded levels
    std::vector<TmxLevel> levels;
    std::vector<LevelRenderData> renderData;
    int currentLevel = 0;

    // Tileset (shared across levels)
    TmxTileset tileset;
    Texture2D atlasTexture = {0};
    Texture2D bumpTexture = {0};          // Flat normal (for Tilemap mode)
    Texture2D bumpAtlasTexture = {0};     // Bump atlas (for CustomTiles mode)

    // Custom tile properties (from tiles.json)
    TilePropertiesConfig tileProperties;

    // Rendering
    SceneRenderer renderer;
    LevelRenderMode renderMode = LevelRenderMode::CustomTiles;
    float worldScale = 1.0f;

    // Reference geometry
    Model refSphereModel = {0};
    bool refSphereValid = false;

    // Camera state
    Camera3D camera = {0};
    float cameraOrbitAngle = 0.0f;
    float cameraOrbitDistance = 20.0f;
    float cameraHeight = 5.0f;
    Vector3 cameraTarget = {0, 0, 0};

    // Camera view modes
    enum class CameraMode { Perspective, Topdown, Isometric };
    CameraMode cameraMode = CameraMode::Topdown;

    // Effective eye height for specular calculations
    // Allows top-down camera to simulate lighting as seen from within the scene
    float effectiveEyeHeight = 1.0f;  // Default: 1 tile width above ground

    // Debug visualization
    int debugMode = 0;
    bool showWaypoints = true;
    bool showWaypointLinks = true;
    bool showGrid = false;
    bool showBounds = false;
    bool backfaceCulling = true;
    bool showRefSphere = false;

    // UI
    bool showHUD = true;
    bool autoRotate = false;

    // Paths
    std::string inputPath;
    std::string assetPath;
    std::string shadersPath;
};

//------------------------------------------------------------------------------
// State Management
//------------------------------------------------------------------------------

// Initialize viewer state with paths
bool viewerStateInit(LevelViewerState* state, const std::string& inputPath,
                     const std::string& assetPath);

// Clean up all resources
void viewerStateDestroy(LevelViewerState* state);

// Load levels from the input directory
bool viewerStateLoadLevels(LevelViewerState* state);

// Build render data for current level
bool viewerStateBuildRenderData(LevelViewerState* state);

// Switch to a different level
bool viewerStateSwitchLevel(LevelViewerState* state, int levelIndex);

// Switch render mode (rebuilds geometry for current level)
void viewerStateSwitchRenderMode(LevelViewerState* state, LevelRenderMode mode);

// Cycle to next render mode
void viewerStateCycleRenderMode(LevelViewerState* state);

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

// Draw the current level
void viewerStateDrawLevel(LevelViewerState* state);

// Draw waypoints and links
void viewerStateDrawWaypoints(LevelViewerState* state);

// Draw reference sphere at origin
void viewerStateDrawRefSphere(LevelViewerState* state);

// Draw bounds visualization
void viewerStateDrawBounds(LevelViewerState* state);

// Draw HUD overlay
void viewerStateDrawHUD(LevelViewerState* state);

//------------------------------------------------------------------------------
// Camera
//------------------------------------------------------------------------------

// Update camera based on current mode
void viewerStateUpdateCamera(LevelViewerState* state);

// Center camera on current level
void viewerStateCenterCamera(LevelViewerState* state);

#endif // VIEWER_STATE_H
