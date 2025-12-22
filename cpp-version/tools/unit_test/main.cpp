#include "test_scene.h"
#include "raylib.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr const char* WINDOW_TITLE = "Unit Test Tool";

// Conventional asset directory structure
constexpr const char* ASSET_MODELS = "models";
constexpr const char* ASSET_TEXTURES = "textures";
constexpr const char* ASSET_SHADERS = "shaders";
constexpr const char* ASSET_UNITS = "units";

//------------------------------------------------------------------------------
// Usage
//------------------------------------------------------------------------------

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <unit_definition.json>\n";
    std::cout << "\n";
    std::cout << "Loads and displays a unit definition for testing.\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --asset-path <dir>  Base path for assets (conventional structure)\n";
    std::cout << "                      Unit paths resolve to: <asset-path>/units/<file>\n";
    std::cout << "                      Model paths resolve to: <asset-path>/models/<path>\n";
    std::cout << "  --help, -h          Show this help\n";
    std::cout << "\n";
    std::cout << "Conventional asset structure:\n";
    std::cout << "  <asset-path>/\n";
    std::cout << "    units/      - Unit definition JSON files\n";
    std::cout << "    models/     - GLTF/GLB model files\n";
    std::cout << "    textures/   - Texture images\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " --asset-path ./output droid_class_3.json\n";
    std::cout << "  " << programName << " /full/path/to/unit.json\n";
    std::cout << "\n";
    std::cout << "Controls:\n";
    std::cout << "  WASD/QE     - Move camera\n";
    std::cout << "  Right Mouse - Look around\n";
    std::cout << "  Scroll      - Zoom in/out\n";
    std::cout << "  Space       - Apply impulse to root body\n";
    std::cout << "  +/-         - Increase/decrease impulse strength\n";
    std::cout << "  B           - Break all joints (deconstruct)\n";
    std::cout << "  X           - Explode (break + impulse all sections)\n";
    std::cout << "  1-9         - Break individual section joints\n";
    std::cout << "  R           - Reset (respawn unit)\n";
    std::cout << "  P           - Pause/resume physics\n";
    std::cout << "  F1          - Toggle debug visualization\n";
    std::cout << "  I           - Toggle info overlay\n";
    std::cout << "  ESC         - Exit\n";
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Parse arguments
    std::string unitPath;
    std::string assetPath;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--asset-path" && i + 1 < argc) {
            assetPath = argv[++i];
        } else if (arg[0] != '-') {
            unitPath = arg;
        }
    }

    if (unitPath.empty()) {
        std::cerr << "Error: No unit definition file specified\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Resolve paths using asset-path convention
    std::string resolvedUnitPath = unitPath;
    std::string modelsBasePath;
    std::string shadersPath = "shaders/";  // Default: look in current directory

    if (!assetPath.empty()) {
        fs::path base(assetPath);

        // Resolve unit path: if relative, prepend asset-path/units/
        if (fs::path(unitPath).is_relative()) {
            resolvedUnitPath = (base / ASSET_UNITS / unitPath).string();
        }

        // Set models base path for resolving model references within unit definitions
        modelsBasePath = (base / ASSET_MODELS).string();

        // Set shaders path (with trailing slash for shader loading)
        shadersPath = (base / ASSET_SHADERS).string() + "/";

        std::cout << "Asset path: " << assetPath << "\n";
        std::cout << "  Units:   " << (base / ASSET_UNITS).string() << "\n";
        std::cout << "  Models:  " << modelsBasePath << "\n";
        std::cout << "  Shaders: " << shadersPath << "\n";
        std::cout << "  Unit file: " << resolvedUnitPath << "\n\n";
    }

    // Initialize window
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(60);

    // Initialize scene with shaders and optional models base path
    TestScene scene = {};
    if (!testSceneInit(&scene, shadersPath.c_str(), modelsBasePath.c_str())) {
        std::cerr << "Failed to initialize scene (check shader path: " << shadersPath << ")" << std::endl;
        CloseWindow();
        return 1;
    }

    // Load the unit
    if (!testSceneLoadUnit(&scene, resolvedUnitPath.c_str())) {
        std::cerr << "Failed to load unit: " << resolvedUnitPath << std::endl;
        testSceneDestroy(&scene);
        CloseWindow();
        return 1;
    }

    // Main loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Update
        testSceneUpdate(&scene, dt);

        // Render
        BeginDrawing();
        ClearBackground({30, 30, 35, 255});

        testSceneRender(&scene);

        EndDrawing();
    }

    // Cleanup
    testSceneDestroy(&scene);
    CloseWindow();

    return 0;
}
