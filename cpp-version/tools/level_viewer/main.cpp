#include "viewer_state.h"
#include "camera_controller.h"
#include "raylib.h"
#include "rlgl.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960

//------------------------------------------------------------------------------
// Print usage information
//------------------------------------------------------------------------------
static void printUsage(const char* progName) {
    printf("Level Viewer - TMX Level Visualization Tool\n\n");
    printf("Usage: %s [options]\n\n", progName);
    printf("Options:\n");
    printf("  --input <path>       Folder containing TMX level files\n");
    printf("                       (default: <asset-path>/ships/ship1/levels)\n");
    printf("  --asset-path <path>  Base path for assets (default: assets)\n");
    printf("  --level <n>          Start at level N (default: 0)\n");
    printf("  --scale <factor>     World units per tile (default: 1.0)\n");
    printf("  -h, --help           Show this help message\n\n");
    printf("Controls:\n");
    printf("  [/]        Previous/Next level\n");
    printf("  1-9        Jump to level N\n");
    printf("  V          Cycle camera mode (Perspective/Topdown/Isometric)\n");
    printf("  WASD       Pan camera\n");
    printf("  Q/E        Orbit camera left/right\n");
    printf("  Up/Down    Zoom in/out\n");
    printf("  PgUp/PgDn  Adjust camera height\n");
    printf("  C          Center camera on level\n");
    printf("  Space      Toggle auto-rotate\n");
    printf("  0-6        Shader debug modes\n");
    printf("  G          Toggle grid\n");
    printf("  P          Toggle waypoints\n");
    printf("  L          Toggle waypoint links\n");
    printf("  B          Toggle bounds\n");
    printf("  K          Toggle backface culling\n");
    printf("  O          Toggle origin reference sphere\n");
    printf("  H          Toggle HUD\n");
    printf("  R          Reset view\n");
    printf("  Esc        Quit\n\n");
    printf("Examples:\n");
    printf("  %s                                    # Use defaults\n", progName);
    printf("  %s --input ./output/ships/ship1/levels\n", progName);
}

//------------------------------------------------------------------------------
// Parse command line arguments
//------------------------------------------------------------------------------
struct AppConfig {
    std::string inputPath;
    std::string assetPath = "assets";
    int startLevel = 0;
    float worldScale = 1.0f;
    bool showHelp = false;
};

static bool parseArgs(int argc, char* argv[], AppConfig& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            config.showHelp = true;
            return true;
        }
        else if (arg == "--input" && i + 1 < argc) {
            config.inputPath = argv[++i];
        }
        else if (arg == "--asset-path" && i + 1 < argc) {
            config.assetPath = argv[++i];
        }
        else if (arg == "--level" && i + 1 < argc) {
            config.startLevel = std::stoi(argv[++i]);
        }
        else if (arg == "--scale" && i + 1 < argc) {
            config.worldScale = std::stof(argv[++i]);
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Handle input
//------------------------------------------------------------------------------
static void handleInput(LevelViewerState* state) {
    // Level navigation
    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
        int newLevel = state->currentLevel - 1;
        if (newLevel >= 0) {
            viewerStateSwitchLevel(state, newLevel);
        }
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
        int newLevel = state->currentLevel + 1;
        if (newLevel < (int)state->levels.size()) {
            viewerStateSwitchLevel(state, newLevel);
        }
    }

    // Number keys for direct level selection
    for (int i = 0; i < 9; i++) {
        if (IsKeyPressed(KEY_ONE + i)) {
            if (i < (int)state->levels.size()) {
                viewerStateSwitchLevel(state, i);
            }
        }
    }

    // Debug modes (0-6)
    for (int i = 0; i <= 6; i++) {
        if (IsKeyPressed(KEY_ZERO + i) || IsKeyPressed(KEY_KP_0 + i)) {
            state->debugMode = i;
            sceneRendererSetDebugMode(&state->renderer, i);
        }
    }

    // Toggle controls
    if (IsKeyPressed(KEY_G)) {
        state->showGrid = !state->showGrid;
    }
    if (IsKeyPressed(KEY_P)) {
        state->showWaypoints = !state->showWaypoints;
    }
    if (IsKeyPressed(KEY_L)) {
        state->showWaypointLinks = !state->showWaypointLinks;
    }
    if (IsKeyPressed(KEY_B)) {
        state->showBounds = !state->showBounds;
    }
    if (IsKeyPressed(KEY_K)) {
        state->backfaceCulling = !state->backfaceCulling;
    }
    if (IsKeyPressed(KEY_O)) {
        state->showRefSphere = !state->showRefSphere;
    }
    if (IsKeyPressed(KEY_H)) {
        state->showHUD = !state->showHUD;
    }
    if (IsKeyPressed(KEY_SPACE)) {
        state->autoRotate = !state->autoRotate;
    }
    if (IsKeyPressed(KEY_C)) {
        viewerStateCenterCamera(state);
    }
    if (IsKeyPressed(KEY_R)) {
        cameraControllerReset(state);
    }
    if (IsKeyPressed(KEY_V)) {
        // Cycle camera view mode: Perspective -> Topdown -> Isometric -> Perspective
        int mode = static_cast<int>(state->cameraMode);
        mode = (mode + 1) % 3;
        state->cameraMode = static_cast<LevelViewerState::CameraMode>(mode);
        viewerStateUpdateCamera(state);
    }
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    AppConfig config;

    if (!parseArgs(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }

    if (config.showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    // Default input path relative to asset path
    if (config.inputPath.empty()) {
        config.inputPath = (fs::path(config.assetPath) / "ships" / "ship1" / "levels").string();
    }

    // Validate paths
    if (!fs::exists(config.inputPath)) {
        fprintf(stderr, "Error: Input path does not exist: %s\n", config.inputPath.c_str());
        return 1;
    }

    if (!fs::exists(config.assetPath)) {
        fprintf(stderr, "Error: Asset path does not exist: %s\n", config.assetPath.c_str());
        return 1;
    }

    // Initialize window
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Level Viewer");
    SetTargetFPS(60);

    // Initialize viewer state
    LevelViewerState state;
    state.worldScale = config.worldScale;

    if (!viewerStateInit(&state, config.inputPath, config.assetPath)) {
        fprintf(stderr, "Error: Failed to initialize viewer\n");
        CloseWindow();
        return 1;
    }

    // Load levels
    if (!viewerStateLoadLevels(&state)) {
        fprintf(stderr, "Error: Failed to load levels from: %s\n", config.inputPath.c_str());
        viewerStateDestroy(&state);
        CloseWindow();
        return 1;
    }

    // Switch to start level if specified
    if (config.startLevel > 0 && config.startLevel < (int)state.levels.size()) {
        viewerStateSwitchLevel(&state, config.startLevel);
    }

    printf("Level Viewer ready. Loaded %zu levels.\n", state.levels.size());
    printf("Press H to toggle HUD, Esc to quit.\n");

    // Main loop
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();

        // Input
        handleInput(&state);
        cameraControllerUpdate(&state, deltaTime);

        // Draw
        BeginDrawing();
        ClearBackground((Color){25, 25, 30, 255});

        BeginMode3D(state.camera);

        // Draw level tiles
        viewerStateDrawLevel(&state);

        // Draw debug elements
        if (state.showGrid) {
            DrawGrid(20, 1.0f);
        }

        viewerStateDrawWaypoints(&state);
        viewerStateDrawBounds(&state);
        viewerStateDrawRefSphere(&state);

        EndMode3D();

        // Draw HUD
        viewerStateDrawHUD(&state);

        // FPS counter
        DrawFPS(WINDOW_WIDTH - 100, 10);

        EndDrawing();
    }

    // Cleanup
    viewerStateDestroy(&state);
    CloseWindow();

    return 0;
}
