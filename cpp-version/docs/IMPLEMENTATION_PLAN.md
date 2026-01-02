# Game Implementation Plan: Shared Level Loading, Physics, and Rendering

## Overview

Transform the game from a test scene to a fully functional level-based game using shared systems from `level_viewer`. The player controls a `droid_class_0` unit navigating tile-based levels with physics collision.

## Key Changes Summary

| Component | Current | New |
|-----------|---------|-----|
| Level | Procedural (procgen.cpp) | TMX files from ship1/levels/ |
| Player | Suzanne.glb model + Entity | droid_class_0.json via UnitManager |
| Renderer | Custom Renderer struct | Shared SceneRenderer |
| Tiles | None | CustomTiles mode with bump mapping |
| Collision | 4 static boxes | Generated from tile collision data |

## Files to Modify

### Primary Changes

1. **src/game.h** - Restructure Game struct
2. **src/game.cpp** - Rewrite initialization, update, render
3. **CMakeLists.txt** - Add level loading dependencies
4. **cmake/SharedSources.cmake** - Add level source files

### Files to Remove

- `src/generation/procgen.h` / `procgen.cpp` - Replaced by level loading
- `src/graphics/renderer.h` / `renderer.cpp` - Replaced by SceneRenderer
- `src/entities/entity.h` / `entity.cpp` - Replaced by UnitManager

### Files to Keep

- `src/physics/physics_world.h` / `.cpp` - Still needed
- `src/input/input.h` / `.cpp` - Keep existing controls
- `src/main.cpp` - Entry point unchanged

---

## Phase 1: Build System Updates

### cmake/SharedSources.cmake

Add level loading sources:

```cmake
set(LEVEL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/level/tmx_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tileset_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/level_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tile_properties_loader.cpp
)
```

### CMakeLists.txt

Add to topdown_game target:
- Include `${LEVEL_SHARED_SOURCES}` in source files
- Link `tinyxml2` library (required for TMX parsing)

---

## Phase 2: Game Struct Restructuring

### src/game.h

Replace current struct with:

```cpp
#include "level/level_types.h"
#include "level/tmx_loader.h"
#include "level/tileset_loader.h"
#include "level/level_renderer.h"
#include "rendering/scene_renderer.h"
#include "units/unit_manager.h"
#include <vector>
#include <string>

struct Game {
    // Physics
    PhysicsWorld physics;

    // Rendering (shared)
    SceneRenderer sceneRenderer;

    // Input
    Input input;

    // Level data (all levels loaded upfront)
    std::vector<TmxLevel> levels;
    std::vector<LevelRenderData> levelRenderData;
    std::vector<LevelCollisionData> levelCollisionData;
    int currentLevel;

    // Tileset (shared across levels)
    TmxTileset tileset;
    Texture2D atlasTexture;
    Texture2D bumpAtlasTexture;
    TilePropertiesConfig tileProperties;

    // Collision bodies from tile data
    std::vector<PhysicsBody> collisionBodies;

    // Unit system
    UnitManager unitManager;
    UnitInstance* playerUnit;
    float playerDesiredRotation;  // For mouse aim

    // Camera
    Camera3D camera;
    float effectiveEyeHeight;

    // State
    bool running;
    int debugMode;

    // Paths
    std::string assetPath;
    std::string shadersPath;
    std::string levelsPath;
};
```

---

## Phase 3: Level Loading

### New function: game_load_levels()

```cpp
bool game_load_levels(Game* game) {
    // 1. Load all TMX files from ship1/levels/
    auto results = loadTmxLevelsFromDirectory(game->levelsPath);

    // 2. Store successful loads
    for (const auto& result : results) {
        if (result.success) {
            game->levels.push_back(result.level);
        }
    }

    // 3. Load tileset (default.tsx)
    std::string tilesetPath = game->levelsPath + game->levels[0].tilesetSource;
    TsxLoadResult tsxResult = loadTsxTileset(tilesetPath);
    game->tileset = tsxResult.tileset;
    game->tileset.firstGid = 1;

    // 4. Load atlas texture (map_blocks.png)
    game->atlasTexture = loadTilesetTexture(game->tileset, game->levelsPath);

    // 5. Load tile properties (tiles.json) for CustomTiles mode
    std::string tilesJsonPath = game->levelsPath + "tiles.json";
    game->tileProperties = loadTileProperties(tilesJsonPath);

    // 6. Load bump atlas texture
    if (game->tileProperties.valid) {
        std::string bumpPath = game->assetPath + "/textures/" +
                               game->tileProperties.bumpAtlas.texture;
        game->bumpAtlasTexture = LoadTexture(bumpPath.c_str());
    }

    // 7. Initialize storage vectors
    game->levelRenderData.resize(game->levels.size());
    game->levelCollisionData.resize(game->levels.size());

    return !game->levels.empty();
}
```

---

## Phase 4: Render Data Building

### New function: game_build_level_render_data()

```cpp
bool game_build_level_render_data(Game* game) {
    LevelRenderData& data = game->levelRenderData[game->currentLevel];
    LevelCollisionData& collision = game->levelCollisionData[game->currentLevel];
    const TmxLevel& level = game->levels[game->currentLevel];

    // 1. Generate collision data
    collision = generateLevelCollision(level, game->tileset, 1.0f);

    // 2. Create render data structure
    data = createLevelRenderData(level, game->tileset,
                                  LevelRenderMode::CustomTiles, 1.0f);

    // 3. Generate mesh with CustomTiles (dual UV for bump mapping)
    data.tileMesh = createLevelTileMeshCustom(
        level, game->tileset, game->tileProperties,
        game->bumpAtlasTexture.width, game->bumpAtlasTexture.height, 1.0f);

    // 4. Create model with textures
    data.tileModel = createLevelTileModel(
        data.tileMesh, game->atlasTexture,
        game->bumpAtlasTexture, &game->sceneRenderer);

    data.meshValid = true;
    return true;
}
```

---

## Phase 5: Physics Collision Creation

### New function: game_create_level_collision()

```cpp
void game_create_level_collision(Game* game) {
    game->collisionBodies.clear();

    const LevelCollisionData& collision = game->levelCollisionData[game->currentLevel];

    // Create static Box2D bodies for each merged collision rectangle
    for (const CollisionRect& rect : collision.rects) {
        // CollisionRect: x,z = world position, halfWidth/halfHeight = extents
        // Physics 2D: X = world X, Y = world Z
        Vector2 physicsPos = {rect.x, rect.z};
        float width = rect.halfWidth * 2.0f;
        float height = rect.halfHeight * 2.0f;

        PhysicsBody body = physics_create_static_box(
            &game->physics, physicsPos, width, height);
        game->collisionBodies.push_back(body);
    }
}
```

---

## Phase 6: Player Unit Spawning

### New function: game_spawn_player()

```cpp
void game_spawn_player(Game* game) {
    // 1. Load player unit definition
    const UnitDefinition* playerDef = game->unitManager.loadDefinition(
        game->assetPath + "/units/droid_class_0.json");

    // 2. Find spawn position from waypoint or use center
    Vector2 spawnPos = {0, 0};
    const LevelRenderData& data = game->levelRenderData[game->currentLevel];
    if (!data.waypointPositions.empty()) {
        Vector3 wpPos = data.waypointPositions[0];
        spawnPos = {wpPos.x, wpPos.z};  // World X,Z -> Physics X,Y
    }

    // 3. Create player instance
    game->playerUnit = game->unitManager.createInstance(
        "droid_class_0", spawnPos, 0.0f);

    // 4. Apply lighting shader to player model
    game->unitManager.applyShaderToModels(
        sceneRendererGetShader(&game->sceneRenderer));

    game->playerDesiredRotation = 0.0f;
}
```

---

## Phase 7: Game Initialization

### Rewritten game_init()

```cpp
void game_init(Game* game, const char* assetPath) {
    // 1. Store paths
    game->assetPath = assetPath;
    game->shadersPath = game->assetPath + "/shaders/";
    game->levelsPath = game->assetPath + "/ships/ship1/levels/";

    // 2. Initialize physics world (zero gravity for top-down)
    physics_world_init(&game->physics);

    // 3. Initialize SceneRenderer
    sceneRendererInit(&game->sceneRenderer, game->shadersPath.c_str());

    // 4. Configure lighting
    sceneRendererAddDirectionalLight(&game->sceneRenderer,
        (Vector3){0, 50, 0}, (Vector3){0, 0, 0}, WHITE);
    game->effectiveEyeHeight = 1.0f;
    sceneRendererSetEffectiveEyeHeight(&game->sceneRenderer,
                                        game->effectiveEyeHeight);

    // 5. Initialize UnitManager
    game->unitManager.init(game->physics.world_id,
                           (game->assetPath + "/units/").c_str());

    // 6. Load all levels
    game_load_levels(game);

    // 7. Build render data for starting level
    game->currentLevel = 0;  // level_0_maintenance.tmx
    game_build_level_render_data(game);

    // 8. Create collision bodies
    game_create_level_collision(game);

    // 9. Spawn player
    game_spawn_player(game);

    // 10. Setup camera (top-down)
    game->camera.position = (Vector3){0, 50, 0};
    game->camera.target = (Vector3){0, 0, 0};
    game->camera.up = (Vector3){0, 0, -1};
    game->camera.fovy = 45.0f;
    game->camera.projection = CAMERA_PERSPECTIVE;

    // 11. Initialize input
    input_init(&game->input);
    input_update_screen_cache(&game->input, &game->camera);

    game->running = true;
    game->debugMode = 0;
}
```

---

## Phase 8: Game Update Loop

### Modified game_update()

```cpp
void game_update(Game* game, float dt) {
    // 1. Process input
    input_update(&game->input);

    // 2. Apply input to player unit
    if (game->playerUnit && game->playerUnit->rootSection) {
        SectionInstance* root = game->playerUnit->rootSection.get();
        if (root && root->hasPhysics) {
            // Movement force
            Vector2 force = {
                game->input.movement.x * 50.0f,
                game->input.movement.y * 50.0f
            };
            b2Body_ApplyForceToCenter(root->bodyId,
                                      (b2Vec2){force.x, force.y}, true);

            // Mouse aim -> desired rotation
            game_update_player_rotation(game);

            // Apply rotation torque (PD controller)
            game_apply_player_rotation_torque(game);
        }
    }

    // 3. Handle quit
    if (game->input.quit) {
        game->running = false;
    }

    // 4. Debug mode toggle (0-6)
    for (int i = 0; i <= 6; i++) {
        if (IsKeyPressed(KEY_ZERO + i)) {
            game->debugMode = i;
            sceneRendererSetDebugMode(&game->sceneRenderer, i);
        }
    }

    // 5. Step physics
    physics_world_step(&game->physics, dt);

    // 6. Update units (syncs physics transforms)
    game->unitManager.update(dt);

    // 7. Camera follows player
    if (game->playerUnit && game->playerUnit->rootSection) {
        SectionInstance* root = game->playerUnit->rootSection.get();
        Vector3 playerPos = {
            root->worldPosition.x,  // Physics X -> World X
            0.0f,
            root->worldPosition.y   // Physics Y -> World Z
        };
        game->camera.target = playerPos;
        game->camera.position = (Vector3){playerPos.x, 50, playerPos.z};
    }
}
```

---

## Phase 9: Game Rendering

### Modified game_render()

```cpp
void game_render(Game* game) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    sceneRendererUpdateCamera(&game->sceneRenderer, game->camera.position);

    BeginMode3D(game->camera);

    // 1. Draw level tiles
    LevelRenderData& data = game->levelRenderData[game->currentLevel];
    if (data.meshValid) {
        DrawModel(data.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
    }

    // 2. Draw units
    game->unitManager.renderAll();

    // 3. Debug: collision shapes (press C)
    if (IsKeyDown(KEY_C)) {
        drawCollisionDebug(game->levelCollisionData[game->currentLevel],
                           RED, 0.05f);
    }

    EndMode3D();

    // HUD
    DrawFPS(10, 10);
    DrawText(TextFormat("Level: %s",
             game->levels[game->currentLevel].name.c_str()), 10, 30, 16, WHITE);

    EndDrawing();
}
```

---

## Phase 10: Cleanup

### Modified game_destroy()

```cpp
void game_destroy(Game* game) {
    // Unit manager
    game->unitManager.destroy();
    game->playerUnit = nullptr;

    // Level render data
    for (auto& data : game->levelRenderData) {
        freeLevelRenderData(&data);
    }

    // Level collision data
    for (auto& data : game->levelCollisionData) {
        freeLevelCollisionData(&data);
    }

    // Textures
    if (game->atlasTexture.id > 0) UnloadTexture(game->atlasTexture);
    if (game->bumpAtlasTexture.id > 0) UnloadTexture(game->bumpAtlasTexture);

    // Scene renderer
    sceneRendererDestroy(&game->sceneRenderer);

    // Physics
    physics_world_destroy(&game->physics);
}
```

---

## Implementation Order

1. Update CMakeLists.txt and SharedSources.cmake
2. Modify game.h with new struct
3. Implement game_load_levels() - test level loading
4. Implement game_build_level_render_data() - test tile rendering
5. Implement game_create_level_collision() - test collision bodies
6. Implement game_spawn_player() - test player unit
7. Update game_update() - test controls
8. Update game_render() - test full rendering
9. Update game_destroy() - test cleanup
10. Remove deprecated files

---

## Assets Used

- **Ship:** `assets/ships/ship1/`
- **Starting Level:** `assets/ships/ship1/levels/level_0_maintenance.tmx`
- **Tileset:** `assets/ships/ship1/levels/default.tsx`
- **Atlas:** `assets/ships/ship1/levels/map_blocks.png`
- **Tile Properties:** `assets/ships/ship1/levels/tiles.json`
- **Player Unit:** `assets/units/droid_class_0.json`
- **Player Model:** `assets/units/models/101.gltf`

---

## Coordinate Systems

| System | X | Y | Z |
|--------|---|---|---|
| Physics (Box2D) | Right | Forward | - |
| Rendering (3D) | Right | Up | Back |
| TMX (Tiled) | Right | Down | - |

**Mapping:**
- Physics (X, Y) -> Render (X, 0, Y)
- TMX grid (col, row) -> World centered coordinates
