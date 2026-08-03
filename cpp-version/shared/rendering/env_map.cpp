#include "env_map.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Clamp a JSON number to 0..1 (env colours are normalised).
float clamp01(const json& v) {
    double d = v.is_number() ? v.get<double>() : 1.0;
    if (d < 0.0) d = 0.0;
    if (d > 1.0) d = 1.0;
    return static_cast<float>(d);
}

unsigned char toByte(float f) {
    return static_cast<unsigned char>(f * 255.0f + 0.5f);
}

}  // namespace

std::vector<EnvMapEntry> envMapReadExtras(const std::string& gltfPath) {
    std::vector<EnvMapEntry> entries;

    // A binary/unreadable/malformed file simply means "no env maps".
    std::ifstream in(gltfPath);
    if (!in) return entries;

    json doc;
    try {
        in >> doc;
    } catch (const std::exception&) {
        return entries;
    }

    auto matsIt = doc.find("materials");
    if (matsIt == doc.end() || !matsIt->is_array()) return entries;

    const json& materials = *matsIt;
    for (size_t i = 0; i < materials.size(); ++i) {
        const json& mat = materials[i];
        auto extrasIt = mat.find("extras");
        if (extrasIt == mat.end() || !extrasIt->is_object()) continue;

        const json& extras = *extrasIt;
        auto texIt = extras.find("envTexture");
        if (texIt == extras.end() || !texIt->is_string()) continue;

        EnvMapEntry e;
        e.gltfMaterialIndex = static_cast<int>(i);
        e.texturePath = texIt->get<std::string>();
        if (e.texturePath.empty()) continue;

        e.color[0] = e.color[1] = e.color[2] = e.color[3] = 1.0f;
        auto colIt = extras.find("envColor");
        if (colIt != extras.end() && colIt->is_array() && colIt->size() >= 3) {
            const json& c = *colIt;
            e.color[0] = clamp01(c[0]);
            e.color[1] = clamp01(c[1]);
            e.color[2] = clamp01(c[2]);
            e.color[3] = (c.size() >= 4) ? clamp01(c[3]) : 1.0f;
        }

        e.intensity = 1.0f;
        auto intIt = extras.find("envIntensity");
        if (intIt != extras.end() && intIt->is_number()) e.intensity = intIt->get<float>();

        entries.push_back(std::move(e));
    }

    return entries;
}

void envMapApplyExtras(Model& model, const std::string& gltfPath) {
    const fs::path baseDir = fs::path(gltfPath).parent_path();

    for (const EnvMapEntry& e : envMapReadExtras(gltfPath)) {
        // Raylib's glTF loader prepends a default material at index 0, so glTF material i maps to
        // model.materials[i + 1] (see rmodels.c LoadGLTF).
        const int matIndex = e.gltfMaterialIndex + 1;
        if (matIndex < 0 || matIndex >= model.materialCount) continue;

        const std::string texPath = (baseDir / e.texturePath).string();
        Texture2D tex = LoadTexture(texPath.c_str());
        if (!IsTextureValid(tex)) {
            TraceLog(LOG_WARNING, "EnvMap: could not load env texture '%s' (glTF material %d of %s)",
                     texPath.c_str(), e.gltfMaterialIndex, gltfPath.c_str());
            continue;
        }
        // Spherical lookup samples across the whole [0,1] range: clamp to avoid a wrap seam,
        // bilinear for a smooth reflection.
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);

        MaterialMap& envSlot = model.materials[matIndex].maps[MATERIAL_MAP_METALNESS];
        envSlot.texture = tex;  // -> texture1 in the lighting shader
        // envColor (legacy SPECULARITY) modulates the env sample; Raylib surfaces this map's color
        // to the shader as `colSpecular`.
        envSlot.color = Color{toByte(e.color[0]), toByte(e.color[1]), toByte(e.color[2]), toByte(e.color[3])};
        // envIntensity rides in the map's scalar value; the unit draw loop reads it per-mesh.
        envSlot.value = e.intensity;

        TraceLog(LOG_INFO, "EnvMap: glTF material %d <- '%s' (intensity %.2f)",
                 e.gltfMaterialIndex, e.texturePath.c_str(), e.intensity);
    }
}
