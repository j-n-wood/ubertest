#pragma once

#include "droidclass_parser.h"
#include "renderobject_parser.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct UnitGeneratorOptions {
    fs::path outputDir;
    fs::path modelsOutputDir;
    fs::path sourceModelsDir;
    fs::path textureSourceDir;
    bool convertModels = true;
    bool skipExisting = true;
    float scale = 0.0254f;
    bool swapYZ = false;  // ASC files from Uberdroid are already Y-up
};

struct UnitGeneratorResult {
    bool success = false;
    std::string errorMsg;
    int unitsGenerated = 0;
    int modelsConverted = 0;
    int modelsSkipped = 0;
    std::vector<std::string> unsupportedModels;
};

[[nodiscard]] UnitGeneratorResult generateUnits(
    const std::vector<DroidClass>& classes,
    const std::vector<RenderObject>& renderObjects,
    const UnitGeneratorOptions& options
);
