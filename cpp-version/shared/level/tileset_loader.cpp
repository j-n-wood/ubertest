#include "tileset_loader.h"
#include <tinyxml2.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace tinyxml2;

//------------------------------------------------------------------------------
// Load a TSX tileset file
//------------------------------------------------------------------------------
TsxLoadResult loadTsxTileset(const std::string& filePath) {
    TsxLoadResult result;
    result.success = false;

    XMLDocument doc;
    XMLError err = doc.LoadFile(filePath.c_str());
    if (err != XML_SUCCESS) {
        result.errorMsg = "Failed to parse TSX file: " + filePath;
        return result;
    }

    XMLElement* tilesetElem = doc.FirstChildElement("tileset");
    if (!tilesetElem) {
        result.errorMsg = "No <tileset> element found in TSX file";
        return result;
    }

    // Extract tileset attributes
    const char* name = tilesetElem->Attribute("name");
    if (name) {
        result.tileset.name = name;
    }

    result.tileset.tileWidth = tilesetElem->IntAttribute("tilewidth", 64);
    result.tileset.tileHeight = tilesetElem->IntAttribute("tileheight", 64);
    result.tileset.spacing = tilesetElem->IntAttribute("spacing", 0);
    result.tileset.tileCount = tilesetElem->IntAttribute("tilecount", 0);
    result.tileset.columns = tilesetElem->IntAttribute("columns", 1);

    // Find image element
    XMLElement* imageElem = tilesetElem->FirstChildElement("image");
    if (imageElem) {
        const char* source = imageElem->Attribute("source");
        if (source) {
            result.tileset.imageSource = source;
        }
        result.tileset.imageWidth = imageElem->IntAttribute("width", 0);
        result.tileset.imageHeight = imageElem->IntAttribute("height", 0);
    }

    // Parse per-tile properties (for future use)
    for (XMLElement* tileElem = tilesetElem->FirstChildElement("tile");
         tileElem != nullptr;
         tileElem = tileElem->NextSiblingElement("tile")) {

        int tileId = tileElem->IntAttribute("id", -1);
        if (tileId < 0) continue;

        TmxTileProperties props;

        XMLElement* propsElem = tileElem->FirstChildElement("properties");
        if (propsElem) {
            for (XMLElement* prop = propsElem->FirstChildElement("property");
                 prop != nullptr;
                 prop = prop->NextSiblingElement("property")) {

                const char* propName = prop->Attribute("name");
                if (!propName) continue;

                if (strcmp(propName, "collision") == 0) {
                    const char* val = prop->Attribute("value");
                    if (val) {
                        props.solid = (strcmp(val, "solid") == 0 ||
                                       strcmp(val, "true") == 0 ||
                                       strcmp(val, "1") == 0);
                    }
                }
                else if (strcmp(propName, "floor") == 0) {
                    props.floor = prop->BoolAttribute("value", true);
                }
                else if (strcmp(propName, "bump_tile") == 0) {
                    props.bumpTileIndex = prop->IntAttribute("value", -1);
                }
                else if (strcmp(propName, "model") == 0) {
                    const char* val = prop->Attribute("value");
                    if (val) {
                        props.modelPath = val;
                    }
                }
            }
        }

        result.tileset.tileProperties[tileId] = props;
    }

    result.success = true;
    return result;
}

//------------------------------------------------------------------------------
// Compute UV coordinates for a tile ID
//------------------------------------------------------------------------------
void getTileUV(const TmxTileset& tileset, int tileId,
               float* u0, float* v0, float* u1, float* v1) {

    // Empty tile or invalid
    if (tileId <= 0 || tileset.imageWidth <= 0 || tileset.imageHeight <= 0) {
        *u0 = *v0 = *u1 = *v1 = 0.0f;
        return;
    }

    // Convert to 0-based local tile index
    int localId = tileId - tileset.firstGid;
    if (localId < 0 || localId >= tileset.tileCount) {
        *u0 = *v0 = *u1 = *v1 = 0.0f;
        return;
    }

    // Calculate grid position
    int col = localId % tileset.columns;
    int row = localId / tileset.columns;

    // Calculate pixel positions (including spacing)
    float pixelX = static_cast<float>(col * (tileset.tileWidth + tileset.spacing));
    float pixelY = static_cast<float>(row * (tileset.tileHeight + tileset.spacing));

    // Convert to UV coordinates (0.0 - 1.0)
    float imgW = static_cast<float>(tileset.imageWidth);
    float imgH = static_cast<float>(tileset.imageHeight);

    *u0 = pixelX / imgW;
    *v0 = pixelY / imgH;
    *u1 = (pixelX + tileset.tileWidth) / imgW;
    *v1 = (pixelY + tileset.tileHeight) / imgH;
}

//------------------------------------------------------------------------------
// Load tileset image as raylib texture
//------------------------------------------------------------------------------
Texture2D loadTilesetTexture(const TmxTileset& tileset, const std::string& basePath) {
    Texture2D texture = {0};

    if (tileset.imageSource.empty()) {
        TraceLog(LOG_WARNING, "Tileset has no image source");
        return texture;
    }

    // Resolve image path relative to basePath
    fs::path imagePath;
    if (fs::path(tileset.imageSource).is_absolute()) {
        imagePath = tileset.imageSource;
    } else {
        imagePath = fs::path(basePath) / tileset.imageSource;
    }

    if (!fs::exists(imagePath)) {
        TraceLog(LOG_WARNING, "Tileset image not found: %s", imagePath.string().c_str());
        return texture;
    }

    texture = LoadTexture(imagePath.string().c_str());
    if (texture.id == 0) {
        TraceLog(LOG_WARNING, "Failed to load tileset texture: %s", imagePath.string().c_str());
    } else {
        TraceLog(LOG_INFO, "Loaded tileset texture: %s (%dx%d)",
                 imagePath.string().c_str(), texture.width, texture.height);
    }

    return texture;
}
