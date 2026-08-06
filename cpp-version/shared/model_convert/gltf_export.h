#ifndef GLTF_EXPORT_H
#define GLTF_EXPORT_H

#include "raylib.h"

// Maximum number of texture paths that can be passed to export
#define GLTF_MAX_TEXTURES 32

// Export options for GLTF files
typedef struct GLTFExportOptions {
    const char* texture_dir;     // Relative path prefix for texture URIs (default: "textures")
    const char* source_dir;      // Source directory for resolving texture paths (e.g., ASC file dir)
    const char* texture_fallback_dir;  // Fallback directory if texture not found at source_dir (--texture-path)
    const char* model_hint;      // Original model filename hint for texture prefix matching (e.g., "head_j5.asc" -> try "j5_head.bmp")
    bool include_extras;         // Include Blinn-Phong extras in materials (default: true)
    bool copy_textures;          // Copy texture files to output (default: true)
    bool include_physics_shape;  // Include physics shape in asset extras (default: true)

    // Texture paths per material (index matches material index)
    // These are the original paths from the source format (e.g., ASC)
    // They will be converted to relative paths like "../textures/filename.ext"
    const char* texture_paths[GLTF_MAX_TEXTURES];
    int texture_count;

    // Optional tangent-space normal (bump) map per material (index matches material index).
    // When set, the exporter emits a glTF material.normalTexture and always writes a TANGENT
    // vertex attribute (generating tangents if absent). Normal maps are kept lossless (BMP->PNG),
    // unlike diffuse (BMP->JPG). This makes bump a standard part of the glTF — no game-side special
    // handling. Leave NULL for materials without a bump map.
    const char* normal_texture_paths[GLTF_MAX_TEXTURES];
    int normal_texture_count;
} GLTFExportOptions;

// Result structure for export operation
typedef struct GLTFExportResult {
    bool success;
    char error_msg[256];
} GLTFExportResult;

// Get default export options
GLTFExportOptions GLTFDefaultOptions(void);

// Export Raylib Model to GLTF 2.0 file
// Returns true on success
bool ExportGLTF(Model model, const char* output_path);

// Export with custom options
bool ExportGLTFWithOptions(Model model, const char* output_path, GLTFExportOptions options);

// Export with detailed error reporting
GLTFExportResult ExportGLTFEx(Model model, const char* output_path, GLTFExportOptions options);

#endif // GLTF_EXPORT_H
