#include "archetile_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

//------------------------------------------------------------------------------
// Archetile Parser
//------------------------------------------------------------------------------

bool parseArchetilesFile(std::string_view path, std::vector<Archetile>& outArchetiles) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open archetiles file: " << path << std::endl;
        return false;
    }

    outArchetiles.clear();

    std::string line;

    // First line: "number <count>"
    if (!std::getline(file, line)) {
        std::cerr << "Archetiles file is empty" << std::endl;
        return false;
    }
    line = trim(line);

    std::istringstream headerIss(line);
    std::string keyword;
    int count;
    headerIss >> keyword >> count;

    if (keyword != "number") {
        std::cerr << "Expected 'number' header, got: " << keyword << std::endl;
        return false;
    }

    outArchetiles.reserve(count);

    // Parse each archetile
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Name and index line: "<Name> <Index>"
        std::istringstream nameIss(line);
        Archetile arch;
        nameIss >> arch.name >> arch.index;

        // Vertex count line
        if (!std::getline(file, line)) break;
        line = trim(line);
        int vertexCount = std::stoi(line);

        arch.tile.vertices.resize(vertexCount);

        // Read vertex positions
        for (int i = 0; i < vertexCount; ++i) {
            if (!std::getline(file, line)) break;
            line = trim(line);
            std::istringstream vss(line);
            vss >> arch.tile.vertices[i].position.x
                >> arch.tile.vertices[i].position.y
                >> arch.tile.vertices[i].position.z;
        }

        // Read first UV set
        for (int i = 0; i < vertexCount; ++i) {
            if (!std::getline(file, line)) break;
            line = trim(line);
            std::istringstream uvss(line);
            uvss >> arch.tile.vertices[i].uv1.x
                 >> arch.tile.vertices[i].uv1.y;
        }

        // Read second UV set
        for (int i = 0; i < vertexCount; ++i) {
            if (!std::getline(file, line)) break;
            line = trim(line);
            std::istringstream uvss(line);
            uvss >> arch.tile.vertices[i].uv2.x
                 >> arch.tile.vertices[i].uv2.y;
        }

        // Texture indices and tile type line
        if (!std::getline(file, line)) break;
        line = trim(line);
        std::istringstream texIss(line);
        int tex1, tex2, tileType;
        texIss >> tex1 >> tex2 >> tileType;
        arch.tile.textureIndex1 = tex1;
        arch.tile.textureIndex2 = tex2;
        arch.tile.properties.tileType = tileType;

        // Read optional properties until "End"
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty()) continue;
            if (line == "End") break;

            std::istringstream propIss(line);
            std::string propName;
            propIss >> propName;

            if (propName == "DiffuseColour") {
                propIss >> arch.tile.properties.diffuseColour.x
                        >> arch.tile.properties.diffuseColour.y
                        >> arch.tile.properties.diffuseColour.z;
            }
            else if (propName == "SpecularColour") {
                propIss >> arch.tile.properties.specularColour.x
                        >> arch.tile.properties.specularColour.y
                        >> arch.tile.properties.specularColour.z;
            }
            else if (propName == "EffectTexture") {
                propIss >> arch.tile.properties.effectTexture;
            }
            else if (propName == "EffectRenderMode") {
                propIss >> arch.tile.properties.effectRenderMode;
            }
            else if (propName == "EffectBlendSource") {
                propIss >> arch.tile.properties.effectBlendSource;
            }
            else if (propName == "EffectBlendDest") {
                propIss >> arch.tile.properties.effectBlendDest;
            }
            else if (propName == "AdditiveBlend") {
                arch.tile.properties.additiveBlend = true;
            }
            else if (propName == "AlphaBlend") {
                arch.tile.properties.alphaBlend = true;
            }
            else if (propName == "TEXROTATE" || propName == "TileType") {
                // TileType already parsed, TEXROTATE is a flag
                if (propName == "TEXROTATE") {
                    arch.tile.properties.texRotate = true;
                } else {
                    int tt;
                    propIss >> tt;
                    arch.tile.properties.tileType = tt;
                }
            }
        }

        outArchetiles.push_back(std::move(arch));
    }

    return true;
}

//------------------------------------------------------------------------------
// Archetile Expansion
//------------------------------------------------------------------------------

Tile expandArchetile(const Archetile& archetype, float xOffset, float yOffset) {
    Tile result = archetype.tile;

    // Apply position offset to all vertices
    for (auto& vertex : result.vertices) {
        vertex.position.x += xOffset;
        vertex.position.y += yOffset;
    }

    return result;
}

//------------------------------------------------------------------------------
// Archetile Cache
//------------------------------------------------------------------------------

ArchetileCache& ArchetileCache::instance() {
    static ArchetileCache cache;
    return cache;
}

bool ArchetileCache::load(std::string_view path) {
    if (loaded_) return true;

    if (!parseArchetilesFile(path, archetiles_)) {
        return false;
    }

    // Build index map
    indexMap_.clear();
    for (size_t i = 0; i < archetiles_.size(); ++i) {
        indexMap_[archetiles_[i].index] = i;
    }

    loaded_ = true;
    return true;
}

const Archetile* ArchetileCache::get(int index) const {
    auto it = indexMap_.find(index);
    if (it == indexMap_.end()) return nullptr;
    return &archetiles_[it->second];
}

void ArchetileCache::clear() {
    archetiles_.clear();
    indexMap_.clear();
    loaded_ = false;
}
