#include "tmx_loader.h"
#include <tinyxml2.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;
using namespace tinyxml2;

//------------------------------------------------------------------------------
// Helper: Parse CSV tile data
//------------------------------------------------------------------------------
static std::vector<int> parseCsvTileData(const char* csvData) {
    std::vector<int> tiles;
    if (!csvData) return tiles;

    std::string data(csvData);
    std::stringstream ss(data);
    std::string token;

    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t\n\r");
        size_t end = token.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            token = token.substr(start, end - start + 1);
            if (!token.empty()) {
                tiles.push_back(std::stoi(token));
            }
        }
    }

    return tiles;
}

//------------------------------------------------------------------------------
// Helper: Parse waypoint links from object properties
//------------------------------------------------------------------------------
static std::vector<int> parseWaypointLinks(XMLElement* objectElem) {
    std::vector<int> links;

    XMLElement* propsElem = objectElem->FirstChildElement("properties");
    if (!propsElem) return links;

    for (XMLElement* prop = propsElem->FirstChildElement("property");
         prop != nullptr;
         prop = prop->NextSiblingElement("property")) {

        const char* name = prop->Attribute("name");
        if (name && strncmp(name, "link-", 5) == 0) {
            int linkId = prop->IntAttribute("value", 0);
            if (linkId > 0) {
                links.push_back(linkId);
            }
        }
    }

    return links;
}

//------------------------------------------------------------------------------
// Helper: read a named integer custom property from an object. Returns true and
// writes *out if the property exists.
//------------------------------------------------------------------------------
static bool findIntProperty(XMLElement* objectElem, const char* propName, int* out) {
    XMLElement* propsElem = objectElem->FirstChildElement("properties");
    if (!propsElem) return false;
    for (XMLElement* prop = propsElem->FirstChildElement("property");
         prop != nullptr;
         prop = prop->NextSiblingElement("property")) {
        const char* name = prop->Attribute("name");
        if (name && strcmp(name, propName) == 0) {
            *out = prop->IntAttribute("value", 0);
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
// Load a single TMX level file
//------------------------------------------------------------------------------
TmxLoadResult loadTmxLevel(const std::string& filePath) {
    TmxLoadResult result;
    result.success = false;

    XMLDocument doc;
    XMLError err = doc.LoadFile(filePath.c_str());
    if (err != XML_SUCCESS) {
        result.errorMsg = "Failed to parse TMX file: " + filePath;
        return result;
    }

    XMLElement* mapElem = doc.FirstChildElement("map");
    if (!mapElem) {
        result.errorMsg = "No <map> element found in TMX file";
        return result;
    }

    // Extract map attributes
    result.level.filePath = filePath;
    result.level.width = mapElem->IntAttribute("width", 0);
    result.level.height = mapElem->IntAttribute("height", 0);
    result.level.tileWidth = mapElem->IntAttribute("tilewidth", 64);
    result.level.tileHeight = mapElem->IntAttribute("tileheight", 64);

    // Optional map-level custom property: base tileset colour row (default 0). The tileset
    // atlas stacks colour-variant rows of the same tiles; this selects which one this deck uses.
    findIntProperty(mapElem, "tileRow", &result.level.tileRow);

    // Extract level name + stable deck number from filename
    result.level.name = extractLevelName(fs::path(filePath).filename().string());
    result.level.number = extractLevelNumber(fs::path(filePath).filename().string());

    // Find tileset reference
    XMLElement* tilesetElem = mapElem->FirstChildElement("tileset");
    if (tilesetElem) {
        const char* source = tilesetElem->Attribute("source");
        if (source) {
            result.level.tilesetSource = source;
        }
    }

    // Find tile layer
    XMLElement* layerElem = mapElem->FirstChildElement("layer");
    if (layerElem) {
        XMLElement* dataElem = layerElem->FirstChildElement("data");
        if (dataElem) {
            const char* encoding = dataElem->Attribute("encoding");
            if (encoding && strcmp(encoding, "csv") == 0) {
                const char* csvData = dataElem->GetText();
                result.level.tiles = parseCsvTileData(csvData);
            } else {
                result.errorMsg = "Only CSV encoding is supported for tile data";
                return result;
            }
        }
    }

    // Validate tile count
    int expectedTiles = result.level.width * result.level.height;
    if ((int)result.level.tiles.size() != expectedTiles) {
        result.errorMsg = "Tile count mismatch: expected " + std::to_string(expectedTiles) +
                          ", got " + std::to_string(result.level.tiles.size());
        return result;
    }

    // Find objects in object layers: lift markers (any object carrying an "elevator"
    // custom property) and waypoints (point objects). Tile size is needed to convert an
    // object's pixel position to a tile coordinate.
    const int tileW = result.level.tileWidth > 0 ? result.level.tileWidth : 64;
    const int tileH = result.level.tileHeight > 0 ? result.level.tileHeight : 64;
    for (XMLElement* objGroupElem = mapElem->FirstChildElement("objectgroup");
         objGroupElem != nullptr;
         objGroupElem = objGroupElem->NextSiblingElement("objectgroup")) {

        for (XMLElement* objElem = objGroupElem->FirstChildElement("object");
             objElem != nullptr;
             objElem = objElem->NextSiblingElement("object")) {

            // Lift marker: an object with an "elevator" custom property. Takes priority
            // over the waypoint interpretation so a lift point object isn't also a waypoint.
            int elevator = 0, stopIndex = 0;
            if (findIntProperty(objElem, "elevator", &elevator)) {
                findIntProperty(objElem, "stop_index", &stopIndex);
                float ox = objElem->FloatAttribute("x", 0.0f);
                float oy = objElem->FloatAttribute("y", 0.0f);
                TmxLift lift;
                lift.col = static_cast<int>(ox / tileW);
                lift.row = static_cast<int>(oy / tileH);
                lift.elevator = elevator;
                lift.stopIndex = stopIndex;
                result.level.lifts.push_back(lift);
                continue;
            }

            // Check if it's a point object (waypoint)
            XMLElement* pointElem = objElem->FirstChildElement("point");
            if (pointElem) {
                TmxWaypoint wp;
                wp.id = objElem->IntAttribute("id", 0);
                wp.x = objElem->FloatAttribute("x", 0.0f);
                wp.y = objElem->FloatAttribute("y", 0.0f);
                wp.links = parseWaypointLinks(objElem);
                result.level.waypoints.push_back(wp);
            }
        }
    }

    result.success = true;
    return result;
}

//------------------------------------------------------------------------------
// Load all TMX files from a directory
//------------------------------------------------------------------------------
std::vector<TmxLoadResult> loadTmxLevelsFromDirectory(const std::string& directoryPath) {
    std::vector<TmxLoadResult> results;

    std::error_code ec;
    if (!fs::exists(directoryPath, ec) || !fs::is_directory(directoryPath, ec)) {
        TmxLoadResult err;
        err.success = false;
        err.errorMsg = "Directory not found: " + directoryPath;
        results.push_back(err);
        return results;
    }

    // Collect all .tmx files
    std::vector<fs::path> tmxFiles;
    for (const auto& entry : fs::directory_iterator(directoryPath, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            // Case-insensitive extension check
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".tmx") {
                tmxFiles.push_back(entry.path());
            }
        }
    }

    // Sort by filename for consistent ordering
    std::sort(tmxFiles.begin(), tmxFiles.end());

    // Load each file
    for (const auto& path : tmxFiles) {
        results.push_back(loadTmxLevel(path.string()));
    }

    return results;
}

//------------------------------------------------------------------------------
// Extract level name from filename
//------------------------------------------------------------------------------
std::string extractLevelName(const std::string& filename) {
    // Expected format: "level_N_name.tmx"
    // Extract "name" and capitalize first letter

    std::string name = filename;

    // Remove .tmx extension
    size_t dotPos = name.rfind('.');
    if (dotPos != std::string::npos) {
        name = name.substr(0, dotPos);
    }

    // Try to find level_N_ pattern
    size_t underscorePos = name.find('_');
    if (underscorePos != std::string::npos) {
        size_t secondUnderscorePos = name.find('_', underscorePos + 1);
        if (secondUnderscorePos != std::string::npos) {
            name = name.substr(secondUnderscorePos + 1);
        }
    }

    // Replace underscores with spaces
    std::replace(name.begin(), name.end(), '_', ' ');

    // Capitalize first letter
    if (!name.empty()) {
        name[0] = std::toupper(name[0]);
    }

    return name;
}

//------------------------------------------------------------------------------
// Extract deck number from filename ("level_<N>_name.tmx" -> N; -1 if absent)
//------------------------------------------------------------------------------
int extractLevelNumber(const std::string& filename) {
    // Expect "level_<N>_..."; read the digits between the first two underscores.
    size_t firstUnderscore = filename.find('_');
    if (firstUnderscore == std::string::npos) return -1;
    size_t start = firstUnderscore + 1;
    size_t end = filename.find('_', start);
    std::string digits = (end == std::string::npos) ? filename.substr(start)
                                                     : filename.substr(start, end - start);
    if (digits.empty()) return -1;
    for (char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    }
    return std::atoi(digits.c_str());
}
