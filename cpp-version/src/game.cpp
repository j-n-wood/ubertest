#include "game.h"
#include "world_scale.h"
#include "transfer_control.h"
#include "rendering/texture_manager.h"
#include "rendering/render_scope.h"
#include "level/spawn_config.h"
#include "units/movement_tuning.h"
#include "units/heal.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "rlgl.h"
#include <algorithm>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

#define PI 3.14159265358979323846f

// Forward declarations
static bool game_load_levels(Game* game);
static bool game_build_level_render_data(Game* game);
static void game_create_level_collision(Game* game);
static void game_create_doors(Game* game);
static std::vector<DoorSpec> game_detect_doors(const Game* game);
static void game_create_chargers(Game* game);
static std::vector<ChargerSpec> game_detect_chargers(const Game* game);
static void game_create_consoles(Game* game);
static std::vector<ConsoleSpec> game_detect_consoles(const Game* game);
static void game_spawn_player(Game* game);
static void game_spawn_enemies(Game* game);
static void game_despawn_enemies(Game* game);
static void game_switch_level(Game* game, int newLevel);
static void game_change_level(Game* game, int newLevel, const Vector2* target);
static void game_teleport_player(Game* game, Vector2 targetPos);
static void game_update_player_rotation(Game* game);
static void game_update_player_turret(Game* game, float dt);
static void game_deactivate_level(Game* game, int level);
static void game_reactivate_current_level(Game* game);
static void game_reap_dead(Game* game);
static void game_update_score(Game* game, float dt);
static void game_update_player_fire(Game* game, float dt);

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

    // Set effective eye height for specular calculations (world-space height → metric)
    game->effectiveEyeHeight = 1.0f * WORLD_SCALE;
    sceneRendererSetEffectiveEyeHeight(&game->sceneRenderer, game->effectiveEyeHeight);

    // Set ambient light
    sceneRendererSetAmbient(&game->sceneRenderer, 0.15f, 0.15f, 0.15f, 1.0f);

    // Initialize UnitManager with physics world
    // UnitManager strips "models/" prefix and appends to base path, so use assets/models/
    std::string modelsPath = game->assetPath + "/models/";
    game->unitManager.init(game->physics.world_id, modelsPath.c_str());
    game->unitManager.setModelCache(&game->modelCache);  // share GLTF models across instances

    // Load all levels
    if (!game_load_levels(game)) {
        TraceLog(LOG_ERROR, "Failed to load levels");
        game->running = false;
        return;
    }

    // One retained Box2D world per level (separate worlds => no cross-level overlap; only
    // the active world is stepped). Level 0 reuses the world created by physics_world_init;
    // the rest are fresh. Each world gets a static origin body for unit motor joints.
    {
        int n = (int)game->levels.size();
        game->levelRuntime.clear();
        game->levelRuntime.resize(n);   // sized once; parallel to levels
        for (int L = 0; L < n; ++L) {
            if (L == 0) {
                game->levelRuntime[0].world = game->physics.world_id;  // reuse the init world
            } else {
                b2WorldDef wd = b2DefaultWorldDef();
                wd.gravity = (b2Vec2){0.0f, 0.0f};
                game->levelRuntime[L].world = b2CreateWorld(&wd);
            }
            game->levelRuntime[L].origin = unit_create_origin_body(game->levelRuntime[L].world);
        }
    }

    // Build render data for starting level (level_0_maintenance)
    game->currentLevel = 0;
    game->physics.world_id = game->levelRuntime[0].world;  // active world
    if (!game_build_level_render_data(game)) {
        TraceLog(LOG_ERROR, "Failed to build render data for level %d", game->currentLevel);
    }

    // Create collision bodies from tile data
    game_create_level_collision(game);

    // Create door + charger + console entities from tile data
    game_create_doors(game);
    game_create_chargers(game);
    game_create_consoles(game);
    game->effectManager.init(game->physics.world_id);  // bind active world (no tiles; dynamic)
    game->particleManager.clear();                      // render-only; no world binding

    // Setup camera (top-down view)
    game->cameraHeight = 10.0f * WORLD_SCALE;  // metric; may change based on unit type
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

    // Build the elevator graph from every level's lift objects (persists across
    // level switches — stops reference stable runtime level indices).
    game->liftManager.build(game->levels);

    // Load the side-on ship rendering data (for the ship-view page).
    std::string shipMapPath = game->assetPath + "/ships/ship1/shipmap.json";
    if (game->shipMap.load(shipMapPath)) {
        TraceLog(LOG_INFO, "Loaded ship map from %s", shipMapPath.c_str());
    } else {
        TraceLog(LOG_WARNING, "Failed to load ship map from %s", shipMapPath.c_str());
    }
    // Ship-view images: few and small, so load once and retain (freed by the manager on
    // exit). ShipViewPage just reads TEX_SHIP_MAP*.
    {
        const std::string shipBase = game->assetPath + "/ships/ship1/";
        if (game->shipMap.loaded() && !game->shipMap.imageName().empty()) {
            gTextures().loadFile(TEX_SHIP_MAP, shipBase + game->shipMap.imageName());
        }
        if (game->shipMap.loaded() && !game->shipMap.imageLitName().empty()) {
            gTextures().loadFile(TEX_SHIP_MAP_LIT, shipBase + game->shipMap.imageLitName());
        }
    }

    // Load weapon definitions (activates AI + player firing) and the projectile sprite.
    std::string weaponsPath = game->assetPath + "/data/weapons.json";
    if (loadWeaponsFromFile(weaponsPath)) {
        TraceLog(LOG_INFO, "Loaded %d weapons from %s", weaponCount(), weaponsPath.c_str());
    } else {
        TraceLog(LOG_WARNING, "Failed to load weapons from %s", weaponsPath.c_str());
    }
    gTextures().loadFile(TEX_FLARE, game->assetPath + "/textures/effects/flare.png");
    gTextures().loadFile(TEX_BLASTER_BLOB, game->assetPath + "/textures/effects/blaster_blob.png");
    // ASMD blast animation (weapon 3): a single 4x1 sprite sheet (frames selected by source
    // rect, not by rebinding textures). Frame count is fixed here; a filename like "asmd4x1"
    // could encode it later.
    gTextures().loadFile(TEX_ASMD, game->assetPath + "/textures/effects/asmd4x1.png");
    // Explosion effect animation (8x1 sheet). See docs/effects.md.
    gTextures().loadFile(TEX_RLBOOM, game->assetPath + "/textures/effects/rlboom.png");
    // Beam weapon frame sets (3 frames each, weapon 1 = plasma, weapon 8 = lightning). The
    // beam quad tiles the texture along its length, so set REPEAT wrap on each frame.
    {
        const char* beamFiles[] = {
            "beam_plasma_0.png", "beam_plasma_1.png", "beam_plasma_2.png",
            "beam_lightning_0.png", "beam_lightning_1.png", "beam_lightning_2.png",
        };
        for (int i = 0; i < 6; ++i) {
            TextureId id = (TextureId)(TEX_BEAM_PLASMA_0 + i);
            if (gTextures().loadFile(id, game->assetPath + "/textures/effects/" + beamFiles[i]))
                SetTextureWrap(gTextures().get(id), TEXTURE_WRAP_REPEAT);
        }
    }
    // Player weapon state (the device's plasma bolt; re-inited if a captured unit is armed).
    if (game->playerUnit && game->playerUnit->definition) {
        game->playerWeapon = initWeaponState(game->playerUnit->definition->properties);
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
            gTextures().set(TEX_TILE_ATLAS, loadTilesetTexture(game->tileset, game->levelsPath));
            if (!gTextures().loaded(TEX_TILE_ATLAS)) {
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
        if (!gTextures().loadFile(TEX_TILE_BUMP, bumpPath)) {
            TraceLog(LOG_WARNING, "Failed to load bump atlas: %s", bumpPath.c_str());
        } else {
            TraceLog(LOG_INFO, "Loaded bump atlas: %dx%d",
                     gTextures().get(TEX_TILE_BUMP).width, gTextures().get(TEX_TILE_BUMP).height);
        }
    } else {
        TraceLog(LOG_WARNING, "No tile properties found, using Tilemap mode");
        // TEX_TILE_BUMP left empty (id 0).
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

// The tileset colour row the active level should render on: its base row (map property
// `tileRow`), or the last ("lights out") row once the level is cleared. Clamped to the atlas'
// row range. Shared by the floor mesh and the animated door/charger tiles so they stay in sync.
static int game_effective_tile_row(const Game* game) {
    int L = game->currentLevel;
    if (L < 0 || L >= (int)game->levels.size()) return 0;
    int totalRows = game->tileset.columns > 0 ? game->tileset.tileCount / game->tileset.columns : 1;
    int lastRow = totalRows > 0 ? totalRows - 1 : 0;
    bool cleared = L < (int)game->levelRuntime.size() && game->levelRuntime[L].cleared;
    int row = cleared ? lastRow : game->levels[L].tileRow;
    if (row < 0) row = 0;
    if (row > lastRow) row = lastRow;
    return row;
}

// Objects3D is the "3D" path: 3D block doors (and, once wired, converted 3D geometry). The 2D tile
// modes (Tilemap/CustomTiles) keep the animated tile doors. Both use the same top-down perspective
// camera looking straight down the Y axis — the perspective projection itself gives off-axis
// doors/walls (and unit shadows) their apparent height; there is no camera tilt.
static bool game_mode_is_3d(const Game* game) {
    return game->levelRenderMode == LevelRenderMode::Objects3D;
}

// Human-readable name for the active level renderer (HUD + logs).
static const char* game_level_render_mode_name(LevelRenderMode m) {
    switch (m) {
        case LevelRenderMode::Tilemap:     return "Tilemap (flat)";
        case LevelRenderMode::CustomTiles: return "CustomTiles (bump)";
        case LevelRenderMode::Objects3D:   return "Objects3D (3D - placeholder)";
    }
    return "?";
}

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
    collision = generateLevelCollision(level, game->tileset, WORLD_SCALE);
    TraceLog(LOG_INFO, "Generated %zu collision rects for level %d",
             collision.rects.size(), game->currentLevel);

    // Exclude door + charger cells from the baked tile mesh — those are drawn
    // (animated) by their own renderers. Collision is unaffected: door tiles carry
    // no collision rects and chargers are non-colliding.
    TmxLevel meshLevel = level;
    for (const DoorSpec& s : game_detect_doors(game)) {
        int idx = s.row * meshLevel.width + s.col;
        if (idx >= 0 && idx < (int)meshLevel.tiles.size()) meshLevel.tiles[idx] = 0;
    }
    for (const ChargerSpec& s : game_detect_chargers(game)) {
        int idx = s.row * meshLevel.width + s.col;
        if (idx >= 0 && idx < (int)meshLevel.tiles.size()) meshLevel.tiles[idx] = 0;
    }

    // Select the active level renderer. This is the swap seam (see Game::levelRenderMode):
    //   Tilemap     — flat tileset-atlas mesh
    //   CustomTiles — per-tile bump-mapped mesh
    //   Objects3D   — converted 3D geometry (GLTF bundle) — roadmap Phase 6/7, not yet wired,
    //                 so it falls back to CustomTiles for now (keeps the game playable).
    LevelRenderMode mode = game->levelRenderMode;
    if (mode == LevelRenderMode::Objects3D) {
        TraceLog(LOG_WARNING, "Level renderer 'Objects3D' not yet implemented "
                 "(needs the exported 3D bundle — roadmap Phase 6/7); using CustomTiles.");
        mode = LevelRenderMode::CustomTiles;
    }

    // Create render data structure
    data = createLevelRenderData(meshLevel, game->tileset, mode, WORLD_SCALE);

    // Effective tileset colour row (base row, or the darkened last row when cleared).
    int effectiveRow = game_effective_tile_row(game);

    // Build the tile mesh for the selected mode. CustomTiles needs tile properties + a bump
    // texture; without them (or in Tilemap mode) fall back to the flat atlas mesh.
    bool useBump = (mode == LevelRenderMode::CustomTiles) &&
                   game->tileProperties.valid && gTextures().loaded(TEX_TILE_BUMP);
    if (useBump) {
        data.tileMesh = createLevelTileMeshCustom(
            meshLevel, game->tileset, game->tileProperties,
            gTextures().get(TEX_TILE_BUMP).width, gTextures().get(TEX_TILE_BUMP).height, WORLD_SCALE,
            effectiveRow);
        TraceLog(LOG_INFO, "Level renderer: CustomTiles (bump) mesh (row %d)", effectiveRow);
    } else {
        data.tileMesh = createLevelTileMesh(meshLevel, game->tileset, WORLD_SCALE, effectiveRow);
        TraceLog(LOG_INFO, "Level renderer: Tilemap (flat) mesh (row %d)", effectiveRow);
    }

    if (data.tileMesh.vertexCount > 0) {
        // Create model with textures
        Texture2D bumpTex = gTextures().loaded(TEX_TILE_BUMP) ?
                            gTextures().get(TEX_TILE_BUMP) : Texture2D{0};

        data.tileModel = createLevelTileModel(
            data.tileMesh, gTextures().get(TEX_TILE_ATLAS), bumpTex, &game->sceneRenderer);
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
// Doors
//------------------------------------------------------------------------------

// Scan the current level's tiles for door cells and build source-agnostic specs.
// Local tile ids (gid - firstGid): 18-22 = horizontal door frames, 27-31 = vertical.
// Maps are authored closed (18/27) but any frame is accepted.
static std::vector<DoorSpec> game_detect_doors(const Game* game) {
    std::vector<DoorSpec> specs;
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) {
        return specs;
    }
    const TmxLevel& lvl = game->levels[game->currentLevel];
    const int firstGid = game->tileset.firstGid;
    const float halfW = lvl.width * 0.5f;   // worldScale = 1.0
    const float halfH = lvl.height * 0.5f;

    for (int row = 0; row < lvl.height; row++) {
        for (int col = 0; col < lvl.width; col++) {
            int gid = lvl.tiles[row * lvl.width + col];
            if (gid <= 0) continue;
            int localId = gid - firstGid;

            // Door-ness, orientation, and initial openness come from custom TSX tile
            // properties (type="door", orientation, closed), not hardcoded indices.
            auto it = game->tileset.tileProperties.find(localId);
            if (it == game->tileset.tileProperties.end() || !it->second.isDoor()) continue;
            const TmxTileProperties& tp = it->second;

            DoorSpec s;
            s.col = col;
            s.row = row;
            s.orientation = (tp.orientation == "vertical") ? DoorOrientation::Vertical
                                                           : DoorOrientation::Horizontal;
            s.size = (s.orientation == DoorOrientation::Horizontal)
                         ? Vector2{1.0f * WORLD_SCALE, 0.5f * WORLD_SCALE}
                         : Vector2{0.5f * WORLD_SCALE, 1.0f * WORLD_SCALE};
            s.initialClosed = tp.closed;
            // Tile centre in physics/world coords (centred on origin, matching walls).
            s.physicsCenter = {(col + 0.5f - halfW) * WORLD_SCALE, (row + 0.5f - halfH) * WORLD_SCALE};
            specs.push_back(s);
        }
    }
    return specs;
}

static void game_create_doors(Game* game) {
    std::vector<DoorSpec> specs = game_detect_doors(game);
    game->doorManager.init(game->physics.world_id, specs);

    // Build both presentations; the active level-render mode picks which one draws each frame.
    // 2D animated-tile doors (Tilemap/CustomTiles):
    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        Texture2D bumpTex = gTextures().loaded(TEX_TILE_BUMP) ? gTextures().get(TEX_TILE_BUMP) : Texture2D{0};
        game->doorRenderer.build(game->levels[game->currentLevel], game->tileset,
                                 game->tileProperties, gTextures().get(TEX_TILE_ATLAS), bumpTex,
                                 &game->sceneRenderer, game->doorManager.views(),
                                 game_effective_tile_row(game));
    }
    // 3D block doors (Objects3D): a shared, level-independent model (idempotent build).
    game->door3DRenderer.build(&game->sceneRenderer,
                               (game->assetPath + "/models/scenery/door.gltf").c_str());
    TraceLog(LOG_INFO, "Created %zu doors from level data", specs.size());
}

//------------------------------------------------------------------------------
// Chargers
//------------------------------------------------------------------------------

// Scan the current level's tiles for charger cells (custom property type="charger").
static std::vector<ChargerSpec> game_detect_chargers(const Game* game) {
    std::vector<ChargerSpec> specs;
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) {
        return specs;
    }
    const TmxLevel& lvl = game->levels[game->currentLevel];
    const int firstGid = game->tileset.firstGid;
    const float halfW = lvl.width * 0.5f;   // worldScale = 1.0
    const float halfH = lvl.height * 0.5f;

    for (int row = 0; row < lvl.height; row++) {
        for (int col = 0; col < lvl.width; col++) {
            int gid = lvl.tiles[row * lvl.width + col];
            if (gid <= 0) continue;
            auto it = game->tileset.tileProperties.find(gid - firstGid);
            if (it == game->tileset.tileProperties.end() || it->second.type != "charger") continue;

            ChargerSpec s;
            s.col = col;
            s.row = row;
            s.size = {1.0f * WORLD_SCALE, 1.0f * WORLD_SCALE};  // one tile footprint (metric)
            s.physicsCenter = {(col + 0.5f - halfW) * WORLD_SCALE, (row + 0.5f - halfH) * WORLD_SCALE};
            specs.push_back(s);
        }
    }
    return specs;
}

static void game_create_chargers(Game* game) {
    std::vector<ChargerSpec> specs = game_detect_chargers(game);
    game->chargerManager.init(game->physics.world_id, specs);

    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        Texture2D bumpTex = gTextures().loaded(TEX_TILE_BUMP) ? gTextures().get(TEX_TILE_BUMP) : Texture2D{0};
        game->chargerRenderer.build(game->levels[game->currentLevel], game->tileset,
                                    game->tileProperties, gTextures().get(TEX_TILE_ATLAS), bumpTex,
                                    &game->sceneRenderer, game->chargerManager.views(),
                                    game_effective_tile_row(game));
    }
    TraceLog(LOG_INFO, "Created %zu chargers from level data", specs.size());
}

//------------------------------------------------------------------------------
// Consoles
//------------------------------------------------------------------------------

// Scan the current level's tiles for console cells (custom property type="console").
static std::vector<ConsoleSpec> game_detect_consoles(const Game* game) {
    std::vector<ConsoleSpec> specs;
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) {
        return specs;
    }
    const TmxLevel& lvl = game->levels[game->currentLevel];
    const int firstGid = game->tileset.firstGid;
    const float halfW = lvl.width * 0.5f;   // worldScale = 1.0
    const float halfH = lvl.height * 0.5f;

    for (int row = 0; row < lvl.height; row++) {
        for (int col = 0; col < lvl.width; col++) {
            int gid = lvl.tiles[row * lvl.width + col];
            if (gid <= 0) continue;
            auto it = game->tileset.tileProperties.find(gid - firstGid);
            if (it == game->tileset.tileProperties.end() || it->second.type != "console") continue;

            ConsoleSpec s;
            s.col = col;
            s.row = row;
            s.physicsCenter = {(col + 0.5f - halfW) * WORLD_SCALE, (row + 0.5f - halfH) * WORLD_SCALE};
            specs.push_back(s);
        }
    }
    return specs;
}

static void game_create_consoles(Game* game) {
    std::vector<ConsoleSpec> specs = game_detect_consoles(game);
    game->consoleManager.init(specs);
    TraceLog(LOG_INFO, "Created %zu consoles from level data", specs.size());
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

    // Prefer a lift tile on the starting level (so the player begins on a lift, ready to
    // travel). Falls back to the waypoint spawn above if the level has no lift.
    if (!game->testConfig.enabled &&
        game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        const TmxLevel& lvl = game->levels[game->currentLevel];
        if (!lvl.lifts.empty()) {
            const TmxLift& lift = lvl.lifts[0];
            spawnPos = {(lift.col + 0.5f - lvl.width * 0.5f) * WORLD_SCALE,
                        (lift.row + 0.5f - lvl.height * 0.5f) * WORLD_SCALE};
            TraceLog(LOG_INFO, "Spawning player on lift tile: (%.2f, %.2f)", spawnPos.x, spawnPos.y);
        }
    }

    // In test mode, offset spawn position to avoid nearby geometry
    if (game->testConfig.enabled) {
        spawnPos.x += -1.0f * WORLD_SCALE;  // Offset in physics X (world X)
        spawnPos.y += 1.0f * WORLD_SCALE;   // Offset in physics Y (world Z)
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

    // Create the level's persistent roster in ITS OWN world (they live there for the
    // ship's lifetime; frozen when the level is inactive). Fixed once per level.
    const int L = game->currentLevel;
    b2WorldId world = game->levelRuntime[L].world;
    b2BodyId origin = game->levelRuntime[L].origin;
    game->levelRuntime[L].units.clear();
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

        UnitInstance* enemy = game->unitManager.createInstance(defId, spawnPos, spawn.angle,
                                                               world, origin);
        if (enemy) {
            enemy->levelIndex = L;
            enemies.push_back(enemy);
            game->levelRuntime[L].units.push_back(enemy);
            game->enemyUnits.push_back(enemy);
        } else {
            TraceLog(LOG_WARNING, "Failed to create enemy '%s'", defId.c_str());
        }
    }
    game->levelRuntime[L].populated = true;
    game->levelRuntime[L].hadEnemies = !game->levelRuntime[L].units.empty();  // gates lights-out

    // Apply lighting shader to all unit models (including new enemies)
    game->unitManager.applyShaderToModels(
        sceneRendererGetShader(&game->sceneRenderer));

    // Initialize AI with the spawned enemies
    game->aiManager.init(spawnEntries,
        renderData.waypointPositions,
        renderData.waypointAdjacency,
        enemies);

    TraceLog(LOG_INFO, "Populated %zu enemies on level %d", enemies.size(), L);
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
// Level Switching + player placement
//------------------------------------------------------------------------------

// Teleport the player to a world/physics position, clearing nearby enemies first so
// the player doesn't materialise inside one. Re-points the motor-joint target so the
// unit holds the new spot rather than being dragged back toward its old target.
static void game_teleport_player(Game* game, Vector2 targetPos) {
    if (!game->playerUnit || !b2Body_IsValid(game->playerUnit->bodyId)) return;

    // Move player out of the way during collision resolution
    float playerAngle = b2Rot_GetAngle(b2Body_GetRotation(game->playerUnit->bodyId));
    b2Body_SetTransform(game->playerUnit->bodyId,
                        (b2Vec2){-100.0f, -100.0f},
                        b2Body_GetRotation(game->playerUnit->bodyId));
    b2Body_SetLinearVelocity(game->playerUnit->bodyId, (b2Vec2){0, 0});
    b2Body_SetAngularVelocity(game->playerUnit->bodyId, 0);
    unit_set_move_target(game->playerUnit, {-100.0f, -100.0f}, playerAngle);

    // Simulate enemies away from the target if any are nearby
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
            if (dist < clearRadius) { collision = true; break; }
        }
        if (!collision) break;

        game->aiManager.update(dt, fakePlayerPos,
                               game->physics.world_id,
                               &game->projectileManager);
        physics_world_step(&game->physics, dt);
        game->unitManager.update(dt);
    }

    b2Body_SetTransform(game->playerUnit->bodyId,
                        (b2Vec2){targetPos.x, targetPos.y},
                        b2Body_GetRotation(game->playerUnit->bodyId));
    b2Body_SetLinearVelocity(game->playerUnit->bodyId, (b2Vec2){0, 0});
    b2Body_SetAngularVelocity(game->playerUnit->bodyId, 0);
    unit_set_move_target(game->playerUnit, targetPos,
                         b2Rot_GetAngle(b2Body_GetRotation(game->playerUnit->bodyId)));
    game->unitManager.update(0);

    TraceLog(LOG_INFO, "Moved player to (%.1f, %.1f) on level %d",
             targetPos.x, targetPos.y, game->currentLevel);
}

// Freeze a level being left: its droids stop updating/rendering (active=false) and remain
// in their own world (positions persist). Also tears down the transient collision bodies
// of that level's world; doors/chargers are rebuilt on the next activation.
static void game_deactivate_level(Game* game, int level) {
    if (level < 0 || level >= (int)game->levelRuntime.size()) return;
    for (UnitInstance* u : game->levelRuntime[level].units) {
        if (u) u->active = false;
    }
    game->levelRuntime[level].lastActive = game->gameClock;
    game->aiManager.components().clear();

    for (auto& body : game->collisionBodies) {
        if (body.valid) { b2DestroyBody(body.body_id); body.valid = false; }
    }
    game->collisionBodies.clear();
}

// Whether the active level has anything worth rolling forward during catch-up. Today that is
// simply live enemy units; kept as an explicit predicate (tested in the roll-forward, not
// assumed) so future off-screen sim sources — moving hazards, timed doors, spreading fire —
// can extend the condition rather than the empty-unit check being baked in.
static bool game_level_has_simulation(const Game* game) {
    return !game->enemyUnits.empty();
}

// True while any hostile (non-captured) enemy remains on the active level. Dead units are
// already reaped out of enemyUnits; the player-captured unit is retained-but-flagged, so this
// treats destroyed AND captured as "gone" — the condition for the level's lights to go out.
static bool game_level_hostiles_remain(const Game* game) {
    for (UnitInstance* u : game->enemyUnits) {
        if (u && u != game->transfer.captured) return true;
    }
    return false;
}

// Roll the active level's simulation forward `seconds` of game-time so its droids are where
// they would have wandered while the player was elsewhere — instead of teleporting them.
// Headless: no input, no rendering, no model animation; patrol-only (a far-away synthetic
// player keeps the AI in Patrol and nothing fires). Coarse fixed step + a cap keep it fast
// (it runs in a single frame, i.e. faster than real time). Requires the level to be active
// (physics.world_id/AI already repointed) — call it at the end of reactivation.
static void game_simulate_level_catchup(Game* game, double seconds) {
    if (seconds <= 0.0) return;
    if (!game_level_has_simulation(game)) return;  // nothing to advance — skip

    constexpr float CATCHUP_DT = 0.1f;            // 10 Hz: patrol movement is slow/smooth enough
    constexpr double CATCHUP_MAX_SECONDS = 60.0;  // bound the modelled interval (and the cost)
    double simSeconds = seconds < CATCHUP_MAX_SECONDS ? seconds : CATCHUP_MAX_SECONDS;
    int steps = (int)(simSeconds / CATCHUP_DT);
    if (steps <= 0) return;

    const Vector2 noPlayer = {1.0e6f, 1.0e6f};  // out of every detection range → AI stays patrolling
    for (int i = 0; i < steps; ++i) {
        // No projectiles/beams/playerUnit → nothing fires; doors still open on proximity so
        // patrol routes through them; unitManager.update is skipped (render/animation only —
        // the AI reads body transforms straight from Box2D).
        game->aiManager.update(CATCHUP_DT, noPlayer, game->physics.world_id,
                               nullptr, nullptr, nullptr);
        game->doorManager.update(CATCHUP_DT);
        physics_world_step(&game->physics, CATCHUP_DT);
        game->aiManager.processCollisions(game->physics.world_id);
    }
}

// Re-enter a level that was already populated: wake its droids where they were left (NO
// teleport — they persist in place), heal them for the time away, rebuild patrol AI resuming
// from the waypoint nearest each droid, then roll the level forward by the time it was away.
static void game_reactivate_current_level(Game* game) {
    const int L = game->currentLevel;
    if (L < 0 || L >= (int)game->levelRuntime.size()) return;
    const LevelRenderData& rd = game->levelRenderData[L];

    const double away = game->gameClock - game->levelRuntime[L].lastActive;

    game->enemyUnits.clear();
    std::vector<SpawnEntry> spawns;
    std::vector<UnitInstance*> enemies;
    for (UnitInstance* u : game->levelRuntime[L].units) {
        if (!u || !u->definition || !b2Body_IsValid(u->bodyId)) continue;
        u->active = true;
        // Regenerate health for the time the level was inactive (single pass).
        u->combatState.currentHealth = away_healed_health(
            u->combatState.currentHealth, u->combatState.maxHealth,
            away, AWAY_HEAL_FRACTION_PER_SEC);
        game->enemyUnits.push_back(u);

        // The droid stays exactly where it froze. Resume patrol from the waypoint nearest its
        // current position, so it continues from where it is and never jumps across a
        // disconnected part of the level. (Off-course/stuck recovery in the AI keeps it within
        // its own reachable region if the nearest node isn't straight-line reachable.)
        b2Vec2 p = b2Body_GetPosition(u->bodyId);
        int nearest = rd.waypointPositions.empty() ? -1 : 0;
        float best = 1.0e30f;
        for (int i = 0; i < (int)rd.waypointPositions.size(); ++i) {
            float dx = rd.waypointPositions[i].x - p.x;
            float dz = rd.waypointPositions[i].z - p.y;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; nearest = i; }
        }

        SpawnEntry e;
        e.classId = u->definition->properties.classId;
        e.waypointIndex = nearest;
        e.angle = b2Rot_GetAngle(b2Body_GetRotation(u->bodyId));
        spawns.push_back(e);
        enemies.push_back(u);
    }
    game->aiManager.init(spawns, rd.waypointPositions, rd.waypointAdjacency, enemies);

    // Simulate the time the player was away so the roster is where it would have wandered.
    game_simulate_level_catchup(game, away);

    TraceLog(LOG_INFO, "Reactivated %zu droids on level %d (away %.1fs, caught up)",
             enemies.size(), L, away);
}

// Remove droids that have been destroyed (health depleted) from the active roster. The
// captured unit is handled by the transfer controller, so it is skipped here.
static void game_reap_dead(Game* game) {
    const int L = game->currentLevel;
    if (L < 0 || L >= (int)game->levelRuntime.size()) return;

    std::vector<UnitInstance*> dead;
    for (UnitInstance* u : game->enemyUnits) {
        if (!u || u == game->transfer.captured) continue;
        if (!u->combatState.alive || u->combatState.currentHealth <= 0.0f) dead.push_back(u);
    }
    if (dead.empty()) return;

    auto drop = [](std::vector<UnitInstance*>& v, UnitInstance* u) {
        v.erase(std::remove(v.begin(), v.end(), u), v.end());
    };
    for (UnitInstance* u : dead) {
        game_award_points(game, u);  // score + alert for a direct kill (captured unit skipped above)
        // Explosion + sparks at the unit's position before it's freed (owner = its own group
        // so it doesn't self-damage). See docs/effects.md.
        if (b2Body_IsValid(u->bodyId)) {
            b2Vec2 p = b2Body_GetPosition(u->bodyId);
            game_spawn_explosion(game, {p.x, p.y}, u->collisionGroupId);
        }
        game->aiManager.forgetUnit(u);
        drop(game->levelRuntime[L].units, u);
        drop(game->enemyUnits, u);
        game->unitManager.destroyInstance(u);  // permanent — won't return on re-entry
    }
}

//------------------------------------------------------------------------------
// Score + alert
//------------------------------------------------------------------------------

void game_award_points(Game* game, const UnitInstance* unit) {
    if (!unit || !unit->definition) return;
    int pts = score_points_for_typecode(unit->definition->properties.typeCode);
    game->score += pts;
    game->alertLevel += pts;  // alert rises by the same amount as points scored
}

// Spark burst thrown by a destroyed unit (additive TEX_FLARE billboards). Tunable.
static const ParticleBurst EXPLOSION_SPARKS = {
    /*count*/ 16, /*speedMin*/ 1.5f, /*speedMax*/ 3.0f, /*lifeMin*/ 0.3f, /*lifeMax*/ 0.6f,
    /*startSize*/ 0.15f, /*endSize*/ 0.0f,
    /*startColor*/ {255, 200, 120, 255}, /*endColor*/ {255, 80, 0, 0},
    /*angularVelMax*/ 180.0f, /*texture*/ TEX_FLARE};

void game_spawn_explosion(Game* game, Vector2 pos, int32_t group) {
    game->effectManager.spawnExplosion(pos, group);      // animated blast + area damage
    game->particleManager.burst(EXPLOSION_SPARKS, pos);  // render-only spark spray
}

// Impact sparks: a directional burst of `count` sparks in colour `color`, reflected off a
// surface. Shared by beam impacts (one per rate-limited emission) and projectile impacts (one
// burst per hit). `count` and `color` come from the firing weapon (weapons.json). `incidentDir`
// is the travel direction; `normal` the surface normal. The reflection r = d − 2(d·n)n is
// sign-independent in n, so the normal's orientation doesn't matter. The spark fades to
// transparent over its life (same hue), so `color` is the bright core.
static void game_spawn_impact_sparks(Game* game, Vector2 pos, Vector2 incidentDir,
                                     Vector2 normal, Color color, int count) {
    if (count <= 0) return;
    float dn = incidentDir.x * normal.x + incidentDir.y * normal.y;
    Vector2 r = {incidentDir.x - 2.0f * dn * normal.x, incidentDir.y - 2.0f * dn * normal.y};
    if (fabsf(r.x) < 1e-6f && fabsf(r.y) < 1e-6f) r = incidentDir;  // degenerate normal

    ParticleBurst spark;
    spark.count = count;
    spark.speedMin = 2.0f;  spark.speedMax = 5.0f;
    spark.lifeMin = 0.22f;  spark.lifeMax = 0.50f;
    spark.startSize = 0.22f; spark.endSize = 0.0f;
    spark.angularVelMax = 360.0f;
    spark.texture = TEX_FLARE;
    spark.dirAngle = atan2f(r.y, r.x);
    spark.spreadRad = 40.0f * DEG2RAD;  // cone about the reflection
    spark.startColor = color;
    spark.endColor   = {color.r, color.g, color.b, 0};  // fade out, same hue
    game->particleManager.burst(spark, pos);
}

// The unit the player is currently driving: the captured unit when piloting, else the device.
static UnitInstance* game_controlled_unit(Game* game) {
    return game->transfer.captured ? game->transfer.captured : game->playerUnit;
}

// Per-frame score/alert step (called while not paused). See docs/scoring.md.
static void game_update_score(Game* game, float dt) {
    // Alert decays steadily toward green.
    game->alertLevel -= ALERT_DECAY_RATE * dt;
    if (game->alertLevel < 0.0) game->alertLevel = 0.0;

    // Alert band trickles score.
    game->score += alert_score_rate(alert_band(game->alertLevel)) * dt;

    // Recharge: while the controlled unit sits on a charger below max health, heal it and
    // drain score; both stop at full health.
    UnitInstance* cu = game_controlled_unit(game);
    if (cu && b2Body_IsValid(cu->bodyId) &&
        cu->combatState.currentHealth < cu->combatState.maxHealth) {
        b2Vec2 p = b2Body_GetPosition(cu->bodyId);
        bool onCharger = false;
        for (const ChargerView& c : game->chargerManager.views()) {
            if (fabsf(p.x - c.worldPos.x) <= c.size.x * 0.5f &&
                fabsf(p.y - c.worldPos.y) <= c.size.y * 0.5f) { onCharger = true; break; }
        }
        if (onCharger) {
            float maxH = cu->combatState.maxHealth;
            float h = cu->combatState.currentHealth + CHARGER_HEAL_FRACTION_PER_SEC * maxH * dt;
            cu->combatState.currentHealth = (h > maxH) ? maxH : h;
            game->score -= RECHARGE_DRAIN_RATE * dt;
        }
    }

    if (game->score < 0.0) game->score = 0.0;
    game->scoreDisplay = score_clock_step(game->scoreDisplay, game->score, dt);
}

// Player weapon firing (LMB). Fires the controlled unit's weapon, or the device's plasma
// bolt if the controlled unit is unarmed. Owner = controlled unit's group (no self-hit).
// See docs/weapons.md.
static void game_update_player_fire(Game* game, float dt) {
    if (game->transfer.mode == ControlMode::Transferring) return;  // input locked mid fly-over
    if (game->input.transferMode) return;  // RMB/Ctrl held: aiming a capture, not firing
    UnitInstance* cu = game_controlled_unit(game);
    if (!cu || !cu->definition || !b2Body_IsValid(cu->bodyId)) return;

    // Effective weapon: the controlled unit's own, else the device's plasma bolt.
    int weaponId = cu->definition->properties.weapon;
    if (weaponId < 0 && game->playerUnit && game->playerUnit->definition) {
        weaponId = game->playerUnit->definition->properties.weapon;
    }
    if (weaponId != game->playerWeapon.definition.id) {
        game->playerWeapon.definition = getWeaponDefinition(weaponId);
        game->playerWeapon.cooldownRemaining = 0.0f;
    }
    updateWeaponCooldown(game->playerWeapon, dt);

    if (!game->input.fire) return;
    const WeaponDefinition& w = game->playerWeapon.definition;

    // Aim along the unit's CURRENT firing facing. For a turret unit that's the turret's
    // (independently-slewed) angle — the turret determines the firing angle — otherwise the
    // body's current facing (the body may still be slewing toward the cursor, so the shot
    // leaves where it points now). Forward = {-sin, cos} (inverse of facing_angle_to).
    SectionInstance* turret = unit_find_section_by_role(cu, SectionRole::Turret);
    float a = turret ? turret->facingAngle : b2Rot_GetAngle(b2Body_GetRotation(cu->bodyId));
    float c = cosf(a), s = sinf(a);
    Vector2 dir = {-s, c};

    b2Vec2 bp = b2Body_GetPosition(cu->bodyId);
    // Fire offset (facing-relative: x = lateral, y = forward), clamped to the unit's collision
    // radius so a stray/old-data offset can't spawn the shot inside a wall.
    Vector2 off2d = {cu->definition->properties.fireOffset.x,
                     cu->definition->properties.fireOffset.y};
    float offLen = sqrtf(off2d.x * off2d.x + off2d.y * off2d.y);
    float maxOff = cu->definition->collisionRadius;
    if (offLen > maxOff && offLen > 1e-5f) {
        off2d.x *= maxOff / offLen;
        off2d.y *= maxOff / offLen;
    }
    auto spawnFrom = [&](Vector2 o) {
        return Vector2{bp.x + o.x * c - o.y * s, bp.y + o.x * s + o.y * c};
    };

    // Beam weapons: fire continuously while held (no fire-rate gate), damaging every enemy
    // the line passes through up to the first wall. Damage accumulates via the realtime
    // damage tick. Other weapons spawn a projectile gated by the cooldown.
    if (w.type == WeaponType::Beam) {
        game->beamManager.fire(game->physics.world_id, spawnFrom(off2d), a, w.maxRange,
                               w.damage, dt, cu, w.id);
        return;
    }
    if (w.type != WeaponType::Projectile) return;  // area/instant deferred
    if (!tryFire(game->playerWeapon)) return;       // respects fire-rate cooldown

    float lifetime = (w.speed > 0.0f) ? (w.maxRange / w.speed) : 1.0f;
    game->projectileManager.spawn(game->physics.world_id, spawnFrom(off2d), dir,
                                  w.speed, w.damage, lifetime, cu->collisionGroupId, w.id, w.radius);
    // Twin: second barrel is the offset mirrored across the facing axis (negate lateral x),
    // so the two shots straddle the centreline instead of stacking.
    if (w.twin) {
        game->projectileManager.spawn(game->physics.world_id, spawnFrom({-off2d.x, off2d.y}), dir,
                                      w.speed, w.damage, lifetime, cu->collisionGroupId, w.id, w.radius);
    }
}

// Switch levels: freeze the old level, activate the new level's world + geometry, migrate
// the player device (and any carried unit type) into the new world, and populate or
// reactivate the new level's droids. `target` is the arrival tile (a lift stop) or null.
static void game_change_level(Game* game, int newLevel, const Vector2* target) {
    if (newLevel < 0 || newLevel >= (int)game->levels.size()) return;
    int oldLevel = game->currentLevel;
    TraceLog(LOG_INFO, "Switching from level %d to level %d", oldLevel, newLevel);

    // Release transfer control (device collision restored); a carried unit type + its
    // current health are re-piloted on the new level after the switch.
    int carriedClass = transfer_captured_class(game);
    float carriedHealth = transfer_captured_health(game);
    transfer_reset(game);

    // Freeze the level we're leaving (droids persist frozen in its world).
    game_deactivate_level(game, oldLevel);

    // Activate the new level's world + geometry.
    game->currentLevel = newLevel;
    game->physics.world_id = game->levelRuntime[newLevel].world;
    if (!game_build_level_render_data(game)) {
        TraceLog(LOG_ERROR, "Failed to build render data for level %d", newLevel);
        return;
    }
    game_create_level_collision(game);
    game_create_doors(game);
    game_create_chargers(game);
    game_create_consoles(game);
    game->effectManager.init(game->physics.world_id);  // rebind to new world; clears old effects
    game->particleManager.clear();                      // drop the old level's particles

    // Arrival placement (lift stop or the level's first lift stop).
    Vector2 tp{0, 0};
    bool have = false;
    if (target) { tp = *target; have = true; }
    else {
        for (const LiftStop& s : game->liftManager.stops()) {
            if (s.level == newLevel) { tp = s.physicsCenter; have = true; break; }
        }
    }
    if (!have) TraceLog(LOG_WARNING, "No lift on level %d; player placed at origin", newLevel);

    // Migrate the persistent player device into the new level's world at the arrival tile.
    if (game->playerUnit && b2Body_IsValid(game->playerUnit->bodyId)) {
        float ang = b2Rot_GetAngle(b2Body_GetRotation(game->playerUnit->bodyId));
        unit_rebind_world(game->playerUnit, game->levelRuntime[newLevel].world,
                          game->levelRuntime[newLevel].origin, tp, ang);
    }

    // Populate on first visit; otherwise wake the persistent roster.
    if (!game->levelRuntime[newLevel].populated) game_spawn_enemies(game);
    else game_reactivate_current_level(game);

    game->unitManager.update(0);  // sync render transforms in the new world

    // Resume piloting the carried unit type on the new level, restoring its health.
    if (carriedClass >= 0) transfer_recapture_class(game, carriedClass, carriedHealth);
}

// Debug level switching (PAGE_UP/PAGE_DOWN): place at the level's first lift stop.
static void game_switch_level(Game* game, int newLevel) {
    if (newLevel == game->currentLevel) return;
    game_change_level(game, newLevel, nullptr);
}

// Lift use: move the player to a specific stop, switching level if needed.
void game_switch_to_stop(Game* game, const LiftStop& stop) {
    if (stop.level == game->currentLevel) {
        game_teleport_player(game, stop.physicsCenter);
    } else {
        game_change_level(game, stop.level, &stop.physicsCenter);
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

    // Face the mouse using the shared facing convention (see facing_angle_to in
    // movement_tuning.h). Using atan2(dx, dz) previously left the X component
    // negated, so mouse left/right gave reversed facing.
    game->playerDesiredRotation = facing_angle_to(dx, dz);
}

// Slew the controlled unit's turret/head sections toward the cursor (playerDesiredRotation),
// mirroring the AI's updateAimingSections so a player-piloted turret droid aims like an AI one.
// Render-only: sets each aiming section's facingAngle before the unit manager reads it.
static void game_update_player_turret(Game* game, float dt) {
    UnitInstance* cu = game_controlled_unit(game);
    if (!cu || !cu->definition) return;
    SectionInstance* turret = unit_find_section_by_role(cu, SectionRole::Turret);
    SectionInstance* head = unit_find_section_by_role(cu, SectionRole::Head);
    if (!turret && !head) return;

    // Turret and head slew at independent per-unit rates (each 0 = global TURRET_SLEW_RATE).
    auto slew = [&](SectionInstance* sec, float rate) {
        if (!sec) return;
        float maxStep = (rate > 0.0f ? rate : TURRET_SLEW_RATE) * dt;
        float diff = normalize_angle(game->playerDesiredRotation - sec->facingAngle);
        if (diff > maxStep) diff = maxStep;
        if (diff < -maxStep) diff = -maxStep;
        sec->facingAngle = normalize_angle(sec->facingAngle + diff);
    };
    slew(turret, cu->definition->properties.turretTurnSpeed);
    slew(head, cu->definition->properties.headTurnSpeed);
}

//------------------------------------------------------------------------------
// Line-of-sight visibility (render filter only — simulation is unaffected)
//------------------------------------------------------------------------------

// True if nothing blocks the straight line from `from` to `to`. Blocked by static
// geometry and CLOSED doors (an open door clears its collision filter so the ray
// passes); other units are ignored so they don't occlude one another.
static bool game_has_line_of_sight(Game* game, Vector2 from, Vector2 to) {
    b2Vec2 origin = {from.x, from.y};
    b2Vec2 translation = {to.x - from.x, to.y - from.y};
    b2QueryFilter filter;
    filter.categoryBits = CATEGORY_PROJECTILE;          // sightline probe
    filter.maskBits = CATEGORY_STATIC | CATEGORY_DOOR;  // walls + closed doors only
    b2RayResult r = b2World_CastRayClosest(game->physics.world_id, origin, translation, filter);
    return !r.hit;
}

// Set each unit's render `visible` flag from the player's line of sight.
static void game_update_unit_visibility(Game* game) {
    if (!game->playerUnit || !b2Body_IsValid(game->playerUnit->bodyId)) return;
    b2Vec2 pp = b2Body_GetPosition(game->playerUnit->bodyId);
    Vector2 playerPos = {pp.x, pp.y};

    for (const auto& inst : game->unitManager.getInstances()) {
        if (!inst) continue;
        if (inst.get() == game->playerUnit) { inst->visible = true; continue; }  // always see self
        if (!b2Body_IsValid(inst->bodyId)) { inst->visible = false; continue; }
        b2Vec2 up = b2Body_GetPosition(inst->bodyId);
        inst->visible = game_has_line_of_sight(game, playerPos, {up.x, up.y});
    }
}

//------------------------------------------------------------------------------
// Game Update
//------------------------------------------------------------------------------

void game_update_gameplay(Game* game, float dt) {
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

    // Apply input via the transfer controller. It drives the controlled unit (the
    // captured AI unit when piloting, else the player device) through the SAME
    // motor-joint layer as the AI, and manages capture/overlay/invulnerability.
    // Mouse aim first so the controlled unit faces the cursor this frame. Gated by
    // pause so the capture animation timer doesn't advance while frozen.
    if (!game->paused) {
        if (!game->testConfig.enabled) {
            game_update_player_rotation(game);
        }
        transfer_update(game, dt);
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

    // Toggle AI/waypoint debug overlay (V key)
    if (IsKeyPressed(KEY_V)) {
        game->showAIDebug = !game->showAIDebug;
    }

    // Cycle the level renderer (G): Tilemap -> CustomTiles -> Objects3D -> …, rebuilding the level.
    // This is the swap seam — the sim is untouched; only the level's draw resources are rebuilt.
    if (IsKeyPressed(KEY_G)) {
        switch (game->levelRenderMode) {
            case LevelRenderMode::Tilemap:     game->levelRenderMode = LevelRenderMode::CustomTiles; break;
            case LevelRenderMode::CustomTiles: game->levelRenderMode = LevelRenderMode::Objects3D;   break;
            case LevelRenderMode::Objects3D:   game->levelRenderMode = LevelRenderMode::Tilemap;     break;
        }
        game_build_level_render_data(game);
        TraceLog(LOG_INFO, "Level renderer: %s", game_level_render_mode_name(game->levelRenderMode));
    }

    // Pause (P) and debug slow-motion (O). These toggles run even while paused so
    // the game can be resumed; they are disabled in the rotation test harness.
    if (!game->testConfig.enabled) {
        if (IsKeyPressed(KEY_P)) game->paused = !game->paused;
        if (IsKeyPressed(KEY_O)) game->slowMotion = !game->slowMotion;
    }

    // Debug: create/cycle the CAPTURED unit's type (F1 = next, F2 = previous). The
    // player device stays type 0; this pilots a droid of the chosen class. See
    // docs/transfer.md.
    if (!game->testConfig.enabled) {
        if (IsKeyPressed(KEY_F1)) transfer_debug_cycle(game, +1);
        if (IsKeyPressed(KEY_F2)) transfer_debug_cycle(game, -1);
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

    // --- Simulation: frozen while paused; time-scaled while in slow-motion. ---
    if (!game->paused) {
        float simDt = game->slowMotion ? dt * 0.1f : dt;

        // Beams are transient (only exist the frame they're fired): clear last frame's set
        // before AI and player firing register this frame's beams.
        game->beamManager.beginFrame();

        // Update AI (applies forces before physics step)
        if (game->playerUnit && game->playerUnit->rootSection) {
            b2Vec2 pp = b2Body_GetPosition(game->playerUnit->bodyId);
            Vector2 playerPos2D = {pp.x, pp.y};
            game->aiManager.update(simDt, playerPos2D,
                                   game->physics.world_id,
                                   &game->projectileManager,
                                   &game->beamManager, game->playerUnit);
        }

        // Player weapon firing (LMB) — spawns before the step so bolts move this frame.
        game_update_player_fire(game, simDt);

        // Advance the shared beam animation cursor.
        game->beamManager.update(simDt);

        // Beam impact sparks: where a beam terminates on any collision (wall, door, or unit),
        // emit a directional jet of sparks reflected across the surface normal, rate-limited to
        // ~30/s per beam.
        {
            const auto& beams = game->beamManager.beams();
            int hitting = 0;
            for (const Beam& b : beams) if (b.hit) ++hitting;
            if (hitting > 0) {
                constexpr float SPARKS_PER_SEC = 30.0f;
                game->beamSparkAccum += hitting * SPARKS_PER_SEC * simDt;
                int toSpawn = (int)game->beamSparkAccum;
                game->beamSparkAccum -= (float)toSpawn;
                for (int k = 0; k < toSpawn; ++k) {
                    // Pick a random hitting beam to emit from this spark.
                    int pick = GetRandomValue(0, hitting - 1);
                    const Beam* b = nullptr;
                    for (const Beam& cand : beams) {
                        if (cand.hit && pick-- == 0) { b = &cand; break; }
                    }
                    if (!b) continue;
                    // One spark per emission, reflected off the surface (incident = {-sin,cos}),
                    // in the weapon's spark colour.
                    Color col = getWeaponDefinition(b->weaponId).sparkColor;
                    game_spawn_impact_sparks(game, b->hitPoint, {-sinf(b->angle), cosf(b->angle)},
                                             b->hitNormal, col, 1);
                }
            }
        }

        // Step physics
        physics_world_step(&game->physics, simDt);

        // Collision response: non-hostile units pause/retreat after bumping obstacles.
        game->aiManager.processCollisions(game->physics.world_id);

        // Doors: advance open/close state + collision toggle, then refresh the 2D animated-tile
        // visual (only when a 2D mode is active; the 3D block renderer reads views live).
        game->doorManager.update(simDt);
        if (!game_mode_is_3d(game)) {
            game->doorRenderer.update(game->doorManager.views());
        }

        // Chargers: update IDLE/CHARGING proximity state + free-running tile animation.
        game->chargerManager.update(simDt);
        game->chargerRenderer.update(simDt, game->chargerManager.views());

        // Line-of-sight: only units the player can see are rendered (render flag only).
        game_update_unit_visibility(game);

        // Update projectiles (lifetime, contact events, cleanup)
        game->projectileManager.update(simDt);
        game->projectileManager.syncFromPhysics();
        game->projectileManager.processContactEvents(game->physics.world_id);

        // Projectile impact sparks: one burst per hit (fired once, unlike the beam's continuous
        // stream). Per-weapon count + colour come from weapons.json (larger shots → more sparks).
        for (const ProjectileImpact& imp : game->projectileManager.impacts()) {
            WeaponDefinition wdef = getWeaponDefinition(imp.weaponId);
            game_spawn_impact_sparks(game, imp.point, imp.incident, imp.normal,
                                     wdef.sparkColor, wdef.impactSparks);
        }

        game->projectileManager.cleanup();

        // Effects (explosions): advance + accumulate area damage onto units in range. Runs
        // before reap so this frame's damage lands; unitManager.update flushes it on the tick.
        game->effectManager.update(simDt);

        // Particles (render-only): advance + expire. Pause/slow-mo aware via simDt.
        game->particleManager.update(simDt);

        // Remove droids destroyed this step (permanent for the level). This may spawn more
        // explosions (chain reactions) — added to effectManager for next frame.
        game_reap_dead(game);

        // Lights out: the first time a populated level has no hostile (non-captured) enemies
        // left, latch it permanently and rebuild the tiles on the darkened atlas row. One-shot
        // (guarded by !cleared), and captures are already applied earlier via transfer_update.
        if (game->currentLevel >= 0 && game->currentLevel < (int)game->levelRuntime.size()) {
            LevelRuntime& lr = game->levelRuntime[game->currentLevel];
            if (!lr.cleared && lr.hadEnemies && !game_level_hostiles_remain(game)) {
                lr.cleared = true;
                game_build_level_render_data(game);   // relight the floor → lights-out row
                int row = game_effective_tile_row(game);
                game->doorRenderer.setRowOffset(row);      // 2D animated door tiles
                game->chargerRenderer.setRowOffset(row);   // (3D block doors are unaffected)
                TraceLog(LOG_INFO, "Level %d cleared — lights out", game->currentLevel);
            }
        }

        // Aim the player-controlled unit's turret/head at the cursor before the manager
        // reads section facings (AI units are aimed inside aiManager.update above).
        game_update_player_turret(game, simDt);

        // Update unit manager (syncs physics transforms)
        game->unitManager.update(simDt);

        // Advance the play clock (drives away-level heal timing).
        game->gameClock += simDt;

        // Score + ship alert (decay, band trickle, charger heal/drain, display clock).
        game_update_score(game, simDt);
    }

    // Console + lift proximity (drive the "Press SPACE" prompts + entry actions). Runs
    // even while paused (player is stationary then) — cheap distance checks.
    if (game->playerUnit && b2Body_IsValid(game->playerUnit->bodyId)) {
        b2Vec2 pp = b2Body_GetPosition(game->playerUnit->bodyId);
        game->consoleManager.update((Vector2){pp.x, pp.y});
        game->liftManager.update((Vector2){pp.x, pp.y}, game->currentLevel);
    }

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

// Debug overlay (toggled with V): waypoint graph + each AI unit's intended target.
// Drawn inside 3D mode.
static void game_draw_ai_debug_3d(Game* game) {
    if (game->currentLevel < 0 || game->currentLevel >= (int)game->levelRenderData.size()) return;
    const LevelRenderData& data = game->levelRenderData[game->currentLevel];
    int wpCount = (int)data.waypointPositions.size();

    // Waypoint links
    for (const auto& link : data.waypointLinks) {
        if (link.first < 0 || link.second < 0 || link.first >= wpCount || link.second >= wpCount) continue;
        const Vector3& a = data.waypointPositions[link.first];
        const Vector3& b = data.waypointPositions[link.second];
        DrawLine3D({a.x, 0.15f, a.z}, {b.x, 0.15f, b.z}, DARKBLUE);
    }
    // Waypoint nodes
    for (const auto& wp : data.waypointPositions) {
        DrawSphere({wp.x, 0.15f, wp.z}, 0.12f, SKYBLUE);
    }
    // Per-unit: collision-radius ring (GREEN = visible, MAGENTA = hidden by LOS, so
    // hidden units can still be picked out) + intended-heading line.
    for (const auto& ai : game->aiManager.components()) {
        if (!ai.unit || !ai.unit->rootSection) continue;
        Vector2 p = ai.unit->rootSection->worldPosition;
        float radius = ai.unit->definition ? ai.unit->definition->collisionRadius : 0.2f;
        Color ring = ai.unit->visible ? GREEN : MAGENTA;
        // Circle default lies in XY; rotate 90° about X to lie flat on the XZ ground.
        DrawCircle3D((Vector3){p.x, 0.12f, p.y}, radius, (Vector3){1, 0, 0}, 90.0f, ring);

        // Detection radius: prominent RED when the unit is HOSTILE (chasing the player),
        // faint green otherwise. Shows where the player trips / loses aggro.
        if (ai.detectionRadius > 0.0f) {
            if (ai.hostile) {
                DrawCircle3D((Vector3){p.x, 0.1f, p.y}, ai.detectionRadius, (Vector3){1, 0, 0}, 90.0f, RED);
                DrawCircle3D((Vector3){p.x, 0.1f, p.y}, ai.detectionRadius * 0.97f, (Vector3){1, 0, 0}, 90.0f, RED);
            } else {
                DrawCircle3D((Vector3){p.x, 0.1f, p.y}, ai.detectionRadius, (Vector3){1, 0, 0}, 90.0f,
                             (Color){70, 130, 70, 150});
            }
        }
        if (ai.targetWaypoint >= 0 && ai.targetWaypoint < wpCount) {
            const Vector3& to = data.waypointPositions[ai.targetWaypoint];
            DrawLine3D({p.x, 0.3f, p.y}, {to.x, 0.3f, to.z}, YELLOW);
        }
    }
}

// Debug overlay (toggled with V): per-unit AI state text. Drawn in 2D (screen space).
static void game_draw_ai_debug_2d(Game* game) {
    for (const auto& ai : game->aiManager.components()) {
        if (!ai.unit || !ai.unit->rootSection) continue;
        Vector2 p = ai.unit->rootSection->worldPosition;
        Vector2 screen = GetWorldToScreen((Vector3){p.x, 0.6f, p.y}, game->camera);

        // Hostile units get a prominent "HOSTILE" tag; others show their state letter.
        // Both carry the unit's health as "cur / max" so damage is visible in debug.
        const char* redirect = (ai.collideCooldown > 0.0f) ? " R" : "";
        float hp = ai.unit->combatState.currentHealth;
        float hpMax = ai.unit->combatState.maxHealth;
        if (ai.hostile) {
            const char* txt = TextFormat("HOSTILE>%d%s  %.0f / %.0f",
                                         ai.targetWaypoint, redirect, hp, hpMax);
            DrawText(txt, (int)screen.x - MeasureText(txt, 14) / 2, (int)screen.y - 2, 14, RED);
        } else {
            const char* st = ai.state == AIState::Flee ? "F" : "P";
            Color col = ai.state == AIState::Flee ? ORANGE : GREEN;
            const char* txt = TextFormat("%s>%d%s  %.0f / %.0f",
                                         st, ai.targetWaypoint, redirect, hp, hpMax);
            DrawText(txt, (int)screen.x - MeasureText(txt, 12) / 2, (int)screen.y, 12, col);
        }
    }
}

// Door debug overlay (toggled with V): a second consumer of DoorManager::views().
// Colour by state; the solid "blocking extent" shrinks along the span as it opens.
static Color game_door_state_color(DoorState s) {
    switch (s) {
        case DoorState::Open:    return GREEN;
        case DoorState::Opening: return ORANGE;
        case DoorState::Closing: return GOLD;
        default:                 return RED;   // Closed
    }
}

static void game_draw_door_debug_3d(Game* game) {
    for (const DoorView& d : game->doorManager.views()) {
        Color col = game_door_state_color(d.state);
        Vector3 pos = {d.worldPos.x, 0.25f, d.worldPos.y};
        // Full doorway footprint (always visible), plus a solid block that retracts
        // along the door's span as openFraction goes 0 -> 1.
        DrawCubeWires(pos, d.size.x, 0.5f, d.size.y, col);
        float remain = 1.0f - d.openFraction;
        float sx = (d.orientation == DoorOrientation::Horizontal) ? d.size.x * remain : d.size.x;
        float sy = (d.orientation == DoorOrientation::Vertical)   ? d.size.y * remain : d.size.y;
        if (sx > 0.01f && sy > 0.01f) {
            DrawCube(pos, sx, 0.4f, sy, col);
        }
    }
}

static void game_draw_door_debug_2d(Game* game) {
    for (const DoorView& d : game->doorManager.views()) {
        Vector2 screen = GetWorldToScreen((Vector3){d.worldPos.x, 0.6f, d.worldPos.y}, game->camera);
        const char* st = d.state == DoorState::Open    ? "OPEN"    :
                         d.state == DoorState::Opening ? "OPENING" :
                         d.state == DoorState::Closing ? "CLOSING" : "CLOSED";
        DrawText(TextFormat("%s %.0f%%", st, d.openFraction * 100.0f),
                 (int)screen.x - 22, (int)screen.y, 10, game_door_state_color(d.state));
    }
}

// Charger debug (V overlay): footprint + IDLE/CHARGING state.
static void game_draw_charger_debug_3d(Game* game) {
    for (const ChargerView& c : game->chargerManager.views()) {
        Color col = (c.state == ChargerState::Charging) ? YELLOW : SKYBLUE;
        DrawCubeWires((Vector3){c.worldPos.x, 0.2f, c.worldPos.y}, c.size.x, 0.3f, c.size.y, col);
    }
}

static void game_draw_charger_debug_2d(Game* game) {
    for (const ChargerView& c : game->chargerManager.views()) {
        Vector2 screen = GetWorldToScreen((Vector3){c.worldPos.x, 0.5f, c.worldPos.y}, game->camera);
        const char* st = (c.state == ChargerState::Charging) ? "CHG" : "IDLE";
        Color col = (c.state == ChargerState::Charging) ? YELLOW : SKYBLUE;
        DrawText(st, (int)screen.x - 12, (int)screen.y, 10, col);
    }
}

// Debug (hold B): draw EVERY shape in the physics world — including bodies not attached to
// any game object — coloured by body type (static=red, dynamic=green, kinematic=blue).
// Draws the shape's true geometry (circle for units, box for polygons) rather than the
// AABB, so units read as circles. Iterates via a world-spanning overlap query, revealing
// stray/orphaned colliders behind "invisible wall" blocks the per-object debug can't show.
static bool game_physics_bounds_cb(b2ShapeId shapeId, void* /*ctx*/) {
    b2BodyId body = b2Shape_GetBody(shapeId);
    b2BodyType type = b2Body_GetType(body);
    Color col = (type == b2_staticBody) ? RED : (type == b2_dynamicBody) ? GREEN : SKYBLUE;

    if (b2Shape_GetType(shapeId) == b2_circleShape) {
        b2Circle c = b2Shape_GetCircle(shapeId);
        b2Vec2 wc = b2Body_GetWorldPoint(body, c.center);  // shape centre in world space
        // Circle laid flat in the ground (XZ) plane; physics Y maps to world Z.
        DrawCircle3D((Vector3){wc.x, 0.3f, wc.y}, c.radius, (Vector3){1, 0, 0}, 90.0f, col);
    } else {
        b2AABB aabb = b2Shape_GetAABB(shapeId);
        Vector3 center = {(aabb.lowerBound.x + aabb.upperBound.x) * 0.5f, 0.3f,
                          (aabb.lowerBound.y + aabb.upperBound.y) * 0.5f};
        DrawCubeWires(center, aabb.upperBound.x - aabb.lowerBound.x, 0.6f,
                      aabb.upperBound.y - aabb.lowerBound.y, col);
    }
    return true;  // continue enumeration
}

static void game_draw_physics_bounds_3d(Game* game) {
    b2AABB huge = {(b2Vec2){-1.0e6f, -1.0e6f}, (b2Vec2){1.0e6f, 1.0e6f}};
    b2QueryFilter filter = b2DefaultQueryFilter();
    filter.categoryBits = ~0ULL;   // match shapes of any category...
    filter.maskBits = ~0ULL;       // ...regardless of their mask
    b2World_OverlapAABB(game->physics.world_id, huge, filter, game_physics_bounds_cb, nullptr);
}

void game_render_gameplay(Game* game) {
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

    // Draw doors + chargers (excluded from the baked tile mesh above). Doors: 3D blocks in the
    // Objects3D mode, animated tiles in the 2D modes — a 2D/3D mix would read as jarring.
    if (game_mode_is_3d(game)) {
        game->door3DRenderer.render(game->doorManager.views());
    } else {
        game->doorRenderer.render();
    }
    game->chargerRenderer.render();

    // Draw all units (player, enemies, etc.)
    game->unitManager.renderAll();

    // Beams: additive quads laid flat in the ground plane along each active beam. The frame
    // texture (plasma for weapon 1, lightning for weapon 8) TILES along the beam length — the
    // V texcoord runs 0..length/BEAM_TILE_WORLD with REPEAT wrap, so a beam truncated by a
    // wall truncates the texture (no stretching). U runs 0..1 across BEAM_HALF_WIDTH*2. The
    // source images are 32x64 vertical strips, so mapping the 64px (height) axis to V lays
    // them lengthwise along the beam. Frame cursor is the manager's shared animFrame().
    {
        const auto& beams = game->beamManager.beams();
        if (!beams.empty()) {
            int frame = game->beamManager.animFrame();
            BeginBlendMode(BLEND_ADDITIVE);
            DisableDepthMaskScope depthGuard;  // additive: test depth vs opaque geometry but don't
                                               // write it, so overlapping effects blend, not occlude
            rlDisableBackfaceCulling();  // ground-plane quad — visible from the top-down camera
            for (const Beam& b : beams) {
                if (b.length <= 0.0f) continue;
                TextureId base = (b.weaponId == 8) ? TEX_BEAM_LIGHTNING_0 : TEX_BEAM_PLASMA_0;
                Texture2D tex = gTextures().get((TextureId)(base + (frame % BEAM_FRAME_COUNT)));
                if (tex.id == 0) continue;
                float s = sinf(b.angle), c = cosf(b.angle);
                Vector2 dir = {-s, c};              // beam direction in the XZ ground plane
                Vector2 nrm = {dir.y, -dir.x};      // perpendicular (across the beam width)
                float hw = BEAM_HALF_WIDTH;
                Vector2 e = {b.origin.x + dir.x * b.length, b.origin.y + dir.y * b.length};
                float vlen = b.length / BEAM_TILE_WORLD;   // tiles; truncates with length
                float h = BEAM_HEIGHT;
                rlSetTexture(tex.id);
                rlBegin(RL_QUADS);
                rlColor4ub(255, 255, 255, 255);
                rlTexCoord2f(0.0f, 0.0f);  rlVertex3f(b.origin.x + nrm.x*hw, h, b.origin.y + nrm.y*hw);
                rlTexCoord2f(1.0f, 0.0f);  rlVertex3f(b.origin.x - nrm.x*hw, h, b.origin.y - nrm.y*hw);
                rlTexCoord2f(1.0f, vlen);  rlVertex3f(e.x - nrm.x*hw,        h, e.y - nrm.y*hw);
                rlTexCoord2f(0.0f, vlen);  rlVertex3f(e.x + nrm.x*hw,        h, e.y + nrm.y*hw);
                rlEnd();
                rlSetTexture(0);
            }
            rlEnableBackfaceCulling();
            EndBlendMode();
        }  // depthGuard restores depth writes here
    }

    // Projectiles: additive glow billboards, drawn above the sim (render-only, like
    // doors/chargers). The physics radius is PROJECTILE_RADIUS (0.1); the flare is a
    // larger purely-visual sprite — the glow has a lot of gradient, so it must be
    // comfortably bigger than the physics circle to read on screen. See docs/weapons.md.
    {
        constexpr float PROJECTILE_HEIGHT = 0.5f;       // billboard centre height (lifted
                                                        // clear of the floor: additive, so
                                                        // no z-fighting with the ground)
        constexpr float PLASMA_VISUAL_SIZE = 0.6f;      // plasma glow diameter (weapon 0)
        constexpr float LASER_VISUAL_LEN = 0.9f;        // laser streak length along travel
        constexpr float ASMD_VISUAL_SIZE = 0.8f;        // weapon-3 blast diameter (radius 0.4)
        constexpr int WEAPON_PLASMA_CANNON = 3;         // uses the animated ASMD blast sprite
        // Use DrawBillboardPro with the billboard up-vector = camera.up, NOT plain
        // DrawBillboard: that hardcodes up={0,1,0}, which for this straight-down camera is
        // parallel to the view direction, so cross(up, toCamera)=0 collapses the quad to a
        // 1px line. camera.up ({0,0,-1}) is perpendicular to the view, laying the sprite flat
        // in the ground plane facing the camera. Origin = half-size centres it on the
        // projectile (the quad otherwise hangs off corner 0).
        //
        // Per-weapon look, keyed by damage type: plasma → round flare glow; laser →
        // blaster_blob, a horizontal streak (image long axis = image +X = billboard right).
        // Rotate the laser so its streak points along travel: at rotation 0 the streak lies
        // on world +X; DrawBillboardPro spins the quad about the view axis (+Y for this
        // camera), and rotating +X about +Y by angle a gives (cos a, 0, -sin a), so
        // a = atan2(-vz, vx) aligns it with the velocity (vx, vz).
        {
            BeginBlendMode(BLEND_ADDITIVE);
            DisableDepthMaskScope depthGuard;  // additive: don't write depth (see beam pass)
            for (const Projectile& p : game->projectileManager.getProjectiles()) {
                if (!p.active) continue;
                Vector3 pos = {p.position.x, PROJECTILE_HEIGHT, p.position.y};

                // Per-weapon look. Weapon 3 (Plasma Cannon) → animated ASMD blast, a sprite sheet
                // whose source rect is chosen from the projectile's own age (bolts animate
                // independently, one texture bind). Laser (2,4) → rotated blaster_blob streak.
                // Everything else (0,5,7,…) → round flare glow. `src` is the drawn region and drives
                // the aspect (frame region for the sheet, full texture otherwise).
                Texture2D tex;
                Rectangle src;
                float len;
                float rotation = 0.0f;
                if (p.weaponId == WEAPON_PLASMA_CANNON) {
                    tex = gTextures().get(game->asmdAnim.sheet);
                    src = game->asmdAnim.sourceRect(p.age, tex.width, tex.height);
                    len = ASMD_VISUAL_SIZE;
                } else if (getWeaponDefinition(p.weaponId).damageType == DamageType::Laser) {
                    tex = gTextures().get(TEX_BLASTER_BLOB);
                    src = {0.0f, 0.0f, (float)tex.width, (float)tex.height};
                    len = LASER_VISUAL_LEN;
                    rotation = atan2f(-p.velocity.y, p.velocity.x) * RAD2DEG;  // streak along travel
                } else {
                    tex = gTextures().get(TEX_FLARE);
                    src = {0.0f, 0.0f, (float)tex.width, (float)tex.height};
                    len = PLASMA_VISUAL_SIZE;
                }
                float aspect = (src.width > 0.0f) ? src.height / src.width : 1.0f;
                Vector2 size = {len, len * aspect};  // preserve the drawn region's proportions
                Vector2 origin = {size.x * 0.5f, size.y * 0.5f};
                DrawBillboardPro(game->camera, tex, src, pos,
                                 game->camera.up, size, origin, rotation, WHITE);
            }
            EndBlendMode();
        }  // depthGuard restores depth writes before the opaque V-mode markers below

        // V-mode: opaque marker ring at each projectile's render position, so a missing
        // or washed-out additive sprite can be told apart from a bad position/render path.
        if (game->showAIDebug) {
            for (const Projectile& p : game->projectileManager.getProjectiles()) {
                if (!p.active) continue;
                Vector3 pos = {p.position.x, PROJECTILE_HEIGHT, p.position.y};
                DrawSphere(pos, p.radius, MAGENTA);  // physics-radius marker (per-projectile)
            }
        }
    }

    // Effects: explosions as animated additive billboards with a per-effect random screen-space
    // rotation (rotation spins the quad about the view axis). Frame from each effect's own age.
    {
        constexpr float EFFECT_HEIGHT = 0.5f;
        const auto& effects = game->effectManager.getEffects();
        if (!effects.empty()) {
            Texture2D boom = gTextures().get(TEX_RLBOOM);
            float diameter = EXPLOSION_RADIUS * 2.0f;   // visual diameter = 2x damage radius
            Vector2 size = {diameter, diameter};
            Vector2 origin = {diameter * 0.5f, diameter * 0.5f};
            BeginBlendMode(BLEND_ADDITIVE);
            DisableDepthMaskScope depthGuard;  // additive: don't write depth (see beam pass)
            for (const Effect& e : effects) {
                if (!e.active) continue;
                Vector3 pos = {e.pos.x, EFFECT_HEIGHT, e.pos.y};
                Rectangle src = game->explosionAnim.sourceRect(e.age, boom.width, boom.height);
                DrawBillboardPro(game->camera, boom, src, pos,
                                 game->camera.up, size, origin, e.rotationDeg, WHITE);
            }
            EndBlendMode();
        }  // depthGuard restores depth writes here
    }

    // Particles: additive billboards, one texture per system → rlgl batches them into ~1 draw
    // call. Size and colour interpolate start→end over each particle's life (alpha fades out).
    {
        ParticleSpan ps = game->particleManager.renderData();
        if (ps.n > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DisableDepthMaskScope depthGuard;  // additive: don't write depth (see beam pass)
            for (std::size_t i = 0; i < ps.n; ++i) {
                float t = (ps.lifetime[i] > 0.0f) ? (ps.age[i] / ps.lifetime[i]) : 1.0f;
                if (t > 1.0f) t = 1.0f;
                float sz = ps.startSize[i] + (ps.endSize[i] - ps.startSize[i]) * t;
                auto lerpU8 = [&](unsigned char a, unsigned char b) {
                    return (unsigned char)(a + (int)((b - a) * t));
                };
                Color col = {lerpU8(ps.startColor[i].r, ps.endColor[i].r),
                             lerpU8(ps.startColor[i].g, ps.endColor[i].g),
                             lerpU8(ps.startColor[i].b, ps.endColor[i].b),
                             lerpU8(ps.startColor[i].a, ps.endColor[i].a)};
                Texture2D tex = gTextures().get((TextureId)ps.texture[i]);
                Vector3 pos = {ps.posX[i], PARTICLE_HEIGHT, ps.posY[i]};
                Rectangle src = {0.0f, 0.0f, (float)tex.width, (float)tex.height};
                Vector2 size = {sz, sz};
                Vector2 origin = {sz * 0.5f, sz * 0.5f};
                DrawBillboardPro(game->camera, tex, src, pos,
                                 game->camera.up, size, origin, ps.rot[i], col);
            }
            EndBlendMode();
        }  // depthGuard restores depth writes here
    }

    // Debug: collision shapes (press C)
    if (IsKeyDown(KEY_C) && game->currentLevel >= 0 &&
        game->currentLevel < (int)game->levelCollisionData.size()) {
        drawCollisionDebug(game->levelCollisionData[game->currentLevel], RED, 0.05f);
    }

    // Debug: unit physics shapes (press U)
    if (IsKeyDown(KEY_U)) {
        game->unitManager.renderDebug();
    }

    // Debug: bounds of ALL physics bodies incl. orphans (hold B)
    if (IsKeyDown(KEY_B)) {
        game_draw_physics_bounds_3d(game);
    }

    // Debug: waypoint graph + AI intended targets + door state (toggle V)
    if (game->showAIDebug) {
        game_draw_ai_debug_3d(game);
        game_draw_door_debug_3d(game);
        game_draw_charger_debug_3d(game);
    }

    EndMode3D();

    // Debug: per-unit AI state + per-door/charger state labels (toggle V)
    if (game->showAIDebug) {
        game_draw_ai_debug_2d(game);
        game_draw_door_debug_2d(game);
        game_draw_charger_debug_2d(game);
    }

    // HUD
    DrawFPS(10, 10);

    const char* debugModes[] = {
        "0:Normal", "1:Normals", "2:LightDir",
        "3:Specular", "4:ViewDir", "5:HalfDir", "6:BumpMap"
    };
    DrawText(TextFormat("Debug: %s (C=collision, U=units, B=all-bodies, N=normalmap)",
             debugModes[game->debugMode]), 10, 30, 16, WHITE);

    // Active level renderer (G to cycle)
    DrawText(TextFormat("Renderer[G]: %s", game_level_render_mode_name(game->levelRenderMode)),
             10, 50, 16, WHITE);

    // Level info
    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        DrawText(TextFormat("Level %d: %s",
                 game->currentLevel,
                 game->levels[game->currentLevel].name.c_str()),
                 10, 50, 16, WHITE);
    }

    // Score (clocked display value, top-right).
    {
        const char* txt = TextFormat("SCORE %06ld", (long)game->scoreDisplay);
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 24) - 16, 10, 24, RAYWHITE);
    }

    // Player health, under the score. Tracks the controlled unit (the device in Free mode,
    // else the piloted droid). Numeric "cur / max" plus a colour-graded bar.
    {
        UnitInstance* cu = game_controlled_unit(game);
        if (cu) {
            float cur = cu->combatState.currentHealth;
            float mx = cu->combatState.maxHealth;
            float frac = (mx > 0.0f) ? (cur / mx) : 0.0f;
            frac = (frac < 0.0f) ? 0.0f : (frac > 1.0f ? 1.0f : frac);

            const int fs = 20;
            const char* txt = TextFormat("HEALTH %.0f / %.0f", cur, mx);
            DrawText(txt, GetScreenWidth() - MeasureText(txt, fs) - 16, 40, fs, RAYWHITE);

            const int barW = 180, barH = 10;
            int bx = GetScreenWidth() - barW - 16;
            int by = 40 + fs + 4;
            Color hc = (frac > 0.5f) ? GREEN : (frac > 0.25f ? YELLOW : RED);
            DrawRectangle(bx, by, barW, barH, Fade(DARKGRAY, 0.6f));
            DrawRectangle(bx, by, (int)(barW * frac), barH, hc);
            DrawRectangleLines(bx, by, barW, barH, BLACK);
        }
    }

    // Player info (angle/rotation telemetry) — debug only (toggle V), not shown in normal play.
    if (game->showAIDebug && game->playerUnit && game->playerUnit->rootSection) {
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
            if (game->playerUnit->definition) {
                const UnitDefinition* pd = game->playerUnit->definition;
                DrawText(TextFormat("Unit: %s  spd=%.0f acc=%.0f dec=%.0f  [F1/F2 to cycle]",
                         pd->name.c_str(), pd->maxSpeed, pd->acceleration, pd->deceleration),
                         10, 110, 16, YELLOW);
            }
            DrawText(TextFormat("Desired: %s%06.1f  err=%s%05.1f  angVel=%s%05.1f",
                     desiredDeg >= 0 ? " " : "", desiredDeg,
                     errDeg >= 0 ? " " : "", errDeg,
                     angVel >= 0 ? " " : "", angVel),
                     10, 90, 16, WHITE);
        }
    }

    // Controls help
    DrawText("WASD: Move | Mouse: Aim | F1/F2: Unit type | V: AI/waypoints/doors | G: Renderer | P: Pause | O: Slow | PgUp/PgDn: Level | 0-6: Debug | ESC: Quit",
             10, GetScreenHeight() - 25, 14, GRAY);

    // Pause / slow-motion state indicator (centred, top).
    if (game->paused) {
        const char* txt = "PAUSED";
        int w = MeasureText(txt, 30);
        DrawText(txt, GetScreenWidth() / 2 - w / 2, 40, 30, YELLOW);
    } else if (game->slowMotion) {
        const char* txt = "SLOW-MO";
        int w = MeasureText(txt, 20);
        DrawText(txt, GetScreenWidth() / 2 - w / 2, 40, 20, SKYBLUE);
    }

    // Console-use prompt (player standing on a console tile).
    if (game->consoleManager.playerInRange()) {
        const char* txt = "Press SPACE to use console";
        int w = MeasureText(txt, 20);
        DrawText(txt, GetScreenWidth() / 2 - w / 2, GetScreenHeight() - 60, 20, YELLOW);
    }

    // Lift-use prompt (player standing on a lift tile).
    if (game->liftManager.onLift()) {
        const char* txt = "Press SPACE - ship lift";
        int w = MeasureText(txt, 20);
        DrawText(txt, GetScreenWidth() / 2 - w / 2, GetScreenHeight() - 60, 20, SKYBLUE);
    }

    EndDrawing();
}

//------------------------------------------------------------------------------
// Game Destroy
//------------------------------------------------------------------------------

void game_destroy(Game* game) {
    // Despawn enemies and clear spawn config
    game_despawn_enemies(game);
    clearSpawnConfig();

    // Destroy unit manager (cleans up all units and their physics). Section instances with
    // shared models leave those GPU buffers alone; the ModelCache frees them next.
    game->unitManager.destroy();
    game->playerUnit = nullptr;

    // Unload shared models (before the GL context closes).
    game->modelCache.destroy();

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

    // GPU textures are owned by the TextureManager and freed once (unloadAll) from main,
    // before CloseWindow — not here. See gTextures() / docs/textures.md.

    // Destroy scene renderer
    sceneRendererDestroy(&game->sceneRenderer);

    // Destroy door bodies before the physics worlds go away, and the door mesh
    game->doorManager.destroy();
    game->doorRenderer.destroy();
    game->door3DRenderer.destroy();
    game->chargerManager.destroy();
    game->chargerRenderer.destroy();
    game->consoleManager.destroy();
    game->effectManager.destroy();  // no bodies, but keep the pre-world-teardown convention
    game->particleManager.clear();  // render-only; just drop the buffers

    // Destroy every per-level world (frees origins, collision, and any remaining bodies).
    // Unit bodies were already freed by unitManager.destroy() above. game->physics.world_id
    // aliases one of these, so don't destroy it separately.
    for (const LevelRuntime& lr : game->levelRuntime) {
        if (!B2_IS_NULL(lr.world)) b2DestroyWorld(lr.world);
    }
    game->levelRuntime.clear();
    game->physics.world_id = b2_nullWorldId;
}
