#include "raylib.h"
#include "game.h"
#include <cstdio>
#include <cstring>

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
    printf("  --help, -h          Show this help\n");
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
    // Parse arguments
    const char* assetPath = "assets";  // Default

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--asset-path") == 0 && i + 1 < argc) {
            assetPath = argv[++i];
        }
    }

    printf("Asset path: %s\n", assetPath);
    printf("  Models:   %s/%s\n", assetPath, ASSET_MODELS);
    printf("  Textures: %s/%s\n", assetPath, ASSET_TEXTURES);
    printf("  Shaders:  %s/%s\n", assetPath, ASSET_SHADERS);
    printf("  Units:    %s/%s\n\n", assetPath, ASSET_UNITS);

    InitWindow(1280, 720, "Top-Down Game");
    SetTargetFPS(60);

    Game game = {0};
    game_init(&game, assetPath);

    while (!WindowShouldClose() && game.running) {
        float dt = GetFrameTime();
        game_update(&game, dt);
        game_render(&game);
    }

    game_destroy(&game);
    CloseWindow();

    return 0;
}
