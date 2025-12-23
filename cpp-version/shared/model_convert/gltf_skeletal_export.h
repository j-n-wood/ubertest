#ifndef GLTF_SKELETAL_EXPORT_H
#define GLTF_SKELETAL_EXPORT_H

#include "skeletal_model.h"
#include <string>

//------------------------------------------------------------------------------
// Export options for skeletal GLTF files
//------------------------------------------------------------------------------
struct SkeletalExportOptions {
    bool binary;             // true = .glb (default), false = .gltf + .bin
    bool embed_textures;     // Embed textures in output (default: true)
    bool export_animations;  // Include animations (default: true)
    const char* texture_dir; // Source directory for textures (optional)
};

//------------------------------------------------------------------------------
// Result structure for export operation
//------------------------------------------------------------------------------
struct SkeletalExportResult {
    bool success;
    char error_msg[256];
};

//------------------------------------------------------------------------------
// Get default export options
//------------------------------------------------------------------------------
SkeletalExportOptions SkeletalExportDefaultOptions();

//------------------------------------------------------------------------------
// Export skeletal model to GLTF/GLB file
//------------------------------------------------------------------------------
SkeletalExportResult ExportSkeletalGLTF(const SkeletalModel& model,
                                         const char* output_path,
                                         SkeletalExportOptions options);

//------------------------------------------------------------------------------
// Convenience function with default options (GLB output)
//------------------------------------------------------------------------------
bool ExportSkeletalGLB(const SkeletalModel& model, const char* output_path);

#endif // GLTF_SKELETAL_EXPORT_H
