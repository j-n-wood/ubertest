#include "renderobject_parser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

bool startsWith(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

std::string_view trim(std::string_view str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r'))
        str.remove_prefix(1);
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n'))
        str.remove_suffix(1);
    return str;
}

std::string normalizePath(const char* path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

} // namespace

RenderObjectParseResult parseRenderObjects(std::string_view filepath) {
    RenderObjectParseResult result;

    FILE* file = fopen(std::string(filepath).c_str(), "r");
    if (!file) {
        result.errorMsg = "Failed to open file";
        return result;
    }

    char lineBuf[1024];
    int lineNum = 0;
    RenderObject* current = nullptr;
    int endCount = 0;

    while (fgets(lineBuf, sizeof(lineBuf), file)) {
        lineNum++;
        std::string_view line = trim(lineBuf);

        if (line.empty()) continue;

        // Check for END
        if (line == "END") {
            endCount++;
            // Most objects have 2 ENDs, some have 1
            if (endCount >= 2 || (current && current->type == RenderObjectType::Particles)) {
                current = nullptr;
                endCount = 0;
            }
            continue;
        }

        // Check for ENDSPRITE (for type 0)
        if (line == "ENDSPRITE" || line == "endsprite") {
            continue;
        }

        // Try to parse a new object header: <name> <index> <type>
        // Note: Some objects only have 1 END before the next object starts,
        // so we also check if this line looks like a new object header even if current != NULL
        char name[256];
        int index, type;
        if (sscanf(lineBuf, "%s %d %d", name, &index, &type) == 3 &&
            !startsWith(line, "MODEL ") && !startsWith(line, "MD2 ") &&
            !startsWith(line, "TEXTURE ") && !startsWith(line, "TEXTURES ") &&
            !startsWith(line, "EFFECTTEXTURE") && !startsWith(line, "SPECULARITY ")) {
            // This looks like a new object header - close the current one if any
            current = nullptr;
            endCount = 0;
            result.objects.push_back(RenderObject{});
            current = &result.objects.back();
            current->name = name;
            current->index = index;
            endCount = 0;

            if (strcmp(name, "OBSOLETE") == 0 || type == -1) {
                current->type = RenderObjectType::Invalid;
                current->isObsolete = true;
            } else {
                switch (type) {
                    case 0: current->type = RenderObjectType::Sprite; break;
                    case 1: current->type = RenderObjectType::ModelMDL; break;
                    case 3: current->type = RenderObjectType::Particles; break;
                    case 4: current->type = RenderObjectType::Beam; break;
                    case 5: current->type = RenderObjectType::ModelMD2; break;
                    case 8: current->type = RenderObjectType::ModelASC; break;
                    case 9: current->type = RenderObjectType::Sectional; break;
                    default: current->type = RenderObjectType::Invalid; break;
                }
            }

            // For type 1 (MDL), the model path is on the next line
            if (current->type == RenderObjectType::ModelMDL) {
                if (fgets(lineBuf, sizeof(lineBuf), file)) {
                    lineNum++;
                    char modelPath[512];
                    int flags;
                    if (sscanf(lineBuf, "%s %d", modelPath, &flags) >= 1) {
                        current->modelPath = normalizePath(modelPath);
                    }
                }
            }
            continue;
        }

        if (!current) continue;

        // Parse content based on type
        if (startsWith(line, "MODEL ")) {
            char modelPath[512];
            int flags;
            if (sscanf(lineBuf, "MODEL %s %d", modelPath, &flags) >= 1) {
                current->modelPath = normalizePath(modelPath);
            }
        }
        else if (startsWith(line, "MD2 ")) {
            char modelPath[512];
            if (sscanf(lineBuf, "MD2 %s", modelPath) == 1) {
                current->modelPath = normalizePath(modelPath);
            }
        }
        else if (startsWith(line, "TEXTURE ")) {
            int slot;
            char texPath[512];
            if (sscanf(lineBuf, "TEXTURE %d %s", &slot, texPath) == 2) {
                std::string normalized = normalizePath(texPath);
                if (slot >= static_cast<int>(current->textures.size())) {
                    current->textures.resize(slot + 1);
                }
                current->textures[slot] = normalized;
            } else if (sscanf(lineBuf, "TEXTURE %d", &slot) == 1) {
                // Beam type just has texture index
            }
        }
        else if (startsWith(line, "EFFECTTEXTURE ")) {
            int slot;
            char texPath[512];
            if (sscanf(lineBuf, "EFFECTTEXTURE %d %s", &slot, texPath) == 2) {
                std::string normalized = normalizePath(texPath);
                if (slot >= static_cast<int>(current->effectTextures.size())) {
                    current->effectTextures.resize(slot + 1);
                }
                current->effectTextures[slot] = normalized;
            }
        }
        else if (startsWith(line, "DRAWTYPE ")) {
            char dtype[64];
            if (sscanf(lineBuf, "DRAWTYPE %s", dtype) == 1) {
                current->drawType = dtype;
            }
        }
        // Ignore other keywords (TEXTURES, EFFECTTEXTURES, EFFECTTEXTURE2, SHADER, etc.)
    }

    fclose(file);
    result.success = true;
    return result;
}

const RenderObject* findRenderObject(const std::vector<RenderObject>& objects, int index) {
    for (const auto& obj : objects) {
        if (obj.index == index) {
            return &obj;
        }
    }
    return nullptr;
}
