#include "tile_properties_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

//------------------------------------------------------------------------------
// Helper Functions
//------------------------------------------------------------------------------

namespace {

TileRenderProperties parseTileProperties(const json& j, const TileRenderProperties& defaults) {
    TileRenderProperties props = defaults;

    if (j.contains("bumpTileIndex") && j["bumpTileIndex"].is_number_integer()) {
        props.bumpTileIndex = j["bumpTileIndex"].get<int>();
    }

    if (j.contains("specularIntensity") && j["specularIntensity"].is_number()) {
        props.specularIntensity = j["specularIntensity"].get<float>();
    }

    if (j.contains("albedoMultiplier") && j["albedoMultiplier"].is_array()) {
        const auto& arr = j["albedoMultiplier"];
        if (arr.size() >= 3) {
            props.albedoMultiplier[0] = arr[0].get<float>();
            props.albedoMultiplier[1] = arr[1].get<float>();
            props.albedoMultiplier[2] = arr[2].get<float>();
        }
    }

    return props;
}

BumpAtlasConfig parseBumpAtlas(const json& j) {
    BumpAtlasConfig config;

    if (j.contains("texture") && j["texture"].is_string()) {
        config.texture = j["texture"].get<std::string>();
    }

    if (j.contains("tileWidth") && j["tileWidth"].is_number_integer()) {
        config.tileWidth = j["tileWidth"].get<int>();
    }

    if (j.contains("tileHeight") && j["tileHeight"].is_number_integer()) {
        config.tileHeight = j["tileHeight"].get<int>();
    }

    if (j.contains("columns") && j["columns"].is_number_integer()) {
        config.columns = j["columns"].get<int>();
    }

    return config;
}

json tilePropertiesToJson(const TileRenderProperties& props) {
    json j = json::object();
    j["bumpTileIndex"] = props.bumpTileIndex;
    j["specularIntensity"] = props.specularIntensity;
    j["albedoMultiplier"] = json::array({
        props.albedoMultiplier[0],
        props.albedoMultiplier[1],
        props.albedoMultiplier[2]
    });
    return j;
}

json bumpAtlasToJson(const BumpAtlasConfig& config) {
    json j = json::object();
    j["texture"] = config.texture;
    j["tileWidth"] = config.tileWidth;
    j["tileHeight"] = config.tileHeight;
    j["columns"] = config.columns;
    return j;
}

} // namespace

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

TilePropertiesConfig loadTileProperties(const std::string& filePath) {
    TilePropertiesConfig config;
    config.valid = false;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open tiles.json: " << filePath << std::endl;
        return config;
    }

    try {
        json j = json::parse(file);

        // Version
        if (j.contains("version") && j["version"].is_number_integer()) {
            config.version = j["version"].get<int>();
        }

        // Bump atlas configuration
        if (j.contains("bumpAtlas") && j["bumpAtlas"].is_object()) {
            config.bumpAtlas = parseBumpAtlas(j["bumpAtlas"]);
        }

        // Default properties (parse first so we can use them as fallbacks)
        if (j.contains("defaults") && j["defaults"].is_object()) {
            config.defaults = parseTileProperties(j["defaults"], TileRenderProperties{});
        }

        // Per-tile properties
        if (j.contains("tiles") && j["tiles"].is_object()) {
            for (auto& [key, value] : j["tiles"].items()) {
                try {
                    int tileId = std::stoi(key);
                    config.tiles[tileId] = parseTileProperties(value, config.defaults);
                } catch (const std::exception& e) {
                    std::cerr << "Warning: Invalid tile ID '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        config.valid = true;
        std::cout << "Loaded tile properties: " << config.tiles.size()
                  << " tile overrides from " << filePath << std::endl;

    } catch (const json::exception& e) {
        std::cerr << "JSON parse error in tiles.json: " << e.what() << std::endl;
        return config;
    }

    return config;
}

bool saveTileProperties(const TilePropertiesConfig& config, const std::string& filePath) {
    json j = json::object();

    j["version"] = config.version;
    j["bumpAtlas"] = bumpAtlasToJson(config.bumpAtlas);
    j["defaults"] = tilePropertiesToJson(config.defaults);

    json tilesJson = json::object();
    for (const auto& [tileId, props] : config.tiles) {
        tilesJson[std::to_string(tileId)] = tilePropertiesToJson(props);
    }
    j["tiles"] = tilesJson;

    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    file << j.dump(2);
    return true;
}
