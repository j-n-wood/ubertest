#include "raylib.h"
#include "viewer.h"
#include "scene_convert/ship_parser.h"
#include "args_file.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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
    printf("  --source <path>        Path to source file (xmapfile0.txt)\n");
    printf("  -o, --output <dir>     Output directory (default: ./output)\n");
    printf("  -t, --tiles <path>     Path to tiles.txt (default: %s)\n", DEFAULT_TILES_PATH);
    printf("  -x, --textures <path>  Path to textures.txt (default: %s)\n", DEFAULT_TEXTURES_PATH);
    printf("  --textures-base <dir>  Base directory for texture files\n");
    printf("  --shaders <dir>        Path to shaders directory (default: assets/shaders/)\n");
    printf("  -s, --scale <factor>   Scale factor override (default: 0.0254)\n");
    printf("  --args-file <path>     Read extra args from a file (also @path); '#' comments. Lets a\n");
    printf("                         fixed command drive different runs by editing the file\n");
    printf("  --no-reference         Don't load reference model\n");
    printf("  --no-textures          Don't load textures\n");
    printf("  --export-all <dir>     Headless: load + export every deck to <dir>, then exit\n");
    printf("  --transport <path>     With --export-all: also export the ship transport.txt lift\n");
    printf("                         network to <dir>/transporters.json (render-metric)\n");
    printf("  --export-dir <dir>     Interactive: the 'X' key writes the current deck's bundle here\n");
    printf("                         (e.g. the game's levels3d) instead of <output>/export\n");
    printf("  --export-split <dir>   Headless: split export (one file per shape) every deck\n");
    printf("  --materials <path>     materials.xml for wall profiles (default: <srcDir>/../data)\n");
    printf("  --no-caps              Disable wall end caps\n");
    printf("  --no-miter             Disable wall corner miter joins\n");
    printf("  --save-dir <dir>       Edited-deck JSON output folder (default <output>/edited)\n");
    printf("  --help                 Show this help\n\n");
    printf("Controls:\n");
    printf("  WASD          Move camera\n");
    printf("  Q/E           Move up/down\n");
    printf("  Shift         Move faster\n");
    printf("  Mouse wheel   Zoom\n");
    printf("  R             Reset camera to current preset\n");
    printf("  T             Top-down camera (game mode)\n");
    printf("  I             Isometric camera (45 degree)\n");
    printf("  P             Perspective camera (free view)\n");
    printf("  0-5           Debug visualization modes\n");
    printf("  F1            Toggle grid\n");
    printf("  F2            Toggle reference model\n");
    printf("  F3            Toggle tiles\n");
    printf("  F4            Toggle geometry\n");
    printf("  F5            Toggle wireframe\n");
    printf("  [ / ]         Previous / next deck (level)\n");
    printf("  U             Toggle class-14 reference unit (size reference)\n");
    printf("  K / M         Toggle wall caps / miter joins (rebuilds the deck)\n");
    printf("  N             Toggle path-node markers + id labels (diagnosis)\n");
    printf("  (N on)        Left-click a node to select it; arrows move it in the floor plane,\n");
    printf("                PageUp/Dn = height, Shift = x16 step (game units); F10 saves\n");
    printf("  G             Toggle wall-link overlay (id/direction/bezier)\n");
    printf("  C             Toggle collision footprint wireframe\n");
    printf("  F9            Reload the current deck from source XML (after editing it)\n");
    printf("  J             Dump per-link profile assignment + trim side to console\n");
    printf("  L             Toggle link inspector: edit/add/remove/reverse links; Save / Save All / Revert\n");
    printf("  F10           Save edited deck to JSON (--save-dir, default <output>/edited)\n");
    printf("  V             Toggle validity report panel\n");
    printf("  X             Export current deck (GLTF + manifest + collision) to <output>/export/\n");
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
    // Expand any --args-file/@file tokens so a fixed command can drive different runs (see args_file.h).
    std::vector<std::string> argStore = expandArgsFiles(argc, argv);
    std::vector<char*> argPtrs;
    for (auto& s : argStore) argPtrs.push_back(const_cast<char*>(s.c_str()));
    argc = (int)argPtrs.size();
    argv = argPtrs.data();

    // Parse command line arguments
    const char* sourcePath = nullptr;
    const char* outputDir = "output";
    const char* tilesPath = DEFAULT_TILES_PATH;
    const char* texturesPath = DEFAULT_TEXTURES_PATH;
    const char* texturesBasePath = DEFAULT_TEXTURES_BASE;
    const char* shadersPath = "assets/shaders/";
    float scale = 0.0254f;  // inches to meters
    bool loadReference = true;
    bool loadTextures = true;
    const char* exportAllDir = nullptr;    // --export-all <dir>: headless export every deck, then exit
    const char* exportDir = nullptr;       // --export-dir <dir>: interactive 'X' key bundle target
    const char* transportPath = nullptr;   // --transport <path>: ship transport.txt (lift network)
    const char* exportSplitDir = nullptr;  // --export-split <dir>: headless split export every deck
    const char* materialsPath = nullptr; // --materials <path>: materials.xml (for wall profiles)
    bool noCaps = false;                 // --no-caps: disable wall end caps
    bool noMiter = false;                // --no-miter: disable wall corner miter joins
    const char* saveDir = nullptr;       // --save-dir <dir>: edited-deck JSON output (default <output>/edited)

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
        } else if (strcmp(argv[i], "--textures-base") == 0 && i + 1 < argc) {
            texturesBasePath = argv[++i];
        } else if (strcmp(argv[i], "--shaders") == 0 && i + 1 < argc) {
            shadersPath = argv[++i];
        } else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            sourcePath = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) && i + 1 < argc) {
            scale = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-reference") == 0) {
            loadReference = false;
        } else if (strcmp(argv[i], "--no-textures") == 0) {
            loadTextures = false;
        } else if (strcmp(argv[i], "--export-all") == 0 && i + 1 < argc) {
            exportAllDir = argv[++i];
        } else if (strcmp(argv[i], "--export-dir") == 0 && i + 1 < argc) {
            exportDir = argv[++i];
        } else if (strcmp(argv[i], "--transport") == 0 && i + 1 < argc) {
            transportPath = argv[++i];
        } else if (strcmp(argv[i], "--export-split") == 0 && i + 1 < argc) {
            exportSplitDir = argv[++i];
        } else if (strcmp(argv[i], "--materials") == 0 && i + 1 < argc) {
            materialsPath = argv[++i];
        } else if (strcmp(argv[i], "--no-caps") == 0) {
            noCaps = true;
        } else if (strcmp(argv[i], "--no-miter") == 0) {
            noMiter = true;
        } else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) {
            saveDir = argv[++i];
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
    if (!viewerInit(&viewer, shadersPath)) {
        fprintf(stderr, "Error: Failed to initialize viewer\n");
        CloseWindow();
        return 1;
    }

    // Reference model: an instance of unit type 14 (class-14 droid), assembled by the real
    // UnitManager (multi-section body + env/lighting shader) — a true in-game scale reference.
    // Shown by default; toggle with F2 or U. (Same path as viewerToggleUnitRef's lazy build.)
    if (loadReference) {
        viewerToggleUnitRef(&viewer);
    }

    // Load texture lookup
    if (loadTextures) {
        if (!viewerLoadTextures(&viewer, texturesPath, texturesBasePath)) {
            fprintf(stderr, "Warning: Failed to load texture lookup\n");
        }
    }

    // Wall build options (default on; CLI can disable for A/B comparison).
    viewer.toggles.enableCaps = !noCaps;
    viewer.toggles.enableMiter = !noMiter;

    // Set up in-app deck cycling: scan the directory that holds xmapfile{N}.txt.
    viewer.outputDir = outputDir;
    viewer.exportDir = exportDir ? exportDir : "";   // 'X' key bundle target (empty => <output>/export)
    viewer.scale = scale;
    // Edited-deck JSON output folder (originals untouched). Default: <output>/edited.
    viewer.saveDir = saveDir ? saveDir : (fs::path(outputDir) / "edited").string();
    {
        fs::path src(sourcePath ? sourcePath : DEFAULT_SOURCE_PATH);
        std::string srcDir = src.parent_path().string();
        viewerScanLevels(&viewer, srcDir.c_str(), tilesPath);

        // Wall profiles from materials.xml (data/ is a sibling of ship1/).
        std::string mpath = materialsPath
            ? materialsPath
            : (fs::path(srcDir).parent_path() / "data" / "materials.xml").string();
        viewer.materialsPath = mpath;
        loadWallProfiles(mpath.c_str(), viewer.wallProfiles);
    }

    // Headless batch export: load + export every deck, then exit.
    if (exportAllDir || exportSplitDir) {
        std::vector<DeckSpawnInfo> spawnDecks;   // xmapfile PROFILE/PLACEDROID per deck -> spawns.json
        for (int level : viewer.levelNumbers) {
            if (!viewerLoadLevel(&viewer, level)) continue;
            if (exportAllDir) viewerExportLevel(&viewer, exportAllDir);
            if (exportSplitDir) viewerExportLevelSplit(&viewer, exportSplitDir);
            DeckSpawnInfo si;
            si.level = level;
            si.profile = viewer.loadedDomain.profile;
            si.placed = viewer.loadedDomain.spawns;
            spawnDecks.push_back(std::move(si));
        }
        // Regenerate the ship's spawns.json from the accumulated domain profiles (the xmapfile
        // PROFILE marker is now the authoritative spawn control).
        if (exportAllDir) viewerExportSpawns(spawnDecks, exportAllDir);
        // Ship-wide lift network (one file for all decks), if a transport.txt was supplied.
        if (exportAllDir && transportPath) {
            std::vector<Transporter> transporters;
            if (parseTransportFile(transportPath, transporters)) {
                if (viewerExportTransporters(transporters, viewer.scale, exportAllDir))
                    printf("Exported %zu transporters to %s/transporters.json\n",
                           transporters.size(), exportAllDir);
            } else {
                fprintf(stderr, "Warning: failed to parse transport file %s\n", transportPath);
            }
        }
        viewerCleanup(&viewer);
        CloseWindow();
        return 0;
    }

    // Load the requested deck (or the first available) via viewerLoadLevel so it sets currentLevelIdx
    // and prefers an edited JSON copy if one exists. Derive the deck number from xmapfile{N}.txt.
    int startLevel = -1;
    if (sourcePath) {
        std::string stem = fs::path(sourcePath).stem().string();  // e.g. "xmapfile7"
        const std::string pre = "xmapfile";
        if (stem.compare(0, pre.size(), pre) == 0) {
            try { startLevel = std::stoi(stem.substr(pre.size())); } catch (...) { startLevel = -1; }
        }
    }
    if (startLevel < 0 && !viewer.levelNumbers.empty()) startLevel = viewer.levelNumbers.front();
    if (startLevel >= 0) {
        viewerLoadLevel(&viewer, startLevel);
    } else if (sourcePath) {
        // Non-standard source name: fall back to a direct convert.
        if (viewerConvertAndLoad(&viewer, sourcePath, tilesPath, outputDir, scale)) viewerValidate(&viewer);
    }

    // Main loop
    while (!WindowShouldClose()) {
        viewerUpdate(&viewer, GetFrameTime());

        BeginDrawing();
        ClearBackground(DARKGRAY);

        viewerRender(&viewer);
        viewerDrawOverlay(&viewer);
        viewerDrawInspector(&viewer);

        EndDrawing();
    }

    // Cleanup
    viewerCleanup(&viewer);
    CloseWindow();

    return 0;
}
