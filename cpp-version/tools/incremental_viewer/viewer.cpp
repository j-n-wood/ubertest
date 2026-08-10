#include "viewer.h"
#include "raymath.h"
#include "rlgl.h"
#include "scene_convert/domain_parser.h"
#include "scene_convert/scene_json.h"
#include "scene_convert/geometry_xml_parser.h"  // mergeDomainSections
#include "rendering/tile_mesh.h"
#include "rendering/geometry_mesh.h"
#include "rendering/wall_mesh.h"          // buildWallCollision
#include "rendering/collision_debug.h"    // drawCollisionWireframe (shared with the game)
#include "rendering/glass_render.h"       // configureGlassMaterial / begin-endGlassPass
#include "rendering/texture_loader.h"
#include <algorithm>
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

    // Initialize tile mesh state
    viewer->tileMesh = {};
    viewer->domainLoaded = false;
    viewer->scale = SCALE_UNITS_TO_METERS;

    // Initialize texture system
    textureCacheInit(viewer->textureCache);
    viewer->texturesLoaded = false;

    // Initialize toggles
    viewer->toggles.showGrid = true;
    viewer->toggles.showTiles = true;
    viewer->toggles.showGeometry = true;
    viewer->toggles.showWireframe = false;
    viewer->toggles.showHelp = false;
    viewer->toggles.showTileIndices = false;  // Debug tile index overlay
    viewer->toggles.backfaceCulling = true;   // Start with culling enabled
    viewer->toggles.enableCaps = true;        // Wall end caps
    viewer->toggles.enableMiter = true;       // Wall corner miter joins
    viewer->toggles.showNodes = false;        // Path-node markers + labels (diagnosis)
    viewer->toggles.showWaypoints = false;    // AI waypoint graph overlay
    viewer->toggles.showWallLinks = false;    // Wall-link id/direction overlay (UV diagnosis)
    viewer->toggles.showCollision = false;    // Collision footprint wireframe (shared with game)
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

    // Fit the ground grid to this level's footprint (render space). Walk every path node
    // (the wall network spans the level) to get X/Z extents; geometry lives in +X / -Z.
    {
        float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geo : area.geometry) {
                for (const auto& node : geo.nodes) {
                    minX = std::min(minX, node.position.x);
                    maxX = std::max(maxX, node.position.x);
                    minZ = std::min(minZ, node.position.z);
                    maxZ = std::max(maxZ, node.position.z);
                }
            }
        }
        if (maxX >= minX) {  // found at least one node
            const float pad = 4.0f;  // metres of margin around the footprint (wall thickness + air)
            viewer->gridMin = {std::floor(minX - pad), 0.0f, std::floor(minZ - pad)};
            viewer->gridMax = {std::ceil(maxX + pad),  0.0f, std::ceil(maxZ + pad)};
            viewer->gridFit = true;
        } else {
            viewer->gridFit = false;
        }
    }

    // Recentre the reference droid on the (re)fitted grid midpoint (no-op until it's built).
    viewerUpdateUnitRefPosition(viewer);

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
                // Bump/normal (texture1) — bind it so the viewer bump-shades walls like the game does.
                if (wm.normalMaterialId > 0) {
                    textureCacheLoad(viewer->textureCache, viewer->textureLookup, wm.normalMaterialId);
                    Texture2D bump = textureCacheGetBump(viewer->textureCache, wm.normalMaterialId);
                    if (bump.id > 0) state.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = bump;
                }
            }

            // Glass tunnel (drawtype 5): reconfigure this batch's material for the transparent
            // env-mapped pass (diffuse env texture -> env slot, blue tint, keep bump) and flag it so
            // it's drawn separately from the opaque geometry.
            state.glass = wm.glass;
            if (state.glass) configureGlassMaterial(&state.model.materials[0]);

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
    // Heal any pre-merge JSON: collapse multiple per-area geometry sections into one unified id space
    // (source-XML parsing already merges inline). No-op for already-single-section areas.
    mergeDomainSections(viewer->loadedDomain);
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
        // Reference is a class-14 droid (real UnitManager), same toggle as U.
        viewerToggleUnitRef(viewer);
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
    if (IsKeyPressed(KEY_F8)) {
        viewer->toggles.showWaypoints = !viewer->toggles.showWaypoints;
        TraceLog(LOG_INFO, "VIEWER: Waypoints %s (%zu)", viewer->toggles.showWaypoints ? "ON" : "OFF",
                 viewer->loadedDomain.waypoints.size());
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
        if (!viewer->toggles.showNodes) viewer->selectedNodeId = -1;
    }

    // Node editing (needs the node overlay on). Left-click selects the nearest node marker; the arrow
    // keys then move it in the floor plane and PageUp/Dn in height. Steps are in GAME units (integers,
    // to match the source data): 1 fine, 16 with Shift. F10 saves the edited deck (loadedDomain).
    if (viewer->toggles.showNodes && viewer->domainLoaded) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            float bestPx = 18.0f; int bestId = -1;
            for (const auto& area : viewer->loadedDomain.areas)
                for (const auto& geom : area.geometry)
                    for (const auto& node : geom.nodes) {
                        Vector2 sp = GetWorldToScreen(node.position, viewer->camera);
                        float d = Vector2Distance(sp, mouse);
                        if (d < bestPx) { bestPx = d; bestId = node.id; }
                    }
            if (bestId >= 0) {
                viewer->selectedNodeId = bestId;
                TraceLog(LOG_INFO, "VIEWER: selected node %d", bestId);
            }
        }
        if (viewer->selectedNodeId >= 0) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            float step = (shift ? 16.0f : 1.0f) * viewer->scale;   // game units -> render metres
            Vector3 d = {0, 0, 0};
            if (IsKeyPressed(KEY_LEFT))      d.x -= step;   // -game x
            if (IsKeyPressed(KEY_RIGHT))     d.x += step;   // +game x
            if (IsKeyPressed(KEY_UP))        d.z -= step;   // +game y (screen up in top-down)
            if (IsKeyPressed(KEY_DOWN))      d.z += step;   // -game y
            if (IsKeyPressed(KEY_PAGE_UP))   d.y += step;   // +game z (height)
            if (IsKeyPressed(KEY_PAGE_DOWN)) d.y -= step;   // -game z
            if (d.x != 0.0f || d.y != 0.0f || d.z != 0.0f) {
                bool moved = false;
                for (auto& area : viewer->loadedDomain.areas)
                    for (auto& geom : area.geometry)
                        for (auto& node : geom.nodes)
                            if (node.id == viewer->selectedNodeId) {
                                node.position.x += d.x; node.position.y += d.y; node.position.z += d.z;
                                moved = true;
                            }
                if (moved) viewerRebuildMeshes(viewer);   // reflect the move in geometry + collision
            }
        }
    }

    // Wall-link overlay (G): centreline (green start -> red finish), yellow bezier control point, and
    // a label "L<id> <start>-><finish> [B]" — so UV/normal artefacts can be pinned to exact links.
    // (K is the caps toggle, so this lives on G.)
    if (IsKeyPressed(KEY_G)) {
        viewer->toggles.showWallLinks = !viewer->toggles.showWallLinks;
        TraceLog(LOG_INFO, "VIEWER: wall-link overlay %s", viewer->toggles.showWallLinks ? "ON" : "OFF");
    }

    // Collision footprint wireframe (C) — the same shapes the game builds (buildWallCollision),
    // drawn with the shared drawCollisionWireframe so viewer + game match.
    if (IsKeyPressed(KEY_C)) {
        viewer->toggles.showCollision = !viewer->toggles.showCollision;
        TraceLog(LOG_INFO, "VIEWER: collision wireframe %s", viewer->toggles.showCollision ? "ON" : "OFF");
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

    // Export the current level: X = combined GLTF; Shift+X = split (one file per shape). Writes to
    // --export-dir when given (e.g. the game's levels3d bundle), else <outputDir>/export.
    if (IsKeyPressed(KEY_X)) {
        const std::string dir = !viewer->exportDir.empty()
            ? viewer->exportDir
            : (fs::path(viewer->outputDir) / "export").string();
        bool split = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool ok = split ? viewerExportLevelSplit(viewer, dir.c_str())
                        : viewerExportLevel(viewer, dir.c_str());
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "VIEWER: %s export %s",
                 split ? "split" : "combined", ok ? "done" : "failed");
    }

    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&viewer->renderer, viewer->camera.position);
}

// Colour-code a waypoint by its most significant flag (spawn/console/charger/lift/transmat),
// else plain SKYBLUE — matches the game's SKYBLUE waypoint spheres for un-flagged nodes.
static Color waypointColor(const WaypointFlags& f) {
    if (f.start)    return GREEN;   // valid droid spawn point
    if (f.console)  return ORANGE;  // near a console
    if (f.recharge) return GOLD;    // near a charger
    if (f.lift)     return VIOLET;  // near a lift/transporter
    if (f.transmat) return PINK;    // transmat beam destination
    return SKYBLUE;
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

    // Draw grid for spatial reference. Fitted to the level footprint (render +X / -Z) when a deck
    // is loaded; otherwise a default region biased into that quadrant where geometry lives.
    if (viewer->toggles.showGrid) {
        float minX = -10.0f, maxX = 40.0f, minZ = -40.0f, maxZ = 10.0f;  // default (no deck)
        if (viewer->gridFit) {
            minX = viewer->gridMin.x; maxX = viewer->gridMax.x;
            minZ = viewer->gridMin.z; maxZ = viewer->gridMax.z;
        }
        const Color line = {110, 110, 110, 255};      // lighter than the DARKGRAY background
        const Color axisLine = {160, 160, 160, 255};   // emphasise the X=0 / Z=0 lines
        for (float x = std::ceil(minX); x <= maxX; x += 1.0f) {
            DrawLine3D((Vector3){x, 0, minZ}, (Vector3){x, 0, maxZ},
                       (x == 0.0f) ? axisLine : line);
        }
        for (float z = std::ceil(minZ); z <= maxZ; z += 1.0f) {
            DrawLine3D((Vector3){minX, 0, z}, (Vector3){maxX, 0, z},
                       (z == 0.0f) ? axisLine : line);
        }

        // Draw axis markers at origin
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){2, 0, 0}, RED);    // X axis
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 2, 0}, GREEN);  // Y axis
        DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 0, 2}, BLUE);   // Z axis
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

    // Draw geometry meshes (floor/wall from PathGeometry). Opaque batches first; glass batches
    // (tunnels) are drawn afterwards in a transparent env-mapped pass.
    if (viewer->toggles.showGeometry && viewer->geometryMesh.loaded) {
        bool anyGlass = false;
        for (const auto& batch : viewer->geometryMesh.batches) {
            if (!batch.valid) continue;
            if (batch.glass) { anyGlass = true; continue; }
            DrawModel(batch.model, (Vector3){0, 0, 0}, 1.0f, WHITE);
            if (viewer->toggles.showWireframe) {
                DrawModelWires(batch.model, (Vector3){0, 0, 0}, 1.0f, MAGENTA);
            }
        }
        if (anyGlass) {
            Shader shader = sceneRendererGetShader(&viewer->renderer);
            beginGlassPass(shader, 1.0f);
            for (const auto& batch : viewer->geometryMesh.batches)
                if (batch.valid && batch.glass) DrawModel(batch.model, (Vector3){0, 0, 0}, 1.0f, WHITE);
            endGlassPass(shader);
        }
    }

    // Collision footprint wireframe (C) — the exact quads the game builds + collides against
    // (buildWallCollision), drawn with the shared helper so viewer and game look identical.
    if (viewer->toggles.showCollision && viewer->domainLoaded) {
        std::vector<std::vector<Vector2>> polys;
        for (const auto& q : buildWallCollision(viewer->loadedDomain, viewer->wallProfiles, viewer->scale))
            polys.push_back({q.v[0], q.v[1], q.v[2], q.v[3]});
        drawCollisionWireframe(polys);
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
                    bool sel = (node.id == viewer->selectedNodeId);
                    DrawSphere(node.position, sel ? 0.4f : 0.25f,
                               sel ? (Color){80, 255, 80, 255} : (Color){255, 60, 60, 255});
                }
            }
        }
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    // Wall-link overlay: centreline with direction (green start -> red finish) + bezier control.
    if (viewer->toggles.showWallLinks && viewer->domainLoaded) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        const Vector3 lift = {0.0f, 0.3f, 0.0f};
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geom : area.geometry) {
                std::unordered_map<int, Vector3> np;
                for (const auto& node : geom.nodes) np[node.id] = node.position;
                const bool hasDefault = !geom.profiles.empty();
                for (const auto& link : geom.links) {
                    const bool isWall = !link.profiles.empty() || (link.useDefaultProfiles && hasDefault);
                    if (!isWall) continue;
                    auto s = np.find(link.start), f = np.find(link.finish);
                    if (s == np.end() || f == np.end()) continue;
                    Vector3 a = Vector3Add(s->second, lift), b = Vector3Add(f->second, lift);
                    DrawLine3D(a, b, ORANGE);
                    DrawSphere(a, 0.10f, GREEN);   // start
                    DrawSphere(b, 0.16f, RED);     // finish (direction)
                    if (link.control) DrawSphere(Vector3Add(link.control->position, lift), 0.13f, YELLOW);
                }
            }
        }
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    // AI waypoint graph overlay: neighbour edges + a colour-coded sphere per waypoint, drawn
    // depth-test-off so they're never hidden. Waypoints share the geometry's render space (same
    // gameToRenderCoords transform on serialize), so no extra scaling is needed here.
    if (viewer->toggles.showWaypoints && viewer->domainLoaded) {
        const auto& wps = viewer->loadedDomain.waypoints;
        const Vector3 lift = {0.0f, 0.15f, 0.0f};  // sit above the floor like the game (y=0.15)
        auto findPos = [&](int id, Vector3& out) -> bool {
            for (const auto& w : wps) { if (w.id == id) { out = w.position; return true; } }
            return false;
        };
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        // Edges (drawn first so nodes sit on top). 0 is padding = "no neighbour".
        for (const auto& w : wps) {
            Vector3 a = Vector3Add(w.position, lift);
            for (int k = 0; k < 6; ++k) {
                int nid = w.neighbors[k];
                if (nid == 0 || nid == w.id) continue;
                Vector3 b;
                if (findPos(nid, b)) DrawLine3D(a, Vector3Add(b, lift), DARKBLUE);
            }
        }
        // Nodes
        for (const auto& w : wps) {
            DrawSphere(Vector3Add(w.position, lift), 0.12f, waypointColor(w.flags));
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
    DrawText(TextFormat("Grid[F1]:%s Ref[F2]:%s Tiles[F3]:%s Geo[F4]:%s Wire[F5]:%s Idx[F6]:%s Cull[F7]:%s WP[F8]:%s",
             viewer->toggles.showGrid ? "ON" : "OFF",
             viewer->toggles.showUnitRef ? "ON" : "OFF",
             viewer->toggles.showTiles ? "ON" : "OFF",
             viewer->toggles.showGeometry ? "ON" : "OFF",
             viewer->toggles.showWireframe ? "ON" : "OFF",
             viewer->toggles.showTileIndices ? "ON" : "OFF",
             viewer->toggles.backfaceCulling ? "ON" : "OFF",
             viewer->toggles.showWaypoints ? "ON" : "OFF"),
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
        DrawText("F8          Toggle waypoint graph", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
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
        const float s = (viewer->scale > 0.0f) ? viewer->scale : 1.0f;
        std::set<int> drawn;
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geom : area.geometry) {
                for (const auto& node : geom.nodes) {
                    if (!drawn.insert(node.id).second) continue;
                    Vector2 sp = GetWorldToScreen(node.position, viewer->camera);
                    if (sp.x < -20 || sp.y < -20 || sp.x > GetScreenWidth() + 20 ||
                        sp.y > GetScreenHeight() + 20) continue;
                    bool sel = (node.id == viewer->selectedNodeId);
                    DrawText(TextFormat("%d", node.id), static_cast<int>(sp.x) + 5,
                             static_cast<int>(sp.y) - 6, 14, sel ? GREEN : YELLOW);
                    if (sel) {
                        // Selected node: show its GAME coords (inverse of gameToRenderCoords) so they
                        // match the source XML — x = render.x/s, y = -render.z/s, z(height) = render.y/s.
                        DrawText(TextFormat("(%.0f, %.0f, %.0f)", node.position.x / s,
                                            -node.position.z / s, node.position.y / s),
                                 static_cast<int>(sp.x) + 5, static_cast<int>(sp.y) + 10, 14, GREEN);
                    }
                }
            }
        }
        // Editing hint / selected-node readout (top-left, below the deck line).
        if (viewer->selectedNodeId >= 0) {
            DrawText(TextFormat("Node %d selected  [arrows: x/y  PgUp/Dn: height  Shift: x16  F10: save]",
                     viewer->selectedNodeId), 10, 90, 14, GREEN);
        } else {
            DrawText("Click a node marker to select; arrows move it (game units)", 10, 90, 14, GRAY);
        }
    }

    // Wall-link labels: "L<id> <start>-><finish> [B]" at each wall link's midpoint (B = bezier).
    if (viewer->toggles.showWallLinks && viewer->domainLoaded) {
        for (const auto& area : viewer->loadedDomain.areas) {
            for (const auto& geom : area.geometry) {
                std::unordered_map<int, Vector3> np;
                for (const auto& node : geom.nodes) np[node.id] = node.position;
                const bool hasDefault = !geom.profiles.empty();
                for (const auto& link : geom.links) {
                    const bool isWall = !link.profiles.empty() || (link.useDefaultProfiles && hasDefault);
                    if (!isWall) continue;
                    auto s = np.find(link.start), f = np.find(link.finish);
                    if (s == np.end() || f == np.end()) continue;
                    Vector3 mid = link.control
                        ? link.control->position
                        : Vector3{(s->second.x + f->second.x) * 0.5f, (s->second.y + f->second.y) * 0.5f,
                                  (s->second.z + f->second.z) * 0.5f};
                    Vector2 sp = GetWorldToScreen(Vector3Add(mid, (Vector3){0, 0.3f, 0}), viewer->camera);
                    if (sp.x < -20 || sp.y < -20 || sp.x > GetScreenWidth() + 20 ||
                        sp.y > GetScreenHeight() + 20) continue;
                    const char* txt = TextFormat("L%d %d>%d%s", link.id, link.start, link.finish,
                                                 link.control ? " B" : "");
                    DrawText(txt, static_cast<int>(sp.x) + 5, static_cast<int>(sp.y) - 6, 12, ORANGE);
                }
            }
        }
    }

    // Waypoint id labels + flag legend.
    if (viewer->toggles.showWaypoints && viewer->domainLoaded) {
        for (const auto& w : viewer->loadedDomain.waypoints) {
            Vector2 sp = GetWorldToScreen(Vector3Add(w.position, (Vector3){0, 0.15f, 0}),
                                          viewer->camera);
            if (sp.x < -20 || sp.y < -20 || sp.x > GetScreenWidth() + 20 ||
                sp.y > GetScreenHeight() + 20) continue;
            DrawText(TextFormat("%d", w.id), static_cast<int>(sp.x) + 5,
                     static_cast<int>(sp.y) - 6, 14, SKYBLUE);
        }
        // Legend for the flag colour-coding (bottom-left, above the help hint).
        int lx = 10, ly = GetScreenHeight() - 130;
        DrawText("Waypoints [F8]:", lx, ly, 12, RAYWHITE);
        DrawText("start", lx, ly + 14, 12, GREEN);
        DrawText("console", lx + 44, ly + 14, 12, ORANGE);
        DrawText("charger", lx + 104, ly + 14, 12, GOLD);
        DrawText("lift", lx + 164, ly + 14, 12, VIOLET);
        DrawText("transmat", lx + 196, ly + 14, 12, PINK);
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


    if (viewer->initialized) {
        sceneRendererDestroy(&viewer->renderer);
        viewer->initialized = false;
    }

    TraceLog(LOG_INFO, "VIEWER: Cleanup complete");
}
