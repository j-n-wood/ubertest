#include "test_scene.h"
#include "raylib.h"
#include <iostream>
#include <string>
#include <string_view>

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr const char* WINDOW_TITLE = "Unit Test Tool";

//------------------------------------------------------------------------------
// Usage
//------------------------------------------------------------------------------

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <unit_definition.json>\n";
    std::cout << "\n";
    std::cout << "Loads and displays a unit definition for testing.\n";
    std::cout << "\n";
    std::cout << "Controls:\n";
    std::cout << "  WASD/QE     - Move camera\n";
    std::cout << "  Right Mouse - Look around\n";
    std::cout << "  Scroll      - Zoom in/out\n";
    std::cout << "  Space       - Apply impulse to root body\n";
    std::cout << "  B           - Break all joints (deconstruct)\n";
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

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            unitPath = arg;
        }
    }

    if (unitPath.empty()) {
        std::cerr << "Error: No unit definition file specified\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Initialize window
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(60);

    // Initialize scene
    TestScene scene = {};
    testSceneInit(&scene);

    // Load the unit
    if (!testSceneLoadUnit(&scene, unitPath.c_str())) {
        std::cerr << "Failed to load unit: " << unitPath << std::endl;
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
