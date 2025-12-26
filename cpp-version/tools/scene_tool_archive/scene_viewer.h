#ifndef SCENE_VIEWER_H
#define SCENE_VIEWER_H

#include "scene_types.h"
#include "rendering/scene_renderer.h"
#include "raylib.h"
#include "box2d/box2d.h"
#include <vector>
#include <string>

//------------------------------------------------------------------------------
// Scale Factors
//------------------------------------------------------------------------------
// Original data uses 64-unit tiles. GLTF conversion assumed 1 unit = 1 inch
// and converted to meters. Tile size is approximately 1-1.5m.
// These factors can be adjusted to match the expected rendering scale.

struct SceneScaleConfig {
    float geometryScale = 1.0f;    // Scale factor for geometry during mesh generation
    float renderScale = 1.0f;      // Scale factor applied at render time
    float cameraHeightFactor = 0.8f; // Camera height as factor of domain size
    float debugDrawHeight = 5.0f;  // Height offset for debug wireframes
    float waypointRadius = 8.0f;   // Radius for waypoint debug spheres
    float waypointHeight = 10.0f;  // Height offset for waypoints
};

//------------------------------------------------------------------------------
// Debug Statistics
//------------------------------------------------------------------------------

struct SceneDebugStats {
    // Mesh generation stats
    int totalTiles = 0;
    int totalTriangles = 0;
    int totalVertices = 0;
    int meshesGenerated = 0;
    int emptyAreas = 0;

    // Domain stats
    int areaCount = 0;
    int featureCount = 0;
    int waypointCount = 0;
    int collisionPolygons = 0;
    int collisionChains = 0;
    int physicsBodies = 0;

    // Bounds (in original units)
    float boundsMinX = 0, boundsMinY = 0;
    float boundsMaxX = 0, boundsMaxY = 0;

    void reset() {
        totalTiles = totalTriangles = totalVertices = meshesGenerated = emptyAreas = 0;
        areaCount = featureCount = waypointCount = 0;
        collisionPolygons = collisionChains = physicsBodies = 0;
        boundsMinX = boundsMinY = boundsMaxX = boundsMaxY = 0;
    }
};

//------------------------------------------------------------------------------
// Scene Viewer
//------------------------------------------------------------------------------

struct SceneViewer {
    // Rendering
    SceneRenderer renderer;
    Camera3D camera;

    // Physics world
    b2WorldId worldId;
    std::vector<b2BodyId> staticBodies;

    // Scene data
    Ship ship;
    int currentDomainIndex;
    bool domainLoaded;
    std::string basePath;  // Base path for resolving domain paths

    // Generated meshes for current domain
    std::vector<Model> tileModels;
    std::vector<Model> featureModels;

    // Reference model for rendering comparison (optional)
    Model referenceModel;
    bool referenceModelLoaded;
    bool showReferenceModel;

    // Scale configuration
    SceneScaleConfig scaleConfig;

    // Debug statistics
    SceneDebugStats debugStats;
    bool showDebugStats;

    // Display options
    bool showPhysics;
    bool showWaypoints;
    bool showObjects;
    bool showTiles;
    bool showFeatures;
    bool showGeometry;
    bool useShader;           // F6 - Toggle shader vs no-shader rendering
    int shaderDebugMode;      // 0-5 shader debug modes (same as model_tool)
    bool backfaceCulling;     // F7 - Toggle backface culling

    // UI state
    bool showHelp;
    std::string statusMessage;
    float statusTimer;
};

// Initialize the scene viewer
bool sceneViewerInit(SceneViewer* viewer, const char* shaderPath);

// Cleanup the scene viewer
void sceneViewerCleanup(SceneViewer* viewer);

// Load a ship from JSON
bool sceneViewerLoadShip(SceneViewer* viewer, const std::string& jsonPath);

// Load a domain from JSON
bool sceneViewerLoadDomain(SceneViewer* viewer, const std::string& jsonPath);

// Switch to a different domain (by index)
bool sceneViewerSwitchDomain(SceneViewer* viewer, int domainIndex);

// Update the viewer (handle input, etc.)
void sceneViewerUpdate(SceneViewer* viewer, float dt);

// Render the scene
void sceneViewerRender(SceneViewer* viewer);

// Draw the 2D overlay (UI, help, etc.)
void sceneViewerDrawOverlay(SceneViewer* viewer);

// Set status message (auto-clears after duration)
void sceneViewerSetStatus(SceneViewer* viewer, const std::string& message, float duration = 3.0f);

// Set scale configuration
void sceneViewerSetScale(SceneViewer* viewer, float geometryScale, float renderScale);

// Print debug statistics to console
void sceneViewerPrintStats(SceneViewer* viewer);

// Load a reference GLTF model for rendering comparison
bool sceneViewerLoadReferenceModel(SceneViewer* viewer, const std::string& modelPath);

#endif // SCENE_VIEWER_H
