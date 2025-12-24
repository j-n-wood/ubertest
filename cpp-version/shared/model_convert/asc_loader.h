#ifndef ASC_LOADER_H
#define ASC_LOADER_H

#include "raylib.h"

// Load options for ASC files
typedef struct ASCLoadOptions {
    float scale;            // Scale factor (default: 0.0254 for inches to meters)
    bool swap_yz;           // Swap Y and Z axes (default: false)
    bool flip_winding;      // Flip triangle winding order (default: false)
    bool skip_gpu_upload;   // Skip GPU upload (for headless conversion)
    bool rotate_forward;    // Rotate 90° around Y to convert +X forward to +Z forward (glTF convention)
} ASCLoadOptions;

// Maximum materials/textures supported
#define ASC_MAX_MATERIALS 32

// Result structure for detailed error reporting
typedef struct ASCLoadResult {
    bool success;
    char error_msg[256];
    int error_line;
    // Bounding box of loaded model (after transforms)
    Vector3 bounds_min;
    Vector3 bounds_max;
    // Material texture paths (indexed by material ID)
    // These are the raw paths from the ASC file (may contain backslashes)
    char texture_paths[ASC_MAX_MATERIALS][256];
    int material_count;
} ASCLoadResult;

// Get default load options
ASCLoadOptions ASCDefaultOptions(void);

// Load a MilkShape 3D ASCII file and return a Raylib Model
// Uses default options (inch to meter scale, Y/Z swap, winding flip)
Model LoadASC(const char* filepath);

// Load with custom options
Model LoadASCWithOptions(const char* filepath, ASCLoadOptions options);

// Load with detailed error reporting and custom options
ASCLoadResult LoadASCEx(const char* filepath, Model* out_model, ASCLoadOptions options);

#endif // ASC_LOADER_H
