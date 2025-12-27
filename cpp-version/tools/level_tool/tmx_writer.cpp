#include "tmx_writer.h"
#include <tinyxml2.h>
#include <sstream>
#include <algorithm>

using namespace tinyxml2;

// Convert spaces to underscores and make lowercase for filename
static std::string sanitizeFilename(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == ' ') c = '_';
        c = std::tolower(c);
    }
    return result;
}

std::string getTmxFilename(const ParadroidLevel& level) {
    std::ostringstream oss;
    oss << "level_" << level.levelNumber << "_" << sanitizeFilename(level.name) << ".tmx";
    return oss.str();
}

bool writeTmxFile(const ParadroidLevel& level,
                  const std::string& outputPath,
                  const TmxWriterConfig& config,
                  std::string& errorMsg) {
    XMLDocument doc;

    // XML declaration
    doc.InsertFirstChild(doc.NewDeclaration("xml version=\"1.0\" encoding=\"UTF-8\""));

    // Calculate next object ID (waypoints start at 1)
    int nextObjectId = level.waypoints.empty() ? 1 : (int)level.waypoints.size() + 1;
    int nextLayerId = level.waypoints.empty() ? 2 : 3;

    // <map> element
    XMLElement* mapElem = doc.NewElement("map");
    mapElem->SetAttribute("version", "1.10");
    mapElem->SetAttribute("tiledversion", "1.11.2");
    mapElem->SetAttribute("orientation", "orthogonal");
    mapElem->SetAttribute("renderorder", "right-down");
    mapElem->SetAttribute("width", level.xlen);
    mapElem->SetAttribute("height", level.ylen);
    mapElem->SetAttribute("tilewidth", config.tileWidth);
    mapElem->SetAttribute("tileheight", config.tileHeight);
    mapElem->SetAttribute("infinite", 0);
    mapElem->SetAttribute("nextlayerid", nextLayerId);
    mapElem->SetAttribute("nextobjectid", nextObjectId);
    doc.InsertEndChild(mapElem);

    // <tileset> element
    XMLElement* tilesetElem = doc.NewElement("tileset");
    tilesetElem->SetAttribute("firstgid", config.firstGid);
    tilesetElem->SetAttribute("source", config.tilesetSource.c_str());
    mapElem->InsertEndChild(tilesetElem);

    // <layer> element
    XMLElement* layerElem = doc.NewElement("layer");
    layerElem->SetAttribute("id", 1);
    layerElem->SetAttribute("name", "Tile Layer 1");
    layerElem->SetAttribute("width", level.xlen);
    layerElem->SetAttribute("height", level.ylen);
    mapElem->InsertEndChild(layerElem);

    // <data> element with CSV encoding
    XMLElement* dataElem = doc.NewElement("data");
    dataElem->SetAttribute("encoding", "csv");

    // Build CSV tile data
    // TMX tile IDs = Paradroid index + firstGid
    std::ostringstream csvData;
    csvData << "\n";
    for (size_t row = 0; row < level.tiles.size(); row++) {
        for (size_t col = 0; col < level.tiles[row].size(); col++) {
            int tmxTileId = level.tiles[row][col] + config.firstGid;
            csvData << tmxTileId;
            // Add comma unless it's the very last tile
            if (row < level.tiles.size() - 1 || col < level.tiles[row].size() - 1) {
                csvData << ",";
            }
        }
        csvData << "\n";
    }

    dataElem->SetText(csvData.str().c_str());
    layerElem->InsertEndChild(dataElem);

    // <objectgroup> element for waypoints
    if (!level.waypoints.empty()) {
        XMLElement* objGroupElem = doc.NewElement("objectgroup");
        objGroupElem->SetAttribute("id", 2);
        objGroupElem->SetAttribute("name", "waypoints");
        mapElem->InsertEndChild(objGroupElem);

        // Add each waypoint as a point object
        // Object IDs are 1-based (waypoint.number + 1)
        for (const auto& wp : level.waypoints) {
            XMLElement* objElem = doc.NewElement("object");
            int objectId = wp.number + 1;  // TMX object IDs are 1-based
            objElem->SetAttribute("id", objectId);

            // Convert tile coordinates to pixel coordinates
            // x,y in source are tile indices, multiply by tile size
            // Add half tile to center the point in the tile
            float pixelX = (wp.x + 0.5f) * config.tileWidth;
            float pixelY = (wp.y + 0.5f) * config.tileHeight;
            objElem->SetAttribute("x", pixelX);
            objElem->SetAttribute("y", pixelY);

            // Add properties for links (connections to other waypoints)
            if (!wp.connections.empty()) {
                XMLElement* propsElem = doc.NewElement("properties");
                for (size_t i = 0; i < wp.connections.size(); i++) {
                    XMLElement* propElem = doc.NewElement("property");
                    std::string propName = "link-" + std::to_string(i);
                    propElem->SetAttribute("name", propName.c_str());
                    propElem->SetAttribute("type", "object");
                    // Reference the object ID (waypoint number + 1)
                    propElem->SetAttribute("value", wp.connections[i] + 1);
                    propsElem->InsertEndChild(propElem);
                }
                objElem->InsertEndChild(propsElem);
            }

            // Add <point/> element to mark this as a point object
            XMLElement* pointElem = doc.NewElement("point");
            objElem->InsertEndChild(pointElem);

            objGroupElem->InsertEndChild(objElem);
        }
    }

    // Save the document
    XMLError result = doc.SaveFile(outputPath.c_str());
    if (result != XML_SUCCESS) {
        errorMsg = "Failed to write TMX file: " + outputPath;
        return false;
    }

    return true;
}
