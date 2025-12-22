#include "droidclass_parser.h"
#include "renderobject_parser.h"
#include "unit_generator.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// Conventional paths relative to uber base directory
// uber/
//   uberdroid/
//     data/droidclasses.txt
//     data/renderobjects.txt
//     models/
//     textures/
constexpr const char* UBER_DROIDCLASSES = "uberdroid/data/droidclasses.txt";
constexpr const char* UBER_RENDEROBJECTS = "uberdroid/data/renderobjects.txt";
constexpr const char* UBER_MODELS = "uberdroid/";  // ASC paths are relative to uberdroid/
constexpr const char* UBER_TEXTURES = "uberdroid/textures";

// Conventional paths relative to output base directory
// output/
//   units/
//   models/
//   textures/
constexpr const char* OUTPUT_UNITS = "units";
constexpr const char* OUTPUT_MODELS = "models";

struct AppConfig {
    fs::path uberPath;           // Base path to uber folder
    fs::path outputPath;         // Base path for all output
    bool convertModels = true;
    bool skipExisting = true;
    int singleClass = -1;
    float scale = 0.0254f;
};

static void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n";
    std::cout << "\n";
    std::cout << "Parses droidclasses.txt and renderobjects.txt to generate unit definition JSON files.\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --uber <path>     Base path to uber folder (contains uberdroid/)\n";
    std::cout << "  --output <path>   Base path for output (creates units/, models/)\n";
    std::cout << "  --class <id>      Process only a single class (for testing)\n";
    std::cout << "  --no-convert      Skip model conversion, only generate JSON\n";
    std::cout << "  --force           Overwrite existing converted models\n";
    std::cout << "  --scale <factor>  Scale factor (default: 0.0254 for inches to meters)\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "\n";
    std::cout << "Conventional directory structure:\n";
    std::cout << "  Input (--uber):\n";
    std::cout << "    uberdroid/data/droidclasses.txt\n";
    std::cout << "    uberdroid/data/renderobjects.txt\n";
    std::cout << "    uberdroid/models/*.asc\n";
    std::cout << "    uberdroid/textures/\n";
    std::cout << "\n";
    std::cout << "  Output (--output):\n";
    std::cout << "    units/*.json\n";
    std::cout << "    models/*.gltf\n";
    std::cout << "    models/textures/*.jpg\n";
    std::cout << "\n";
    std::cout << "Example:\n";
    std::cout << "  " << progName << " --uber ../uber --output ./droid_output\n";
}

static bool parseArgs(int argc, char* argv[], AppConfig& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }
        else if (arg == "--uber" && i + 1 < argc) {
            config.uberPath = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        }
        else if (arg == "--class" && i + 1 < argc) {
            config.singleClass = std::atoi(argv[++i]);
        }
        else if (arg == "--no-convert") {
            config.convertModels = false;
        }
        else if (arg == "--force") {
            config.skipExisting = false;
        }
        else if (arg == "--scale" && i + 1 < argc) {
            config.scale = std::atof(argv[++i]);
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    // Validate required paths
    if (config.uberPath.empty()) {
        std::cerr << "Error: --uber is required\n";
        return false;
    }
    if (config.outputPath.empty()) {
        std::cerr << "Error: --output is required\n";
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "droid_tool - Uberdroid to Unit Definition Converter\n";
    std::cout << "====================================================\n\n";

    AppConfig config;
    if (!parseArgs(argc, argv, config)) {
        std::cout << "\n";
        printUsage(argv[0]);
        return 1;
    }

    // Build conventional paths from base directories
    fs::path droidclassesPath = config.uberPath / UBER_DROIDCLASSES;
    fs::path renderobjectsPath = config.uberPath / UBER_RENDEROBJECTS;
    fs::path sourceModelsDir = config.uberPath / UBER_MODELS;
    fs::path textureSourceDir = config.uberPath / UBER_TEXTURES;
    fs::path outputDir = config.outputPath / OUTPUT_UNITS;
    fs::path modelsOutputDir = config.outputPath / OUTPUT_MODELS;

    std::cout << "Paths:\n";
    std::cout << "  Uber base:      " << config.uberPath.string() << "\n";
    std::cout << "  Output base:    " << config.outputPath.string() << "\n";
    std::cout << "  Droidclasses:   " << droidclassesPath.string() << "\n";
    std::cout << "  Renderobjects:  " << renderobjectsPath.string() << "\n";
    std::cout << "  Source models:  " << sourceModelsDir.string() << "\n";
    std::cout << "  Textures:       " << textureSourceDir.string() << "\n";
    std::cout << "  Output units:   " << outputDir.string() << "\n";
    std::cout << "  Output models:  " << modelsOutputDir.string() << "\n";
    std::cout << "\n";

    // Check input files exist
    if (!fs::exists(droidclassesPath)) {
        std::cerr << "Error: droidclasses.txt not found: " << droidclassesPath.string() << "\n";
        return 1;
    }
    if (!fs::exists(renderobjectsPath)) {
        std::cerr << "Error: renderobjects.txt not found: " << renderobjectsPath.string() << "\n";
        return 1;
    }

    // Parse droidclasses.txt
    std::cout << "Parsing droidclasses.txt...\n";
    auto droidResult = parseDroidClasses(droidclassesPath.string());
    if (!droidResult.success) {
        std::cerr << "Error parsing droidclasses.txt at line " << droidResult.errorLine
                  << ": " << droidResult.errorMsg << "\n";
        return 1;
    }
    std::cout << "  Found " << droidResult.classes.size() << " droid classes\n";

    // Parse renderobjects.txt
    std::cout << "Parsing renderobjects.txt...\n";
    auto renderResult = parseRenderObjects(renderobjectsPath.string());
    if (!renderResult.success) {
        std::cerr << "Error parsing renderobjects.txt at line " << renderResult.errorLine
                  << ": " << renderResult.errorMsg << "\n";
        return 1;
    }
    std::cout << "  Found " << renderResult.objects.size() << " render objects\n";

    // Filter to single class if requested
    std::vector<DroidClass> classesToProcess;
    if (config.singleClass >= 0) {
        for (const auto& dc : droidResult.classes) {
            if (dc.classId == config.singleClass) {
                classesToProcess.push_back(dc);
                break;
            }
        }
        if (classesToProcess.empty()) {
            std::cerr << "Error: Class " << config.singleClass << " not found\n";
            return 1;
        }
        std::cout << "\nProcessing single class: " << config.singleClass << "\n";
    } else {
        classesToProcess = droidResult.classes;
    }

    // Generate units
    std::cout << "\nGenerating unit definitions...\n\n";

    UnitGeneratorOptions genOpts;
    genOpts.outputDir = outputDir;
    genOpts.modelsOutputDir = modelsOutputDir;
    genOpts.sourceModelsDir = sourceModelsDir;
    genOpts.textureSourceDir = textureSourceDir;
    genOpts.convertModels = config.convertModels;
    genOpts.skipExisting = config.skipExisting;
    genOpts.scale = config.scale;

    auto genResult = generateUnits(classesToProcess, renderResult.objects, genOpts);

    // Summary
    std::cout << "\n====================================================\n";
    std::cout << "Summary:\n";
    std::cout << "  Units generated: " << genResult.unitsGenerated << "\n";
    if (config.convertModels) {
        std::cout << "  Models converted: " << genResult.modelsConverted << "\n";
        std::cout << "  Models skipped: " << genResult.modelsSkipped << "\n";
    }
    if (!genResult.unsupportedModels.empty()) {
        std::cout << "  Unsupported models: " << genResult.unsupportedModels.size()
                  << " (MD2/MDL - see unsupported_models.txt)\n";
    }

    if (!genResult.success) {
        std::cerr << "\nError: " << genResult.errorMsg << "\n";
        return 1;
    }

    std::cout << "\nDone!\n";
    return 0;
}
