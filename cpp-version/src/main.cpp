#include "raylib.h"
#include "game.h"
#include "args_file.h"
#include "rendering/texture_manager.h"
#include "pages/page_manager.h"
#include "pages/game_page.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
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

// Trace-log callback that keeps the log readable *as a file* (so a run's output can be captured to
// e.g. build/shot.log and inspected with a text reader instead of scrolling the terminal). raylib
// emits hundreds of per-mesh/per-texture INFO lines at startup; those are dropped here, while the
// game's own diagnostics and every WARNING/ERROR/FATAL are kept. Wired via SetTraceLogCallback
// before InitWindow.
static void conciseTraceLog(int level, const char* text, va_list args) {
    if (level == LOG_INFO) {
        // raylib boilerplate is prefixed with an uppercase "TAG:"; the game's messages are not.
        static const char* kNoisy[] = {
            "VAO:", "VBO:", "TEXTURE:", "SHADER:", "FILEIO:", "RLGL:", "GL:", "GLAD:", "IMAGE:",
            "MODEL:", "MATERIAL:", "MESH:", "DISPLAY:", "PLATFORM:", "AUDIO:", "VR:", "FBO:",
        };
        for (const char* p : kNoisy)
            if (strncmp(text, p, strlen(p)) == 0) return;
    }
    const char* tag = "INFO";
    switch (level) {
        case LOG_TRACE:   tag = "TRACE"; break;
        case LOG_DEBUG:   tag = "DEBUG"; break;
        case LOG_WARNING: tag = "WARN";  break;
        case LOG_ERROR:   tag = "ERROR"; break;
        case LOG_FATAL:   tag = "FATAL"; break;
        default:          tag = "INFO";  break;
    }
    FILE* out = (level >= LOG_WARNING) ? stderr : stdout;
    fprintf(out, "%s: ", tag);
    vfprintf(out, text, args);
    fputc('\n', out);
}

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
    printf("  --screenshot <path> Capture a PNG after --shot-frame frames, then exit\n");
    printf("  --shot-frame <n>    Frame to capture on (default: 30)\n");
    printf("  --shot-eye x,y,z    Screenshot camera position (world coords)\n");
    printf("  --shot-target x,y,z Screenshot camera look-at target\n");
    printf("  --shot-overview     Screenshot camera frames the whole deck\n");
    printf("  --shot-debug        Enable V-mode debug overlays (collision + entity markers) in the shot\n");
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

// Parse "x,y,z" into a Vector3 (for the --shot-eye / --shot-target screenshot camera args).
static bool parseVec3Arg(const char* s, Vector3& out) {
    return sscanf(s, "%f,%f,%f", &out.x, &out.y, &out.z) == 3;
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
    // Fallback start deck, used ONLY if the ship has no transmat (player-start) pads. The normal
    // start is game_start_at_transmat (see below); randomisation of the start will live there (over
    // the transmat pads), not here. Reached via the normal world-switch (player migrated + placed).
    constexpr int GAME_START_DECK = 7;
    int startDeck = -1;  // --deck N override (debug); -1 = transmat pad, else GAME_START_DECK fallback

    // Screenshot capture (dev/QA): after --shot-frame frames, save the framebuffer to a PNG and exit.
    // Combine with --args-file to keep the command line stable. Optional camera override via
    // --shot-eye/--shot-target (world coords "x,y,z") or --shot-overview (frame the whole deck).
    const char* screenshotPath = nullptr;
    int shotFrame = 30;
    Vector3 shotEye{}, shotTarget{};
    bool hasShotEye = false, hasShotTarget = false, shotOverview = false, shotDebug = false;

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
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--shot-frame") == 0 && i + 1 < argc) {
            shotFrame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--shot-eye") == 0 && i + 1 < argc) {
            hasShotEye = parseVec3Arg(argv[++i], shotEye);
        } else if (strcmp(argv[i], "--shot-target") == 0 && i + 1 < argc) {
            hasShotTarget = parseVec3Arg(argv[++i], shotTarget);
        } else if (strcmp(argv[i], "--shot-overview") == 0) {
            shotOverview = true;
        } else if (strcmp(argv[i], "--shot-debug") == 0) {
            shotDebug = true;   // enable the V-mode overlays (collision wireframe, object/entity markers)
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

    SetTraceLogCallback(conciseTraceLog);  // trim raylib boot spam so the log reads cleanly as a file
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
    // Start position: a --deck override wins (debug); otherwise place the player at the ship's
    // transmat pad (uber's player-start); if the ship has no transmat pads, fall back to
    // GAME_START_DECK's lift stop. All paths migrate the player via the normal world-switch.
    if (startDeck >= 0) {
        game_debug_goto_deck(&game, startDeck);
    } else if (!game_start_at_transmat(&game)) {
        game_debug_goto_deck(&game, GAME_START_DECK);
    }

    // View-states are pages on a stack; gameplay is the base GamePage. Other pages
    // (console, future title) are pushed on top and drive update/render while active.
    PageManager pages;
    pages.push(std::make_unique<GamePage>(&game, &pages));

    int frameNo = 0;
    while (!WindowShouldClose() && game.running) {
        float dt = GetFrameTime();
        pages.update(dt);
        // Screenshot camera override (applied after the game's per-frame camera update, before draw).
        if (screenshotPath) {
            if (shotDebug) game.showAIDebug = true;   // V-mode overlays (collision + entity markers)
            if (shotOverview && game.currentLevel >= 0 &&
                game.currentLevel < (int)game.levelRenderData.size()) {
                const LevelRenderData& d = game.levelRenderData[game.currentLevel];
                Vector3 lo = d.boundsMin, hi = d.boundsMax;
                Vector3 c = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
                float sx = hi.x - lo.x, sz = hi.z - lo.z;
                game.camera.position = (sx >= sz) ? (Vector3){lo.x - sx * 0.12f, c.y + sz * 0.5f, c.z}
                                                  : (Vector3){c.x, c.y + sx * 0.5f, lo.z - sz * 0.12f};
                game.camera.target = c;
                game.camera.up = {0, 1, 0};
            } else if (hasShotEye || hasShotTarget) {
                if (hasShotEye) game.camera.position = shotEye;
                if (hasShotTarget) game.camera.target = shotTarget;
                game.camera.up = {0, 1, 0};
            }
        }
        pages.render();
        if (screenshotPath && ++frameNo >= shotFrame) {
            TakeScreenshot(screenshotPath);
            TraceLog(LOG_INFO, "Saved screenshot: %s (frame %d)", screenshotPath, shotFrame);
            break;
        }
    }

    game_destroy(&game);
    textures.reset();   // unloadAll() while the GL context is still alive
    CloseWindow();

    return 0;
}
