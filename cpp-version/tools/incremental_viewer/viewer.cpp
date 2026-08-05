#include "viewer.h"
#include "raymath.h"
#include "rlgl.h"
#include "scene_convert/domain_parser.h"
#include "scene_convert/scene_json.h"
#include "rendering/tile_mesh.h"
#include "rendering/geometry_mesh.h"
#include "rendering/texture_loader.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
static constexpr float DEFAULT_MOVE_SPEED = 10.0f;
static constexpr float DEFAULT_ZOOM_SPEED = 20.0f;
static constexpr float FAST_MULTIPLIER = 3.0f;
static constexpr float MIN_CAMERA_DISTANCE = 1.0f;
static constexpr float MAX_CAMERA_DISTANCE = 100.0f;

// Reference model position (outside expected data range)
static constexpr Vector3 REFERENCE_POSITION = {-1.0f, 0.0f, -1.0f};

//------------------------------------------------------------------------------
// Initialize viewer
//------------------------------------------------------------------------------
bool viewerInit(Viewer* viewer, const char* shaderPath) {
    if (!viewer) return false;

    // Initialize scene renderer with lighting shader
    if (!sceneRendererInit(&viewer->renderer, shaderPath)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to initialize scene renderer from: %s", shaderPath);
        return false;
    }

    // Add directional light from above. The shader computes lightDir = normalize(position - target)
    // (direction TOWARD the light), so the light source must be ABOVE (position +Y) and the target
    // below — otherwise lightDir points down and up-facing floors/tiles get zero diffuse (dark).
    // (This matches the game's setup in game.cpp; previously these were swapped.)
    sceneRendererAddDirectionalLight(&viewer->renderer,
        (Vector3){0, 50, 0},   // Position: light source above
        (Vector3){0, 0, 0},    // Target: below
        WHITE);

    // Set ambient light
    sceneRendererSetAmbient(&viewer->renderer, 0.3f, 0.3f, 0.3f, 1.0f);

    // Initialize camera (orbital view looking at origin)
    viewer->camera.position = (Vector3){5.0f, 8.0f, 5.0f};
    viewer->camera.target = (Vector3){0.0f, 0.0f, 0.0f};
    viewer->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    viewer->camera.fovy = 45.0f;
    viewer->camera.projection = CAMERA_PERSPECTIVE;

    // Initialize reference model state
    viewer->referenceLoaded = false;
    viewer->referencePosition = REFERENCE_POSITION;

    // Initialize tile mesh state
    viewer->tileMesh = {};
    viewer->domainLoaded = false;
    viewer->scale = SCALE_UNITS_TO_METERS;

    // Initialize texture system
    textureCacheInit(viewer->textureCache);
    viewer->texturesLoaded = false;

    // Initialize toggles
    viewer->toggles.showGrid = true;
    viewer->toggles.showReference = true;
    viewer->toggles.showTiles = true;
    viewer->toggles.showGeometry = true;
    viewer->toggles.showWireframe = false;
    viewer->toggles.showHelp = false;
    viewer->toggles.showTileIndices = false;  // Debug tile index overlay
    viewer->toggles.backfaceCulling = true;   // Start with culling enabled
    viewer->toggles.enableCaps = true;        // Wall end caps
    viewer->toggles.enableMiter = true;       // Wall corner miter joins
    viewer->toggles.showNodes = false;        // Path-node markers + labels (diagnosis)
    viewer->cameraPreset = CameraPreset::TopDown;  // Default to game mode

    // Initialize geometry mesh state
    viewer->geometryMesh = {};

    // Movement settings
    viewer->moveSpeed = DEFAULT_MOVE_SPEED;
    viewer->zoomSpeed = DEFAULT_ZOOM_SPEED;

    viewer->initialized = true;
    TraceLog(LOG_INFO, "VIEWER: Initialized successfully");
    return true;
}

//------------------------------------------------------------------------------
// Load reference model
//------------------------------------------------------------------------------
bool viewerLoadReference(Viewer* viewer, const char* modelPath) {
    if (!viewer || !viewer->initialized) return false;

    viewer->referenceModel = LoadModel(modelPath);
    if (viewer->referenceModel.meshCount == 0) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to load reference model: %s", modelPath);
        return false;
    }

    // Apply lighting shader to reference model
    sceneRendererApplyShader(&viewer->renderer, &viewer->referenceModel);

    viewer->referenceLoaded = true;
    TraceLog(LOG_INFO, "VIEWER: Loaded reference model: %s (%d meshes)",
             modelPath, viewer->referenceModel.meshCount);
    return true;
}

//------------------------------------------------------------------------------
// Load textures.txt
//------------------------------------------------------------------------------
bool viewerLoadTextures(Viewer* viewer, const char* texturesPath, const char* basePath) {
    if (!viewer || !viewer->initialized) return false;

    viewer->texturesPath = texturesPath;
    viewer->texturesBasePath = basePath;

    if (!parseTexturesFile(texturesPath, basePath, viewer->textureLookup)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to parse textures file: %s", texturesPath);
        return false;
    }

    // Load flat normal map (texture 0 or assets/textures/flat_normal.png)
    std::string flatNormalPath = getTextureFullPath(viewer->textureLookup, 0);
    if (flatNormalPath.empty() || !fs::exists(flatNormalPath)) {
        // Fall back to local assets
        flatNormalPath = "assets/textures/flat_normal.png";
    }

    if (!textureCacheLoadFlatNormal(viewer->textureCache, flatNormalPath.c_str())) {
        TraceLog(LOG_WARNING, "VIEWER: Could not load flat normal map, bump mapping may not work");
    }

    viewer->texturesLoaded = true;
    TraceLog(LOG_INFO, "VIEWER: Loaded texture lookup with %zu entries",
             viewer->textureLookup.entries.size());
    return true;
}

//------------------------------------------------------------------------------
// Convert source file to JSON and reload
//------------------------------------------------------------------------------
bool viewerConvertAndLoad(Viewer* viewer, const char* sourcePath,
                          const char* tilesPath, const char* outputDir, float scale) {
    if (!viewer || !viewer->initialized) return false;

    viewer->scale = scale;
    viewer->outputDir = outputDir;

    TraceLog(LOG_INFO, "=== CONVERSION STARTED ===");
    TraceLog(LOG_INFO, "Source: %s", sourcePath);
    TraceLog(LOG_INFO, "Tiles: %s", tilesPath);
    TraceLog(LOG_INFO, "Output: %s", outputDir);
    TraceLog(LOG_INFO, "Scale: %.6f", scale);

    // =========================================
    // PHASE A: PARSING & CONVERSION (tool-only)
    // =========================================
    TraceLog(LOG_INFO, "--- PHASE A: PARSING & CONVERSION ---");

    // Parse source file
    Domain sourceDomain;
    fs::path sourceFile(sourcePath);
    fs::path basePath = sourceFile.parent_path();

    if (!parseDomainFile(sourcePath, sourceDomain, basePath, tilesPath)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to parse source file: %s", sourcePath);
        return false;
    }

    // Count tiles
    int totalTiles = 0;
    for (const auto& area : sourceDomain.areas) {
        totalTiles += static_cast<int>(area.tiles.size());
    }
    TraceLog(LOG_INFO, "Parsed %d tiles from %zu areas", totalTiles, sourceDomain.areas.size());

    // =========================================
    // WRITE TO DISK
    // =========================================
    TraceLog(LOG_INFO, "--- WRITING TO DISK ---");

    // Create output directory if needed
    fs::path outPath(outputDir);
    if (!fs::exists(outPath)) {
        fs::create_directories(outPath);
        TraceLog(LOG_INFO, "Created output directory: %s", outputDir);
    }

    // Write domain JSON
    std::string domainJsonPath = (outPath / "domain.json").string();
    if (!saveDomainToFile(domainJsonPath, sourceDomain)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to write domain JSON: %s", domainJsonPath.c_str());
        return false;
    }
    TraceLog(LOG_INFO, "Wrote: %s", domainJsonPath.c_str());
    TraceLog(LOG_INFO, ">>> JSON can now be manually inspected <<<");

    // =========================================
    // PHASE B: RELOAD (shared code - same as game)
    // =========================================
    TraceLog(LOG_INFO, "--- PHASE B: RELOAD (shared code) ---");

    return viewerReloadFromJson(viewer, domainJsonPath.c_str());
}

//------------------------------------------------------------------------------
// Reload domain from JSON
//------------------------------------------------------------------------------
// Rebuild all render meshes (tiles + floor geometry + walls) from viewer->loadedDomain. Used after
// a JSON reload and after live edits in the link inspector. Does not touch the camera.
void viewerRebuildMeshes(Viewer* viewer) {
    if (!viewer || !viewer->initialized || !viewer->domainLoaded) return;

    // Unload previous tile meshes
    if (viewer->tileMesh.loaded) {
        for (auto& batch : viewer->tileMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->tileMesh.batches.clear();

        if (viewer->tileMesh.model.meshCount > 0) {
            UnloadModel(viewer->tileMesh.model);
        }
        viewer->tileMesh = {};
    }

    // Unload previous geometry meshes
    if (viewer->geometryMesh.loaded) {
        for (auto& batch : viewer->geometryMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->geometryMesh.batches.clear();
        viewer->geometryMesh = {};
    }

    // Count tiles
    int totalTiles = 0;
    for (const auto& area : viewer->loadedDomain.areas) {
        totalTiles += static_cast<int>(area.tiles.size());
    }
    TraceLog(LOG_INFO, "Loaded %d tiles from JSON (using shared loader)", totalTiles);

    // Generate batched meshes from loaded data
    TraceLog(LOG_INFO, "Generating batched meshes from loaded data...");

    // Create batched meshes (grouped by texture indices)
    TileBatchCollection batchCollection = createDomainBatchedMeshes(viewer->loadedDomain);
    if (!batchCollection.success) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to create batched tile meshes: %s",
                 batchCollection.error ? batchCollection.error : "unknown error");
        return;
    }

    // Collect unique texture indices for loading
    std::set<int> textureIndices;
    for (const auto& batch : batchCollection.batches) {
        textureIndices.insert(batch.textureIndex1);
        textureIndices.insert(batch.textureIndex2);
    }

    // Load textures if texture lookup is available
    if (viewer->texturesLoaded) {
        TraceLog(LOG_INFO, "Loading %zu unique textures...", textureIndices.size());
        for (int idx : textureIndices) {
            if (idx > 0) {  // Skip 0 (flat normal is already loaded)
                textureCacheLoad(viewer->textureCache, viewer->textureLookup, idx);
            }
        }
    }

    // Convert batches to models with textures
    for (const auto& batch : batchCollection.batches) {
        if (!batch.valid) continue;

        TileBatchState state = {};
        state.model = LoadModelFromMesh(batch.mesh);
        state.textureIndex1 = batch.textureIndex1;
        state.textureIndex2 = batch.textureIndex2;
        state.tileCount = batch.tileCount;
        state.triangleCount = batch.triangleCount;
        state.valid = true;

        // Apply lighting shader
        sceneRendererApplyShader(&viewer->renderer, &state.model);

        // Set diffuse color to white (actual colors are in vertex colors)
        state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

        // Apply textures if available
        if (viewer->texturesLoaded) {
            // Diffuse texture
            Texture2D diffuse = textureCacheGetDiffuse(viewer->textureCache, batch.textureIndex1);
            if (diffuse.id > 0) {
                state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
            }

            // Bump/normal texture
            Texture2D bump = textureCacheGetBump(viewer->textureCache, batch.textureIndex2);
            if (bump.id > 0) {
                state.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = bump;
            }
        }

        viewer->tileMesh.batches.push_back(state);
        viewer->tileMesh.triangleCount += batch.triangleCount;
    }

    viewer->tileMesh.tileCount = totalTiles;
    viewer->tileMesh.boundsMin = batchCollection.boundsMin;
    viewer->tileMesh.boundsMax = batchCollection.boundsMax;
    viewer->tileMesh.loaded = !viewer->tileMesh.batches.empty();

    TraceLog(LOG_INFO, "Created %zu batched meshes, %d total triangles",
             viewer->tileMesh.batches.size(), viewer->tileMesh.triangleCount);
    TraceLog(LOG_INFO, "Bounds: min(%.2f, %.2f, %.2f) max(%.2f, %.2f, %.2f)",
             viewer->tileMesh.boundsMin.x, viewer->tileMesh.boundsMin.y, viewer->tileMesh.boundsMin.z,
             viewer->tileMesh.boundsMax.x, viewer->tileMesh.boundsMax.y, viewer->tileMesh.boundsMax.z);

    // =========================================
    // Generate geometry meshes from PathGeometry areas
    // =========================================
    TraceLog(LOG_INFO, "Generating geometry meshes from PathGeometry...");

    GeometryMeshCollection geoCollection = createDomainGeometryMeshes(viewer->loadedDomain, viewer->scale);
    if (geoCollection.success && !geoCollection.meshes.empty()) {
        // Load textures for geometry materials
        if (viewer->texturesLoaded) {
            // Load floor texture (default material texture0=79, texture1=1)
            textureCacheLoad(viewer->textureCache, viewer->textureLookup, DEFAULT_FLOOR_MATERIAL.diffuseTextureIndex);
            textureCacheLoad(viewer->textureCache, viewer->textureLookup, DEFAULT_FLOOR_MATERIAL.normalTextureIndex);
        }

        // Convert geometry meshes to models
        for (const auto& geoMesh : geoCollection.meshes) {
            if (geoMesh.vertices.empty()) continue;

            GeometryBatchState state = {};
            Mesh raylibMesh = geometryMeshToRaylibMesh(geoMesh);
            if (raylibMesh.vertexCount == 0) continue;

            state.model = LoadModelFromMesh(raylibMesh);
            state.materialId = geoMesh.materialId;
            state.triangleCount = raylibMesh.triangleCount;
            state.valid = true;

            // Apply lighting shader
            sceneRendererApplyShader(&viewer->renderer, &state.model);

            // Set diffuse color to white
            state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

            // Apply textures based on material
            if (viewer->texturesLoaded) {
                // Use default floor material textures for now
                Texture2D diffuse = textureCacheGetDiffuse(viewer->textureCache, DEFAULT_FLOOR_MATERIAL.diffuseTextureIndex);
                if (diffuse.id > 0) {
                    state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
                }

                Texture2D bump = textureCacheGetBump(viewer->textureCache, DEFAULT_FLOOR_MATERIAL.normalTextureIndex);
                if (bump.id > 0) {
                    state.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = bump;
                }
            }

            viewer->geometryMesh.batches.push_back(state);
            viewer->geometryMesh.totalTriangles += state.triangleCount;
        }

        viewer->geometryMesh.boundsMin = geoCollection.totalBounds.min;
        viewer->geometryMesh.boundsMax = geoCollection.totalBounds.max;
        viewer->geometryMesh.loaded = !viewer->geometryMesh.batches.empty();

        TraceLog(LOG_INFO, "Created %zu geometry meshes, %d total triangles",
                 viewer->geometryMesh.batches.size(), viewer->geometryMesh.totalTriangles);
    } else {
        TraceLog(LOG_INFO, "No geometry meshes generated (no PathGeometry areas found)");
    }

    freeGeometryMeshCollection(&geoCollection);

    // =========================================
    // Walls: sweep each link's profile (materials.xml). Appended to the geometry batches so
    // they render under the F4 (geometry) toggle.
    // =========================================
    if (viewer->wallProfiles.loaded) {
        GeometryMeshCollection wallCol = createDomainWallMeshes(viewer->loadedDomain, viewer->scale,
                                                                viewer->wallProfiles,
                                                                viewer->toggles.enableCaps,
                                                                viewer->toggles.enableMiter);
        int wallTris = 0;
        size_t wallCount = wallCol.meshes.size();
        for (const auto& wm : wallCol.meshes) {
            if (wm.vertices.empty()) continue;
            GeometryBatchState state = {};
            Mesh rm = geometryMeshToRaylibMesh(wm);
            if (rm.vertexCount == 0) continue;

            state.model = LoadModelFromMesh(rm);
            state.materialId = wm.materialId;  // diffuse texture index
            state.triangleCount = rm.triangleCount;
            state.valid = true;

            sceneRendererApplyShader(&viewer->renderer, &state.model);
            state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

            if (viewer->texturesLoaded && wm.materialId > 0) {
                textureCacheLoad(viewer->textureCache, viewer->textureLookup, wm.materialId);
                Texture2D diffuse = textureCacheGetDiffuse(viewer->textureCache, wm.materialId);
                if (diffuse.id > 0) state.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
            }

            viewer->geometryMesh.batches.push_back(state);
            wallTris += state.triangleCount;
        }
        viewer->geometryMesh.totalTriangles += wallTris;
        viewer->geometryMesh.loaded = viewer->geometryMesh.loaded || !viewer->geometryMesh.batches.empty();
        freeGeometryMeshCollection(&wallCol);
        TraceLog(LOG_INFO, "Created %zu wall meshes (%d tris)", wallCount, wallTris);
    }

    TraceLog(LOG_INFO, "=== MESHES REBUILT ===");
}

bool viewerReloadFromJson(Viewer* viewer, const char* jsonPath) {
    if (!viewer || !viewer->initialized) return false;

    TraceLog(LOG_INFO, "Loading domain from: %s", jsonPath);
    if (!loadDomainFromFile(jsonPath, viewer->loadedDomain)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to load domain from JSON: %s", jsonPath);
        return false;
    }
    viewer->domainLoaded = true;

    viewerRebuildMeshes(viewer);

    // Position camera to frame the mesh (only on a fresh load, not on live edits).
    if (viewer->tileMesh.loaded) {
        Vector3 center = {
            (viewer->tileMesh.boundsMin.x + viewer->tileMesh.boundsMax.x) / 2.0f,
            (viewer->tileMesh.boundsMin.y + viewer->tileMesh.boundsMax.y) / 2.0f,
            (viewer->tileMesh.boundsMin.z + viewer->tileMesh.boundsMax.z) / 2.0f
        };
        float size = Vector3Distance(viewer->tileMesh.boundsMin, viewer->tileMesh.boundsMax);
        viewer->camera.target = center;
        viewer->camera.position = (Vector3){
            center.x + size * 0.5f,
            center.y + size * 0.8f,
            center.z + size * 0.5f
        };
    } else {
        TraceLog(LOG_WARNING, "VIEWER: No triangles generated from tiles");
    }

    viewerSetCameraPreset(viewer, viewer->cameraPreset);
    return true;
}

//------------------------------------------------------------------------------
// Set camera to preset view
//------------------------------------------------------------------------------
void viewerSetCameraPreset(Viewer* viewer, CameraPreset preset) {
    if (!viewer || !viewer->initialized) return;

    viewer->cameraPreset = preset;

    // Calculate center and size from bounds
    Vector3 center;
    float size;

    if (viewer->tileMesh.loaded) {
        center = {
            (viewer->tileMesh.boundsMin.x + viewer->tileMesh.boundsMax.x) / 2.0f,
            (viewer->tileMesh.boundsMin.y + viewer->tileMesh.boundsMax.y) / 2.0f,
            (viewer->tileMesh.boundsMin.z + viewer->tileMesh.boundsMax.z) / 2.0f
        };
        size = Vector3Distance(viewer->tileMesh.boundsMin, viewer->tileMesh.boundsMax);
    } else {
        center = {0.0f, 0.0f, 0.0f};
        size = 10.0f;
    }

    // Height above the scene
    float height = size * 0.8f;

    switch (preset) {
        case CameraPreset::TopDown:
            // Game mode: looking straight down from above
            viewer->camera.position = {center.x, center.y + height, center.z};
            viewer->camera.target = center;
            viewer->camera.up = {0.0f, 0.0f, -1.0f};  // -Z is "up" on screen
            viewer->camera.projection = CAMERA_PERSPECTIVE;
            TraceLog(LOG_INFO, "VIEWER: Camera preset: Top-Down (game mode)");
            break;

        case CameraPreset::Isometric:
            // 45 degree isometric view for 3D validation
            {
                float offset = height * 0.707f;  // sin(45) = cos(45) ≈ 0.707
                viewer->camera.position = {
                    center.x + offset,
                    center.y + height,
                    center.z + offset
                };
                viewer->camera.target = center;
                viewer->camera.up = {0.0f, 1.0f, 0.0f};
                viewer->camera.projection = CAMERA_PERSPECTIVE;
            }
            TraceLog(LOG_INFO, "VIEWER: Camera preset: Isometric (45 degree)");
            break;

        case CameraPreset::Perspective:
            // Free perspective view (same as old default)
            viewer->camera.position = {
                center.x + size * 0.5f,
                center.y + size * 0.8f,
                center.z + size * 0.5f
            };
            viewer->camera.target = center;
            viewer->camera.up = {0.0f, 1.0f, 0.0f};
            viewer->camera.projection = CAMERA_PERSPECTIVE;
            TraceLog(LOG_INFO, "VIEWER: Camera preset: Perspective");
            break;
    }
}

//------------------------------------------------------------------------------
// Update camera and handle input
//------------------------------------------------------------------------------
void viewerUpdate(Viewer* viewer, float deltaTime) {
    if (!viewer || !viewer->initialized) return;

    // Speed multiplier when holding shift
    float speedMult = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? FAST_MULTIPLIER : 1.0f;
    float moveAmount = viewer->moveSpeed * speedMult * deltaTime;

    // Calculate camera forward/right vectors (in XZ plane)
    Vector3 forward = Vector3Subtract(viewer->camera.target, viewer->camera.position);
    Vector3 right;

    // Handle top-down mode specially (forward vector is nearly vertical)
    if (fabsf(forward.x) < 0.001f && fabsf(forward.z) < 0.001f) {
        // Camera looking straight down - use up vector to determine forward
        // In top-down mode, camera.up is {0, 0, -1} meaning -Z is "up" on screen
        // So W (forward) should move in -Z direction, which is the camera.up direction
        forward = viewer->camera.up;
        forward.y = 0;
        forward = Vector3Normalize(forward);
        right = Vector3CrossProduct(forward, (Vector3){0, 1, 0});
    } else {
        // Normal camera - flatten forward to XZ plane
        forward.y = 0;
        forward = Vector3Normalize(forward);
        right = Vector3Normalize(Vector3CrossProduct(forward, viewer->camera.up));
        right.y = 0;
        right = Vector3Normalize(right);
    }

    // WASD movement
    Vector3 movement = {0, 0, 0};
    if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, forward);
    if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, forward);
    if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, right);
    if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, right);

    // Q/E for up/down
    if (IsKeyDown(KEY_E)) movement.y += 1.0f;
    if (IsKeyDown(KEY_Q)) movement.y -= 1.0f;

    // Apply movement to both camera position and target
    if (Vector3Length(movement) > 0.001f) {
        movement = Vector3Scale(Vector3Normalize(movement), moveAmount);
        viewer->camera.position = Vector3Add(viewer->camera.position, movement);
        viewer->camera.target = Vector3Add(viewer->camera.target, movement);
    }

    // Mouse wheel zoom
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector3 toTarget = Vector3Subtract(viewer->camera.target, viewer->camera.position);
        float distance = Vector3Length(toTarget);
        float newDistance = distance - wheel * viewer->zoomSpeed * deltaTime * 10.0f;
        newDistance = Clamp(newDistance, MIN_CAMERA_DISTANCE, MAX_CAMERA_DISTANCE);

        if (newDistance != distance) {
            Vector3 direction = Vector3Normalize(toTarget);
            viewer->camera.position = Vector3Subtract(viewer->camera.target, Vector3Scale(direction, newDistance));
        }
    }

    // Camera preset keys
    if (IsKeyPressed(KEY_T)) {
        viewerSetCameraPreset(viewer, CameraPreset::TopDown);
    }
    if (IsKeyPressed(KEY_I)) {
        viewerSetCameraPreset(viewer, CameraPreset::Isometric);
    }
    if (IsKeyPressed(KEY_P)) {
        viewerSetCameraPreset(viewer, CameraPreset::Perspective);
    }

    // Reset camera (R key) - reapply current preset
    if (IsKeyPressed(KEY_R)) {
        viewerSetCameraPreset(viewer, viewer->cameraPreset);
        TraceLog(LOG_INFO, "VIEWER: Camera reset to current preset");
    }

    // Debug mode keys (0-5)
    if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) {
        sceneRendererSetDebugMode(&viewer->renderer, 0);
    }
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
        sceneRendererSetDebugMode(&viewer->renderer, 1);
    }
    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) {
        sceneRendererSetDebugMode(&viewer->renderer, 2);
    }
    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) {
        sceneRendererSetDebugMode(&viewer->renderer, 3);
    }
    if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) {
        sceneRendererSetDebugMode(&viewer->renderer, 4);
    }
    if (IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5)) {
        sceneRendererSetDebugMode(&viewer->renderer, 5);
    }

    // Toggle keys (F1-F4, H)
    if (IsKeyPressed(KEY_F1)) {
        viewer->toggles.showGrid = !viewer->toggles.showGrid;
        TraceLog(LOG_INFO, "VIEWER: Grid %s", viewer->toggles.showGrid ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F2)) {
        viewer->toggles.showReference = !viewer->toggles.showReference;
        TraceLog(LOG_INFO, "VIEWER: Reference %s", viewer->toggles.showReference ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F3)) {
        viewer->toggles.showTiles = !viewer->toggles.showTiles;
        TraceLog(LOG_INFO, "VIEWER: Tiles %s", viewer->toggles.showTiles ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F4)) {
        viewer->toggles.showGeometry = !viewer->toggles.showGeometry;
        TraceLog(LOG_INFO, "VIEWER: Geometry %s", viewer->toggles.showGeometry ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F5)) {
        viewer->toggles.showWireframe = !viewer->toggles.showWireframe;
        TraceLog(LOG_INFO, "VIEWER: Wireframe %s", viewer->toggles.showWireframe ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F6)) {
        viewer->toggles.showTileIndices = !viewer->toggles.showTileIndices;
        TraceLog(LOG_INFO, "VIEWER: Tile indices %s", viewer->toggles.showTileIndices ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_F7)) {
        viewer->toggles.backfaceCulling = !viewer->toggles.backfaceCulling;
        TraceLog(LOG_INFO, "VIEWER: Backface culling %s", viewer->toggles.backfaceCulling ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_H)) {
        viewer->toggles.showHelp = !viewer->toggles.showHelp;
    }

    // Level cycling across decks (xmapfile{N}.txt).
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) viewerCycleLevel(viewer, +1);
    if (IsKeyPressed(KEY_LEFT_BRACKET))  viewerCycleLevel(viewer, -1);

    // Validity report panel.
    if (IsKeyPressed(KEY_V)) {
        viewer->toggles.showValidation = !viewer->toggles.showValidation;
        if (viewer->toggles.showValidation && !viewer->validation.run) viewerValidate(viewer);
    }

    // Class-14 reference unit (size reference).
    if (IsKeyPressed(KEY_U)) viewerToggleUnitRef(viewer);

    // Node markers + id labels (diagnosis).
    if (IsKeyPressed(KEY_N)) {
        viewer->toggles.showNodes = !viewer->toggles.showNodes;
        TraceLog(LOG_INFO, "VIEWER: node markers %s", viewer->toggles.showNodes ? "ON" : "OFF");
    }

    // Reload the current deck from source (re-parses the geometry XML — edit it, then press F9).
    if (IsKeyPressed(KEY_F9) && viewer->currentLevelIdx >= 0) {
        TraceLog(LOG_INFO, "VIEWER: reloading deck from source XML...");
        viewerLoadLevel(viewer, viewer->levelNumbers[viewer->currentLevelIdx]);
    }

    // Dump per-link profile assignment (which profiles each wall link gets, and the trim side).
    if (IsKeyPressed(KEY_J)) viewerDumpProfiles(viewer);

    // Link inspector panel (live-edit profiles / direction).
    if (IsKeyPressed(KEY_L)) {
        viewer->showInspector = !viewer->showInspector;
        TraceLog(LOG_INFO, "VIEWER: link inspector %s", viewer->showInspector ? "ON" : "OFF");
    }

    // Save the edited deck to JSON (saveDir/level_<n>.json; originals untouched).
    if (IsKeyPressed(KEY_F10)) viewerSaveEdited(viewer);

    // Wall caps / miter toggles (rebuild the current deck to apply).
    if (IsKeyPressed(KEY_K) || IsKeyPressed(KEY_M)) {
        if (IsKeyPressed(KEY_K)) viewer->toggles.enableCaps = !viewer->toggles.enableCaps;
        if (IsKeyPressed(KEY_M)) viewer->toggles.enableMiter = !viewer->toggles.enableMiter;
        TraceLog(LOG_INFO, "VIEWER: caps %s, miter %s",
                 viewer->toggles.enableCaps ? "ON" : "OFF", viewer->toggles.enableMiter ? "ON" : "OFF");
        if (viewer->currentLevelIdx >= 0) {
            viewerLoadLevel(viewer, viewer->levelNumbers[viewer->currentLevelIdx]);
        }
    }

    // Export the current level: X = combined GLTF; Shift+X = split (one file per shape).
    if (IsKeyPressed(KEY_X)) {
        const std::string dir = (fs::path(viewer->outputDir) / "export").string();
        bool split = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool ok = split ? viewerExportLevelSplit(viewer, dir.c_str())
                        : viewerExportLevel(viewer, dir.c_str());
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "VIEWER: %s export %s",
                 split ? "split" : "combined", ok ? "done" : "failed");
    }

    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&viewer->renderer, viewer->camera.position);
}

//------------------------------------------------------------------------------
// Render 3D scene
//------------------------------------------------------------------------------
void viewerRender(Viewer* viewer) {
    if (!viewer || !viewer->initialized) return;

    // Control backface culling
    if (viewer->toggles.backfaceCulling) {
        rlEnableBackfaceCulling();
    } else {
        rlDisableBackfaceCulling();
    }

    BeginMode3D(viewer->camera);

    // Draw grid for spatial reference
    if (viewer->toggles.showGrid) {
        DrawGrid(20, 1.0f);

        // Draw axis markers at origin
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){2, 0, 0}, RED);    // X axis
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 2, 0}, GREEN);  // Y axis
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 0, 2}, BLUE);   // Z axis
    }

    // Draw reference model (Suzanne)
    if (viewer->toggles.showReference && viewer->referenceLoaded) {
        DrawModel(viewer->referenceModel, viewer->referencePosition, 1.0f, WHITE);

        // Draw wireframe overlay if enabled
        if (viewer->toggles.showWireframe) {
            DrawModelWires(viewer->referenceModel, viewer->referencePosition, 1.0f, YELLOW);
        }
    }

    // Draw tile meshes (batched by texture)
    if (viewer->toggles.showTiles && viewer->tileMesh.loaded) {
        for (const auto& batch : viewer->tileMesh.batches) {
            if (batch.valid) {
                DrawModel(batch.model, (Vector3){0, 0, 0}, 1.0f, WHITE);

                // Draw wireframe overlay if enabled
                if (viewer->toggles.showWireframe) {
                    DrawModelWires(batch.model, (Vector3){0, 0, 0}, 1.0f, YELLOW);
                }
            }
        }
    }

    // Draw geometry meshes (floor polygons from PathGeometry)
    if (viewer->toggles.showGeometry && viewer->geometryMesh.loaded) {
        for (const auto& batch : viewer->geometryMesh.batches) {
            if (batch.valid) {
                DrawModel(batch.model, (Vector3){0, 0, 0}, 1.0f, WHITE);

                // Draw wireframe overlay if enabled
                if (viewer->toggles.showWireframe) {
                    DrawModelWires(batch.model, (Vector3){0, 0, 0}, 1.0f, MAGENTA);
                }
            }
        }
    }

    // Class-14 reference unit (drawn in the same 3D pass, with the lighting/env shader).
    if (viewer->toggles.showUnitRef) {
        viewerRenderUnitRef(viewer);
    }

    // Path-node markers (diagnosis): a sphere at each node position, drawn with depth test off so
    // they are never hidden behind walls/floors.
    if (viewer->toggles.showNodes && viewer->domainLoaded) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geom : area.geometry) {
                for (const auto& node : geom.nodes) {
                    DrawSphere(node.position, 0.25f, (Color){255, 60, 60, 255});
                }
            }
        }
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    EndMode3D();
}

//------------------------------------------------------------------------------
// Draw 2D overlay
//------------------------------------------------------------------------------
void viewerDrawOverlay(Viewer* viewer) {
    if (!viewer || !viewer->initialized) return;

    // Debug mode labels
    const char* debugLabels[] = {
        "0: Normal",
        "1: Normals RGB",
        "2: Light Dir",
        "3: Specular",
        "4: View Dir",
        "5: Half Angle"
    };

    // Camera preset labels
    const char* presetLabels[] = {
        "Top-Down",
        "Isometric",
        "Perspective"
    };

    int y = 10;
    DrawText("Incremental Scene Viewer - Phase 2", 10, y, 20, WHITE);
    y += 30;

    DrawText(TextFormat("View: %s [T/I/P]  Debug: %s",
             presetLabels[static_cast<int>(viewer->cameraPreset)],
             debugLabels[viewer->renderer.debugMode]), 10, y, 16, YELLOW);
    y += 20;

    DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
             viewer->camera.position.x, viewer->camera.position.y, viewer->camera.position.z),
             10, y, 16, LIGHTGRAY);
    y += 20;

    // Current deck + cycling hint.
    if (!viewer->levelNumbers.empty()) {
        int levelNum = (viewer->currentLevelIdx >= 0)
                           ? viewer->levelNumbers[viewer->currentLevelIdx] : -1;
        DrawText(TextFormat("Deck: %d  (%d/%zu)  [ ] cycle   U unit-ref   V validate   X export",
                 levelNum, viewer->currentLevelIdx + 1, viewer->levelNumbers.size()),
                 10, y, 16, SKYBLUE);
        y += 20;

        // Source geometry XML + diagnosis hints.
        if (!viewer->loadedDomain.areas.empty() && !viewer->loadedDomain.areas[0].geometry.empty()) {
            DrawText(TextFormat("XML: %s   [F9 reload]  [N nodes]  [J dump]  [M miter]  [L inspect]",
                     viewer->loadedDomain.areas[0].geometry[0].sourceFile.c_str()),
                     10, y, 14, LIGHTGRAY);
            y += 18;
        }
        DrawText(TextFormat("Source: %s   Save-dir: %s   [F10 save]",
                 viewer->loadedFromEdited ? "EDITED json" : "original XML",
                 viewer->saveDir.c_str()),
                 10, y, 14, viewer->loadedFromEdited ? GREEN : LIGHTGRAY);
        y += 18;
    }

    // Validity report panel.
    if (viewer->toggles.showValidation && viewer->validation.run) {
        const ValidationReport& v = viewer->validation;
        y += 6;
        DrawText(v.ok() ? "VALIDITY: OK" : "VALIDITY: ISSUES", 10, y, 18,
                 v.ok() ? GREEN : ORANGE);
        y += 22;
        DrawText(TextFormat("areas=%d  floorMeshes=%d  tileBatches=%d  tris=%d",
                 v.areas, v.floorMeshes, v.tileBatches, v.triangles), 10, y, 15, LIGHTGRAY);
        y += 18;
        DrawText(TextFormat("collision: polys=%d  chains=%d", v.collisionPolys, v.collisionChains),
                 10, y, 15, LIGHTGRAY);
        y += 18;
        for (const auto& w : v.warnings) {
            DrawText(TextFormat("- %s", w.c_str()), 14, y, 15, ORANGE);
            y += 17;
        }
    }

    // Tile info
    if (viewer->tileMesh.loaded) {
        DrawText(TextFormat("Tiles: %d (%d triangles, %zu batches)",
                 viewer->tileMesh.tileCount, viewer->tileMesh.triangleCount,
                 viewer->tileMesh.batches.size()),
                 10, y, 16, LIME);
        y += 20;
    } else if (viewer->domainLoaded) {
        DrawText("Tiles: loaded but no mesh", 10, y, 16, ORANGE);
        y += 20;
    }

    // Geometry info
    if (viewer->geometryMesh.loaded) {
        DrawText(TextFormat("Geometry: %d triangles, %zu meshes",
                 viewer->geometryMesh.totalTriangles,
                 viewer->geometryMesh.batches.size()),
                 10, y, 16, MAGENTA);
        y += 20;
    }

    // Texture info
    if (viewer->texturesLoaded) {
        DrawText(TextFormat("Textures: %zu cached",
                 viewer->textureCache.textures.size()),
                 10, y, 16, SKYBLUE);
        y += 20;
    }

    // Domain info
    if (viewer->domainLoaded) {
        DrawText(TextFormat("Domain: %s (Level %d)",
                 viewer->loadedDomain.name.empty() ? "unnamed" : viewer->loadedDomain.name.c_str(),
                 viewer->loadedDomain.levelNumber),
                 10, y, 16, SKYBLUE);
        y += 20;
    }

    // Toggle states
    DrawText(TextFormat("Grid[F1]:%s Ref[F2]:%s Tiles[F3]:%s Geo[F4]:%s Wire[F5]:%s Idx[F6]:%s Cull[F7]:%s",
             viewer->toggles.showGrid ? "ON" : "OFF",
             viewer->toggles.showReference ? "ON" : "OFF",
             viewer->toggles.showTiles ? "ON" : "OFF",
             viewer->toggles.showGeometry ? "ON" : "OFF",
             viewer->toggles.showWireframe ? "ON" : "OFF",
             viewer->toggles.showTileIndices ? "ON" : "OFF",
             viewer->toggles.backfaceCulling ? "ON" : "OFF"),
             10, y, 14, GRAY);
    y += 20;

    DrawText("Press H for help", 10, y, 14, DARKGRAY);

    // Help overlay
    if (viewer->toggles.showHelp) {
        int helpX = 200;
        int helpY = 80;
        int helpW = 400;
        int helpH = 380;

        DrawRectangle(helpX, helpY, helpW, helpH, (Color){30, 30, 40, 240});
        DrawRectangleLines(helpX, helpY, helpW, helpH, WHITE);

        int ty = helpY + 10;
        DrawText("CONTROLS", helpX + 10, ty, 20, WHITE);
        ty += 30;

        DrawText("WASD        Move camera", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("Q/E         Move up/down", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("Shift       Move faster", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("Mouse wheel Zoom", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("R           Reset camera", helpX + 10, ty, 14, LIGHTGRAY); ty += 28;

        DrawText("CAMERA PRESETS", helpX + 10, ty, 16, WHITE); ty += 22;
        DrawText("T           Top-down (game mode)", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("I           Isometric (45 deg)", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("P           Perspective", helpX + 10, ty, 14, LIGHTGRAY); ty += 28;

        DrawText("0-5         Debug modes", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F1          Toggle grid", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F2          Toggle reference", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F3          Toggle tiles", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F4          Toggle geometry", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F5          Toggle wireframe", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F6          Toggle tile indices", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F7          Toggle backface cull", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("H           Toggle this help", helpX + 10, ty, 14, LIGHTGRAY); ty += 28;

        DrawText("ESC         Quit", helpX + 10, ty, 14, LIGHTGRAY);
    }

    // FPS
    DrawFPS(GetScreenWidth() - 100, 10);

    // Tile index debug overlay - draw tile indices at centroids
    if (viewer->toggles.showTileIndices && viewer->domainLoaded) {
        int tileIndex = 0;
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& tile : area.tiles) {
                // Calculate centroid of tile vertices
                Vector3 centroid = {0, 0, 0};
                for (const auto& v : tile.vertices) {
                    centroid.x += v.position.x;
                    centroid.y += v.position.y;
                    centroid.z += v.position.z;
                }
                if (!tile.vertices.empty()) {
                    float n = static_cast<float>(tile.vertices.size());
                    centroid.x /= n;
                    centroid.y /= n;
                    centroid.z /= n;
                }

                // Lift centroid slightly above the floor for visibility
                centroid.y += 0.1f;

                // Project 3D centroid to 2D screen position
                Vector2 screenPos = GetWorldToScreen(centroid, viewer->camera);

                // Only draw if on screen
                if (screenPos.x >= 0 && screenPos.x <= GetScreenWidth() &&
                    screenPos.y >= 0 && screenPos.y <= GetScreenHeight()) {

                    // Draw background for readability
                    const char* label = TextFormat("%d", tileIndex);
                    int textWidth = MeasureText(label, 14);
                    DrawRectangle(
                        static_cast<int>(screenPos.x) - textWidth/2 - 2,
                        static_cast<int>(screenPos.y) - 8,
                        textWidth + 4,
                        16,
                        (Color){0, 0, 0, 180}
                    );

                    // Color based on vertex count (4 = green, other = red)
                    Color textColor = (tile.vertices.size() == 4) ? GREEN : RED;

                    DrawText(label,
                             static_cast<int>(screenPos.x) - textWidth/2,
                             static_cast<int>(screenPos.y) - 7,
                             14, textColor);
                }

                tileIndex++;
            }
        }
    }

    // Node id labels (diagnosis): project each unique node to screen and draw its id.
    if (viewer->toggles.showNodes && viewer->domainLoaded) {
        std::set<int> drawn;
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geom : area.geometry) {
                for (const auto& node : geom.nodes) {
                    if (!drawn.insert(node.id).second) continue;
                    Vector2 sp = GetWorldToScreen(node.position, viewer->camera);
                    if (sp.x < -20 || sp.y < -20 || sp.x > GetScreenWidth() + 20 ||
                        sp.y > GetScreenHeight() + 20) continue;
                    const char* txt = TextFormat("%d", node.id);
                    DrawText(txt, static_cast<int>(sp.x) + 5, static_cast<int>(sp.y) - 6, 14, YELLOW);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------
void viewerCleanup(Viewer* viewer) {
    if (!viewer) return;

    // Tear down the reference unit first (frees its bodies/models before the GL context closes).
    viewerDestroyUnitRef(viewer);

    // Cleanup tile meshes
    if (viewer->tileMesh.loaded) {
        for (auto& batch : viewer->tileMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->tileMesh.batches.clear();

        if (viewer->tileMesh.model.meshCount > 0) {
            UnloadModel(viewer->tileMesh.model);
        }
        viewer->tileMesh = {};
    }

    // Cleanup geometry meshes
    if (viewer->geometryMesh.loaded) {
        for (auto& batch : viewer->geometryMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->geometryMesh.batches.clear();
        viewer->geometryMesh = {};
    }

    // Cleanup texture cache
    if (viewer->texturesLoaded) {
        textureCacheDestroy(viewer->textureCache);
        viewer->texturesLoaded = false;
    }

    if (viewer->referenceLoaded) {
        UnloadModel(viewer->referenceModel);
        viewer->referenceLoaded = false;
    }

    if (viewer->initialized) {
        sceneRendererDestroy(&viewer->renderer);
        viewer->initialized = false;
    }

    TraceLog(LOG_INFO, "VIEWER: Cleanup complete");
}
