#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "physics/physics_world.h"
#include "input/input.h"
#include "transfer_control.h"

// Shared includes
#include "level/level_types.h"
#include "level/tmx_loader.h"
#include "level/tileset_loader.h"
#include "level/level_renderer.h"
#include "level/tile_properties_loader.h"
#include "level/console_manager.h"
#include "level/lift_manager.h"
#include "level/ship_map.h"
#include "score/scoring.h"
#include "rendering/scene_renderer.h"
#include "rendering/door_renderer.h"
#include "rendering/charger_renderer.h"
#include "units/unit_manager.h"
#include "units/weapon.h"
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

    // Per-level physics worlds (one per level, retained for the ship's lifetime). Only the
    // active level's world (== physics.world_id) is stepped/rendered; inactive levels'
    // droids freeze in place in their own world. See docs/levels.md.
    std::vector<b2WorldId> levelWorlds;                 // parallel to levels
    std::vector<b2BodyId> levelOrigins;                 // motor-joint anchor per world
    std::vector<std::vector<UnitInstance*>> levelUnits; // persistent roster per level
    std::vector<bool> levelPopulated;                   // roster created (lazily, on first entry)
    std::vector<double> levelLastActive;                // gameClock when last deactivated (heal timing)
    double gameClock = 0.0;                              // accumulated gameplay time (for away-heal)

    // Tileset (shared across levels)
    TmxTileset tileset;
    Texture2D atlasTexture;
    Texture2D bumpAtlasTexture;
    Texture2D flareTexture;       // additive glow sprite for plasma projectiles (see docs/weapons.md)
    Texture2D blasterBlobTexture; // horizontal streak sprite for laser projectiles (rotated to travel dir)
    TilePropertiesConfig tileProperties;

    // Collision bodies from tile data
    std::vector<PhysicsBody> collisionBodies;

    // Unit system
    ModelCache modelCache;   // shared GLTF models across all instances/levels/library
    UnitManager unitManager;
    UnitInstance* playerUnit;
    float playerDesiredRotation;  // For mouse aim
    std::vector<UnitInstance*> enemyUnits;  // Tracked for level-switch cleanup

    // Player weapon cooldown state (the controlled unit's weapon, or the device's plasma
    // bolt fallback). Re-inited when the effective weapon changes. See docs/weapons.md.
    WeaponState playerWeapon;

    // Transfer mechanic: the player device pilots AI units (see docs/transfer.md).
    TransferState transfer;

    // Combat & AI
    ProjectileManager projectileManager;
    AIManager aiManager;

    // Doors: simulation, plus the interim 2D renderer that reads doorManager.views()
    DoorManager doorManager;
    DoorRenderer doorRenderer;

    // Chargers: animated walkable objects (proximity IDLE/CHARGING + free-run anim)
    ChargerManager chargerManager;
    ChargerRenderer chargerRenderer;

    // Consoles: static usable tiles (player-near-centre -> SPACE opens console page)
    ConsoleManager consoleManager;

    // Lifts: elevator graph built from in-map lift objects (SPACE opens the ship view);
    // shipMap holds the side-on rendering rects (shipmap.json).
    LiftManager liftManager;
    ShipMap shipMap;

    // Camera
    Camera3D camera;
    float cameraHeight;        // Height above ground (Y position)
    float effectiveEyeHeight;  // For specular lighting calculations

    // Score + ship alert (see docs/scoring.md). score/alertLevel update immediately on
    // award; scoreDisplay lags and clocks toward score at SCORE_CLOCK_RATE. alertLevel
    // resets to 0 (green) on ship activation; score persists across ships.
    double score = 0.0;
    double scoreDisplay = 0.0;
    double alertLevel = 0.0;

    // State
    bool running;
    int debugMode;
    bool showAIDebug = false;  // V key: draw waypoints + per-unit AI state
    bool paused = false;       // P key: freeze all game-state updates (still renders)
    bool slowMotion = false;   // O key: debug 1/10-speed simulation

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
// The gameplay view-state's per-frame update/render (driven by GamePage via the
// PageManager). Named *_gameplay because the console/title screens are other pages.
void game_update_gameplay(Game* game, float dt);
void game_render_gameplay(Game* game);
void game_destroy(Game* game);

// Move the player to a lift stop, switching level if the stop is on another deck.
// Called by the ship-view page when the player confirms a destination.
void game_switch_to_stop(Game* game, const LiftStop& stop);

// Award score + raise the alert level for destroying or capturing `unit` (50 x its
// class). Called on a kill (game_reap_dead) and on a completed capture (transfer).
void game_award_points(Game* game, const UnitInstance* unit);

#endif
