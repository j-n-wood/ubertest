#include "game.h"
#include "world_scale.h"
#include "transfer_control.h"
#include "rendering/texture_manager.h"
#include "rendering/render_scope.h"
#include "rendering/collision_debug.h"
#include "rendering/glass_render.h"
#include "level/level3d_loader.h"
#include "level/spawn_config.h"
#include "combat/disruptor.h"
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
static bool game_mode_is_3d(const Game* game);
static bool game_build_level_render_data(Game* game);
static void game_create_level_collision(Game* game);
static void game_create_doors(Game* game);
static std::vector<DoorSpec> game_detect_doors(const Game* game);
static void game_create_chargers(Game* game);
static std::vector<ChargerSpec> game_detect_chargers(const Game* game);
static void game_create_consoles(Game* game);
static std::vector<ConsoleSpec> game_detect_consoles(const Game* game);
static void game_create_objects(Game* game);
static void game_spawn_player(Game* game);
static Vector2 game_level_spawn_pos(const Game* game);
static void game_build_lift_network(Game* game);
static void game_switch_renderer(Game* game, LevelRenderMode newMode);
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
static void game_reap_objects(Game* game);
static void game_update_drips(Game* game, float dt);
static void game_update_ship_status(Game* game);
static void game_census_spawn(Game* game, UnitInstance* enemy);
static void game_census_despawn(Game* game, UnitInstance* unit);
static void game_populate_level_roster(Game* game, int L);
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
    game->shadowMap.build(2048);   // depth-map shadows

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

    // Build render data for the initial level (index 0). The configured start deck (GAME_START_DECK
    // in main) is reached AFTER init via the normal world-switch path (game_debug_goto_deck ->
    // game_change_level), which migrates the player device into that deck's world and places it at
    // the lift stop — this init sequence only knows how to build index 0.
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
    game->objectManager.loadDefinitions(game->assetPath + "/objects");   // catalog, once
    game_create_objects(game);
    game->effectManager.init(game->physics.world_id);  // bind active world (no tiles; dynamic)
    game->particleManager.clear();                      // render-only; no world binding
    game->decalManager.build((int)game->levels.size()); // per-deck floor decal storage
    game->decalManager.setActiveLevel(game->currentLevel);

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

    // Build the elevator graph before spawning, so a 3D spawn can start the player on a lift.
    game_build_lift_network(game);

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
    // Floor decal masks (RGBA): scorch + drip. See DecalManager / docs/decals.md.
    gTextures().loadFile(TEX_DECAL_BLASTMARK, game->assetPath + "/textures/decals/blastmark.png");
    gTextures().loadFile(TEX_DECAL_DRIP, game->assetPath + "/textures/decals/drip.png");
    gTextures().loadFile(TEX_DECAL_BIOHAZARD, game->assetPath + "/textures/decals/biohaz.png");
    gTextures().loadFile(TEX_DECAL_STORAGEAREA, game->assetPath + "/textures/decals/storagearea.png");
    gTextures().loadFile(TEX_DECAL_PROCESSINGAREA, game->assetPath + "/textures/decals/processingarea.png");
    gTextures().loadFile(TEX_DECAL_TEXT_BIOHAZARD, game->assetPath + "/textures/decals/text_biohazard.png");
    gTextures().loadFile(TEX_DECAL_TEXT_DANGER, game->assetPath + "/textures/decals/text_danger.png");
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

    // Spawn enemies for the starting level (spawned + activated), then EAGER-POPULATE every other
    // deck's roster (frozen in its own world) so the ship-wide droid count is accurate from load —
    // not only for decks the player has visited. Rosters are retained for the ship's lifetime and
    // woken on entry (game_reactivate_current_level). Other decks need only their waypoints to place
    // spawns (no geometry build), so this stays cheap.
    game_spawn_enemies(game);
    for (int L = 0; L < (int)game->levels.size(); ++L) {
        if (L == game->currentLevel) continue;
        if (game->levelRenderData[L].waypointPositions.empty())
            load3DLevelWaypoints(game->assetPath, game->levels[L].number, game->levelRenderData[L]);
        game_populate_level_roster(game, L);
    }
    // Level-authored floor decals belong to their deck permanently, so load them for EVERY deck
    // (including the current one, unlike rosters) — returning to a deck must show the same authored
    // marks. addLevelDecal keeps them in a store the runtime cap/clean/reap logic never touches.
    for (int L = 0; L < (int)game->levels.size(); ++L) {
        std::vector<Decal> decals;
        load3DLevelDecals(game->assetPath, game->levels[L].number, decals);
        for (const Decal& d : decals) game->decalManager.addLevelDecal(L, d);
    }
    // Bind the lit shader to every enemy model just loaded (other decks may introduce new classes).
    game->unitManager.applyShaderToModels(sceneRendererGetShader(&game->sceneRenderer));
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
        case LevelRenderMode::Objects3D:   return "Objects3D (converted 3D)";
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

    // Objects3D: load the converted 3D bundle (geometry + waypoints, domain frame) instead of the
    // TMX-baked mesh. Waypoints feed spawns + AI, so units land in the same frame as the geometry.
    // Collision + objects are handled separately (see game_create_level_collision). Falls back to
    // the TMX build if no bundle exists for this deck.
    if (game->levelRenderMode == LevelRenderMode::Objects3D) {
        if (load3DLevel(game->assetPath, level.number, &game->sceneRenderer, data)) {
            return data.meshValid;
        }
        TraceLog(LOG_WARNING, "Objects3D: no 3D bundle for level %d; using CustomTiles.", level.number);
    }

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
    // Objects3D only reaches here when its bundle was missing (warned above) — fall back to bump tiles.
    LevelRenderMode mode = (game->levelRenderMode == LevelRenderMode::Objects3D)
                               ? LevelRenderMode::CustomTiles : game->levelRenderMode;

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

    // Objects3D uses the converted level's own collision (from the bundle's collision.json), in the
    // domain frame (render X, render Z). Each `polygon` is a wall-footprint quad (the profiled wall
    // swept to its real thickness, following its Bézier curve), built as a solid static body. Floor
    // areas are walkable and are not emitted. Not the TMX rects.
    if (game_mode_is_3d(game)) {
        if (game->currentLevel < 0 || game->currentLevel >= (int)game->levels.size()) return;
        const int deck = game->levels[game->currentLevel].number;
        Collision3D coll;
        if (!load3DLevelCollision(game->assetPath, deck, coll)) {
            TraceLog(LOG_WARNING, "No collision.json for level %d; 3D level has no wall collision", deck);
            return;
        }
        int wallN = 0, skipped = 0, glassN = 0;
        for (size_t i = 0; i < coll.polygons.size(); ++i) {
            auto& poly = coll.polygons[i];
            // Glass walls get CATEGORY_GLASS so sight raycasts pass through; normal walls CATEGORY_STATIC.
            bool glass = i < coll.polygonGlass.size() && coll.polygonGlass[i];
            uint16_t cat = glass ? CATEGORY_GLASS : CATEGORY_STATIC;
            PhysicsBody b = physics_create_static_polygon(&game->physics, poly.data(), (int)poly.size(), cat);
            if (b.valid) { game->collisionBodies.push_back(b); wallN++; if (glass) glassN++; } else skipped++;
        }
        TraceLog(LOG_INFO, "3D collision level %d: %d wall polygons (%d glass, %d skipped)",
                 deck, wallN, glassN, skipped);
        return;
    }

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
    // Objects3D sources doors from the bundle (domain frame); the 2D modes detect them from TMX tiles.
    std::vector<DoorSpec> specs;
    if (game_mode_is_3d(game)) {
        load3DLevelDoors(game->assetPath, game->levels[game->currentLevel].number, specs);
    } else {
        specs = game_detect_doors(game);
    }
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
    // Objects3D sources chargers from the bundle (domain frame); the 2D modes detect them from TMX.
    std::vector<ChargerSpec> specs;
    if (game_mode_is_3d(game)) {
        load3DLevelChargers(game->assetPath, game->levels[game->currentLevel].number, specs);
    } else {
        specs = game_detect_chargers(game);
    }
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
    // Objects3D sources consoles from the bundle (domain frame, with facing); the 2D modes detect
    // them from TMX tiles. Mirrors doors/chargers.
    std::vector<ConsoleSpec> specs;
    if (game_mode_is_3d(game)) {
        load3DLevelConsoles(game->assetPath, game->levels[game->currentLevel].number, specs);
    } else {
        specs = game_detect_consoles(game);
    }
    game->consoleManager.init(specs);
    // Build the 3D console model (idempotent; only drawn in Objects3D mode).
    game->console3DRenderer.build(&game->sceneRenderer,
                                  (game->assetPath + "/models/scenery/console.gltf").c_str());

    // Objects3D: consoles are solid models, so give each a static collision footprint matching the
    // drawn model (oriented by the same facing). Appended to the level's collision bodies, which are
    // rebuilt per frame/level. The 2D tile modes rely on the tilemap's own collision.
    int consoleColliders = 0;
    if (game_mode_is_3d(game)) {
        for (const ConsoleSpec& c : specs) {
            const float a = consoleFacingAngle(c.facingRad);
            const float ca = std::cos(a), sa = std::sin(a);
            Vector2 corners[4];
            const float sx[4] = {-1, 1, 1, -1}, sz[4] = {-1, -1, 1, 1};
            for (int i = 0; i < 4; ++i) {
                float x = sx[i] * CONSOLE_HALF_X, z = sz[i] * CONSOLE_HALF_Z;
                float xr = x * ca + z * sa, zr = -x * sa + z * ca;   // rotate about up (matches DrawModelEx)
                corners[i] = {c.physicsCenter.x + xr, c.physicsCenter.y + zr};
            }
            PhysicsBody b = physics_create_static_polygon(&game->physics, corners, 4);
            if (b.valid) { game->collisionBodies.push_back(b); consoleColliders++; }
        }
    }
    TraceLog(LOG_INFO, "Created %zu consoles from level data (%d collision footprints)",
             specs.size(), consoleColliders);
}

// Scenery objects (Objects3D only): instance this level's bundle objects[] against the object
// definition catalog, build their models, and give non-floating (solid) instances a static
// collision footprint. Definitions were preloaded once in game_init. See docs/scenery_entities.md.
static void game_create_objects(Game* game) {
    game->objectManager.clear();
    if (!game_mode_is_3d(game)) return;   // scenery objects render only in the 3D path

    std::vector<ObjectSpec> specs;
    load3DLevelObjects(game->assetPath, game->levels[game->currentLevel].number, specs);
    game->objectManager.setInstances(specs);
    game->object3DRenderer.build(&game->sceneRenderer, game->objectManager.definitions(), game->assetPath);

    int colliders = 0;
    for (ObjectInstance& inst : game->objectManager.instancesMut()) {
        if (!inst.def || inst.def->floating || inst.def->collisionRadius <= 0.0f) continue;
        // Grounded solid object: axis-aligned static box footprint at its ground position (X,Z).
        const float r = inst.def->collisionRadius;
        Vector2 centre = {inst.position.x, inst.position.z};
        PhysicsBody b = physics_create_static_box(&game->physics, centre, 2.0f * r, 2.0f * r);
        if (b.valid) {
            // Tag the body so a projectile contact resolves back to this instance (destructibles
            // take damage; the reap sweep destroys the footprint on death). Address is stable —
            // instancesMut() isn't grown after setInstances.
            inst.bodyId = b.body_id;
            inst.bodyUserData = {BodyTag::Object, &inst};
            b2Body_SetUserData(b.body_id, &inst.bodyUserData);
            game->collisionBodies.push_back(b);
            colliders++;
        }
    }
    TraceLog(LOG_INFO, "Created %zu scenery objects (%d collision footprints)",
             game->objectManager.instances().size(), colliders);
}

//------------------------------------------------------------------------------
// Player Spawning
//------------------------------------------------------------------------------

// Build the elevator graph. Stops reference stable runtime level indices and persist across level
// switches. In Objects3D the TMX lift tiles are in the wrong frame, so build from the exported ship
// transporters (render-metric); fall back to the TMX build if absent. Renderer-frame dependent, so
// it is rebuilt on a runtime renderer switch (see game_switch_renderer).
static void game_build_lift_network(Game* game) {
    if (game_mode_is_3d(game)) {
        std::vector<TransporterSpec> transporters;
        if (load3DLevelTransporters(game->assetPath, transporters)) {
            game->liftManager.buildFromTransporters(transporters, game->levels);
            TraceLog(LOG_INFO, "Built 3D lift network from %zu transporters", transporters.size());
            return;
        }
        TraceLog(LOG_WARNING, "No transporters.json; falling back to TMX lift build");
    }
    game->liftManager.build(game->levels);
}

// The player's start position for the current level in the ACTIVE renderer's frame: the first
// waypoint, upgraded to the level's first lift stop when one exists (so onLift() fires). The frame
// differs by mode (3D domain vs TMX grid), so this is recomputed on a renderer switch.
static Vector2 game_level_spawn_pos(const Game* game) {
    Vector2 spawnPos = {0, 0};
    if (game->currentLevel >= 0 && game->currentLevel < (int)game->levelRenderData.size()) {
        const LevelRenderData& data = game->levelRenderData[game->currentLevel];
        if (!data.waypointPositions.empty()) {
            Vector3 wpPos = data.waypointPositions[0];
            spawnPos = {wpPos.x, wpPos.z};   // World X,Z -> Physics X,Y
        }
    }
    if (game_mode_is_3d(game)) {
        for (const LiftStop& s : game->liftManager.stops()) {
            if (s.level == game->currentLevel) { spawnPos = s.physicsCenter; break; }
        }
    } else if (game->currentLevel >= 0 && game->currentLevel < (int)game->levels.size()) {
        const TmxLevel& lvl = game->levels[game->currentLevel];
        if (!lvl.lifts.empty()) {
            const TmxLift& lift = lvl.lifts[0];
            spawnPos = {(lift.col + 0.5f - lvl.width * 0.5f) * WORLD_SCALE,
                        (lift.row + 0.5f - lvl.height * 0.5f) * WORLD_SCALE};
        }
    }
    return spawnPos;
}

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

    // Spawn position for the current level/renderer frame (waypoint, upgraded to a lift stop).
    Vector2 spawnPos = game_level_spawn_pos(game);
    TraceLog(LOG_INFO, "Spawning player at (%.2f, %.2f)", spawnPos.x, spawnPos.y);

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

    // Get spawn definition for the current level, keyed by its STABLE DECK NUMBER (not the array
    // index — levels load in alphabetical filename order, so index != deck). spawns.json is
    // authored per deck number, which is renderer-independent and also works for non-TMX 3D decks.
    const int deck = game->levels[game->currentLevel].number;
    const LevelSpawnDef* spawnDef = getSpawnDef(0, deck);
    if (!spawnDef) {
        TraceLog(LOG_WARNING, "No spawn definition for deck %d", deck);
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
        playerWaypointIdx,
        renderData.waypointIsStart);   // spawn only on "droid start" waypoints (skip isolated nets)

    if (spawnEntries.empty()) {
        TraceLog(LOG_INFO, "No enemies to spawn on deck %d", deck);
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
            game_census_spawn(game, enemy);   // count into the ship-wide droid tally
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

    TraceLog(LOG_INFO, "Populated %zu enemies on deck %d (level index %d)", enemies.size(), deck, L);
}

// Eager population: create level L's persistent enemy roster in ITS OWN world, FROZEN (active=false)
// until the deck is entered. Retained for the ship's lifetime and counted into the ship-wide census,
// so the droid tally is accurate from ship load rather than only for visited decks. Does NOT touch
// enemyUnits/aiManager — waking + AI happen on entry (game_reactivate_current_level). Requires level
// L's waypoints in levelRenderData[L] (load3DLevelWaypoints fills them for un-built decks).
static void game_populate_level_roster(Game* game, int L) {
    if (L < 0 || L >= (int)game->levels.size()) return;
    if (game->levelRuntime[L].populated) return;   // once per level

    const LevelRenderData& rd = game->levelRenderData[L];
    const int deck = game->levels[L].number;
    const LevelSpawnDef* spawnDef = getSpawnDef(0, deck);
    // No waypoints or no spawn def -> nothing to place; still mark the deck populated so the
    // ship-clear check treats an empty deck as already swept.
    if (rd.waypointPositions.empty() || !spawnDef) {
        game->levelRuntime[L].populated = true;
        game->levelRuntime[L].hadEnemies = false;
        return;
    }

    // -1 = no player waypoint to avoid (there is no player on an inactive deck).
    auto spawnEntries = resolveSpawns(*spawnDef, (int)rd.waypointPositions.size(), -1,
                                      rd.waypointIsStart);   // spawn only on "droid start" waypoints
    b2WorldId world = game->levelRuntime[L].world;
    b2BodyId origin = game->levelRuntime[L].origin;
    game->levelRuntime[L].units.clear();
    for (const auto& spawn : spawnEntries) {
        if (spawn.waypointIndex < 0 || spawn.waypointIndex >= (int)rd.waypointPositions.size()) continue;
        Vector3 wpPos = rd.waypointPositions[spawn.waypointIndex];
        std::string defId = "droid_class_" + std::to_string(spawn.classId);
        UnitInstance* enemy = game->unitManager.createInstance(defId, {wpPos.x, wpPos.z}, spawn.angle,
                                                               world, origin);
        if (!enemy) { TraceLog(LOG_WARNING, "Failed to create enemy '%s'", defId.c_str()); continue; }
        enemy->levelIndex = L;
        enemy->active = false;   // frozen until the deck is entered (then woken by reactivation)
        game->levelRuntime[L].units.push_back(enemy);
        game_census_spawn(game, enemy);
    }
    game->levelRuntime[L].populated = true;
    game->levelRuntime[L].hadEnemies = !game->levelRuntime[L].units.empty();
    TraceLog(LOG_INFO, "Eager-populated %zu enemies on deck %d (level index %d)",
             game->levelRuntime[L].units.size(), deck, L);
}

static void game_despawn_enemies(Game* game) {
    // Clear AI components (they reference enemy units)
    game->aiManager.components().clear();

    // Destroy enemy unit instances
    for (auto* enemy : game->enemyUnits) {
        if (enemy) {
            game_census_despawn(game, enemy);  // torn down (not defeated) — drop from the live tally
            game->unitManager.destroyInstance(enemy);
        }
    }
    game->enemyUnits.clear();
}

// Runtime renderer switch (G key). The 2D (TMX) and 3D (converted-bundle) paths use DIFFERENT
// coordinate frames, so this is a full in-place level reload — not just a mesh swap. Every
// frame-dependent artifact is rebuilt in the new frame: the lift network, geometry, static
// collision, doors, chargers and consoles; the player is repositioned and enemies re-populated.
// The prior enemy/level state (health, positions, lights-out) is discarded — this is a view/debug
// toggle, not a gameplay event. The sim world itself (and the persistent player device) survive.
static void game_switch_renderer(Game* game, LevelRenderMode newMode) {
    const int L = game->currentLevel;
    if (L < 0 || L >= (int)game->levels.size() || newMode == game->levelRenderMode) return;

    // Release transfer control, then tear down the current level's frame-specific state IN THIS
    // world — destroy the enemy bodies and the static wall bodies so nothing stale or misplaced
    // survives into the new frame.
    transfer_reset(game);
    game_despawn_enemies(game);
    game->levelRuntime[L].units.clear();
    game->levelRuntime[L].populated = false;
    game->levelRuntime[L].hadEnemies = false;
    game->levelRuntime[L].cleared = false;   // enemies are about to be re-populated
    for (auto& body : game->collisionBodies) {
        if (body.valid) { b2DestroyBody(body.body_id); body.valid = false; }
    }
    game->collisionBodies.clear();
    game->effectManager.init(game->physics.world_id);   // clear old-frame effects (same world)
    game->particleManager.clear();

    game->levelRenderMode = newMode;

    // Rebuild every frame-dependent artifact in the new mode/frame.
    game_build_lift_network(game);
    game_build_level_render_data(game);
    game_create_level_collision(game);
    game_create_doors(game);
    game_create_chargers(game);
    game_create_consoles(game);
    game_create_objects(game);

    // Reposition the persistent player into the new frame, then re-populate enemies around it.
    if (game->playerUnit && b2Body_IsValid(game->playerUnit->bodyId)) {
        Vector2 tp = game_level_spawn_pos(game);
        float ang = b2Rot_GetAngle(b2Body_GetRotation(game->playerUnit->bodyId));
        unit_rebind_world(game->playerUnit, game->levelRuntime[L].world,
                          game->levelRuntime[L].origin, tp, ang);
    }
    game_spawn_enemies(game);

    TraceLog(LOG_INFO, "Level renderer: %s (reloaded in-place)", game_level_render_mode_name(newMode));
}

// Advance to the next level renderer (Tilemap -> CustomTiles -> Objects3D -> …) and reload the
// current level in its frame. Public so the G-key handler and debug/test paths share one cycle.
void game_cycle_renderer(Game* game) {
    LevelRenderMode next = game->levelRenderMode;
    switch (game->levelRenderMode) {
        case LevelRenderMode::Tilemap:     next = LevelRenderMode::CustomTiles; break;
        case LevelRenderMode::CustomTiles: next = LevelRenderMode::Objects3D;   break;
        case LevelRenderMode::Objects3D:   next = LevelRenderMode::Tilemap;     break;
    }
    game_switch_renderer(game, next);
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

//------------------------------------------------------------------------------
// Ship-wide droid census — a running count maintained AS units spawn / are defeated, so it matches
// the spawn logic by construction (no spawn-def lookups, no waypoint-cap guesswork). "Defeated" =
// destroyed OR captured (a captured droid still exists but is no longer an enemy). A per-unit flag
// guards against double-counting a unit whose live->dead transition is observed more than once.
//------------------------------------------------------------------------------

// Count a freshly spawned enemy into the ship census. Called once per enemy at roster population
// (NOT for the player device, and NOT for a captured unit re-instantiated on a level change — those
// don't go through the enemy-spawn path).
static void game_census_spawn(Game* game, UnitInstance* enemy) {
    if (!enemy) return;
    enemy->defeatedCounted = false;
    game->shipDroidsRemaining++;
    if (game->shipDroidsRemaining > game->shipDroidsTotal) game->shipDroidsTotal = game->shipDroidsRemaining;
}

// Count an enemy as defeated (killed or captured). Guarded by the unit's flag so a repeat call
// (e.g. capture, then a later reap of the same body) never double-decrements.
void game_census_defeat(Game* game, UnitInstance* unit) {
    if (!unit || unit->defeatedCounted) return;
    unit->defeatedCounted = true;
    if (game->shipDroidsRemaining > 0) game->shipDroidsRemaining--;
}

// Drop an enemy that is going away WITHOUT being defeated — the roster is being torn down to be
// rebuilt (a renderer switch re-spawns and re-counts them). Not a defeat, so leave the flag clear;
// an already-defeated (e.g. captured) unit isn't live, so it's skipped.
static void game_census_despawn(Game* game, UnitInstance* unit) {
    if (!unit || unit->defeatedCounted) return;
    if (game->shipDroidsRemaining > 0) game->shipDroidsRemaining--;
}

// The whole ship is clear iff it ever had droids and none remain live anywhere. Every deck's roster
// is populated up-front (eager population at ship load), so shipDroidsRemaining is the true shipwide
// live count from the start — no "have all decks been visited?" caveat is needed.
bool game_ship_is_clear(const Game* game) {
    return game->shipDroidsTotal > 0 && game->shipDroidsRemaining == 0;
}

// Latch the one-shot clear event the first frame the ship becomes clear.
static void game_update_ship_status(Game* game) {
    if (!game->shipCleared && game_ship_is_clear(game)) {
        game->shipCleared = true;   // event hook (later: switch ships)
        TraceLog(LOG_INFO, "SHIP CLEAR — all %d droids on '%s' defeated",
                 game->shipDroidsTotal, game->shipMap.name().c_str());
    }
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
        game_census_defeat(game, u); // remove from the ship-wide droid tally (once)
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

// Destroy scenery destructibles whose health has hit zero (mirrors game_reap_dead for units): fire
// the shared explosion/sparks at the object's ground position and remove its collision footprint.
// The instance stays in the vector (marked !alive) so its address — held in body userData — never
// dangles; render/shadow/collision all skip a dead instance. See docs/scenery_entities.md.
static void game_reap_objects(Game* game) {
    for (ObjectInstance& inst : game->objectManager.instancesMut()) {
        if (!inst.alive || !inst.def || !inst.def->destructible || inst.health > 0) continue;
        inst.alive = false;
        // Owner group 0 is no unit's group (unit groups are negative), so nearby droids take the blast.
        // Scale the blast by the def's explodeSize (bigger scenery → bigger boom + wider chain reach).
        const float blast = (inst.def->explodeSize > 0.0f) ? inst.def->explodeSize : 1.0f;
        game_spawn_explosion(game, {inst.position.x, inst.position.z}, 0, blast);  // + a scorch decal
        if (b2Body_IsValid(inst.bodyId)) {
            // Invalidate the matching collisionBodies slot BEFORE destroying, so the level-teardown
            // sweep (which destroys every valid body) can't double-free this one.
            for (auto& body : game->collisionBodies) {
                if (body.valid && B2_ID_EQUALS(body.body_id, inst.bodyId)) { body.valid = false; break; }
            }
            b2DestroyBody(inst.bodyId);
            inst.bodyId = b2_nullBodyId;
        }
    }
}

// Damaged droids leak fluid onto the floor as they move (a cleanable "dirty mark"). A droid drips
// while its health is below its def's dripThreshold AND it's actually moving, on a per-unit cooldown
// that shortens as it gets more hurt. Only the active deck's roster (enemyUnits, incl. a captured
// pilot) is considered; the player device has no threshold so never drips. Mirrors uber droid.cpp.
static void game_update_drips(Game* game, float dt) {
    constexpr float DRIP_MOVE_SPEED = 0.25f;   // u/s below which the droid counts as stationary
    for (UnitInstance* u : game->enemyUnits) {
        if (!u || !u->active || !u->definition) continue;
        const float thr = u->definition->properties.dripThreshold;
        if (thr <= 0.0f) continue;
        const float hp = u->combatState.currentHealth;
        if (hp <= 0.0f || hp >= thr) continue;            // not damaged past the threshold
        if (u->dripCooldown > 0.0f) { u->dripCooldown -= dt; continue; }
        if (!b2Body_IsValid(u->bodyId)) continue;
        b2Vec2 v = b2Body_GetLinearVelocity(u->bodyId);
        if (v.x * v.x + v.y * v.y < DRIP_MOVE_SPEED * DRIP_MOVE_SPEED) continue;  // must be moving

        b2Vec2 p = b2Body_GetPosition(u->bodyId);
        game->decalManager.spawnDrip({p.x, p.y}, u->definition->collisionRadius * 0.8f);
        // Next drip: base + jitter + a health term, so a near-dead droid leaks faster.
        float healthFrac = (u->combatState.maxHealth > 0.0f) ? hp / u->combatState.maxHealth : 0.0f;
        u->dripCooldown = 0.35f + (float)GetRandomValue(0, 600) / 1000.0f + healthFrac * 1.5f;
    }
}

// Disruptor (area weapon): a fired unit's windup counts down; when it elapses the blast damages
// every non-shielded unit within maxRange that has a clear wall/door line-of-sight from the firer,
// EXCEPT the firer itself. No team filter — an enemy disruptor hits the player and other droids too
// (matches uber). Damage bypasses armour. Death/explosions/scoring are handled the same frame by the
// game_reap_dead sweep that runs right after this. Both the player device (or captured pilot) and the
// enemy roster can be firers, so both are ticked. See docs/weapons.md.
static void game_update_disruptors(Game* game, float dt) {
    auto tick = [&](UnitInstance* firer) {
        if (!firer || firer->disruptorWindup <= 0.0f) return;
        firer->disruptorWindup -= dt;
        if (firer->disruptorWindup > 0.0f) return;                 // still charging
        firer->disruptorWindup = 0.0f;
        const int weaponId = firer->disruptorWeaponId;
        firer->disruptorWeaponId = -1;
        if (!firer->active || !b2Body_IsValid(firer->bodyId)) return;   // firer died mid-windup
        b2Vec2 fp = b2Body_GetPosition(firer->bodyId);

        // Candidate set = the player device (or captured pilot) + the active enemy roster.
        std::vector<UnitInstance*> candidates;
        candidates.reserve(game->enemyUnits.size() + 1);
        if (game->playerUnit) candidates.push_back(game->playerUnit);
        for (UnitInstance* e : game->enemyUnits) candidates.push_back(e);

        int hits = disruptorBlast(game->physics.world_id, {fp.x, fp.y}, firer,
                                  getWeaponDefinition(weaponId), candidates);
        game->effectManager.spawnDisruptorFlash({fp.x, fp.y});   // bright white bloom at the firer
        TraceLog(LOG_INFO, "Disruptor blast: %d hit", hits);
    };
    tick(game->playerUnit);
    for (UnitInstance* e : game->enemyUnits) tick(e);
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

void game_spawn_explosion(Game* game, Vector2 pos, int32_t group, float sizeScale) {
    game->effectManager.spawnExplosion(pos, group, sizeScale);   // animated blast + area damage (scaled)
    game->particleManager.burst(EXPLOSION_SPARKS, pos);          // render-only spark spray
    // Every explosion (unit or destructible) leaves a scorch mark on the floor (a cleanable decal).
    game->decalManager.spawnBlastmark(pos, 0.4f * sizeScale);
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
    // Area (disruptor): no projectile — arm a windup on the firing unit; game_update_disruptors runs
    // the omnidirectional LOS area-damage sweep when it elapses. Cooldown-gated like any weapon.
    if (w.type == WeaponType::Area) {
        if (tryFire(game->playerWeapon)) {
            cu->disruptorWindup   = (w.windup > 0.0f) ? w.windup : 0.4f;
            cu->disruptorWeaponId = w.id;
        }
        return;
    }
    if (w.type != WeaponType::Projectile) return;  // instant deferred
    if (!tryFire(game->playerWeapon)) return;       // respects fire-rate cooldown

    float lifetime = weaponProjectileLifetime(w);   // lifetime controls travel; maxRange is AI-only
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
    game_create_objects(game);
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

// Debug: jump to a deck by its stable deck number (for --deck testing).
void game_debug_goto_deck(Game* game, int deckNumber) {
    for (int i = 0; i < (int)game->levels.size(); ++i) {
        if (game->levels[i].number == deckNumber) {
            if (i != game->currentLevel) game_switch_level(game, i);
            return;
        }
    }
    TraceLog(LOG_WARNING, "game_debug_goto_deck: no deck with number %d", deckNumber);
}

bool game_start_at_transmat(Game* game) {
    // Gather the ship's transmat (player-start) pads from every deck's waypoints. Ship-specific: the
    // flags come from the loaded ship's bundles, so a new ship brings its own pads automatically.
    // Every deck's waypoints are already loaded (eager population at ship load).
    struct Pad { int level; int deckNumber; Vector2 pos; };
    std::vector<Pad> pads;
    for (int L = 0; L < (int)game->levelRenderData.size() && L < (int)game->levels.size(); ++L) {
        const LevelRenderData& rd = game->levelRenderData[L];
        for (size_t i = 0; i < rd.waypointIsTransmat.size() && i < rd.waypointPositions.size(); ++i) {
            if (!rd.waypointIsTransmat[i]) continue;
            Vector3 p = rd.waypointPositions[i];   // render X,Yup,Z → physics is (X, Z)
            pads.push_back({L, game->levels[L].number, {p.x, p.z}});
        }
    }
    if (pads.empty()) return false;

    // TODO: randomise the choice among pads for a future "random start". For now pick the lowest deck
    // number so the start is stable/reproducible.
    const Pad* chosen = &pads[0];
    for (const Pad& pd : pads) if (pd.deckNumber < chosen->deckNumber) chosen = &pd;

    game_change_level(game, chosen->level, &chosen->pos);   // migrate the player onto the pad's deck
    TraceLog(LOG_INFO, "Ship start: player at transmat pad on deck %d (%zu pad(s) on this ship)",
             chosen->deckNumber, pads.size());
    return true;
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

    // Cycle the level renderer (G): Tilemap -> CustomTiles -> Objects3D -> …. Because the 2D and 3D
    // paths use different coordinate frames, this does a full in-place level reload in the new frame
    // (collision/doors/chargers/lifts/player/enemies), not just a mesh swap. See game_switch_renderer.
    if (IsKeyPressed(KEY_G)) {
        game_cycle_renderer(game);
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

    // Floor decals live per deck; point the manager at the current deck for spawns/cleaning/render.
    game->decalManager.setActiveLevel(game->currentLevel);

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
                                   &game->beamManager, game->playerUnit,
                                   &game->decalManager);
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

        // Chargers: update IDLE/CHARGING proximity state, then the active renderer's animation
        // (3D particles in Objects3D, free-running tile frames in the 2D modes).
        game->chargerManager.update(simDt);
        if (game_mode_is_3d(game)) {
            game->charger3DRenderer.update(simDt, game->chargerManager.views());
        } else {
            game->chargerRenderer.update(simDt, game->chargerManager.views());
        }

        // Scenery objects: advance spin (fans, etc.); empty/no-op outside Objects3D.
        game->objectManager.update(simDt);

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

        // Plasma travel sparks: a bolt whose weapon sets travelSparkRate sheds sparks as it flies —
        // radiating in random directions at a small speed, tinted with the weapon's spriteColor. The
        // rate is accumulated per bolt (simDt → pause/slow-mo aware). See docs/weapons.md.
        for (Projectile& p : game->projectileManager.getProjectilesMutable()) {
            if (!p.active) continue;
            const WeaponDefinition& wdef = getWeaponDefinition(p.weaponId);
            if (wdef.travelSparkRate <= 0.0f) continue;
            p.sparkAccum += wdef.travelSparkRate * simDt;
            int n = (int)p.sparkAccum;
            if (n <= 0) continue;
            p.sparkAccum -= (float)n;
            const Color& col = wdef.spriteColor;
            ParticleBurst tb;
            tb.count = n;
            tb.speedMin = 0.15f;  tb.speedMax = 0.6f;    // small velocity range
            tb.lifeMin = wdef.travelSparkLife * 0.6f;    // per-weapon lifetime (with a little spread)
            tb.lifeMax = wdef.travelSparkLife;
            tb.startSize = wdef.travelSparkSize; tb.endSize = 0.0f;
            tb.startColor = col;
            tb.endColor = {col.r, col.g, col.b, 0};      // fade out (additive)
            tb.angularVelMax = 180.0f;
            tb.texture = TEX_FLARE;
            tb.spreadRad = PI;                            // full radial — random directions
            // Position jitter: without it a fast bolt drops sparks at fixed spacing (one spawn per
            // sim tick) → visible banding. Scatter each spark in a disc of radius travelSparkJitter.
            const float jit = wdef.travelSparkJitter;
            if (jit > 0.0f) {
                tb.count = 1;
                for (int s = 0; s < n; ++s) {
                    float ang = GetRandomValue(0, 6283) / 1000.0f;             // 0..2π
                    float rad = jit * sqrtf(GetRandomValue(0, 1000) / 1000.0f); // uniform over the disc
                    game->particleManager.burst(tb, {p.position.x + cosf(ang) * rad,
                                                     p.position.y + sinf(ang) * rad});
                }
            } else {
                game->particleManager.burst(tb, p.position);
            }
        }

        // Effects (explosions): advance + accumulate area damage onto units in range. Runs
        // before reap so this frame's damage lands; unitManager.update flushes it on the tick.
        game->effectManager.update(simDt);

        // Particles (render-only): advance + expire. Pause/slow-mo aware via simDt.
        game->particleManager.update(simDt);

        // Floor decals: damaged moving droids drip; then reap any decal a cleaner faded to nothing
        // (the AI drives the fade in aiManager.update above).
        game_update_drips(game, simDt);
        game->decalManager.update(simDt);

        // Disruptor windups: resolve any area blast whose windup elapses this step (LOS area damage),
        // BEFORE the reap so its kills are collected below in the same frame.
        game_update_disruptors(game, simDt);

        // Remove droids destroyed this step (permanent for the level). This may spawn more
        // explosions (chain reactions) — added to effectManager for next frame.
        game_reap_dead(game);

        // Same for destructible scenery (tanks): explode + drop the footprint when shot to death.
        game_reap_objects(game);

        // Refresh the ship-wide droid census (drives the Ship Data page + the ship-clear event).
        game_update_ship_status(game);

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

// V-debug: outline each console's "use" activation zone (the box in front where SPACE works).
static void game_draw_console_debug_3d(Game* game) {
    for (const ConsoleSpec& c : game->consoleManager.consoles()) {
        std::array<Vector2, 4> q = consoleUseZoneCorners(c);
        for (int i = 0; i < 4; ++i) {
            Vector2 a = q[i], b = q[(i + 1) % 4];
            DrawLine3D((Vector3){a.x, 0.15f, a.y}, (Vector3){b.x, 0.15f, b.y}, LIME);
        }
    }
}

// V-debug: mark every scenery object (incl. invisible shadow-only fans) with a wire box + a stalk to
// the floor, colour-coded — red=destructible, magenta=shadow-only, sky=floating cosmetic.
static void game_draw_object_debug_3d(Game* game) {
    for (const ObjectInstance& inst : game->objectManager.instances()) {
        if (!inst.def) continue;
        Color col = inst.def->destructible ? RED
                  : (inst.def->drawType == ObjectDrawType::ShadowOnly) ? MAGENTA : SKYBLUE;
        Vector3 p = inst.position;
        DrawCubeWires(p, 0.4f, 0.4f, 0.4f, col);
        DrawLine3D(p, (Vector3){p.x, 0.0f, p.z}, Fade(col, 0.5f));   // stalk to floor
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

// Lift debug overlay (toggled with V): show every lift stop's activation zone on the current
// deck — the ring is the exact use-radius (player must be inside to trigger), plus a vertical beam
// so it's easy to spot from the top-down camera. GREEN = the stop the player is currently on.
static void game_draw_lift_debug_3d(Game* game) {
    for (const LiftStop& s : game->liftManager.stops()) {
        if (s.level != game->currentLevel) continue;
        Vector3 c = {s.physicsCenter.x, 0.1f, s.physicsCenter.y};
        bool active = (game->liftManager.currentStop() == &s);
        Color col = active ? GREEN : SKYBLUE;
        // Exact use-radius ring (double-drawn for visibility) laid flat on the XZ ground.
        DrawCircle3D(c, LIFT_USE_RADIUS, (Vector3){1, 0, 0}, 90.0f, col);
        DrawCircle3D(c, LIFT_USE_RADIUS * 0.96f, (Vector3){1, 0, 0}, 90.0f, col);
        // Vertical beam + footprint cylinder — visible even when the camera looks straight down.
        DrawCylinderWires(c, LIFT_USE_RADIUS, LIFT_USE_RADIUS, 1.6f, 14, (Color){120, 200, 255, 160});
        DrawCube((Vector3){c.x, 0.8f, c.z}, 0.12f, 1.6f, 0.12f, col);
    }
}

// Lift debug text (toggled with V): label each stop (deck + elevator/index) and show the player's
// live onLift state + distance to the nearest stop, in screen space.
static void game_draw_lift_debug_2d(Game* game) {
    for (const LiftStop& s : game->liftManager.stops()) {
        if (s.level != game->currentLevel) continue;
        Vector2 screen = GetWorldToScreen((Vector3){s.physicsCenter.x, 1.0f, s.physicsCenter.y}, game->camera);
        const char* txt = TextFormat("LIFT d%d e%d/%d", s.levelNumber, s.elevator, s.stopIndex);
        DrawText(txt, (int)screen.x - MeasureText(txt, 14) / 2, (int)screen.y, 14, SKYBLUE);
    }
    const char* state = game->liftManager.onLift() ? "ON LIFT" : "off";
    DrawText(TextFormat("Lift: %s  (%zu stops on deck)", state,
                        [&]{ size_t n = 0; for (const LiftStop& s : game->liftManager.stops())
                                             if (s.level == game->currentLevel) n++; return n; }()),
             10, 70, 16, game->liftManager.onLift() ? GREEN : GRAY);
}

// Collision debug overlay (toggled with V, Objects3D): draw the static wall-footprint polygons as
// raised outlines over the geometry. Collects each collision body's polygon shapes and hands them to
// the SHARED drawCollisionWireframe (also used by the viewer, so the two match exactly).
static void game_draw_collision_debug_3d(Game* game) {
    if (!game_mode_is_3d(game)) return;
    std::vector<std::vector<Vector2>> polys;
    for (const PhysicsBody& body : game->collisionBodies) {
        if (!body.valid) continue;
        b2ShapeId shapes[8];
        int n = b2Body_GetShapes(body.body_id, shapes, 8);
        for (int i = 0; i < n; ++i) {
            if (b2Shape_GetType(shapes[i]) != b2_polygonShape) continue;
            b2Polygon p = b2Shape_GetPolygon(shapes[i]);
            std::vector<Vector2> poly;
            for (int k = 0; k < p.count; ++k) poly.push_back({p.vertices[k].x, p.vertices[k].y});
            polys.push_back(std::move(poly));
        }
    }
    drawCollisionWireframe(polys);
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
    ClearBackground(game_mode_is_3d(game) ? BLACK : DARKGRAY);  // 3D reads better against black

    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&game->sceneRenderer, game->camera.position);

    // 3D "lights out": dim the scene for a cleared level. In the 3D renderer a cleared level
    // literally dims the lights; the 2D tile modes keep the darkened atlas row instead
    // (game_effective_tile_row), so leave darkness at 0 there.
    {
        constexpr float LIGHTS_OUT_DARKNESS = 0.6f;   // fraction of brightness removed when cleared
        const int L = game->currentLevel;
        const bool cleared = L >= 0 && L < (int)game->levelRuntime.size() && game->levelRuntime[L].cleared;
        float target = (game_mode_is_3d(game) && cleared) ? LIGHTS_OUT_DARKNESS : 0.0f;
        if (L != game->lightsOutSyncedLevel) {
            // Entered a different level — SNAP to its state. The lights are already on (or already
            // out) for the player when they arrive; they aren't turning on/off now, so no fade.
            game->lightsOutDarkness = target;
            game->lightsOutSyncedLevel = L;
        } else {
            // Same level: ease toward the target so the lights fade out (~0.3 s) at the moment the
            // level is cleared while the player is standing in it.
            float k = 1.0f - std::exp(-3.0f * GetFrameTime());   // frame-rate-independent ease
            game->lightsOutDarkness += (target - game->lightsOutDarkness) * k;
        }
        sceneRendererSetDarkness(&game->sceneRenderer, game->lightsOutDarkness);
    }

    // Shadow-map depth pass (Objects3D): render all shadow casters from the light's POV (orthographic,
    // straight down — matching the scene's vertical directional light) into a sampleable depth target,
    // so ceiling casters (fans, alert lights, lift tops) and walls shadow whatever passes underneath.
    // Must run before the main BeginMode3D — it needs its own light camera. See docs/scenery_entities.md.
    Shader sceneShader = sceneRendererGetShader(&game->sceneRenderer);
    if (game_mode_is_3d(game) && game->shadowMap.ready()) {
        ShadowMap::disable(sceneShader);   // no self-sampling while the depth target is being written
        Vector3 center = {game->camera.target.x, 0.0f, game->camera.target.z};
        game->shadowMap.beginDepth(center, /*extent(m)*/ 26.0f, /*height(m)*/ 10.0f);
        if (game->currentLevel >= 0 && game->currentLevel < (int)game->levelRenderData.size()) {
            LevelRenderData& data = game->levelRenderData[game->currentLevel];
            if (data.meshValid) DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
        }
        game->object3DRenderer.renderDepth(game->objectManager.instances());
        game->unitManager.renderAll();
        game->shadowMap.endDepth();
        game->shadowMap.apply(sceneShader, /*bias*/ 0.0015f);
    }

    BeginMode3D(game->camera);

    // Draw level tiles
    if (game->currentLevel >= 0 &&
        game->currentLevel < (int)game->levelRenderData.size()) {
        LevelRenderData& data = game->levelRenderData[game->currentLevel];
        if (data.meshValid) {
            if (data.glassMeshIndices.empty()) {
                DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
            } else {
                // Draw opaque meshes only; glass meshes go in a later transparent pass.
                std::vector<bool> isGlass(data.tileModel.meshCount, false);
                for (int gi : data.glassMeshIndices)
                    if (gi >= 0 && gi < data.tileModel.meshCount) isGlass[gi] = true;
                for (int i = 0; i < data.tileModel.meshCount; ++i) {
                    if (isGlass[i]) continue;
                    DrawMesh(data.tileModel.meshes[i],
                             data.tileModel.materials[data.tileModel.meshMaterial[i]],
                             data.tileModel.transform);
                }
            }
        }
    }

    // Draw doors + chargers (excluded from the baked tile mesh above). Doors: 3D blocks in the
    // Objects3D mode, animated tiles in the 2D modes — a 2D/3D mix would read as jarring.
    if (game_mode_is_3d(game)) {
        game->door3DRenderer.render(game->doorManager.views());
        game->charger3DRenderer.render(game->camera, gTextures().get(TEX_FLARE));
        game->console3DRenderer.render(game->consoleManager.consoles());
        // Alert beacon: band colour (green/yellow/amber/red) × sine pulse, the rate rising with the
        // alert level. Phase is integrated so a rate change doesn't jump the wave. Fed to the object
        // renderer for glowSource: Alert defs (the alert lights). Mirrors uber's domainView glow.
        {
            AlertColor ac = alert_band_color(alert_band(game->alertLevel));
            game->alertGlowPhase += (float)(alert_pulse_hz(game->alertLevel) * GetFrameTime());
            float pulse = 0.5f + 0.5f * sinf(2.0f * PI * game->alertGlowPhase);
            game->object3DRenderer.setAlertGlow((Vector3){ac.r * pulse, ac.g * pulse, ac.b * pulse});
        }
        game->object3DRenderer.render(game->objectManager.instances());
    } else {
        game->doorRenderer.render();
        game->chargerRenderer.render();
    }

    // Floor decals: alpha-blended textured quads laid FLAT on the floor (blastmarks + drips), drawn
    // after the geometry/objects and before units (so units + walls sit on top). Manual ground-plane
    // quad like the beam pass, but BLEND_ALPHA and lifted a hair (Y≈0.02) above the floor to avoid
    // z-fighting. See DecalManager / docs/decals.md.
    if (game_mode_is_3d(game)) {
        // Two decal stores share this pass: runtime "dirty marks" (active(), faded/cleaned) and
        // permanent level-authored decals (activeLevelDecals()). Both draw identically as flat quads.
        const std::vector<Decal>& runtimeDecals = game->decalManager.active();
        const std::vector<Decal>& levelDecals   = game->decalManager.activeLevelDecals();
        if (!runtimeDecals.empty() || !levelDecals.empty()) {
            constexpr float DECAL_Y = 0.03f;    // just above the floor to avoid z-fighting
            constexpr float SCREEN_MARGIN = 140.0f;  // px slack for a decal's on-screen extent
            const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
            // Off-screen test: project the mark's centre and skip it if well outside the viewport.
            // The top-down camera keeps every floor decal in front of it, so no behind-camera case.
            auto onScreen = [&](Vector2 p) {
                Vector2 s = GetWorldToScreen((Vector3){p.x, DECAL_Y, p.y}, game->camera);
                return s.x > -SCREEN_MARGIN && s.x < sw + SCREEN_MARGIN &&
                       s.y > -SCREEN_MARGIN && s.y < sh + SCREEN_MARGIN;
            };

            // Guards destroy in reverse construction order: the flush scope (constructed last) fires
            // first, submitting the quads while blend/depth/cull are still set (see render_scope.h).
            BlendModeScope blend(BLEND_ALPHA);
            DisableDepthMaskScope depthGuard;         // test vs walls (occlude) but don't write depth
            DisableBackfaceCullScope cullGuard;       // horizontal quad, seen from the top-down camera
            RenderBatchFlushScope flushGuard;         // draw the quads before culling is restored

            // Emit one decal as a floor quad: half-width (size*aspect) along the texture U axis,
            // half-depth (size) along V, rotated by the decal's yaw about +Y and centred on it.
            auto emit = [&](const Decal& d) {
                if (d.alpha <= 0.0f || !onScreen(d.pos)) return;
                float c = cosf(d.rotation), s = sinf(d.rotation);
                float hw = d.size * d.aspect, hv = d.size;
                auto corner = [&](float lx, float lz) -> Vector3 {
                    return (Vector3){d.pos.x + lx * c - lz * s, DECAL_Y, d.pos.y + lx * s + lz * c};
                };
                Vector3 v0 = corner(-hw, -hv), v1 = corner(hw, -hv);
                Vector3 v2 = corner(hw, hv),   v3 = corner(-hw, hv);
                rlColor4ub(255, 255, 255, (unsigned char)(d.alpha * 255.0f));
                rlTexCoord2f(0.0f, 0.0f);  rlVertex3f(v0.x, v0.y, v0.z);
                rlTexCoord2f(1.0f, 0.0f);  rlVertex3f(v1.x, v1.y, v1.z);
                rlTexCoord2f(1.0f, 1.0f);  rlVertex3f(v2.x, v2.y, v2.z);
                rlTexCoord2f(0.0f, 1.0f);  rlVertex3f(v3.x, v3.y, v3.z);
            };

            // Group by texture: bind each decal texture ONCE and emit all its (on-screen) quads —
            // from both stores — in a single RL_QUADS batch, so we don't churn the bound texture.
            const TextureId decalTextures[] = {
                TEX_DECAL_BLASTMARK, TEX_DECAL_DRIP,
                TEX_DECAL_BIOHAZARD, TEX_DECAL_STORAGEAREA, TEX_DECAL_PROCESSINGAREA,
                TEX_DECAL_TEXT_BIOHAZARD, TEX_DECAL_TEXT_DANGER,
            };
            for (TextureId tid : decalTextures) {
                Texture2D tex = gTextures().get(tid);
                if (tex.id == 0) continue;
                TextureBindScope texGuard(tex.id);
                rlBegin(RL_QUADS);
                for (const Decal& d : runtimeDecals) if (d.texture == tid) emit(d);
                for (const Decal& d : levelDecals)   if (d.texture == tid) emit(d);
                rlEnd();
            }
        }
    }

    // Draw all units (player, enemies, etc.)
    game->unitManager.renderAll();

    // Glass tunnels: transparent, env-mapped pass drawn AFTER the opaque geometry + units, so it
    // tests against walls but doesn't occlude what's behind (depth-write off).
    if (game_mode_is_3d(game) && game->currentLevel >= 0 &&
        game->currentLevel < (int)game->levelRenderData.size()) {
        LevelRenderData& data = game->levelRenderData[game->currentLevel];
        if (data.meshValid && !data.glassMeshIndices.empty()) {
            Shader shader = sceneRendererGetShader(&game->sceneRenderer);
            beginGlassPass(shader, 1.0f);
            for (int gi : data.glassMeshIndices) {
                if (gi < 0 || gi >= data.tileModel.meshCount) continue;
                DrawMesh(data.tileModel.meshes[gi],
                         data.tileModel.materials[data.tileModel.meshMaterial[gi]],
                         data.tileModel.transform);
            }
            endGlassPass(shader);
        }
    }

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
                const WeaponDefinition& wdef = getWeaponDefinition(p.weaponId);
                Texture2D tex;
                Rectangle src;
                float len;
                float rotation = 0.0f;
                if (p.weaponId == WEAPON_PLASMA_CANNON) {
                    tex = gTextures().get(game->asmdAnim.sheet);
                    src = game->asmdAnim.sourceRect(p.age, tex.width, tex.height);
                    len = ASMD_VISUAL_SIZE;
                } else if (wdef.damageType == DamageType::Laser) {
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
                // Per-weapon diffuse tint (default white = the texture's own colour). Additive blend,
                // so the tint's alpha scales the sprite's brightness. Fade out over the last
                // PROJECTILE_FADE_FRAC of the bolt's life so it dims away instead of popping off at
                // its lifetime end (short-range weapons rely on lifetime for range). See docs/weapons.md.
                constexpr float PROJECTILE_FADE_FRAC = 0.35f;   // tail fraction over which alpha ramps 1→0
                float lifeFrac = (p.lifetime > 0.0f) ? (p.remainingLifetime / p.lifetime) : 1.0f;
                float fade = (lifeFrac >= PROJECTILE_FADE_FRAC) ? 1.0f : (lifeFrac / PROJECTILE_FADE_FRAC);
                Color tint = wdef.spriteColor;
                tint.a = (unsigned char)(tint.a * fade);
                DrawBillboardPro(game->camera, tex, src, pos,
                                 game->camera.up, size, origin, rotation, tint);
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

    // Effects: additive billboards with a per-effect random screen-space rotation (rotation spins the
    // quad about the view axis). Explosions animate the rlboom sheet from their own age; disruptor
    // flashes are a single white flare that expands and fades over their short life.
    {
        constexpr float EFFECT_HEIGHT = 0.5f;
        const auto& effects = game->effectManager.getEffects();
        if (!effects.empty()) {
            Texture2D boom = gTextures().get(TEX_RLBOOM);
            Texture2D flare = gTextures().get(TEX_FLARE);
            BeginBlendMode(BLEND_ADDITIVE);
            DisableDepthMaskScope depthGuard;  // additive: don't write depth (see beam pass)
            for (const Effect& e : effects) {
                if (!e.active) continue;
                Vector3 pos = {e.pos.x, EFFECT_HEIGHT, e.pos.y};
                if (e.type == EffectType::DisruptorFlash) {
                    // Bright white bloom: expand (ease-out) from start to max diameter while the
                    // added brightness fades to nothing — reads as a quick flash from the firer.
                    float t = e.age / DISRUPTOR_FLASH_LIFETIME;
                    if (t > 1.0f) t = 1.0f;
                    float ease = 1.0f - (1.0f - t) * (1.0f - t);            // ease-out expansion
                    float diameter = DISRUPTOR_FLASH_START_DIAM +
                                     (DISRUPTOR_FLASH_MAX_DIAM - DISRUPTOR_FLASH_START_DIAM) * ease;
                    float fade = 1.0f - t * t;                             // stays bright, fades late
                    unsigned char a = (unsigned char)(fade * 255.0f);
                    Vector2 size = {diameter, diameter};
                    Vector2 origin = {diameter * 0.5f, diameter * 0.5f};
                    Rectangle src = {0.0f, 0.0f, (float)flare.width, (float)flare.height};
                    DrawBillboardPro(game->camera, flare, src, pos, game->camera.up, size, origin,
                                     e.rotationDeg, Color{255, 255, 255, a});
                    continue;
                }
                // Explosion: visual diameter = 2x damage radius, scaled per-effect (e.g. explodeSize).
                float diameter = EXPLOSION_RADIUS * 2.0f * e.sizeScale;
                Vector2 size = {diameter, diameter};
                Vector2 origin = {diameter * 0.5f, diameter * 0.5f};
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
        game_draw_console_debug_3d(game);
        game_draw_object_debug_3d(game);
        game_draw_lift_debug_3d(game);
        game_draw_collision_debug_3d(game);
    }

    EndMode3D();

    // Debug: per-unit AI state + per-door/charger state labels (toggle V)
    if (game->showAIDebug) {
        game_draw_ai_debug_2d(game);
        game_draw_door_debug_2d(game);
        game_draw_charger_debug_2d(game);
        game_draw_lift_debug_2d(game);
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

    // Ship-clear event message (hover-prompt style), shown while the whole ship is empty of droids.
    // For now it's just an on-screen banner; later it drives switching to the next ship.
    if (game_ship_is_clear(game)) {
        const char* txt = "SHIP CLEAR OF DROIDS";
        int w = MeasureText(txt, 24);
        DrawText(txt, GetScreenWidth() / 2 - w / 2, GetScreenHeight() - 100, 24, GREEN);
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
    game->charger3DRenderer.destroy();
    game->consoleManager.destroy();
    game->console3DRenderer.destroy();
    game->objectManager.clear();
    game->object3DRenderer.destroy();
    game->shadowMap.destroy();
    game->effectManager.destroy();  // no bodies, but keep the pre-world-teardown convention
    game->particleManager.clear();  // render-only; just drop the buffers
    game->decalManager.clear();     // render-only floor decals; drop all decks' marks

    // Destroy every per-level world (frees origins, collision, and any remaining bodies).
    // Unit bodies were already freed by unitManager.destroy() above. game->physics.world_id
    // aliases one of these, so don't destroy it separately.
    for (const LevelRuntime& lr : game->levelRuntime) {
        if (!B2_IS_NULL(lr.world)) b2DestroyWorld(lr.world);
    }
    game->levelRuntime.clear();
    game->physics.world_id = b2_nullWorldId;
}
