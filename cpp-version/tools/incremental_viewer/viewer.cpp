#include "viewer.h"
#include "raymath.h"
#include "rlgl.h"
#include "scene_convert/domain_parser.h"
#include "scene_convert/scene_json.h"
#include "rendering/tile_mesh.h"
#include "rendering/texture_loader.h"
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

    // Add directional light from above
    sceneRendererAddDirectionalLight(&viewer->renderer,
        (Vector3){0, 0, 0},    // Position (reference point)
        (Vector3){0, 50, 0},   // Target (light direction = UP)
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
    viewer->toggles.showWireframe = false;
    viewer->toggles.showHelp = false;

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
bool viewerReloadFromJson(Viewer* viewer, const char* jsonPath) {
    if (!viewer || !viewer->initialized) return false;

    TraceLog(LOG_INFO, "Loading domain from: %s", jsonPath);

    // Unload previous meshes
    if (viewer->tileMesh.loaded) {
        // Unload batched models
        for (auto& batch : viewer->tileMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->tileMesh.batches.clear();

        // Unload legacy model if used
        if (viewer->tileMesh.model.meshCount > 0) {
            UnloadModel(viewer->tileMesh.model);
        }
        viewer->tileMesh = {};
    }

    // Load from JSON using shared loader
    if (!loadDomainFromFile(jsonPath, viewer->loadedDomain)) {
        TraceLog(LOG_ERROR, "VIEWER: Failed to load domain from JSON: %s", jsonPath);
        return false;
    }

    viewer->domainLoaded = true;

    // Count tiles after reload
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
        return false;
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

    // Position camera to view the mesh
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

    TraceLog(LOG_INFO, "=== READY FOR VIEWING ===");
    return true;
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
    Vector3 forward = Vector3Normalize(Vector3Subtract(viewer->camera.target, viewer->camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, viewer->camera.up));

    // Flatten to XZ plane for movement
    forward.y = 0;
    forward = Vector3Normalize(forward);
    right.y = 0;
    right = Vector3Normalize(right);

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

    // Reset camera (R key)
    if (IsKeyPressed(KEY_R)) {
        if (viewer->tileMesh.loaded) {
            // Reset to view tile mesh
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
            viewer->camera.position = (Vector3){5.0f, 8.0f, 5.0f};
            viewer->camera.target = (Vector3){0.0f, 0.0f, 0.0f};
        }
        TraceLog(LOG_INFO, "VIEWER: Camera reset");
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
        viewer->toggles.showWireframe = !viewer->toggles.showWireframe;
        TraceLog(LOG_INFO, "VIEWER: Wireframe %s", viewer->toggles.showWireframe ? "ON" : "OFF");
    }
    if (IsKeyPressed(KEY_H)) {
        viewer->toggles.showHelp = !viewer->toggles.showHelp;
    }

    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&viewer->renderer, viewer->camera.position);
}

//------------------------------------------------------------------------------
// Render 3D scene
//------------------------------------------------------------------------------
void viewerRender(Viewer* viewer) {
    if (!viewer || !viewer->initialized) return;

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

    int y = 10;
    DrawText("Incremental Scene Viewer - Phase 2", 10, y, 20, WHITE);
    y += 30;

    DrawText(TextFormat("Debug: %s", debugLabels[viewer->renderer.debugMode]), 10, y, 16, YELLOW);
    y += 20;

    DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
             viewer->camera.position.x, viewer->camera.position.y, viewer->camera.position.z),
             10, y, 16, LIGHTGRAY);
    y += 20;

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
    DrawText(TextFormat("Grid[F1]:%s  Ref[F2]:%s  Tiles[F3]:%s  Wire[F4]:%s",
             viewer->toggles.showGrid ? "ON" : "OFF",
             viewer->toggles.showReference ? "ON" : "OFF",
             viewer->toggles.showTiles ? "ON" : "OFF",
             viewer->toggles.showWireframe ? "ON" : "OFF"),
             10, y, 14, GRAY);
    y += 20;

    DrawText("Press H for help", 10, y, 14, DARKGRAY);

    // Help overlay
    if (viewer->toggles.showHelp) {
        int helpX = 200;
        int helpY = 100;
        int helpW = 400;
        int helpH = 320;

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

        DrawText("0-5         Debug modes", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F1          Toggle grid", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F2          Toggle reference", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F3          Toggle tiles", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("F4          Toggle wireframe", helpX + 10, ty, 14, LIGHTGRAY); ty += 18;
        DrawText("H           Toggle this help", helpX + 10, ty, 14, LIGHTGRAY); ty += 28;

        DrawText("ESC         Quit", helpX + 10, ty, 14, LIGHTGRAY);
    }

    // FPS
    DrawFPS(GetScreenWidth() - 100, 10);
}

//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------
void viewerCleanup(Viewer* viewer) {
    if (!viewer) return;

    // Cleanup tile meshes
    if (viewer->tileMesh.loaded) {
        // Unload batched models
        for (auto& batch : viewer->tileMesh.batches) {
            if (batch.valid) {
                UnloadModel(batch.model);
            }
        }
        viewer->tileMesh.batches.clear();

        // Unload legacy model if used
        if (viewer->tileMesh.model.meshCount > 0) {
            UnloadModel(viewer->tileMesh.model);
        }
        viewer->tileMesh = {};
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
