#include "scene_viewer.h"
#include "scene_types.h"
#include "scene_json.h"
#include "ship_parser.h"
#include "domain_parser.h"
#include "archetile_parser.h"
#include "raylib.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Command Line Parsing
//------------------------------------------------------------------------------

struct Arguments {
    enum class Mode {
        Help,
        ConvertShip,
        ConvertDomain,
        View
    };

    Mode mode = Mode::Help;
    std::string inputPath;
    std::string outputPath;
    std::string tilesPath;
    std::string assetPath;
    std::string referenceModelPath;  // Optional GLTF model for reference rendering
    int domainIndex = 0;
    bool showPhysics = true;
};

static void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  --convert-ship <path>    Convert ship file to JSON\n";
    std::cout << "  --convert-domain <path>  Convert domain file to JSON\n";
    std::cout << "  --view <path>            View ship or domain JSON file\n";
    std::cout << "  --help                   Show this help message\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <path>      Output directory or file path\n";
    std::cout << "  -t, --tiles <path>       Path to tiles.txt for archetile expansion\n";
    std::cout << "  -a, --asset-path <path>  Base path for assets (shaders, etc.)\n";
    std::cout << "  -d, --domain <n>         Domain index to view (default: 0)\n";
    std::cout << "  --reference-model <path> Load GLTF model for reference rendering comparison\n";
    std::cout << "  --no-physics             Disable physics debug display\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " --convert-ship ../uber/uberdroid/data/ship1.txt -o output/\n";
    std::cout << "  " << programName << " --convert-domain ../uber/uberdroid/ship1/xmapfile0.txt -t ../uber/uberdroid/data/tiles.txt -o domain_0.json\n";
    std::cout << "  " << programName << " --view output/ship.json --domain 0\n";
}

static Arguments parseArguments(int argc, char* argv[]) {
    Arguments args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            args.mode = Arguments::Mode::Help;
            return args;
        }
        else if (arg == "--convert-ship") {
            args.mode = Arguments::Mode::ConvertShip;
            if (i + 1 < argc) args.inputPath = argv[++i];
        }
        else if (arg == "--convert-domain") {
            args.mode = Arguments::Mode::ConvertDomain;
            if (i + 1 < argc) args.inputPath = argv[++i];
        }
        else if (arg == "--view") {
            args.mode = Arguments::Mode::View;
            if (i + 1 < argc) args.inputPath = argv[++i];
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) args.outputPath = argv[++i];
        }
        else if (arg == "-t" || arg == "--tiles") {
            if (i + 1 < argc) args.tilesPath = argv[++i];
        }
        else if (arg == "-a" || arg == "--asset-path") {
            if (i + 1 < argc) args.assetPath = argv[++i];
        }
        else if (arg == "-d" || arg == "--domain") {
            if (i + 1 < argc) args.domainIndex = std::stoi(argv[++i]);
        }
        else if (arg == "--no-physics") {
            args.showPhysics = false;
        }
        else if (arg == "--reference-model") {
            if (i + 1 < argc) args.referenceModelPath = argv[++i];
        }
    }

    return args;
}

//------------------------------------------------------------------------------
// Conversion Functions
//------------------------------------------------------------------------------

static bool convertShip(const Arguments& args) {
    std::cout << "Converting ship: " << args.inputPath << std::endl;

    Ship ship;
    if (!parseShipFile(args.inputPath, ship)) {
        std::cerr << "Failed to parse ship file" << std::endl;
        return false;
    }

    // Determine output path
    fs::path outputDir = args.outputPath.empty() ? "output" : args.outputPath;
    fs::create_directories(outputDir);
    fs::create_directories(outputDir / "domains");

    // Determine tiles path
    fs::path tilesPath = args.tilesPath;
    if (tilesPath.empty()) {
        // Try to find tiles.txt relative to ship file
        fs::path shipDir = fs::path(args.inputPath).parent_path();
        tilesPath = shipDir / "tiles.txt";
        if (!fs::exists(tilesPath)) {
            tilesPath = shipDir.parent_path() / "data" / "tiles.txt";
        }
    }

    // Load archetiles
    if (fs::exists(tilesPath)) {
        std::cout << "Loading archetiles from: " << tilesPath << std::endl;
        ensureArchetilesLoaded(tilesPath);
    } else {
        std::cout << "Warning: tiles.txt not found, archetiles will not be expanded" << std::endl;
    }

    // Convert and save each domain
    // Domain paths are relative to the uberdroid root (parent of data directory)
    fs::path shipDir = fs::path(args.inputPath).parent_path();
    fs::path uberdroidRoot = shipDir.parent_path();  // Go up from data/ to uberdroid/
    std::vector<std::string> domainJsonPaths;

    for (size_t i = 0; i < ship.domainPaths.size(); ++i) {
        fs::path domainSrcPath = uberdroidRoot / ship.domainPaths[i];
        std::cout << "Converting domain " << i << ": " << domainSrcPath << std::endl;

        Domain domain;
        if (parseDomainFile(domainSrcPath.string(), domain, uberdroidRoot, tilesPath)) {
            std::string domainFilename = "domain_" + std::to_string(i) + ".json";
            fs::path domainOutPath = outputDir / "domains" / domainFilename;

            if (saveDomainToFile(domainOutPath.string(), domain)) {
                std::cout << "  Saved: " << domainOutPath << std::endl;
                domainJsonPaths.push_back("domains/" + domainFilename);
            } else {
                std::cerr << "  Failed to save domain" << std::endl;
            }
        } else {
            std::cerr << "  Failed to parse domain" << std::endl;
        }
    }

    // Update ship's domain paths to point to JSON files
    ship.domainPaths = domainJsonPaths;

    // Save ship JSON
    fs::path shipOutPath = outputDir / "ship.json";
    if (saveShipToFile(shipOutPath.string(), ship)) {
        std::cout << "Saved ship: " << shipOutPath << std::endl;
    } else {
        std::cerr << "Failed to save ship JSON" << std::endl;
        return false;
    }

    std::cout << "Conversion complete!" << std::endl;
    return true;
}

static bool convertDomain(const Arguments& args) {
    std::cout << "Converting domain: " << args.inputPath << std::endl;

    // Load archetiles if path provided
    if (!args.tilesPath.empty()) {
        std::cout << "Loading archetiles from: " << args.tilesPath << std::endl;
        ensureArchetilesLoaded(args.tilesPath);
    }

    Domain domain;
    fs::path basePath = fs::path(args.inputPath).parent_path();

    if (!parseDomainFile(args.inputPath, domain, basePath, args.tilesPath)) {
        std::cerr << "Failed to parse domain file" << std::endl;
        return false;
    }

    // Determine output path
    std::string outputPath = args.outputPath;
    if (outputPath.empty()) {
        outputPath = "domain_" + std::to_string(domain.levelNumber) + ".json";
    }

    if (saveDomainToFile(outputPath, domain)) {
        std::cout << "Saved domain: " << outputPath << std::endl;
    } else {
        std::cerr << "Failed to save domain JSON" << std::endl;
        return false;
    }

    // Print summary
    int tileCount = 0;
    int featureCount = 0;
    for (const auto& area : domain.areas) {
        tileCount += static_cast<int>(area.tiles.size());
        featureCount += static_cast<int>(area.features.size());
    }

    std::cout << "\nDomain Summary:" << std::endl;
    std::cout << "  Name: " << domain.name << std::endl;
    std::cout << "  Level: " << domain.levelNumber << std::endl;
    std::cout << "  Areas: " << domain.areas.size() << std::endl;
    std::cout << "  Tiles: " << tileCount << std::endl;
    std::cout << "  Features: " << featureCount << std::endl;
    std::cout << "  Waypoints: " << domain.waypoints.size() << std::endl;
    std::cout << "  Doors: " << domain.objects.doors.size() << std::endl;
    std::cout << "  Consoles: " << domain.objects.consoles.size() << std::endl;

    return true;
}

static bool viewScene(const Arguments& args) {
    // Determine asset path for shaders
    std::string shaderPath = args.assetPath;
    if (shaderPath.empty()) {
        // Try to find shaders relative to executable
        shaderPath = "shaders/";
    }

    // Initialize window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Scene Tool - Scene Viewer");
    SetTargetFPS(60);

    // Initialize viewer
    SceneViewer viewer;
    if (!sceneViewerInit(&viewer, shaderPath.c_str())) {
        std::cerr << "Failed to initialize scene viewer" << std::endl;
        CloseWindow();
        return false;
    }

    viewer.showPhysics = args.showPhysics;

    // Load reference model if specified (for rendering comparison)
    if (!args.referenceModelPath.empty()) {
        if (!sceneViewerLoadReferenceModel(&viewer, args.referenceModelPath)) {
            std::cerr << "Warning: Failed to load reference model: " << args.referenceModelPath << std::endl;
        }
    }

    // Load scene
    fs::path inputPath(args.inputPath);
    bool loaded = false;

    if (inputPath.filename() == "ship.json" || inputPath.string().find("ship") != std::string::npos) {
        loaded = sceneViewerLoadShip(&viewer, args.inputPath);
    } else {
        loaded = sceneViewerLoadDomain(&viewer, args.inputPath);
    }

    if (!loaded) {
        std::cerr << "Failed to load scene" << std::endl;
        sceneViewerCleanup(&viewer);
        CloseWindow();
        return false;
    }

    // Main loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        sceneViewerUpdate(&viewer, dt);

        BeginDrawing();
        ClearBackground(DARKGRAY);

        sceneViewerRender(&viewer);
        sceneViewerDrawOverlay(&viewer);

        DrawFPS(GetScreenWidth() - 100, 10);

        EndDrawing();
    }

    sceneViewerCleanup(&viewer);
    CloseWindow();

    return true;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Arguments args = parseArguments(argc, argv);

    switch (args.mode) {
        case Arguments::Mode::Help:
            printUsage(argv[0]);
            return 0;

        case Arguments::Mode::ConvertShip:
            if (args.inputPath.empty()) {
                std::cerr << "Error: No input path specified\n";
                printUsage(argv[0]);
                return 1;
            }
            return convertShip(args) ? 0 : 1;

        case Arguments::Mode::ConvertDomain:
            if (args.inputPath.empty()) {
                std::cerr << "Error: No input path specified\n";
                printUsage(argv[0]);
                return 1;
            }
            return convertDomain(args) ? 0 : 1;

        case Arguments::Mode::View:
            if (args.inputPath.empty()) {
                std::cerr << "Error: No input path specified\n";
                printUsage(argv[0]);
                return 1;
            }
            return viewScene(args) ? 0 : 1;
    }

    return 0;
}
