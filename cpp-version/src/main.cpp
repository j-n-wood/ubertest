#include "raylib.h"
#include "game.h"
#include "args_file.h"
#include "rendering/texture_manager.h"
#include "pages/page_manager.h"
#include "pages/game_page.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

namespace fs = std::filesystem;

// Conventional asset directory structure
constexpr const char* ASSET_MODELS = "models";
constexpr const char* ASSET_TEXTURES = "textures";
constexpr const char* ASSET_SHADERS = "shaders";
constexpr const char* ASSET_UNITS = "units";

void printUsage(const char* programName) {
    printf("Usage: %s [options]\n", programName);
    printf("\n");
    printf("Top-Down Game\n");
    printf("\n");
    printf("Options:\n");
    printf("  --asset-path <dir>  Base path for assets (conventional structure)\n");
    printf("                      Default: ./assets\n");
    printf("  --unit <id>         Unit ID for player (default: droid_class_0)\n");
    printf("  --renderer <mode>   Level renderer: tilemap | custom | 3d (default: 3d; toggle in-game with G)\n");
    printf("  --deck <n>          Jump to deck number n after init (debug)\n");
    printf("  --args-file <path>  Read extra args from a file (also @path); '#' comments. Lets a fixed\n");
    printf("                      command drive different runs by editing the file\n");
    printf("  --help, -h          Show this help\n");
    printf("\n");
    printf("Rotation Test Mode:\n");
    printf("  --test-rotation     Enable rotation test mode (headless, exits after test)\n");
    printf("  --initial-rot <deg> Initial rotation in degrees (default: 0)\n");
    printf("  --target-rot <deg>  Target rotation in degrees (default: 90)\n");
    printf("  --test-frames <n>   Number of frames to run (default: 300)\n");
    printf("  --sample-interval <n> Report rotation every N frames (default: 30)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --unit droid_class_3\n", programName);
    printf("  %s --test-rotation --initial-rot 0 --target-rot 90 --unit droid_class_1\n", programName);
    printf("\n");
    printf("Conventional asset structure:\n");
    printf("  <asset-path>/\n");
    printf("    models/     - Model files (GLTF/GLB)\n");
    printf("    textures/   - Texture images\n");
    printf("    shaders/    - Shader files\n");
    printf("    units/      - Unit definition JSON files\n");
    printf("\n");
    printf("Controls:\n");
    printf("  WASD        - Move\n");
    printf("  Mouse       - Aim\n");
    printf("  0-5         - Debug visualization modes\n");
    printf("  ESC         - Quit\n");
}

int main(int argc, char* argv[]) {
    // Expand any --args-file/@file tokens so a fixed command can drive different runs (see args_file.h).
    std::vector<std::string> argStore = expandArgsFiles(argc, argv);
    std::vector<char*> argPtrs;
    for (auto& s : argStore) argPtrs.push_back(const_cast<char*>(s.c_str()));
    argc = (int)argPtrs.size();
    argv = argPtrs.data();

    // Parse arguments
#ifdef GAME_SOURCE_ASSETS_DIR
    const char* assetPath = GAME_SOURCE_ASSETS_DIR;  // absolute source dir (edits persist)
#else
    const char* assetPath = "assets";  // Default (relative to cwd)
#endif
    const char* unitId = nullptr;      // Default (will use droid_class_0)
    RotationTestConfig testConfig;
    LevelRenderMode renderMode = LevelRenderMode::Objects3D;  // default; --renderer overrides, G toggles at runtime
    int startDeck = -1;  // --deck N: jump to deck N after init (debug), -1 = start on deck 0

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--asset-path") == 0 && i + 1 < argc) {
            assetPath = argv[++i];
        } else if (strcmp(argv[i], "--test-rotation") == 0) {
            testConfig.enabled = true;
        } else if (strcmp(argv[i], "--initial-rot") == 0 && i + 1 < argc) {
            testConfig.initialRotation = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--target-rot") == 0 && i + 1 < argc) {
            testConfig.targetRotation = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--test-frames") == 0 && i + 1 < argc) {
            testConfig.testFrames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sample-interval") == 0 && i + 1 < argc) {
            testConfig.sampleInterval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char* r = argv[++i];
            if (strcmp(r, "tilemap") == 0)      renderMode = LevelRenderMode::Tilemap;
            else if (strcmp(r, "custom") == 0)  renderMode = LevelRenderMode::CustomTiles;
            else if (strcmp(r, "3d") == 0)      renderMode = LevelRenderMode::Objects3D;
            else fprintf(stderr, "Unknown --renderer '%s' (use tilemap|custom|3d)\n", r);
        } else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unitId = argv[++i];
            testConfig.unitId = unitId;  // Also set in test config for compatibility
        } else if (strcmp(argv[i], "--deck") == 0 && i + 1 < argc) {
            startDeck = atoi(argv[++i]);   // jump to this deck number after init (debug)
        }
    }

    // Validate asset path exists
    if (!fs::exists(assetPath) || !fs::is_directory(assetPath)) {
        fprintf(stderr, "Error: Asset path does not exist: %s\n", assetPath);
        return 1;
    }

    if (testConfig.enabled) {
        printf("=== ROTATION TEST MODE ===\n");
        printf("Unit: %s\n", testConfig.unitId.c_str());
        printf("Initial rotation: %.1f deg\n", testConfig.initialRotation);
        printf("Target rotation: %.1f deg\n", testConfig.targetRotation);
        printf("Test frames: %d\n", testConfig.testFrames);
        printf("Sample interval: %d frames\n", testConfig.sampleInterval);
        printf("==========================\n\n");
    } else {
        printf("Asset path: %s\n", assetPath);
        printf("  Models:   %s/%s\n", assetPath, ASSET_MODELS);
        printf("  Textures: %s/%s\n", assetPath, ASSET_TEXTURES);
        printf("  Shaders:  %s/%s\n", assetPath, ASSET_SHADERS);
        printf("  Units:    %s/%s\n\n", assetPath, ASSET_UNITS);
    }

    InitWindow(1280, 720, testConfig.enabled ? "Rotation Test" : "Top-Down Game");
    // Disable raylib's built-in ESC-to-close: ESC is handled per-context instead
    // (console pages pop back a level; gameplay input treats it as quit).
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    // Own the texture manager here (after the GL context exists) so its unloadAll()
    // runs at textures.reset() below — BEFORE CloseWindow. A static-duration singleton
    // would instead be torn down at process exit, after the context is gone.
    auto textures = std::make_unique<TextureManager>();

    Game game{};
    game.levelRenderMode = renderMode;  // startup renderer selection (before the first build in game_init)
    game_init(&game, assetPath, unitId, testConfig.enabled ? &testConfig : nullptr);
    if (startDeck >= 0) game_debug_goto_deck(&game, startDeck);  // debug: jump to a specific deck

    // View-states are pages on a stack; gameplay is the base GamePage. Other pages
    // (console, future title) are pushed on top and drive update/render while active.
    PageManager pages;
    pages.push(std::make_unique<GamePage>(&game, &pages));

    while (!WindowShouldClose() && game.running) {
        float dt = GetFrameTime();
        pages.update(dt);
        pages.render();
    }

    game_destroy(&game);
    textures.reset();   // unloadAll() while the GL context is still alive
    CloseWindow();

    return 0;
}
