#include "viewer_state.h"
#include "level/tile_properties_loader.h"
#include "rlgl.h"
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// State Management
//------------------------------------------------------------------------------

bool viewerStateInit(LevelViewerState* state, const std::string& inputPath,
                     const std::string& assetPath) {
    state->inputPath = inputPath;
    state->assetPath = assetPath;
    state->shadersPath = (fs::path(assetPath) / "shaders").string() + "/";

    // Initialize scene renderer
    if (!sceneRendererInit(&state->renderer, state->shadersPath.c_str())) {
        TraceLog(LOG_ERROR, "Failed to initialize scene renderer from: %s",
                 state->shadersPath.c_str());
        return false;
    }

    // Add directional light from above (pointing down)
    // lightDir = normalize(target - position), so position above, target below
    sceneRendererAddDirectionalLight(&state->renderer,
        (Vector3){0, 50, 0},   // Light source position (above)
        (Vector3){0, 0, 0},    // Target (below) - light shines down
        WHITE);

    // Initialize camera
    state->camera.position = (Vector3){0, state->cameraHeight, -state->cameraOrbitDistance};
    state->camera.target = (Vector3){0, 0, 0};
    state->camera.up = (Vector3){0, 1, 0};
    state->camera.fovy = 45.0f;
    state->camera.projection = CAMERA_PERSPECTIVE;

    // Set effective eye height for specular calculations (default: 1 tile = 1 world unit)
    sceneRendererSetEffectiveEyeHeight(&state->renderer, state->effectiveEyeHeight);

    // Create reference sphere (1m diameter)
    Mesh sphereMesh = GenMeshSphere(0.5f, 16, 16);
    state->refSphereModel = LoadModelFromMesh(sphereMesh);
    sceneRendererApplyShader(&state->renderer, &state->refSphereModel);
    state->refSphereModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){255, 100, 100, 255};
    state->refSphereValid = true;

    return true;
}

void viewerStateDestroy(LevelViewerState* state) {
    // Free render data for all levels
    for (auto& data : state->renderData) {
        freeLevelRenderData(&data);
    }
    state->renderData.clear();

    // Free collision data for all levels
    for (auto& data : state->collisionData) {
        freeLevelCollisionData(&data);
    }
    state->collisionData.clear();

    state->levels.clear();

    // Unload textures
    if (state->atlasTexture.id > 0) {
        UnloadTexture(state->atlasTexture);
        state->atlasTexture = {0};
    }
    if (state->bumpTexture.id > 0) {
        UnloadTexture(state->bumpTexture);
        state->bumpTexture = {0};
    }
    if (state->bumpAtlasTexture.id > 0) {
        UnloadTexture(state->bumpAtlasTexture);
        state->bumpAtlasTexture = {0};
    }

    // Unload reference sphere
    if (state->refSphereValid) {
        UnloadModel(state->refSphereModel);
        state->refSphereValid = false;
    }

    // Destroy renderer
    sceneRendererDestroy(&state->renderer);
}

bool viewerStateLoadLevels(LevelViewerState* state) {
    // Load TMX files from input directory
    auto results = loadTmxLevelsFromDirectory(state->inputPath);

    if (results.empty()) {
        TraceLog(LOG_ERROR, "No TMX files found in: %s", state->inputPath.c_str());
        return false;
    }

    // Process results
    state->levels.clear();
    for (const auto& result : results) {
        if (result.success) {
            state->levels.push_back(result.level);
            TraceLog(LOG_INFO, "Loaded level: %s (%dx%d, %zu waypoints)",
                     result.level.name.c_str(),
                     result.level.width, result.level.height,
                     result.level.waypoints.size());
        } else {
            TraceLog(LOG_WARNING, "Failed to load level: %s", result.errorMsg.c_str());
        }
    }

    if (state->levels.empty()) {
        TraceLog(LOG_ERROR, "No valid levels loaded");
        return false;
    }

    // Load tileset from first level's reference
    if (!state->levels[0].tilesetSource.empty()) {
        fs::path tilesetPath = fs::path(state->inputPath) / state->levels[0].tilesetSource;
        TsxLoadResult tsxResult = loadTsxTileset(tilesetPath.string());

        if (tsxResult.success) {
            state->tileset = tsxResult.tileset;
            state->tileset.firstGid = 1;  // TMX files use firstgid=1

            // Load atlas texture
            state->atlasTexture = loadTilesetTexture(state->tileset, state->inputPath);

            // Load flat normal bump texture (for Tilemap mode)
            fs::path bumpPath = fs::path(state->assetPath) / "textures" / "flat_normal.png";
            if (fs::exists(bumpPath)) {
                state->bumpTexture = LoadTexture(bumpPath.string().c_str());
                TraceLog(LOG_INFO, "Loaded flat normal texture: %s", bumpPath.string().c_str());
            }
        } else {
            TraceLog(LOG_ERROR, "Failed to load tileset: %s", tsxResult.errorMsg.c_str());
            return false;
        }
    }

    // Load tile properties (tiles.json) for CustomTiles mode
    fs::path tilesJsonPath = fs::path(state->inputPath) / "tiles.json";
    if (fs::exists(tilesJsonPath)) {
        state->tileProperties = loadTileProperties(tilesJsonPath.string());
        if (state->tileProperties.valid) {
            // Load bump atlas texture
            // The texture path in tiles.json is relative to assets folder
            fs::path bumpAtlasPath = fs::path(state->assetPath) / state->tileProperties.bumpAtlas.texture;
            if (fs::exists(bumpAtlasPath)) {
                state->bumpAtlasTexture = LoadTexture(bumpAtlasPath.string().c_str());
                if (state->bumpAtlasTexture.id > 0) {
                    TraceLog(LOG_INFO, "Loaded bump atlas: %s (%dx%d)",
                             bumpAtlasPath.string().c_str(),
                             state->bumpAtlasTexture.width, state->bumpAtlasTexture.height);
                } else {
                    TraceLog(LOG_ERROR, "Failed to load bump atlas texture: %s", bumpAtlasPath.string().c_str());
                }
            } else {
                TraceLog(LOG_WARNING, "Bump atlas file not found: %s", bumpAtlasPath.string().c_str());
            }
        }
    } else {
        TraceLog(LOG_INFO, "No tiles.json found, CustomTiles mode unavailable");
    }

    // Initialize render data and collision data vectors
    state->renderData.resize(state->levels.size());
    state->collisionData.resize(state->levels.size());

    // Build render data for first level
    state->currentLevel = 0;
    viewerStateBuildRenderData(state);

    // Center camera on first level
    viewerStateCenterCamera(state);

    TraceLog(LOG_INFO, "Loaded %zu levels from %s",
             state->levels.size(), state->inputPath.c_str());
    return true;
}

bool viewerStateBuildRenderData(LevelViewerState* state) {
    if (state->currentLevel < 0 || state->currentLevel >= (int)state->levels.size()) {
        return false;
    }

    LevelRenderData& data = state->renderData[state->currentLevel];
    LevelCollisionData& collision = state->collisionData[state->currentLevel];
    const TmxLevel& level = state->levels[state->currentLevel];

    // Free existing render data
    freeLevelRenderData(&data);

    // Free existing collision data and regenerate
    freeLevelCollisionData(&collision);
    collision = generateLevelCollision(level, state->tileset, state->worldScale);

    // Create new render data
    data = createLevelRenderData(level, state->tileset, state->renderMode, state->worldScale);

    // Create mesh based on render mode
    if (state->renderMode == LevelRenderMode::CustomTiles && state->tileProperties.valid) {
        // Custom tiles mode: generate mesh with per-tile bump UVs
        int atlasW = state->bumpAtlasTexture.width > 0 ? state->bumpAtlasTexture.width : 128;
        int atlasH = state->bumpAtlasTexture.height > 0 ? state->bumpAtlasTexture.height : 128;
        data.tileMesh = createLevelTileMeshCustom(
            level, state->tileset, state->tileProperties, atlasW, atlasH, state->worldScale);
    } else {
        // Tilemap mode: standard mesh generation
        data.tileMesh = createLevelTileMesh(level, state->tileset, state->worldScale);
    }

    if (data.tileMesh.vertexCount > 0) {
        // Select bump texture based on render mode
        Texture2D bumpTex = state->bumpTexture;  // Default: flat normal
        if (state->renderMode == LevelRenderMode::CustomTiles && state->bumpAtlasTexture.id > 0) {
            bumpTex = state->bumpAtlasTexture;
        }

        // Create model with textures
        data.tileModel = createLevelTileModel(
            data.tileMesh,
            state->atlasTexture,
            bumpTex,
            &state->renderer
        );
        data.meshValid = true;
    }

    return data.meshValid;
}

bool viewerStateSwitchLevel(LevelViewerState* state, int levelIndex) {
    if (levelIndex < 0 || levelIndex >= (int)state->levels.size()) {
        return false;
    }

    state->currentLevel = levelIndex;

    // Build render data if not already built
    if (!state->renderData[levelIndex].meshValid) {
        viewerStateBuildRenderData(state);
    }

    // Center camera on new level
    viewerStateCenterCamera(state);

    return true;
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void viewerStateDrawLevel(LevelViewerState* state) {
    if (state->currentLevel < 0 || state->currentLevel >= (int)state->renderData.size()) {
        return;
    }

    LevelRenderData& data = state->renderData[state->currentLevel];

    if (!data.meshValid) {
        return;
    }

    // Update camera position for specular
    sceneRendererUpdateCamera(&state->renderer, state->camera.position);

    // Set backface culling
    if (state->backfaceCulling) {
        rlEnableBackfaceCulling();
    } else {
        rlDisableBackfaceCulling();
    }

    // Draw tile model
    DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);

    // Restore culling state
    rlEnableBackfaceCulling();
}

void viewerStateDrawWaypoints(LevelViewerState* state) {
    if (!state->showWaypoints && !state->showWaypointLinks) {
        return;
    }

    if (state->currentLevel < 0 || state->currentLevel >= (int)state->renderData.size()) {
        return;
    }

    LevelRenderData& data = state->renderData[state->currentLevel];

    // Draw waypoint links
    if (state->showWaypointLinks) {
        for (const auto& link : data.waypointLinks) {
            if (link.first < (int)data.waypointPositions.size() &&
                link.second < (int)data.waypointPositions.size()) {
                Vector3 p1 = data.waypointPositions[link.first];
                Vector3 p2 = data.waypointPositions[link.second];
                // Elevate slightly above floor
                p1.y = 0.1f;
                p2.y = 0.1f;
                DrawLine3D(p1, p2, YELLOW);
            }
        }
    }

    // Draw waypoint spheres
    if (state->showWaypoints) {
        for (size_t i = 0; i < data.waypointPositions.size(); i++) {
            Vector3 pos = data.waypointPositions[i];
            pos.y = 0.2f;  // Elevate above floor
            DrawSphere(pos, 0.3f, GREEN);
        }
    }
}

void viewerStateDrawRefSphere(LevelViewerState* state) {
    if (!state->showRefSphere || !state->refSphereValid) {
        return;
    }

    // Draw 1m reference sphere at origin
    DrawModel(state->refSphereModel, (Vector3){0, 0.5f, 0}, 1.0f, WHITE);
}

void viewerStateDrawBounds(LevelViewerState* state) {
    if (!state->showBounds) {
        return;
    }

    if (state->currentLevel < 0 || state->currentLevel >= (int)state->renderData.size()) {
        return;
    }

    LevelRenderData& data = state->renderData[state->currentLevel];

    // Draw bounds wireframe
    Vector3 min = data.boundsMin;
    Vector3 max = data.boundsMax;

    // Floor rectangle
    DrawLine3D((Vector3){min.x, 0, min.z}, (Vector3){max.x, 0, min.z}, MAGENTA);
    DrawLine3D((Vector3){max.x, 0, min.z}, (Vector3){max.x, 0, max.z}, MAGENTA);
    DrawLine3D((Vector3){max.x, 0, max.z}, (Vector3){min.x, 0, max.z}, MAGENTA);
    DrawLine3D((Vector3){min.x, 0, max.z}, (Vector3){min.x, 0, min.z}, MAGENTA);
}

void viewerStateDrawCollision(LevelViewerState* state) {
    if (!state->showCollision) {
        return;
    }

    if (state->currentLevel < 0 || state->currentLevel >= (int)state->collisionData.size()) {
        return;
    }

    const LevelCollisionData& collision = state->collisionData[state->currentLevel];
    drawCollisionDebug(collision, RED, 0.02f);
}

void viewerStateDrawHUD(LevelViewerState* state) {
    if (!state->showHUD) {
        return;
    }

    int y = 10;
    int lineHeight = 18;

    // Level info
    if (state->currentLevel >= 0 && state->currentLevel < (int)state->levels.size()) {
        const TmxLevel& level = state->levels[state->currentLevel];
        const LevelRenderData& data = state->renderData[state->currentLevel];

        DrawText(TextFormat("Level: %d/%d - %s",
                 state->currentLevel + 1, (int)state->levels.size(),
                 level.name.c_str()), 10, y, 16, WHITE);
        y += lineHeight;

        DrawText(TextFormat("Size: %dx%d tiles", level.width, level.height), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        DrawText(TextFormat("Tiles: %d rendered", data.tileCount), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        DrawText(TextFormat("Waypoints: %zu", data.waypointPositions.size()), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        // Collision info
        if (state->currentLevel < (int)state->collisionData.size()) {
            const LevelCollisionData& collision = state->collisionData[state->currentLevel];
            Color collColor = state->showCollision ? RED : LIGHTGRAY;
            DrawText(TextFormat("Collision: %zu rects [X]", collision.rects.size()), 10, y, 14, collColor);
            y += lineHeight;
        }
        y += 6;
    }

    // Debug mode
    const char* debugNames[] = {
        "0: Normal", "1: Normals RGB", "2: Light Dir",
        "3: Specular", "4: View Dir", "5: Half Angle", "6: Bump Map"
    };
    int debugIdx = state->debugMode;
    if (debugIdx < 0 || debugIdx > 6) debugIdx = 0;
    DrawText(TextFormat("Debug: %s", debugNames[debugIdx]), 10, y, 14, YELLOW);
    y += lineHeight;

    // Render mode
    const char* modeNames[] = {"Tilemap", "Custom Tiles", "3D Objects"};
    int modeIdx = static_cast<int>(state->renderMode);
    Color modeColor = (state->renderMode == LevelRenderMode::CustomTiles && state->tileProperties.valid) ? GREEN : LIGHTGRAY;
    DrawText(TextFormat("Render Mode: %s [M]", modeNames[modeIdx]), 10, y, 14, modeColor);
    y += lineHeight;

    // Backface culling
    DrawText(TextFormat("Backface Culling: %s", state->backfaceCulling ? "ON" : "OFF"),
             10, y, 14, LIGHTGRAY);
    y += lineHeight;

    // Camera mode
    const char* cameraModeNames[] = {"Perspective", "Topdown", "Isometric"};
    int camModeIdx = static_cast<int>(state->cameraMode);
    DrawText(TextFormat("Camera Mode: %s", cameraModeNames[camModeIdx]), 10, y, 14, LIGHTGRAY);
    y += lineHeight;

    // Camera position
    DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
             state->camera.position.x, state->camera.position.y, state->camera.position.z),
             10, y, 14, GRAY);
    y += lineHeight;

    // Effective eye height for specular calculations
    DrawText(TextFormat("Eye Height: %.2f [Ctrl +/-]", state->effectiveEyeHeight), 10, y, 14, GRAY);
    y += lineHeight + 10;

    // Controls help
    DrawText("[H]UD [G]rid [P]oints [L]inks [X] Collision", 10, y, 12, DARKGRAY);
    y += 14;
    DrawText("[0-6] Debug  [Space] Rotate  [V] View  [K]ull", 10, y, 12, DARKGRAY);
    y += 14;
    DrawText("[/] Level  [WASD] Pan  [Q/E] Orbit  [O]rigin", 10, y, 12, DARKGRAY);
}

//------------------------------------------------------------------------------
// Camera
//------------------------------------------------------------------------------

void viewerStateUpdateCamera(LevelViewerState* state) {
    float angle = state->cameraOrbitAngle * DEG2RAD;

    switch (state->cameraMode) {
        case LevelViewerState::CameraMode::Perspective:
            // Standard perspective orbit camera
            state->camera.projection = CAMERA_PERSPECTIVE;
            state->camera.fovy = 45.0f;
            state->camera.position.x = state->cameraTarget.x + sinf(angle) * state->cameraOrbitDistance;
            state->camera.position.y = state->cameraTarget.y + state->cameraHeight;
            state->camera.position.z = state->cameraTarget.z + cosf(angle) * state->cameraOrbitDistance;
            state->camera.up = (Vector3){0, 1, 0};
            break;

        case LevelViewerState::CameraMode::Topdown:
            // Perspective top-down view (directly above, looking down)
            // Camera up vector points toward -Z so TMX row 0 appears at top of screen
            state->camera.projection = CAMERA_PERSPECTIVE;
            state->camera.fovy = 45.0f;
            state->camera.position.x = state->cameraTarget.x;
            state->camera.position.y = state->cameraTarget.y + state->cameraHeight;
            state->camera.position.z = state->cameraTarget.z;
            // Up vector rotates with orbit angle, base direction is -Z (toward TMX row 0)
            state->camera.up = (Vector3){-sinf(angle), 0, -cosf(angle)};
            break;

        case LevelViewerState::CameraMode::Isometric:
            // Orthographic isometric view at 45 degrees from horizontal
            state->camera.projection = CAMERA_ORTHOGRAPHIC;
            state->camera.fovy = state->cameraOrbitDistance * 2.0f;
            {
                // 45 degree angle from horizontal
                float isoAngle = 45.0f * DEG2RAD;
                float horizDist = state->cameraOrbitDistance * cosf(isoAngle);
                float vertDist = state->cameraOrbitDistance * sinf(isoAngle);
                state->camera.position.x = state->cameraTarget.x + sinf(angle) * horizDist;
                state->camera.position.y = state->cameraTarget.y + vertDist;
                state->camera.position.z = state->cameraTarget.z + cosf(angle) * horizDist;
            }
            state->camera.up = (Vector3){0, 1, 0};
            break;
    }

    state->camera.target = state->cameraTarget;
}

void viewerStateCenterCamera(LevelViewerState* state) {
    if (state->currentLevel < 0 || state->currentLevel >= (int)state->renderData.size()) {
        state->cameraTarget = (Vector3){0, 0, 0};
        return;
    }

    LevelRenderData& data = state->renderData[state->currentLevel];

    // Center on level bounds
    state->cameraTarget.x = (data.boundsMin.x + data.boundsMax.x) * 0.5f;
    state->cameraTarget.y = 0;
    state->cameraTarget.z = (data.boundsMin.z + data.boundsMax.z) * 0.5f;

    // Adjust orbit distance based on level size
    float levelWidth = data.boundsMax.x - data.boundsMin.x;
    float levelDepth = data.boundsMax.z - data.boundsMin.z;
    float maxSize = fmaxf(levelWidth, levelDepth);
    state->cameraOrbitDistance = maxSize * 0.75f;
    state->cameraHeight = maxSize * 0.5f;

    viewerStateUpdateCamera(state);
}

//------------------------------------------------------------------------------
// Render Mode
//------------------------------------------------------------------------------

void viewerStateSwitchRenderMode(LevelViewerState* state, LevelRenderMode mode) {
    if (state->renderMode == mode) {
        return;  // No change
    }

    // Check if CustomTiles mode is available
    if (mode == LevelRenderMode::CustomTiles && !state->tileProperties.valid) {
        TraceLog(LOG_WARNING, "CustomTiles mode unavailable - no valid tiles.json");
        return;
    }

    state->renderMode = mode;

    // Invalidate all render data - geometry needs to be rebuilt for new mode
    for (auto& data : state->renderData) {
        if (data.meshValid) {
            freeLevelRenderData(&data);
        }
    }

    // Rebuild current level
    viewerStateBuildRenderData(state);

    const char* modeNames[] = {"Tilemap", "Custom Tiles", "3D Objects"};
    TraceLog(LOG_INFO, "Switched to render mode: %s", modeNames[static_cast<int>(mode)]);
}

void viewerStateCycleRenderMode(LevelViewerState* state) {
    // Cycle: Tilemap -> CustomTiles -> Tilemap (skip Objects3D for now)
    if (state->renderMode == LevelRenderMode::Tilemap) {
        viewerStateSwitchRenderMode(state, LevelRenderMode::CustomTiles);
    } else {
        viewerStateSwitchRenderMode(state, LevelRenderMode::Tilemap);
    }
}
