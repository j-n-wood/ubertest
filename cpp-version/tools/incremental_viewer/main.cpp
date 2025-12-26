#include "raylib.h"
#include "viewer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE "Incremental Scene Viewer"

// Default paths for source data (relative to uber directory)
static const char* DEFAULT_TILES_PATH = "../uber/uberdroid/data/tiles.txt";
static const char* DEFAULT_SOURCE_PATH = "../uber/uberdroid/ship1/xmapfile0.txt";
static const char* DEFAULT_TEXTURES_PATH = "../uber/uberdroid/data/textures.txt";
static const char* DEFAULT_TEXTURES_BASE = "../uber/uberdroid/";

//------------------------------------------------------------------------------
// Print usage help
//------------------------------------------------------------------------------
static void printHelp() {
    printf("Incremental Scene Viewer - validates scene data conversion\n\n");
    printf("Usage: incremental_viewer [options] [source_path]\n\n");
    printf("Options:\n");
    printf("  -o, --output <dir>     Output directory (default: ./output)\n");
    printf("  -t, --tiles <path>     Path to tiles.txt (default: %s)\n", DEFAULT_TILES_PATH);
    printf("  -x, --textures <path>  Path to textures.txt (default: %s)\n", DEFAULT_TEXTURES_PATH);
    printf("  -s, --scale <factor>   Scale factor override (default: 0.0254)\n");
    printf("  --no-reference         Don't load reference model\n");
    printf("  --no-textures          Don't load textures\n");
    printf("  --help                 Show this help\n\n");
    printf("Controls:\n");
    printf("  WASD          Move camera\n");
    printf("  Q/E           Move up/down\n");
    printf("  Shift         Move faster\n");
    printf("  Mouse wheel   Zoom\n");
    printf("  R             Reset camera\n");
    printf("  0-5           Debug visualization modes\n");
    printf("  F1            Toggle grid\n");
    printf("  F2            Toggle reference model\n");
    printf("  F3            Toggle tiles\n");
    printf("  F4            Toggle wireframe\n");
    printf("  H             Toggle help overlay\n");
    printf("  ESC           Quit\n\n");
    printf("Examples:\n");
    printf("  incremental_viewer\n");
    printf("  incremental_viewer %s\n", DEFAULT_SOURCE_PATH);
    printf("  incremental_viewer ../uber/uberdroid/ship1/xmapfile0.txt -o output/\n");
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Parse command line arguments
    const char* sourcePath = nullptr;
    const char* outputDir = "output";
    const char* tilesPath = DEFAULT_TILES_PATH;
    const char* texturesPath = DEFAULT_TEXTURES_PATH;
    const char* texturesBasePath = DEFAULT_TEXTURES_BASE;
    float scale = 0.0254f;  // inches to meters
    bool loadReference = true;
    bool loadTextures = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            outputDir = argv[++i];
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tiles") == 0) && i + 1 < argc) {
            tilesPath = argv[++i];
        } else if ((strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--textures") == 0) && i + 1 < argc) {
            texturesPath = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) && i + 1 < argc) {
            scale = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-reference") == 0) {
            loadReference = false;
        } else if (strcmp(argv[i], "--no-textures") == 0) {
            loadTextures = false;
        } else if (argv[i][0] != '-') {
            sourcePath = argv[i];
        }
    }

    printf("=== Incremental Scene Viewer ===\n");
    printf("Scale factor: %.6f\n", scale);
    printf("Output directory: %s\n", outputDir);
    printf("Tiles path: %s\n", tilesPath);
    printf("Textures path: %s\n", texturesPath);
    if (sourcePath) {
        printf("Source path: %s\n", sourcePath);
    }
    printf("\n");

    // Initialize window
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(60);

    // Initialize viewer
    Viewer viewer = {};
    if (!viewerInit(&viewer, "assets/shaders/")) {
        fprintf(stderr, "Error: Failed to initialize viewer\n");
        CloseWindow();
        return 1;
    }

    // Load reference model (Suzanne)
    if (loadReference) {
        if (!viewerLoadReference(&viewer, "assets/models/Suzanne.glb")) {
            fprintf(stderr, "Warning: Failed to load reference model\n");
        }
    }

    // Load texture lookup
    if (loadTextures) {
        if (!viewerLoadTextures(&viewer, texturesPath, texturesBasePath)) {
            fprintf(stderr, "Warning: Failed to load texture lookup\n");
        }
    }

    // If source path provided, convert and load
    if (sourcePath) {
        if (!viewerConvertAndLoad(&viewer, sourcePath, tilesPath, outputDir, scale)) {
            fprintf(stderr, "Warning: Failed to convert source file\n");
        }
    }

    // Main loop
    while (!WindowShouldClose()) {
        viewerUpdate(&viewer, GetFrameTime());

        BeginDrawing();
        ClearBackground(DARKGRAY);

        viewerRender(&viewer);
        viewerDrawOverlay(&viewer);

        EndDrawing();
    }

    // Cleanup
    viewerCleanup(&viewer);
    CloseWindow();

    return 0;
}
