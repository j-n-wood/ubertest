#ifndef VIEWER_H
#define VIEWER_H

#include "raylib.h"
#include "rendering/scene_renderer.h"
#include "rendering/texture_loader.h"
#include "rendering/tile_mesh.h"
#include "rendering/geometry_mesh.h"
#include "rendering/wall_mesh.h"
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
    bool showTiles;
    bool showGeometry;
    bool showWireframe;
    bool showHelp;
    bool showTileIndices;  // Debug: show tile index at centroid
    bool backfaceCulling;  // Debug: toggle backface culling
    bool showValidation;   // Show the validity report panel
    bool showUnitRef;      // Show the class-14 reference unit
    bool enableCaps;       // Generate wall end caps (rebuild on change)
    bool enableMiter;      // Generate wall corner miter joins (rebuild on change)
    bool showNodes;        // Draw path-node markers (spheres) + id labels for diagnosis
    bool showWaypoints;    // Draw AI waypoint graph (spheres + neighbor edges) + id labels
    bool showWallLinks;    // Draw wall-link centrelines + id/direction/bezier labels (UV diagnosis)
    bool showCollision;    // Draw wall collision footprint quads as wireframe (shared with the game)
};

// Validity report for the currently loaded level (geometry + collision + textures).
struct ValidationReport {
    bool run = false;
    // Counts
    int areas = 0;
    int floorMeshes = 0;
    int tileBatches = 0;
    int triangles = 0;
    int collisionPolys = 0;
    int collisionChains = 0;
    // Problems
    int emptyAreas = 0;         // areas that produced no mesh
    int degenerateTris = 0;     // zero-area triangles
    int nonFiniteVerts = 0;     // NaN/Inf positions
    int oversizePolys = 0;      // collision polys > 8 verts (invalid for Box2D)
    int openChains = 0;         // collision chains that don't loop
    int missingTextures = 0;    // referenced texture indices with no file
    bool boundsFinite = true;
    std::vector<std::string> warnings;
    bool ok() const { return warnings.empty(); }
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

    // Tile mesh from converted data
    TileMeshState tileMesh;

    // Geometry mesh from PathGeometry areas (floor polygons)
    GeometryMeshState geometryMesh;

    // Loaded domain data (after JSON reload)
    Domain loadedDomain;
    bool domainLoaded;

    // Ground grid extent (render space), fit to the loaded level footprint on each rebuild.
    // Level geometry sits in render +X / -Z (game +x/+y), so a symmetric origin grid misses it.
    Vector3 gridMin = {0, 0, 0};
    Vector3 gridMax = {0, 0, 0};
    bool gridFit = false;

    // Texture system
    TextureLookup textureLookup;
    TextureCache textureCache;
    bool texturesLoaded;

    // Wall generation (swept link profiles) from materials.xml.
    WallProfileTable wallProfiles;
    std::string materialsPath;   // path to materials.xml

    // Link inspector (raygui): live-edit link profiles / direction and rebuild.
    bool showInspector = false;
    Vector2 inspectorScroll{0, 0};

    // Node editing: click a node marker (with the N overlay on) to select it, then the arrow keys move
    // it in the floor plane (PageUp/Dn = height); F10 saves. -1 = nothing selected.
    int selectedNodeId = -1;

    // Edited-copy workflow: saved JSON decks live in saveDir (originals untouched); once a deck is
    // saved there, loads prefer it. `loadedFromEdited` reflects the current deck's source.
    std::string saveDir;
    bool loadedFromEdited = false;
    // Inspector "add link" node fields (edited via GuiValueBox).
    int addLinkStart = 0;
    int addLinkFinish = 0;
    bool addEditStart = false;
    bool addEditFinish = false;

    // Paths
    std::string outputDir;
    std::string exportDir;     // 'X' key combined-bundle target (default <outputDir>/export; set to the
                               // game's levels3d to edit-and-save straight into the game assets)
    std::string texturesPath;  // Path to textures.txt
    std::string texturesBasePath;  // Base path for texture files
    float scale;

    // Level cycling: the source directory holds xmapfile{N}.txt for each deck.
    std::string sourceDir;          // directory containing xmapfile{N}.txt
    std::string tilesPath;          // tiles.txt (for archetile expansion)
    std::vector<int> levelNumbers;  // available deck numbers, sorted
    int currentLevelIdx;            // index into levelNumbers (-1 = none loaded)

    // Validity report for the loaded level.
    ValidationReport validation;

    // Class-14 reference unit (size reference). Held via opaque owner to keep unit/box2d deps
    // out of this header; created lazily on first toggle.
    struct UnitRef* unitRef;

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

// Rebuild all render meshes (tiles + floors + walls) from the current in-memory loadedDomain,
// without reloading from disk or moving the camera. Call after live edits (link inspector).
void viewerRebuildMeshes(Viewer* viewer);

// Draw the link inspector panel (raygui): per-link profile checkboxes + reverse button. Editing a
// row mutates loadedDomain and rebuilds meshes. Call in the 2D pass. No-op unless showInspector.
void viewerDrawInspector(Viewer* viewer);

// Scan `sourceDir` for xmapfile{N}.txt and remember the tiles path, enabling in-app level
// cycling. Records available deck numbers; does not load anything yet.
void viewerScanLevels(Viewer* viewer, const char* sourceDir, const char* tilesPath);

// Load a deck by its number (xmapfile{level}.txt in sourceDir): convert, load, validate.
// Returns true on success.
bool viewerLoadLevel(Viewer* viewer, int levelNumber);

// Step to the next/previous available deck (wraps). No-op if none scanned.
void viewerCycleLevel(Viewer* viewer, int delta);

// Recompute the validity report for the currently loaded level.
void viewerValidate(Viewer* viewer);

// Print the per-link profile assignment for the loaded deck to the console (which profiles each
// wall link gets and the trim side), to aid manual XML edits.
void viewerDumpProfiles(Viewer* viewer);

// Save the current (edited) deck domain as JSON into saveDir/level_<n>.json. Originals are never
// touched; subsequent loads of this deck prefer the saved copy. Returns true on success.
bool viewerSaveEdited(Viewer* viewer);

// Revert the current deck to the original: delete its saved edit (if any) and reload from XML.
bool viewerRevertToOriginal(Viewer* viewer);

// Save the whole ship as JSON (current edits + a JSON for every deck lacking one). Returns count.
int viewerSaveAll(Viewer* viewer);

// Toggle the class-14 reference unit (created lazily). No-op if unit assets missing.
void viewerToggleUnitRef(Viewer* viewer);

// Recentre the class-14 reference droid on the current (fitted) grid midpoint. No-op if not built.
void viewerUpdateUnitRefPosition(Viewer* viewer);

// Draw the class-14 reference unit; call inside BeginMode3D. No-op if not created/visible.
void viewerRenderUnitRef(Viewer* viewer);

// Tear down the reference unit (bodies, models, world). Safe if never created.
void viewerDestroyUnitRef(Viewer* viewer);

// Export the current level (geometry + tiles GLTF, manifest, collision) into `dir`.
// Returns true on success. Used by the 'X' key and headless --export-all.
bool viewerExportLevel(Viewer* viewer, const char* dir);

// Split export: one .gltf per shape (floor/wall/tile) named by kind+index, plus an _index.json
// with per-file bounds — for isolating which sections have bad geometry. 'Shift+X' / --export-split.
bool viewerExportLevelSplit(Viewer* viewer, const char* dir);

// Export the ship-wide transporter (lift) network to `<dir>/transporters.json` in the render-metric
// frame (positions transformed by gameToRenderCoords, matching the level GLTFs). Ship-level, so
// written once for the whole deck set, not per level. Returns true on success.
bool viewerExportTransporters(const std::vector<Transporter>& transporters, float scale, const char* dir);

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
