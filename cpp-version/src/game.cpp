#include "game.h"
#include "level/spawn_config.h"
#include <cmath>
#include <cstring>

#define MAX_TORQUE 100.0f
#define MOVEMENT_FORCE 7.0f
#define PI 3.14159265358979323846f

// Lift tile GIDs in the TMX tileset (from tiles 0.json annotations)
static constexpr int LIFT_TILE_GID_A = 16;
static constexpr int LIFT_TILE_GID_B = 17;

// Forward declarations
static bool game_load_levels(Game* game);
static bool game_build_level_render_data(Game* game);
static void game_create_level_collision(Game* game);
static void game_spawn_player(Game* game);
static void game_spawn_enemies(Game* game);
static void game_despawn_enemies(Game* game);
static void game_switch_level(Game* game, int newLevel);
static std::vector<Vector2> game_find_lift_positions(const Game* game, int level);
static void game_update_player_rotation(Game* game);
static void game_apply_player_rotation_torque(Game* game);

// Normalize angle to [-PI, PI]
static float normalize_angle(float angle) {
    while (angle > PI) angle -= 2.0f * PI;
    while (angle < -PI) angle += 2.0f * PI;
    return angle;
}

void game_init(Game* game, const char* assetPath, const char* unitId, const RotationTestConfig* testConfig) {
    // Store test config if provided
    if (testConfig) {
        game->testConfig = *testConfig;
    }
    game->testFrameCount = 0;

    // Store player unit ID (command line overrides test config which overrides default)
    if (unitId && unitId[0] != '\0') {
        game->playerUnitId = unitId;
    } else if (testConfig && !testConfig->unitId.empty()) {
        game->playerUnitId = testConfig->unitId;
    } else {
        game->playerUnitId = "droid_class_0";
    }

    // Store paths
    game->assetPath = assetPath;
    game->shadersPath = game->assetPath + "/shaders/";
    game->levelsPath = game->assetPath + "/ships/ship1/levels/";

    // Initialize physics world (zero gravity for top-down)
    physics_world_init(&game->physics);

    // Initialize SceneRenderer
    if (!sceneRendererInit(&game->sceneRenderer, game->shadersPath.c_str())) {
        TraceLog(LOG_ERROR, "Failed to initialize scene renderer");
        game->running = false;
        return;
    }

    // Configure lighting - directional light from above
    sceneRendererAddDirectionalLight(&game->sceneRenderer,
        (Vector3){0, 50, 0},   // Position above
        (Vector3){0, 0, 0},    // Target below
        WHITE);

    // Set effective eye height for specular calculations
    game->effectiveEyeHeight = 1.0f;
    sceneRendererSetEffectiveEyeHeight(&game->sceneRenderer, game->effectiveEyeHeight);

    // Set ambient light
    sceneRendererSetAmbient(&game->sceneRenderer, 0.15f, 0.15f, 0.15f, 1.0f);

    // Initialize UnitManager with physics world
    // UnitManager strips "models/" prefix and appends to base path, so use assets/models/
    std::string modelsPath = game->assetPath + "/models/";
    game->unitManager.init(game->physics.world_id, modelsPath.c_str());

    // Load all levels
    if (!game_load_levels(game)) {
        TraceLog(LOG_ERROR, "Failed to load levels");
        game->running = false;
        return;
    }

    // Build render data for starting level (level_0_maintenance)
    game->currentLevel = 0;
    if (!game_build_level_render_data(game)) {
        TraceLog(LOG_ERROR, "Failed to build render data for level %d", game->currentLevel);
    }

    // Create collision bodies from tile data
    game_create_level_collision(game);

    // Setup camera (top-down view)
    game->cameraHeight = 10.0f;  // May change based on unit type
    game->camera.position = (Vector3){0, game->cameraHeight, 0};
    game->camera.target = (Vector3){0, 0, 0};
    game->camera.up = (Vector3){0, 0, -1};  // -Z up for top-down view
    game->camera.fovy = 45.0f;
    game->camera.projection = CAMERA_PERSPECTIVE;

    // Initialize input
    input_init(&game->input);
    input_update_screen_cache(&game->input, &game->camera);

    game->running = true;
    game->debugMode = 0;
    game->playerDesiredRotation = 0.0f;
    game->playerUnit = nullptr;

    // Spawn player unit
    game_spawn_player(game);

    // Preload all unit definitions for enemy spawning
    std::string unitsPath = game->assetPath + "/units/";
    game->unitManager.preloadDefinitions(unitsPath);

    // Build type-class map from loaded definitions (needed by resolveSpawns)
    {
        std::vector<DroidProperties> allProps;
        auto defIds = game->unitManager.getDefinitionIds();
        for (const auto& id : defIds) {
            const UnitDefinition* def = game->unitManager.getDefinition(id);
            if (def) {
                allProps.push_back(def->properties);
            }
        }
        if (!allProps.empty()) {
            buildTypeClassMap(allProps.data(), (int)allProps.size());
        }
    }

    // Load ship spawn data
    std::string spawnPath = game->assetPath + "/ships/ship1/spawns.json";
    if (loadShipSpawns(spawnPath)) {
        TraceLog(LOG_INFO, "Loaded spawn data from %s", spawnPath.c_str());
    } else {
        TraceLog(LOG_WARNING, "Failed to load spawn data from %s", spawnPath.c_str());
    }

    // Spawn enemies for the starting level
    game_spawn_enemies(game);
}

//------------------------------------------------------------------------------
// Level Loading
//------------------------------------------------------------------------------

static bool game_load_levels(Game* game) {
    // Load all TMX files from levels directory
    auto results = loadTmxLevelsFromDirectory(game->levelsPath);

    if (results.empty()) {
        TraceLog(LOG_ERROR, "No levels found in: %s", game->levelsPath.c_str());
        return false;
    }

    game->levels.clear();
    for (const auto& result : results) {
        if (result.success) {
            game->levels.push_back(result.level);
            TraceLog(LOG_INFO, "Loaded level: %s (%dx%d tiles)",
                     result.level.name.c_str(),
                     result.level.width, result.level.height);
        } else {
            TraceLog(LOG_WARNING, "Failed to load level: %s", result.errorMsg.c_str());
        }
    }

    if (game->levels.empty()) {
        TraceLog(LOG_ERROR, "No valid levels loaded");
        return false;
    }

    // Load tileset from first level's reference
    if (!game->levels[0].tilesetSource.empty()) {
        std::string tilesetPath = game->levelsPath + game->levels[0].tilesetSource;
        TsxLoadResult tsxResult = loadTsxTileset(tilesetPath);

        if (tsxResult.success) {
            game->tileset = tsxResult.tileset;
            game->tileset.firstGid = 1;
            TraceLog(LOG_INFO, "Loaded tileset: %s (%dx%d tiles)",
                     game->tileset.name.c_str(),
                     game->tileset.columns, game->tileset.tileCount / game->tileset.columns);

            // Load atlas texture
            game->atlasTexture = loadTilesetTexture(game->tileset, game->levelsPath);
            if (game->atlasTexture.id == 0) {
                TraceLog(LOG_ERROR, "Failed to load atlas texture");
                return false;
            }
        } else {
            TraceLog(LOG_ERROR, "Failed to load tileset: %s", tsxResult.errorMsg.c_str());
            return false;
        }
    }

    // Load tile properties (tiles.json) for CustomTiles mode
    std::string tilesJsonPath = game->levelsPath + "tiles.json";
    game->tileProperties = loadTileProperties(tilesJsonPath);

    if (game->tileProperties.valid) {
        TraceLog(LOG_INFO, "Loaded tile properties with %zu custom tiles",
                 game->tileProperties.tiles.size());

        // Load bump atlas texture
        // bumpAtlas.texture is relative to assets folder (e.g., "textures/bump_atlas.png")
        std::string bumpPath = game->assetPath + "/" +
                               game->tileProperties.bumpAtlas.texture;
        game->bumpAtlasTexture = LoadTexture(bumpPath.c_str());

        if (game->bumpAtlasTexture.id == 0) {
            TraceLog(LOG_WARNING, "Failed to load bump atlas: %s", bumpPath.c_str());
        } else {
            TraceLog(LOG_INFO, "Loaded bump atlas: %dx%d",
                     game->bumpAtlasTexture.width, game->bumpAtlasTexture.height);
        }
    } else {
        TraceLog(LOG_WARNING, "No tile properties found, using Tilemap mode");
        game->bumpAtlasTexture = {0};
    }

    // Initialize storage vectors for render/collision data
    game->levelRenderData.resize(game->levels.size());
    game->levelCollisionData.resize(game->levels.size());

    TraceLog(LOG_INFO, "Loaded %zu levels total", game->levels.size());
    return true;
}

//------------------------------------------------------------------------------
// Render Data Building
//------------------------------------------------------------------------------

static bool game_build_level_render_data(Game* game) {
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) {
        return false;
    }

    LevelRenderData& data = game->levelRenderData[game->currentLevel];
    LevelCollisionData& collision = game->levelCollisionData[game->currentLevel];
    const TmxLevel& level = game->levels[game->currentLevel];

    // Free existing data if any
    freeLevelRenderData(&data);
    freeLevelCollisionData(&collision);

    // Generate collision data
    collision = generateLevelCollision(level, game->tileset, 1.0f);
    TraceLog(LOG_INFO, "Generated %zu collision rects for level %d",
             collision.rects.size(), game->currentLevel);

    // Create render data structure
    data = createLevelRenderData(level, game->tileset, LevelRenderMode::CustomTiles, 1.0f);

    // Generate mesh based on available resources
    if (game->tileProperties.valid && game->bumpAtlasTexture.id > 0) {
        // CustomTiles mode with bump mapping
        data.tileMesh = createLevelTileMeshCustom(
            level, game->tileset, game->tileProperties,
            game->bumpAtlasTexture.width, game->bumpAtlasTexture.height, 1.0f);
        TraceLog(LOG_INFO, "Created CustomTiles mesh");
    } else {
        // Fallback to standard Tilemap mode
        data.tileMesh = createLevelTileMesh(level, game->tileset, 1.0f);
        TraceLog(LOG_INFO, "Created Tilemap mesh (fallback)");
    }

    if (data.tileMesh.vertexCount > 0) {
        // Create model with textures
        Texture2D bumpTex = game->bumpAtlasTexture.id > 0 ?
                            game->bumpAtlasTexture : Texture2D{0};

        data.tileModel = createLevelTileModel(
            data.tileMesh, game->atlasTexture, bumpTex, &game->sceneRenderer);
        data.meshValid = true;

        TraceLog(LOG_INFO, "Level %d render data: %d vertices, bounds (%.1f,%.1f) to (%.1f,%.1f)",
                 game->currentLevel, data.tileMesh.vertexCount,
                 data.boundsMin.x, data.boundsMin.z,
                 data.boundsMax.x, data.boundsMax.z);
    }

    return data.meshValid;
}

//------------------------------------------------------------------------------
// Collision Body Creation
//------------------------------------------------------------------------------

static void game_create_level_collision(Game* game) {
    // Clear existing collision bodies
    // Note: Physics bodies are destroyed when physics world is destroyed
    game->collisionBodies.clear();

    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levelCollisionData.size()) {
        return;
    }

    const LevelCollisionData& collision = game->levelCollisionData[game->currentLevel];

    // Create static Box2D bodies for each merged collision rectangle
    for (const CollisionRect& rect : collision.rects) {
        // CollisionRect: x,z = world position (center), halfWidth/halfHeight = extents
        // Physics 2D space: X = world X, Y = world Z
        Vector2 physicsPos = {rect.x, rect.z};
        float width = rect.halfWidth * 2.0f;
        float height = rect.halfHeight * 2.0f;

        PhysicsBody body = physics_create_static_box(&game->physics,
                                                     physicsPos, width, height);
        game->collisionBodies.push_back(body);
    }

    TraceLog(LOG_INFO, "Created %zu collision bodies from level data",
             game->collisionBodies.size());
}

//------------------------------------------------------------------------------
// Player Spawning
//------------------------------------------------------------------------------

static void game_spawn_player(Game* game) {
    // Use the stored player unit ID (already resolved in game_init)
    const std::string& unitId = game->playerUnitId;

    // Load player unit definition
    std::string unitPath = game->assetPath + "/units/" + unitId + ".json";
    const UnitDefinition* playerDef = game->unitManager.loadDefinition(unitPath);

    if (!playerDef) {
        TraceLog(LOG_ERROR, "Failed to load player unit definition: %s", unitPath.c_str());
        return;
    }

    // Find spawn position from waypoint or use level center
    Vector2 spawnPos = {0, 0};

    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levelRenderData.size()) {
        const LevelRenderData& data = game->levelRenderData[game->currentLevel];
        if (!data.waypointPositions.empty()) {
            // Use first waypoint as spawn point
            Vector3 wpPos = data.waypointPositions[0];
            spawnPos = {wpPos.x, wpPos.z};  // World X,Z -> Physics X,Y
            TraceLog(LOG_INFO, "Spawning player at waypoint: (%.2f, %.2f)", spawnPos.x, spawnPos.y);
        }
    }

    // In test mode, offset spawn position to avoid nearby geometry
    if (game->testConfig.enabled) {
        spawnPos.x += -1.0f;  // Offset in physics X (world X)
        spawnPos.y += 1.0f;   // Offset in physics Y (world Z)
        TraceLog(LOG_INFO, "Test mode: offset spawn to (%.2f, %.2f)", spawnPos.x, spawnPos.y);
    }

    // Determine initial rotation (from test config or default)
    float initialRotation = game->testConfig.enabled ?
        game->testConfig.initialRotation * DEG2RAD : 0.0f;

    // Create player instance
    game->playerUnit = game->unitManager.createInstance(unitId, spawnPos, initialRotation);

    if (game->playerUnit) {
        // Apply lighting shader to player model
        game->unitManager.applyShaderToModels(
            sceneRendererGetShader(&game->sceneRenderer));

        TraceLog(LOG_INFO, "Spawned player unit '%s' at (%.2f, %.2f) rot=%.1f deg",
                 playerDef->name.c_str(), spawnPos.x, spawnPos.y,
                 initialRotation * RAD2DEG);
    } else {
        TraceLog(LOG_ERROR, "Failed to create player instance");
    }

    // Set initial desired rotation (from test config or default)
    game->playerDesiredRotation = game->testConfig.enabled ?
        game->testConfig.targetRotation * DEG2RAD : 0.0f;
}

//------------------------------------------------------------------------------
// Enemy Spawning
//------------------------------------------------------------------------------

static void game_spawn_enemies(Game* game) {
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) return;

    const LevelRenderData& renderData = game->levelRenderData[game->currentLevel];
    if (renderData.waypointPositions.empty()) {
        TraceLog(LOG_WARNING, "No waypoints for level %d, skipping enemy spawn", game->currentLevel);
        return;
    }

    // Get spawn definition for current level (ship index 0)
    const LevelSpawnDef* spawnDef = getSpawnDef(0, game->currentLevel);
    if (!spawnDef) {
        TraceLog(LOG_WARNING, "No spawn definition for level %d", game->currentLevel);
        return;
    }

    // Find player waypoint index (closest to player position)
    int playerWaypointIdx = 0;
    if (game->playerUnit && game->playerUnit->rootSection) {
        Vector2 playerPos = game->playerUnit->rootSection->worldPosition;
        float bestDist = 1e9f;
        for (int i = 0; i < (int)renderData.waypointPositions.size(); i++) {
            Vector3 wp = renderData.waypointPositions[i];
            float dx = wp.x - playerPos.x;
            float dz = wp.z - playerPos.y;
            float dist = dx * dx + dz * dz;
            if (dist < bestDist) {
                bestDist = dist;
                playerWaypointIdx = i;
            }
        }
    }

    // Resolve spawns to concrete entries
    auto spawnEntries = resolveSpawns(*spawnDef,
        (int)renderData.waypointPositions.size(),
        playerWaypointIdx);

    if (spawnEntries.empty()) {
        TraceLog(LOG_INFO, "No enemies to spawn on level %d", game->currentLevel);
        return;
    }

    // Create enemy instances
    game->enemyUnits.clear();
    std::vector<UnitInstance*> enemies;

    for (const auto& spawn : spawnEntries) {
        std::string defId = "droid_class_" + std::to_string(spawn.classId);

        if (spawn.waypointIndex < 0 || spawn.waypointIndex >= (int)renderData.waypointPositions.size()) {
            TraceLog(LOG_WARNING, "Invalid waypoint index %d for spawn", spawn.waypointIndex);
            continue;
        }

        Vector3 wpPos = renderData.waypointPositions[spawn.waypointIndex];
        Vector2 spawnPos = {wpPos.x, wpPos.z};  // World X,Z -> Physics X,Y

        UnitInstance* enemy = game->unitManager.createInstance(defId, spawnPos, spawn.angle);
        if (enemy) {
            enemies.push_back(enemy);
            game->enemyUnits.push_back(enemy);
        } else {
            TraceLog(LOG_WARNING, "Failed to create enemy '%s'", defId.c_str());
        }
    }

    // Apply lighting shader to all unit models (including new enemies)
    game->unitManager.applyShaderToModels(
        sceneRendererGetShader(&game->sceneRenderer));

    // Initialize AI with the spawned enemies
    game->aiManager.init(spawnEntries,
        renderData.waypointPositions,
        renderData.waypointAdjacency,
        enemies);

    TraceLog(LOG_INFO, "Spawned %zu enemies on level %d", enemies.size(), game->currentLevel);
}

static void game_despawn_enemies(Game* game) {
    // Clear AI components (they reference enemy units)
    game->aiManager.components().clear();

    // Destroy enemy unit instances
    for (auto* enemy : game->enemyUnits) {
        if (enemy) {
            game->unitManager.destroyInstance(enemy);
        }
    }
    game->enemyUnits.clear();
}

//------------------------------------------------------------------------------
// Lift Tile Detection
//------------------------------------------------------------------------------

static std::vector<Vector2> game_find_lift_positions(const Game* game, int level) {
    std::vector<Vector2> positions;
    if (level < 0 || level >= (int)game->levels.size()) return positions;

    const TmxLevel& lvl = game->levels[level];

    for (int row = 0; row < lvl.height; row++) {
        for (int col = 0; col < lvl.width; col++) {
            int gid = lvl.tiles[row * lvl.width + col];
            if (gid == LIFT_TILE_GID_A || gid == LIFT_TILE_GID_B) {
                // Tile center in world/physics coordinates (worldScale = 1.0)
                float px = col + 0.5f;
                float py = row + 0.5f;
                positions.push_back({px, py});
            }
        }
    }

    return positions;
}

//------------------------------------------------------------------------------
// Debug Level Switching
//------------------------------------------------------------------------------

static void game_switch_level(Game* game, int newLevel) {
    if (newLevel < 0 || newLevel >= (int)game->levels.size()) return;
    if (newLevel == game->currentLevel) return;

    TraceLog(LOG_INFO, "Switching from level %d to level %d", game->currentLevel, newLevel);

    // 1. Despawn all enemies
    game_despawn_enemies(game);

    // 2. Destroy current collision bodies (static Box2D bodies)
    for (auto& body : game->collisionBodies) {
        if (body.valid) {
            b2DestroyBody(body.body_id);
            body.valid = false;
        }
    }
    game->collisionBodies.clear();

    // 3. Switch level
    game->currentLevel = newLevel;

    // 4. Build render data and collision data for new level
    if (!game_build_level_render_data(game)) {
        TraceLog(LOG_ERROR, "Failed to build render data for level %d", newLevel);
        return;
    }

    // 5. Create collision bodies for new level
    game_create_level_collision(game);

    // 6. Spawn enemies for new level
    game_spawn_enemies(game);

    // 7. Find lift positions in the new level and teleport player
    auto liftPositions = game_find_lift_positions(game, newLevel);

    if (!liftPositions.empty() && game->playerUnit && b2Body_IsValid(game->playerUnit->bodyId)) {
        Vector2 targetPos = liftPositions[0];

        // Move player out of the way during collision resolution
        b2Body_SetTransform(game->playerUnit->bodyId,
                            (b2Vec2){-100.0f, -100.0f},
                            b2Body_GetRotation(game->playerUnit->bodyId));
        b2Body_SetLinearVelocity(game->playerUnit->bodyId, (b2Vec2){0, 0});
        b2Body_SetAngularVelocity(game->playerUnit->bodyId, 0);

        // Simulate enemies away from the lift position if any are nearby
        const float clearRadius = 1.5f;
        float dt = 1.0f / 60.0f;
        int maxSteps = 300;  // up to 5 seconds of simulation
        Vector2 fakePlayerPos = {-1000.0f, -1000.0f};  // Keep enemies in patrol mode

        for (int step = 0; step < maxSteps; step++) {
            bool collision = false;
            for (auto* enemy : game->enemyUnits) {
                if (!enemy || !enemy->active || !enemy->rootSection) continue;
                Vector2 ePos = enemy->rootSection->worldPosition;
                float dx = ePos.x - targetPos.x;
                float dy = ePos.y - targetPos.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < clearRadius) {
                    collision = true;
                    break;
                }
            }
            if (!collision) break;

            // Advance AI and physics to move enemies along patrol routes
            game->aiManager.update(dt, fakePlayerPos,
                                   game->physics.world_id,
                                   &game->projectileManager);
            physics_world_step(&game->physics, dt);
            game->unitManager.update(dt);
        }

        // Teleport player to lift position
        b2Body_SetTransform(game->playerUnit->bodyId,
                            (b2Vec2){targetPos.x, targetPos.y},
                            b2Body_GetRotation(game->playerUnit->bodyId));
        b2Body_SetLinearVelocity(game->playerUnit->bodyId, (b2Vec2){0, 0});
        b2Body_SetAngularVelocity(game->playerUnit->bodyId, 0);

        // Sync unit positions from physics
        game->unitManager.update(0);

        TraceLog(LOG_INFO, "Moved player to lift at (%.1f, %.1f) on level %d",
                 targetPos.x, targetPos.y, newLevel);
    } else if (liftPositions.empty()) {
        TraceLog(LOG_WARNING, "No lift tiles found on level %d, player stays at current position", newLevel);
    }
}

//------------------------------------------------------------------------------
// Player Input Helpers
//------------------------------------------------------------------------------

static void game_update_player_rotation(Game* game) {
    if (!game->playerUnit || game->playerUnit->allSections.empty()) return;

    SectionInstance* root = game->playerUnit->rootSection.get();
    if (!root) return;

    // Get player position in physics space
    Vector2 playerPhysPos = root->worldPosition;

    // Get mouse position in world space
    Vector2 mouseScreen = game->input.mouse_pos;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Normalized screen coordinates (-1 to 1)
    // Screen Y is inverted (0 at top), so we flip it
    float normX = (mouseScreen.x / (float)screenWidth) * 2.0f - 1.0f;
    float normY = -((mouseScreen.y / (float)screenHeight) * 2.0f - 1.0f);

    // Estimate visible world area (rough approximation for top-down view)
    float cameraHeight = game->camera.position.y - game->camera.target.y;
    float halfVisibleHeight = cameraHeight * tanf(game->camera.fovy * 0.5f * DEG2RAD);
    float aspectRatio = (float)screenWidth / (float)screenHeight;
    float halfVisibleWidth = halfVisibleHeight * aspectRatio;

    // Convert mouse screen position to world coordinates
    // World X = screen X direction, World Z = screen Y direction (inverted by camera up)
    // Camera up is {0,0,-1}, so screen top → world -Z
    float mouseWorldX = game->camera.target.x + normX * halfVisibleWidth;
    float mouseWorldZ = game->camera.target.z - normY * halfVisibleHeight;

    // Convert world position to physics coordinates
    // Physics: X = World X, Y = World Z
    float mousePhysX = mouseWorldX;
    float mousePhysY = mouseWorldZ;

    // Calculate angle from player to mouse in world coordinates
    // World: X = screen right, Z = physics Y (screen down with camera up = -Z)
    // glTF model: +Z is forward
    //
    // We want: mouse right → model +Z points right (+X)
    //          mouse up → model +Z points up (-Z in world)
    //
    // Using atan2(x, z) gives angle from +Z axis toward +X axis
    // When mouse is at +X (right): atan2(positive, 0) = π/2
    // When mouse is at -Z (up): atan2(0, negative) = π
    //
    // DrawModelEx with right-hand rule around Y: positive angle rotates +Z toward +X
    // So render angle π/2 makes model face +X ✓
    // And render angle π makes model face -Z ✓
    //
    // Therefore: renderAngle = atan2(dx, dz) where dz is toward mouse in world Z

    float dx = mousePhysX - playerPhysPos.x;  // world X offset (screen right positive)
    float dz = mousePhysY - playerPhysPos.y;  // world Z offset (screen down positive)

    // atan2(dx, dz) gives the angle to rotate model +Z to face the mouse
    game->playerDesiredRotation = atan2f(dx, dz);
}

static void game_apply_player_rotation_torque(Game* game) {
    if (!game->playerUnit || !game->playerUnit->rootSection) return;
    if (!b2Body_IsValid(game->playerUnit->bodyId)) return;

    SectionInstance* root = game->playerUnit->rootSection.get();
    float current = root->worldRotation;
    float target = game->playerDesiredRotation;

    // Calculate shortest angular difference
    float diff = normalize_angle(target - current);

    float angular_vel = b2Body_GetAngularVelocity(game->playerUnit->bodyId);

    // Inertia-scaled PD gains
    float inertia = b2Body_GetInertiaTensor(game->playerUnit->bodyId);

    // Base gains tuned for inertia ~0.1 (droid_class_1 scale)
    // Scale proportionally so smaller bodies don't oscillate wildly
    float baseKp = 30.0f;
    float baseKd = 5.0f;
    float inertiaScale = inertia / 0.1f;  // Normalize to reference inertia
    if (inertiaScale < 0.1f) inertiaScale = 0.1f;  // Clamp minimum

    float Kp = baseKp * inertiaScale;
    float Kd = baseKd * inertiaScale;

    float torque = diff * Kp - angular_vel * Kd;

    // Clamp to max torque
    if (torque > MAX_TORQUE) torque = MAX_TORQUE;
    if (torque < -MAX_TORQUE) torque = -MAX_TORQUE;

    b2Body_ApplyTorque(game->playerUnit->bodyId, torque, true);
}

//------------------------------------------------------------------------------
// Game Update
//------------------------------------------------------------------------------

void game_update(Game* game, float dt) {
    // Test mode: check frame count and report
    if (game->testConfig.enabled) {
        game->testFrameCount++;

        // Report rotation at sample intervals
        if (game->playerUnit && game->playerUnit->rootSection) {
            SectionInstance* root = game->playerUnit->rootSection.get();
            if (root && (game->testFrameCount % game->testConfig.sampleInterval == 0)) {
                float currentRot = root->worldRotation * RAD2DEG;
                float targetRot = game->testConfig.targetRotation;
                float error = normalize_angle(game->playerDesiredRotation - root->worldRotation) * RAD2DEG;
                float angVel = b2Body_IsValid(game->playerUnit->bodyId)
                             ? b2Body_GetAngularVelocity(game->playerUnit->bodyId) : 0.0f;

                printf("Frame %4d: rot=%7.2f deg  target=%7.2f deg  error=%7.2f deg  angVel=%7.2f\n",
                       game->testFrameCount, currentRot, targetRot, error, angVel);
            }
        }

        // Exit after test frames completed
        if (game->testFrameCount >= game->testConfig.testFrames) {
            // Final report
            if (game->playerUnit && game->playerUnit->rootSection) {
                SectionInstance* root = game->playerUnit->rootSection.get();
                if (root) {
                    float finalRot = root->worldRotation * RAD2DEG;
                    float targetRot = game->testConfig.targetRotation;
                    float finalError = normalize_angle(game->playerDesiredRotation - root->worldRotation) * RAD2DEG;

                    printf("\n=== TEST COMPLETE ===\n");
                    printf("Final rotation: %.2f deg\n", finalRot);
                    printf("Target rotation: %.2f deg\n", targetRot);
                    printf("Final error: %.2f deg\n", finalError);
                    printf("Converged: %s\n", fabsf(finalError) < 5.0f ? "YES" : "NO");
                    printf("=====================\n");
                }
            }
            game->running = false;
            return;
        }
    }

    // Process input (skip in test mode)
    if (!game->testConfig.enabled) {
        input_update(&game->input);
    }

    // Apply input to player unit
    if (game->playerUnit && game->playerUnit->rootSection) {
        if (b2Body_IsValid(game->playerUnit->bodyId)) {
            // Apply movement force (skip in test mode - no movement)
            if (!game->testConfig.enabled) {
                Vector2 force = {
                    game->input.movement.x * MOVEMENT_FORCE,
                    game->input.movement.y * MOVEMENT_FORCE
                };
                b2Body_ApplyForceToCenter(game->playerUnit->bodyId, (b2Vec2){force.x, force.y}, true);

                // Update rotation from mouse (normal mode only)
                game_update_player_rotation(game);
            }

            // Apply rotation torque (always - this is what we're testing)
            game_apply_player_rotation_torque(game);
        }
    }

    // Handle quit (skip in test mode)
    if (!game->testConfig.enabled && game->input.quit) {
        game->running = false;
    }

    // Debug mode toggle (0-6)
    for (int i = 0; i <= 6; i++) {
        if (IsKeyPressed(KEY_ZERO + i)) {
            game->debugMode = i;
            sceneRendererSetDebugMode(&game->sceneRenderer, i);
        }
    }

    // Toggle normal mapping (N key)
    static bool normalMapEnabled = true;
    if (IsKeyPressed(KEY_N)) {
        normalMapEnabled = !normalMapEnabled;
        sceneRendererSetNormalMapEnabled(&game->sceneRenderer, normalMapEnabled);
    }

    // Debug: Level switching (Page Up / Page Down)
    if (IsKeyPressed(KEY_PAGE_UP)) {
        int newLevel = game->currentLevel + 1;
        if (newLevel < (int)game->levels.size()) {
            game_switch_level(game, newLevel);
        }
    }
    if (IsKeyPressed(KEY_PAGE_DOWN)) {
        int newLevel = game->currentLevel - 1;
        if (newLevel >= 0) {
            game_switch_level(game, newLevel);
        }
    }

    // Update AI (applies forces before physics step)
    if (game->playerUnit && game->playerUnit->rootSection) {
        b2Vec2 pp = b2Body_GetPosition(game->playerUnit->bodyId);
        Vector2 playerPos2D = {pp.x, pp.y};
        game->aiManager.update(dt, playerPos2D,
                               game->physics.world_id,
                               &game->projectileManager);
    }

    // Step physics
    physics_world_step(&game->physics, dt);

    // Update projectiles (lifetime, contact events, cleanup)
    game->projectileManager.update(dt);
    game->projectileManager.syncFromPhysics();
    game->projectileManager.processContactEvents(game->physics.world_id);
    game->projectileManager.cleanup();

    // Update unit manager (syncs physics transforms)
    game->unitManager.update(dt);

    // Camera follows player
    if (game->playerUnit && game->playerUnit->rootSection) {
        SectionInstance* root = game->playerUnit->rootSection.get();
        if (root) {
            // Map physics 2D to world 3D
            // Physics X -> World X, Physics Y -> World Z
            Vector3 playerPos = {
                root->worldPosition.x,
                0.0f,
                root->worldPosition.y
            };
            game->camera.target = playerPos;
            game->camera.position = (Vector3){playerPos.x, game->cameraHeight, playerPos.z};
        }
    }
}

//------------------------------------------------------------------------------
// Game Render
//------------------------------------------------------------------------------

void game_render(Game* game) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&game->sceneRenderer, game->camera.position);

    BeginMode3D(game->camera);

    // Draw level tiles
    if (game->currentLevel >= 0 &&
        game->currentLevel < (int)game->levelRenderData.size()) {
        LevelRenderData& data = game->levelRenderData[game->currentLevel];
        if (data.meshValid) {
            DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
        }
    }

    // Draw all units (player, enemies, etc.)
    game->unitManager.renderAll();

    // Debug: collision shapes (press C)
    if (IsKeyDown(KEY_C) && game->currentLevel >= 0 &&
        game->currentLevel < (int)game->levelCollisionData.size()) {
        drawCollisionDebug(game->levelCollisionData[game->currentLevel], RED, 0.05f);
    }

    // Debug: unit physics shapes (press U)
    if (IsKeyDown(KEY_U)) {
        game->unitManager.renderDebug();
    }

    EndMode3D();

    // HUD
    DrawFPS(10, 10);

    const char* debugModes[] = {
        "0:Normal", "1:Normals", "2:LightDir",
        "3:Specular", "4:ViewDir", "5:HalfDir", "6:BumpMap"
    };
    DrawText(TextFormat("Debug: %s (C=collision, U=units, N=normalmap)",
             debugModes[game->debugMode]), 10, 30, 16, WHITE);

    // Level info
    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        DrawText(TextFormat("Level %d: %s",
                 game->currentLevel,
                 game->levels[game->currentLevel].name.c_str()),
                 10, 50, 16, WHITE);
    }

    // Player info
    if (game->playerUnit && game->playerUnit->rootSection) {
        SectionInstance* root = game->playerUnit->rootSection.get();
        if (root) {
            float rotDeg = root->worldRotation * RAD2DEG;
            float desiredDeg = game->playerDesiredRotation * RAD2DEG;
            float errDeg = normalize_angle(game->playerDesiredRotation - root->worldRotation) * RAD2DEG;
            float angVel = b2Body_IsValid(game->playerUnit->bodyId)
                         ? b2Body_GetAngularVelocity(game->playerUnit->bodyId) : 0.0f;
            DrawText(TextFormat("Player: (%.1f, %.1f) rot=%s%06.1f",
                     root->worldPosition.x, root->worldPosition.y,
                     rotDeg >= 0 ? " " : "", rotDeg),
                     10, 70, 16, WHITE);
            DrawText(TextFormat("Desired: %s%06.1f  err=%s%05.1f  angVel=%s%05.1f",
                     desiredDeg >= 0 ? " " : "", desiredDeg,
                     errDeg >= 0 ? " " : "", errDeg,
                     angVel >= 0 ? " " : "", angVel),
                     10, 90, 16, WHITE);
        }
    }

    // Controls help
    DrawText("WASD: Move | Mouse: Aim | PgUp/PgDn: Level | 0-6: Debug | ESC: Quit",
             10, GetScreenHeight() - 25, 14, GRAY);

    EndDrawing();
}

//------------------------------------------------------------------------------
// Game Destroy
//------------------------------------------------------------------------------

void game_destroy(Game* game) {
    // Despawn enemies and clear spawn config
    game_despawn_enemies(game);
    clearSpawnConfig();

    // Destroy unit manager (cleans up all units and their physics)
    game->unitManager.destroy();
    game->playerUnit = nullptr;

    // Free level render data
    for (auto& data : game->levelRenderData) {
        freeLevelRenderData(&data);
    }
    game->levelRenderData.clear();

    // Free collision data
    for (auto& data : game->levelCollisionData) {
        freeLevelCollisionData(&data);
    }
    game->levelCollisionData.clear();

    // Clear collision bodies vector (bodies destroyed with physics world)
    game->collisionBodies.clear();

    // Unload textures
    if (game->atlasTexture.id > 0) {
        UnloadTexture(game->atlasTexture);
        game->atlasTexture = {0};
    }
    if (game->bumpAtlasTexture.id > 0) {
        UnloadTexture(game->bumpAtlasTexture);
        game->bumpAtlasTexture = {0};
    }

    // Destroy scene renderer
    sceneRendererDestroy(&game->sceneRenderer);

    // Destroy physics world
    physics_world_destroy(&game->physics);
}
