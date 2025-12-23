#ifndef MDL_LOADER_H
#define MDL_LOADER_H

#include "skeletal_model.h"

//------------------------------------------------------------------------------
// Load options for MDL files
//------------------------------------------------------------------------------
struct MDLLoadOptions {
    float scale;              // Scale factor (default: 1.0)
    bool swap_yz;             // Convert from Z-up to Y-up coordinate system
    bool load_animations;     // Load animation sequences (default: true)
    bool load_textures;       // Extract embedded textures (default: true)
};

//------------------------------------------------------------------------------
// Result structure for MDL loading
//------------------------------------------------------------------------------
struct MDLLoadResult {
    bool success;
    char error_msg[256];
    int error_line;           // For debugging parse errors
    SkeletalModel model;
};

//------------------------------------------------------------------------------
// Get default load options
//------------------------------------------------------------------------------
MDLLoadOptions MDLDefaultOptions();

//------------------------------------------------------------------------------
// Load MDL file into skeletal model representation
//------------------------------------------------------------------------------
MDLLoadResult LoadMDL(const char* filepath, MDLLoadOptions options);

//------------------------------------------------------------------------------
// Get animation sequence names from an MDL file (without full load)
//------------------------------------------------------------------------------
std::vector<std::string> GetMDLSequenceNames(const char* filepath);

#endif // MDL_LOADER_H
