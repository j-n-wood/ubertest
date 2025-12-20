#include "raylib.h"
#include "rlgl.h"
#include "asc_loader.h"
#include "gltf_export.h"
#include "lighting/light.h"
#include "utils/string_utils.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960

//------------------------------------------------------------------------------
// Viewport structure for four-panel display
//------------------------------------------------------------------------------
struct Viewport {
    Rectangle bounds;
    std::string label;
    Model model;
    bool valid;
    bool load_attempted;  // true if we tried to load a file (vs procedural)
    // Bounds info for this viewport's model
    Vector3 model_bounds_min;
    Vector3 model_bounds_max;
    bool has_bounds;
};

//------------------------------------------------------------------------------
// Application state
//------------------------------------------------------------------------------
struct AppState {
    Shader lighting_shader;
    Light lights[MAX_LIGHTS];
    int light_count;
    int ambient_loc;
    int debug_mode_loc;
    int debug_mode;

    Camera3D camera;
    float rotation_angle;
    bool auto_rotate;

    Viewport viewports[4];

    std::string model_a_path;
    std::string model_b_path;

    // ASC loader options
    ASCLoadOptions asc_options;

    // Conversion mode
    bool convert_mode;
    std::string convert_input;
    std::string convert_output;
    std::string texture_source_path;  // Override path for texture source files
};

//------------------------------------------------------------------------------
// Initialize renderer (shader, lights)
//------------------------------------------------------------------------------
static void init_renderer(AppState* app) {
    // Load lighting shader
    app->lighting_shader = LoadShader("shaders/lighting.vs", "shaders/lighting.fs");

    if (!IsShaderValid(app->lighting_shader)) {
        TraceLog(LOG_ERROR, "Failed to load shaders from shaders/ directory");
        return;
    }

    // Get shader locations
    app->lighting_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(app->lighting_shader, "viewPos");
    app->lighting_shader.locs[SHADER_LOC_COLOR_SPECULAR] = GetShaderLocation(app->lighting_shader, "colSpecular");
    app->ambient_loc = GetShaderLocation(app->lighting_shader, "ambient");
    app->debug_mode_loc = GetShaderLocation(app->lighting_shader, "debugMode");
    app->debug_mode = 0;

    // Set ambient light
    float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(app->lighting_shader, app->ambient_loc, ambient, SHADER_UNIFORM_VEC4);

    // Set default specular values
    int specPowerLoc = GetShaderLocation(app->lighting_shader, "specularPower");
    int specIntensityLoc = GetShaderLocation(app->lighting_shader, "specularIntensity");
    float defaultSpecPower = 32.0f;
    float defaultSpecIntensity = 0.5f;
    SetShaderValue(app->lighting_shader, specPowerLoc, &defaultSpecPower, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->lighting_shader, specIntensityLoc, &defaultSpecIntensity, SHADER_UNIFORM_FLOAT);

    // Initialize debug mode to 0 (normal rendering)
    SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);

    // Create directional light from above (same as main project)
    app->light_count = 0;
    app->lights[app->light_count++] = create_light(
        LIGHT_DIRECTIONAL,
        (Vector3){0, 0, 0},    // Light shines toward origin
        (Vector3){0, 50, 0},   // Light comes from above -> lightDir = (0, 1, 0)
        WHITE,
        app->lighting_shader,
        0
    );
}

//------------------------------------------------------------------------------
// Create procedural box model with explicit normals
//------------------------------------------------------------------------------
static Model create_procedural_box(Shader shader) {
    // Generate cube mesh - Raylib's GenMeshCube has correct normals
    Mesh mesh = GenMeshCube(2.0f, 2.0f, 2.0f);
    Model model = LoadModelFromMesh(mesh);

    // Apply lighting shader
    model.materials[0].shader = shader;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){100, 100, 100, 255};

    return model;
}

//------------------------------------------------------------------------------
// Create procedural sphere model
//------------------------------------------------------------------------------
static Model create_procedural_sphere(Shader shader) {
    // Generate sphere mesh
    Mesh mesh = GenMeshSphere(1.5f, 32, 32);
    Model model = LoadModelFromMesh(mesh);

    // Apply lighting shader
    model.materials[0].shader = shader;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){100, 100, 100, 255};

    return model;
}

//------------------------------------------------------------------------------
// Check if input path indicates batch mode (directory or wildcard pattern)
//------------------------------------------------------------------------------
static bool is_batch_input(const std::string& path) {
    // Check for wildcard
    if (path.find('*') != std::string::npos) {
        return true;
    }
    // Check if it's a directory
    std::error_code ec;
    return fs::is_directory(path, ec);
}

//------------------------------------------------------------------------------
// Collect .asc files from directory or wildcard pattern
// Returns vector of paths to input files
//------------------------------------------------------------------------------
static std::vector<fs::path> collect_asc_files(const std::string& input_path) {
    std::vector<fs::path> files;

    size_t wildcard_pos = input_path.find('*');

    if (wildcard_pos != std::string::npos) {
        // Wildcard mode: extract directory and pattern
        fs::path full_path(input_path);
        fs::path dir = full_path.parent_path();
        std::string pattern = full_path.filename().string();

        // Convert glob pattern to simple prefix/suffix matching
        // e.g., "*.asc" -> prefix="", suffix=".asc"
        // e.g., "model*.asc" -> prefix="model", suffix=".asc"
        size_t star_in_pattern = pattern.find('*');
        std::string prefix = pattern.substr(0, star_in_pattern);
        std::string suffix = pattern.substr(star_in_pattern + 1);

        if (dir.empty()) dir = ".";

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();

            // Check prefix and suffix match
            bool matches = true;
            if (!prefix.empty() && filename.find(prefix) != 0) {
                matches = false;
            }
            if (!suffix.empty() &&
                (filename.length() < suffix.length() ||
                 filename.compare(filename.length() - suffix.length(),
                                  suffix.length(), suffix) != 0)) {
                matches = false;
            }

            if (matches) {
                files.push_back(entry.path());
            }
        }
    } else {
        // Directory mode: find all .asc files
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(input_path, ec)) {
            if (!entry.is_regular_file()) continue;
            if (has_extension(entry.path(), ".asc")) {
                files.push_back(entry.path());
            }
        }
    }

    // Sort for consistent ordering
    std::sort(files.begin(), files.end());
    return files;
}

//------------------------------------------------------------------------------
// Compute bounds for a Raylib model
//------------------------------------------------------------------------------
static void compute_model_bounds(Model model, Vector3* out_min, Vector3* out_max) {
    bool first = true;
    Vector3 bmin = {0, 0, 0};
    Vector3 bmax = {0, 0, 0};

    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];
        if (!mesh->vertices) continue;

        for (int v = 0; v < mesh->vertexCount; v++) {
            float x = mesh->vertices[v * 3 + 0];
            float y = mesh->vertices[v * 3 + 1];
            float z = mesh->vertices[v * 3 + 2];

            if (first) {
                bmin = (Vector3){x, y, z};
                bmax = (Vector3){x, y, z};
                first = false;
            } else {
                if (x < bmin.x) bmin.x = x;
                if (y < bmin.y) bmin.y = y;
                if (z < bmin.z) bmin.z = z;
                if (x > bmax.x) bmax.x = x;
                if (y > bmax.y) bmax.y = y;
                if (z > bmax.z) bmax.z = z;
            }
        }
    }

    *out_min = bmin;
    *out_max = bmax;
}

//------------------------------------------------------------------------------
// Load a model (GLTF/GLB/ASC) and apply shader
// Returns bounds via out_min/out_max if not NULL
//------------------------------------------------------------------------------
static Model load_model_with_shader(const std::string& path, Shader shader, ASCLoadOptions asc_opts,
                                    Vector3* out_min, Vector3* out_max, bool* out_has_bounds) {
    Model model = {0};
    bool has_bounds = false;
    Vector3 bounds_min = {0, 0, 0};
    Vector3 bounds_max = {0, 0, 0};

    // Check for ASC file
    if (has_extension(path, ".asc")) {
        TraceLog(LOG_INFO, "Loading ASC file: %s", path.c_str());

        ASCLoadResult result = {0};
        result = LoadASCEx(path.c_str(), &model, asc_opts);

        if (result.success) {
            bounds_min = result.bounds_min;
            bounds_max = result.bounds_max;
            has_bounds = true;
            TraceLog(LOG_INFO, "ASC bounds: min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)",
                     bounds_min.x, bounds_min.y, bounds_min.z,
                     bounds_max.x, bounds_max.y, bounds_max.z);
        } else {
            TraceLog(LOG_ERROR, "ASC load failed: %s (line %d)", result.error_msg, result.error_line);
        }
    } else {
        // Use Raylib's native loader for GLTF/GLB/OBJ etc
        model = LoadModel(path.c_str());

        if (model.meshCount > 0) {
            compute_model_bounds(model, &bounds_min, &bounds_max);
            has_bounds = true;
            TraceLog(LOG_INFO, "Model bounds: min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)",
                     bounds_min.x, bounds_min.y, bounds_min.z,
                     bounds_max.x, bounds_max.y, bounds_max.z);
        }
    }

    if (model.meshCount > 0) {
        // Apply lighting shader to all materials
        for (int i = 0; i < model.materialCount; i++) {
            model.materials[i].shader = shader;
        }
    }

    // Return bounds info
    if (out_min) *out_min = bounds_min;
    if (out_max) *out_max = bounds_max;
    if (out_has_bounds) *out_has_bounds = has_bounds;

    return model;
}

//------------------------------------------------------------------------------
// Initialize viewports
//------------------------------------------------------------------------------
static void init_viewports(AppState* app) {
    float half_w = WINDOW_WIDTH / 2.0f;
    float half_h = WINDOW_HEIGHT / 2.0f;

    // Top-left: Procedural Box (2x2x2)
    app->viewports[0].bounds = (Rectangle){0, 0, half_w, half_h};
    app->viewports[0].label = "Procedural Box";
    app->viewports[0].model = create_procedural_box(app->lighting_shader);
    app->viewports[0].valid = true;
    app->viewports[0].load_attempted = false;
    app->viewports[0].model_bounds_min = (Vector3){-1.0f, -1.0f, -1.0f};
    app->viewports[0].model_bounds_max = (Vector3){1.0f, 1.0f, 1.0f};
    app->viewports[0].has_bounds = true;

    // Top-right: Procedural Sphere (radius 1.5)
    app->viewports[1].bounds = (Rectangle){half_w, 0, half_w, half_h};
    app->viewports[1].label = "Procedural Sphere";
    app->viewports[1].model = create_procedural_sphere(app->lighting_shader);
    app->viewports[1].valid = true;
    app->viewports[1].load_attempted = false;
    app->viewports[1].model_bounds_min = (Vector3){-1.5f, -1.5f, -1.5f};
    app->viewports[1].model_bounds_max = (Vector3){1.5f, 1.5f, 1.5f};
    app->viewports[1].has_bounds = true;

    // Bottom-left: Model A (configurable)
    app->viewports[2].bounds = (Rectangle){0, half_h, half_w, half_h};
    app->viewports[2].label = "Model A (none)";
    app->viewports[2].valid = false;
    app->viewports[2].load_attempted = false;
    app->viewports[2].has_bounds = false;

    // Bottom-right: Model B (configurable)
    app->viewports[3].bounds = (Rectangle){half_w, half_h, half_w, half_h};
    app->viewports[3].label = "Model B (none)";
    app->viewports[3].valid = false;
    app->viewports[3].load_attempted = false;
    app->viewports[3].has_bounds = false;

    // Load Model A if path provided
    if (!app->model_a_path.empty()) {
        app->viewports[2].load_attempted = true;
        app->viewports[2].model = load_model_with_shader(
            app->model_a_path, app->lighting_shader, app->asc_options,
            &app->viewports[2].model_bounds_min, &app->viewports[2].model_bounds_max,
            &app->viewports[2].has_bounds);
        app->viewports[2].valid = (app->viewports[2].model.meshCount > 0);

        std::string filename = fs::path(app->model_a_path).filename().string();
        if (app->viewports[2].valid) {
            app->viewports[2].label = "A: " + filename;
            TraceLog(LOG_INFO, "Loaded Model A: %s", app->model_a_path.c_str());
        } else {
            app->viewports[2].label = "A: " + filename + " [FAILED]";
            TraceLog(LOG_WARNING, "Failed to load Model A: %s", app->model_a_path.c_str());
        }
    }

    // Load Model B if path provided
    if (!app->model_b_path.empty()) {
        app->viewports[3].load_attempted = true;
        app->viewports[3].model = load_model_with_shader(
            app->model_b_path, app->lighting_shader, app->asc_options,
            &app->viewports[3].model_bounds_min, &app->viewports[3].model_bounds_max,
            &app->viewports[3].has_bounds);
        app->viewports[3].valid = (app->viewports[3].model.meshCount > 0);

        std::string filename = fs::path(app->model_b_path).filename().string();
        if (app->viewports[3].valid) {
            app->viewports[3].label = "B: " + filename;
            TraceLog(LOG_INFO, "Loaded Model B: %s", app->model_b_path.c_str());
        } else {
            app->viewports[3].label = "B: " + filename + " [FAILED]";
            TraceLog(LOG_WARNING, "Failed to load Model B: %s", app->model_b_path.c_str());
        }
    }
}

//------------------------------------------------------------------------------
// Initialize camera (overhead, looking down)
//------------------------------------------------------------------------------
static void init_camera(AppState* app) {
    app->camera.position = (Vector3){0, 15, 0};    // Above, looking down
    app->camera.target = (Vector3){0, 0, 0};       // Looking at origin
    app->camera.up = (Vector3){0, 0, -1};          // -Z is "up" on screen
    app->camera.fovy = 45.0f;
    app->camera.projection = CAMERA_PERSPECTIVE;

    app->rotation_angle = 0.0f;
    app->auto_rotate = true;
}

//------------------------------------------------------------------------------
// Draw a single viewport
//------------------------------------------------------------------------------
static void draw_viewport(AppState* app, Viewport* vp) {
    // Set scissor/viewport for this panel
    BeginScissorMode((int)vp->bounds.x, (int)vp->bounds.y,
                     (int)vp->bounds.width, (int)vp->bounds.height);

    // Clear this viewport area
    DrawRectangleRec(vp->bounds, (Color){30, 30, 35, 255});

    // Update camera position for specular calculations
    float cameraPos[3] = {app->camera.position.x, app->camera.position.y, app->camera.position.z};
    SetShaderValue(app->lighting_shader, app->lighting_shader.locs[SHADER_LOC_VECTOR_VIEW],
                   cameraPos, SHADER_UNIFORM_VEC3);

    // Set up viewport for 3D rendering
    rlViewport((int)vp->bounds.x, (int)(WINDOW_HEIGHT - vp->bounds.y - vp->bounds.height),
               (int)vp->bounds.width, (int)vp->bounds.height);

    BeginMode3D(app->camera);

    if (vp->valid) {
        // Draw model with rotation
        Vector3 axis = {0, 1, 0};
        DrawModelEx(vp->model, (Vector3){0, 0, 0}, axis, app->rotation_angle,
                    (Vector3){1, 1, 1}, WHITE);
    } else {
        // Draw placeholder grid
        DrawGrid(10, 1.0f);
    }

    EndMode3D();

    // Reset viewport to full window for 2D overlay
    rlViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // Draw label with color based on load status
    Color label_color = WHITE;
    if (vp->load_attempted && !vp->valid) {
        label_color = RED;
    }
    DrawText(vp->label.c_str(), (int)vp->bounds.x + 10, (int)vp->bounds.y + 10, 20, label_color);

    // Draw size info below the label
    if (vp->has_bounds) {
        Vector3 size = {
            vp->model_bounds_max.x - vp->model_bounds_min.x,
            vp->model_bounds_max.y - vp->model_bounds_min.y,
            vp->model_bounds_max.z - vp->model_bounds_min.z
        };
        DrawText(TextFormat("Size: %.2f x %.2f x %.2f", size.x, size.y, size.z),
                 (int)vp->bounds.x + 10, (int)vp->bounds.y + 32, 14, LIME);
    }

    // Draw border
    DrawRectangleLinesEx(vp->bounds, 2, (Color){80, 80, 80, 255});

    EndScissorMode();
}

//------------------------------------------------------------------------------
// Handle input
//------------------------------------------------------------------------------
static void handle_input(AppState* app) {
    // Debug modes 0-5
    if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) {
        app->debug_mode = 0;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
        app->debug_mode = 1;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }
    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) {
        app->debug_mode = 2;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }
    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) {
        app->debug_mode = 3;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }
    if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) {
        app->debug_mode = 4;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }
    if (IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5)) {
        app->debug_mode = 5;
        SetShaderValue(app->lighting_shader, app->debug_mode_loc, &app->debug_mode, SHADER_UNIFORM_INT);
    }

    // Toggle auto-rotate
    if (IsKeyPressed(KEY_SPACE)) {
        app->auto_rotate = !app->auto_rotate;
    }

    // Manual rotation
    if (IsKeyDown(KEY_LEFT)) {
        app->rotation_angle -= 60.0f * GetFrameTime();
    }
    if (IsKeyDown(KEY_RIGHT)) {
        app->rotation_angle += 60.0f * GetFrameTime();
    }

    // Reset
    if (IsKeyPressed(KEY_R)) {
        app->rotation_angle = 0.0f;
    }

    // Zoom
    if (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD)) {
        app->camera.position.y -= 10.0f * GetFrameTime();
        if (app->camera.position.y < 3.0f) app->camera.position.y = 3.0f;
    }
    if (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT)) {
        app->camera.position.y += 10.0f * GetFrameTime();
        if (app->camera.position.y > 50.0f) app->camera.position.y = 50.0f;
    }
}

//------------------------------------------------------------------------------
// Draw HUD overlay
//------------------------------------------------------------------------------
static void draw_hud(AppState* app) {
    const char* debug_labels[] = {
        "0: Normal",
        "1: Normals RGB",
        "2: Light Dir",
        "3: Specular",
        "4: View Dir",
        "5: Half Angle"
    };

    int y = WINDOW_HEIGHT - 100;

    DrawText(TextFormat("Debug Mode: %s", debug_labels[app->debug_mode]), 10, y, 16, YELLOW);
    y += 20;
    DrawText(TextFormat("Rotation: %.1f deg  [Space: %s]", app->rotation_angle,
                        app->auto_rotate ? "Auto" : "Manual"), 10, y, 16, LIGHTGRAY);
    y += 20;
    DrawText("Keys: 0-5 debug | Arrows rotate | +/- zoom | R reset", 10, y, 16, GRAY);
    y += 20;
    DrawText(TextFormat("Camera Y: %.1f", app->camera.position.y), 10, y, 16, GRAY);
}

//------------------------------------------------------------------------------
// Parse command line arguments
//------------------------------------------------------------------------------
static void parse_args(int argc, char** argv, AppState* app) {
    app->model_a_path.clear();
    app->model_b_path.clear();
    app->convert_mode = false;
    app->convert_input.clear();
    app->convert_output.clear();
    app->texture_source_path.clear();

    // Initialize ASC options to defaults
    app->asc_options = ASCDefaultOptions();

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--model-a" && i + 1 < argc) {
            app->model_a_path = argv[i + 1];
            i++;
        } else if (arg == "--model-b" && i + 1 < argc) {
            app->model_b_path = argv[i + 1];
            i++;
        } else if (arg == "--convert" && i + 1 < argc) {
            app->convert_mode = true;
            app->convert_input = argv[i + 1];
            i++;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            app->convert_output = argv[i + 1];
            i++;
        } else if (arg == "--texture-path" && i + 1 < argc) {
            app->texture_source_path = argv[i + 1];
            i++;
        } else if (arg == "--scale" && i + 1 < argc) {
            app->asc_options.scale = std::stof(argv[i + 1]);
            i++;
        } else if (arg == "--swap-yz") {
            app->asc_options.swap_yz = true;
        } else if (arg == "--flip-winding") {
            app->asc_options.flip_winding = true;
        } else if (arg == "--help" || arg == "-h") {
            printf("Model Tool - 3D Model Viewer and Converter\n\n");
            printf("Usage: model_tool [options]\n\n");
            printf("Viewer Mode (default):\n");
            printf("  --model-a <path>   Load model into slot A (bottom-left)\n");
            printf("  --model-b <path>   Load model into slot B (bottom-right)\n\n");
            printf("Conversion Mode (single file):\n");
            printf("  --convert <file>     Convert input ASC file to GLTF\n");
            printf("  -o, --output <path>  Output GLTF file path\n\n");
            printf("Batch Conversion Mode:\n");
            printf("  --convert <dir>      Convert all .asc files in directory\n");
            printf("  --convert \"<pattern>\" Convert files matching pattern (quote to prevent\n");
            printf("                       shell expansion, e.g., \"*.asc\")\n");
            printf("  -o, --output <dir>   Output directory (created if needed)\n\n");
            printf("Conversion Options:\n");
            printf("  --texture-path <dir> Override source path for texture files\n");
            printf("                       (used if textures not found relative to input)\n\n");
            printf("ASC Loader Options:\n");
            printf("  --scale <factor>   Scale factor (default: 0.0254 for inches to meters)\n");
            printf("  --swap-yz          Enable Y/Z axis swap (Z-up to Y-up conversion)\n");
            printf("  --flip-winding     Enable triangle winding flip\n\n");
            printf("Other Options:\n");
            printf("  --help, -h         Show this help\n\n");
            printf("Controls (Viewer Mode):\n");
            printf("  0-5       Debug visualization modes\n");
            printf("  Space     Toggle auto-rotate\n");
            printf("  Arrows    Manual rotation\n");
            printf("  +/-       Zoom in/out\n");
            printf("  R         Reset rotation\n\n");
            printf("Examples:\n");
            printf("  # Single file conversion\n");
            printf("  model_tool --convert model.asc -o out/model.gltf --swap-yz\n\n");
            printf("  # Batch conversion (all .asc files in directory)\n");
            printf("  model_tool --convert ./models/ -o ./output/ --swap-yz\n\n");
            printf("  # Batch conversion (wildcard pattern)\n");
            printf("  model_tool --convert \"../source/*.asc\" -o ./converted/\n\n");
            printf("  # Viewer mode\n");
            printf("  model_tool --model-a model.asc --swap-yz\n");
            exit(0);
        }
    }

    // Log ASC options if any model is being loaded
    if (!app->model_a_path.empty() || !app->model_b_path.empty()) {
        printf("ASC Loader Options:\n");
        printf("  Scale: %.6f %s\n", app->asc_options.scale,
               app->asc_options.scale == 0.0254f ? "(inches to meters)" : "");
        printf("  Swap Y/Z: %s\n", app->asc_options.swap_yz ? "yes (Z-up to Y-up)" : "no");
        printf("  Flip winding: %s\n", app->asc_options.flip_winding ? "yes" : "no");
    }
}

//------------------------------------------------------------------------------
// Convert a single ASC file to GLTF
// Returns 0 on success, 1 on failure
//------------------------------------------------------------------------------
static int convert_single_file(const std::string& input_path, const std::string& output_path,
                               AppState* app) {
    printf("Converting: %s -> %s\n", input_path.c_str(), output_path.c_str());

    // Load the model (skip GPU upload for headless conversion)
    ASCLoadOptions opts = app->asc_options;
    opts.skip_gpu_upload = true;  // No OpenGL context available
    Model model = {0};
    ASCLoadResult load_result = LoadASCEx(input_path.c_str(), &model, opts);

    if (!load_result.success) {
        fprintf(stderr, "Error: Failed to load %s: %s (line %d)\n",
                input_path.c_str(), load_result.error_msg, load_result.error_line);
        return 1;
    }

    printf("Loaded: %d meshes, %d materials\n", model.meshCount, model.materialCount);
    printf("Bounds: min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)\n",
           load_result.bounds_min.x, load_result.bounds_min.y, load_result.bounds_min.z,
           load_result.bounds_max.x, load_result.bounds_max.y, load_result.bounds_max.z);

    // Export to GLTF
    GLTFExportOptions export_opts = GLTFDefaultOptions();

    // Extract source directory from input path for texture resolution
    std::string source_dir;
    size_t last_slash = input_path.rfind('/');
    size_t last_backslash = input_path.rfind('\\');
    size_t sep_pos = std::string::npos;
    if (last_slash != std::string::npos && last_backslash != std::string::npos) {
        sep_pos = std::max(last_slash, last_backslash);
    } else if (last_slash != std::string::npos) {
        sep_pos = last_slash;
    } else if (last_backslash != std::string::npos) {
        sep_pos = last_backslash;
    }
    if (sep_pos != std::string::npos) {
        source_dir = input_path.substr(0, sep_pos);
        export_opts.source_dir = source_dir.c_str();
        printf("Source directory: %s\n", source_dir.c_str());
    }

    // Set texture fallback directory if specified via --texture-path
    if (!app->texture_source_path.empty()) {
        export_opts.texture_fallback_dir = app->texture_source_path.c_str();
        printf("Texture fallback path: %s\n", app->texture_source_path.c_str());
    }

    // Pass texture paths from ASC load result to GLTF exporter
    export_opts.texture_count = load_result.material_count;
    for (int i = 0; i < load_result.material_count && i < GLTF_MAX_TEXTURES; i++) {
        if (load_result.texture_paths[i][0] != '\0') {
            export_opts.texture_paths[i] = load_result.texture_paths[i];
            printf("  Material %d texture: %s\n", i, load_result.texture_paths[i]);
        }
    }

    GLTFExportResult export_result = ExportGLTFEx(model, output_path.c_str(), export_opts);

    if (!export_result.success) {
        fprintf(stderr, "Error: Failed to export %s: %s\n",
                output_path.c_str(), export_result.error_msg);
        // Note: Can't unload model without OpenGL context
        return 1;
    }

    printf("Success: Exported to %s\n", output_path.c_str());
    return 0;
}

//------------------------------------------------------------------------------
// Run batch conversion mode
//------------------------------------------------------------------------------
static int run_batch_conversion(AppState* app) {
    // Collect input files
    auto files = collect_asc_files(app->convert_input);

    if (files.empty()) {
        fprintf(stderr, "Error: No .asc files found matching '%s'\n",
                app->convert_input.c_str());
        return 1;
    }

    printf("Found %zu file(s) to convert\n", files.size());
    printf("Options: scale=%.6f swap_yz=%s flip_winding=%s\n",
           app->asc_options.scale,
           app->asc_options.swap_yz ? "yes" : "no",
           app->asc_options.flip_winding ? "yes" : "no");

    // Ensure output directory is specified
    if (app->convert_output.empty()) {
        fprintf(stderr, "Error: Output directory required for batch mode (-o <dir>)\n");
        return 1;
    }

    fs::path output_dir(app->convert_output);

    // Create output directory if it doesn't exist
    std::error_code ec;
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir, ec);
        if (ec) {
            fprintf(stderr, "Error: Cannot create output directory '%s': %s\n",
                    app->convert_output.c_str(), ec.message().c_str());
            return 1;
        }
        printf("Created output directory: %s\n", app->convert_output.c_str());
    }

    // Initialize minimal Raylib context for model loading
    SetTraceLogLevel(LOG_WARNING);

    // Convert each file
    int success_count = 0;
    int fail_count = 0;

    for (size_t i = 0; i < files.size(); i++) {
        const auto& input_file = files[i];

        // Generate output path: output_dir / stem.gltf
        fs::path output_file = output_dir / input_file.stem();
        output_file.replace_extension(".gltf");

        printf("\n[%zu/%zu] %s\n", i + 1, files.size(),
               input_file.filename().string().c_str());
        printf("----------------------------------------\n");

        if (convert_single_file(input_file.string(),
                                output_file.string(), app) == 0) {
            success_count++;
        } else {
            fail_count++;
        }
    }

    // Summary
    printf("\n========================================\n");
    printf("Batch conversion complete:\n");
    printf("  Succeeded: %d\n", success_count);
    printf("  Failed:    %d\n", fail_count);
    printf("  Total:     %zu\n", files.size());
    printf("========================================\n");

    return fail_count > 0 ? 1 : 0;
}

//------------------------------------------------------------------------------
// Run conversion mode (headless) - entry point
//------------------------------------------------------------------------------
static int run_conversion(AppState* app) {
    // Validate inputs
    if (app->convert_input.empty()) {
        fprintf(stderr, "Error: No input file specified for conversion\n");
        return 1;
    }

    // Check for batch mode (directory or wildcard)
    if (is_batch_input(app->convert_input)) {
        return run_batch_conversion(app);
    }

    // Single file mode
    // Generate output filename if not specified or if output is a directory
    fs::path input_path(app->convert_input);
    fs::path output_path(app->convert_output);

    if (app->convert_output.empty()) {
        // No output specified - replace extension with .gltf
        app->convert_output = app->convert_input;
        size_t dot_pos = app->convert_output.rfind('.');
        if (dot_pos != std::string::npos) {
            app->convert_output = app->convert_output.substr(0, dot_pos) + ".gltf";
        } else {
            app->convert_output += ".gltf";
        }
    } else {
        // Check if output is a directory (ends with / or is existing directory)
        std::error_code ec;
        bool is_dir = false;
        if (!app->convert_output.empty() &&
            (app->convert_output.back() == '/' || app->convert_output.back() == '\\')) {
            is_dir = true;
        } else if (fs::is_directory(output_path, ec)) {
            is_dir = true;
        }

        if (is_dir) {
            // Create directory if needed
            if (!fs::exists(output_path)) {
                fs::create_directories(output_path, ec);
                if (ec) {
                    fprintf(stderr, "Error: Cannot create output directory '%s': %s\n",
                            app->convert_output.c_str(), ec.message().c_str());
                    return 1;
                }
            }
            // Generate output filename: output_dir / input_stem.gltf
            fs::path out_file = output_path / input_path.stem();
            out_file.replace_extension(".gltf");
            app->convert_output = out_file.string();
        }
    }

    printf("Options: scale=%.6f swap_yz=%s flip_winding=%s\n",
           app->asc_options.scale,
           app->asc_options.swap_yz ? "yes" : "no",
           app->asc_options.flip_winding ? "yes" : "no");

    // Initialize minimal Raylib context for model loading
    SetTraceLogLevel(LOG_WARNING);

    return convert_single_file(app->convert_input, app->convert_output, app);
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    AppState app = {};

    // Parse command line
    parse_args(argc, argv, &app);

    // Handle conversion mode (headless)
    if (app.convert_mode) {
        return run_conversion(&app);
    }

    // Initialize window for viewer mode
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Model Tool - Viewer & Converter");
    SetTargetFPS(60);

    // Initialize subsystems
    init_renderer(&app);
    init_camera(&app);
    init_viewports(&app);

    // Main loop
    while (!WindowShouldClose()) {
        // Update
        handle_input(&app);

        if (app.auto_rotate) {
            app.rotation_angle += 30.0f * GetFrameTime();
        }

        // Draw
        BeginDrawing();
        ClearBackground((Color){20, 20, 25, 255});

        // Draw all four viewports
        for (int i = 0; i < 4; i++) {
            draw_viewport(&app, &app.viewports[i]);
        }

        // Draw HUD
        draw_hud(&app);

        DrawFPS(WINDOW_WIDTH - 100, 10);

        EndDrawing();
    }

    // Cleanup
    for (int i = 0; i < 4; i++) {
        if (app.viewports[i].valid) {
            UnloadModel(app.viewports[i].model);
        }
    }
    UnloadShader(app.lighting_shader);

    CloseWindow();
    return 0;
}
