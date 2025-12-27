#include "paradroid_parser.h"
#include "tmx_writer.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// Default paths (relative to tool binary location in build/tools/level_tool/)
constexpr const char* DEFAULT_INPUT = "../../../../tiled/Paradroid.maps";
constexpr const char* DEFAULT_OUTPUT = "./output/ships/ship1/levels";
constexpr const char* DEFAULT_TILESET = "default.tsx";
constexpr const char* DEFAULT_TILESET_SOURCE = "../../../../tiled/default.tsx";

struct AppConfig {
    fs::path inputPath = DEFAULT_INPUT;
    fs::path outputPath = DEFAULT_OUTPUT;
    std::string tilesetSource = DEFAULT_TILESET;
    bool showHelp = false;
};

static void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n";
    std::cout << "\n";
    std::cout << "Converts Paradroid.maps to Tiled TMX format.\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --convert         Run conversion (default mode)\n";
    std::cout << "  --input <path>    Input Paradroid.maps file\n";
    std::cout << "                    (default: " << DEFAULT_INPUT << ")\n";
    std::cout << "  -o, --output <dir> Output directory for TMX files\n";
    std::cout << "                    (default: " << DEFAULT_OUTPUT << ")\n";
    std::cout << "  --tileset <path>  Tileset source reference in TMX\n";
    std::cout << "                    (default: " << DEFAULT_TILESET << ")\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --convert\n";
    std::cout << "  " << progName << " --convert --input ../data/Paradroid.maps -o ./levels\n";
    std::cout << "\n";
    std::cout << "Output structure:\n";
    std::cout << "  <output_dir>/\n";
    std::cout << "    level_0_maintenance.tmx\n";
    std::cout << "    level_1_engineering.tmx\n";
    std::cout << "    ...\n";
    std::cout << "    level_15_shuttle_bay.tmx\n";
}

static bool parseArgs(int argc, char* argv[], AppConfig& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            config.showHelp = true;
            return true;
        }
        else if (arg == "--convert") {
            // Default mode, nothing to do
        }
        else if (arg == "--input" && i + 1 < argc) {
            config.inputPath = argv[++i];
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            config.outputPath = argv[++i];
        }
        else if (arg == "--tileset" && i + 1 < argc) {
            config.tilesetSource = argv[++i];
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

static int runConvert(const AppConfig& config) {
    std::cout << "level_tool: Converting Paradroid.maps to TMX\n";
    std::cout << "  Input:   " << config.inputPath << "\n";
    std::cout << "  Output:  " << config.outputPath << "\n";
    std::cout << "  Tileset: " << config.tilesetSource << "\n";
    std::cout << "\n";

    // Parse input file
    std::cout << "Parsing input file...\n";
    auto result = parseParadroidMaps(config.inputPath.string());
    if (!result.success) {
        std::cerr << "Error: " << result.errorMsg << "\n";
        return 1;
    }

    std::cout << "Found " << result.mapFile.levels.size() << " levels in \""
              << result.mapFile.areaName << "\"\n\n";

    // Create output directory
    std::error_code ec;
    if (!fs::exists(config.outputPath)) {
        fs::create_directories(config.outputPath, ec);
        if (ec) {
            std::cerr << "Error: Failed to create output directory: "
                      << config.outputPath << "\n";
            return 1;
        }
    }

    // Copy tileset file to output directory
    fs::path inputDir = config.inputPath.parent_path();
    fs::path tilesetSource = inputDir / config.tilesetSource;
    fs::path tilesetDest = config.outputPath / config.tilesetSource;
    if (fs::exists(tilesetSource)) {
        fs::copy_file(tilesetSource, tilesetDest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Warning: Failed to copy tileset: " << tilesetSource << "\n";
        } else {
            std::cout << "Copied tileset: " << config.tilesetSource << "\n";
        }
    } else {
        std::cerr << "Warning: Tileset not found: " << tilesetSource << "\n";
    }

    // Copy tileset image (map_blocks.png) to output directory
    fs::path imageSource = inputDir / "map_blocks.png";
    fs::path imageDest = config.outputPath / "map_blocks.png";
    if (fs::exists(imageSource)) {
        fs::copy_file(imageSource, imageDest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Warning: Failed to copy tileset image: " << imageSource << "\n";
        } else {
            std::cout << "Copied tileset image: map_blocks.png\n";
        }
    } else {
        std::cerr << "Warning: Tileset image not found: " << imageSource << "\n";
    }
    std::cout << "\n";

    // Configure TMX writer
    TmxWriterConfig tmxConfig;
    tmxConfig.tilesetSource = config.tilesetSource;

    // Convert each level
    int successCount = 0;
    for (const auto& level : result.mapFile.levels) {
        std::string filename = getTmxFilename(level);
        fs::path outputFile = config.outputPath / filename;

        std::string errorMsg;
        if (writeTmxFile(level, outputFile.string(), tmxConfig, errorMsg)) {
            std::cout << "  [OK] " << filename
                      << " (" << level.xlen << "x" << level.ylen << ")\n";
            successCount++;
        } else {
            std::cerr << "  [FAIL] " << filename << ": " << errorMsg << "\n";
        }
    }

    std::cout << "\nConversion complete: " << successCount << "/"
              << result.mapFile.levels.size() << " levels\n";

    return (successCount == result.mapFile.levels.size()) ? 0 : 1;
}

int main(int argc, char* argv[]) {
    AppConfig config;

    if (!parseArgs(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }

    if (config.showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    return runConvert(config);
}
