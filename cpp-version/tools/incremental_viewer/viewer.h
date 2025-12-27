#ifndef VIEWER_H
#define VIEWER_H

#include "raylib.h"
#include "rendering/scene_renderer.h"
#include "rendering/texture_loader.h"
#include "rendering/tile_mesh.h"
#include "rendering/geometry_mesh.h"
#include "scene_convert/scene_types.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Incremental Scene Viewer - validates scene data conversion with GLTF reference
//------------------------------------------------------------------------------

// Camera preset modes
enum class CameraPreset {
    TopDown,    // Game mode - looking straight down
    Isometric,  // 45 degree angle for 3D validation
    Perspective // Free perspective view
};

// Display toggles
struct ViewerToggles {
    bool showGrid;
    bool showReference;
    bool showTiles;
    bool showGeometry;
    bool showWireframe;
    bool showHelp;
    bool showTileIndices;  // Debug: show tile index at centroid
    bool backfaceCulling;  // Debug: toggle backface culling
};

// Batched tile mesh state (one model per texture group)
struct TileBatchState {
    Model model;
    int textureIndex1;  // Diffuse texture
    int textureIndex2;  // Bump texture
    int tileCount;
    int triangleCount;
    bool valid;
};

// Tile mesh state
struct TileMeshState {
    std::vector<TileBatchState> batches;  // Batched by texture
    Model model;                           // Legacy single model (for fallback)
    bool loaded;
    int triangleCount;
    int tileCount;
    Vector3 boundsMin;
    Vector3 boundsMax;
};

// Viewer state
struct Viewer {
    SceneRenderer renderer;
    Camera3D camera;

    // Reference model (Suzanne at -1, 0, -1)
    Model referenceModel;
    bool referenceLoaded;
    Vector3 referencePosition;

    // Tile mesh from converted data
    TileMeshState tileMesh;

    // Geometry mesh from PathGeometry areas (floor polygons)
    GeometryMeshState geometryMesh;

    // Loaded domain data (after JSON reload)
    Domain loadedDomain;
    bool domainLoaded;

    // Texture system
    TextureLookup textureLookup;
    TextureCache textureCache;
    bool texturesLoaded;

    // Paths
    std::string outputDir;
    std::string texturesPath;  // Path to textures.txt
    std::string texturesBasePath;  // Base path for texture files
    float scale;

    // Display state
    ViewerToggles toggles;
    CameraPreset cameraPreset;

    // Camera movement
    float moveSpeed;
    float zoomSpeed;

    // State
    bool initialized;
};

// Initialize the viewer with shaders from the given path
// shaderPath: path to shaders directory (e.g., "assets/shaders/")
// Returns true on success
bool viewerInit(Viewer* viewer, const char* shaderPath);

// Load the reference model (Suzanne.glb)
// modelPath: path to the reference model file
// Returns true on success
bool viewerLoadReference(Viewer* viewer, const char* modelPath);

// Load textures.txt for texture lookup
// texturesPath: path to textures.txt
// basePath: base directory for resolving texture paths
// Returns true on success
bool viewerLoadTextures(Viewer* viewer, const char* texturesPath, const char* basePath);

// Convert source file to JSON and reload
// sourcePath: path to xmapfile0.txt or similar
// tilesPath: path to tiles.txt for archetile expansion
// outputDir: directory to write JSON output
// scale: scale factor for coordinates
// Returns true on success
bool viewerConvertAndLoad(Viewer* viewer, const char* sourcePath,
                          const char* tilesPath, const char* outputDir, float scale);

// Reload domain from JSON (after manual edits)
// jsonPath: path to domain JSON file
// Returns true on success
bool viewerReloadFromJson(Viewer* viewer, const char* jsonPath);

// Set camera to a preset view
// preset: TopDown (game mode), Isometric (45 degree), or Perspective
void viewerSetCameraPreset(Viewer* viewer, CameraPreset preset);

// Update viewer state (camera controls, input handling)
// deltaTime: frame time in seconds
void viewerUpdate(Viewer* viewer, float deltaTime);

// Render the 3D scene
void viewerRender(Viewer* viewer);

// Draw 2D overlay (HUD, help text)
void viewerDrawOverlay(Viewer* viewer);

// Cleanup and release resources
void viewerCleanup(Viewer* viewer);

#endif // VIEWER_H
