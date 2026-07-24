#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "physics/physics_world.h"
#include "input/input.h"

// Shared includes
#include "level/level_types.h"
#include "level/tmx_loader.h"
#include "level/tileset_loader.h"
#include "level/level_renderer.h"
#include "level/tile_properties_loader.h"
#include "rendering/scene_renderer.h"
#include "rendering/door_renderer.h"
#include "units/unit_manager.h"
#include "combat/projectile_manager.h"
#include "ai/ai_manager.h"
#include "level/door_manager.h"

#include <vector>
#include <string>

// Test mode configuration for rotation testing
struct RotationTestConfig {
    bool enabled = false;
    float initialRotation = 0.0f;    // Initial rotation in degrees
    float targetRotation = 90.0f;    // Target rotation in degrees
    int testFrames = 300;            // Number of frames to run
    int sampleInterval = 30;         // Report rotation every N frames
    std::string unitId = "droid_class_0";  // Unit to test with
};

struct Game {
    // Physics
    PhysicsWorld physics;

    // Rendering (shared scene renderer)
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
    std::vector<UnitInstance*> enemyUnits;  // Tracked for level-switch cleanup

    // Combat & AI
    ProjectileManager projectileManager;
    AIManager aiManager;

    // Doors: simulation, plus the interim 2D renderer that reads doorManager.views()
    DoorManager doorManager;
    DoorRenderer doorRenderer;

    // Camera
    Camera3D camera;
    float cameraHeight;        // Height above ground (Y position)
    float effectiveEyeHeight;  // For specular lighting calculations

    // State
    bool running;
    int debugMode;
    bool showAIDebug = false;  // V key: draw waypoints + per-unit AI state

    // Paths
    std::string assetPath;
    std::string shadersPath;
    std::string levelsPath;

    // Player unit ID (can be set via command line)
    std::string playerUnitId;

    // Test mode
    RotationTestConfig testConfig;
    int testFrameCount;
};

// Initialize game with asset path, optional unit ID, and optional test config
void game_init(Game* game, const char* assetPath = "assets", const char* unitId = nullptr, const RotationTestConfig* testConfig = nullptr);
void game_update(Game* game, float dt);
void game_render(Game* game);
void game_destroy(Game* game);

#endif
