#pragma once

#include <string>
#include <vector>

enum class RenderObjectType {
    Sprite = 0,
    ModelMDL = 1,
    Particles = 3,
    Beam = 4,
    ModelMD2 = 5,
    ModelASC = 8,
    Sectional = 9,
    Invalid = -1
};

struct RenderObject {
    int index = 0;
    std::string name;
    RenderObjectType type = RenderObjectType::Invalid;
    std::string modelPath;
    std::vector<std::string> textures;
    std::vector<std::string> effectTextures;
    std::string drawType;
    bool isObsolete = false;
};

struct RenderObjectParseResult {
    bool success = false;
    std::string errorMsg;
    int errorLine = 0;
    std::vector<RenderObject> objects;
};

[[nodiscard]] RenderObjectParseResult parseRenderObjects(std::string_view filepath);
[[nodiscard]] const RenderObject* findRenderObject(const std::vector<RenderObject>& objects, int index);
