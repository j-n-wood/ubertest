#include "rendering/object3d_renderer.h"
#include "raymath.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace {
// Read per-material glow flags from a glTF's `materials[i].extras.glow` (Raylib's glTF loader
// ignores extras). Returns one byte per material (1 = glows); empty on any parse failure. Glow is a
// per-material property in uber (SHADER <matIndex> diffuseglow), so only flagged submeshes emit.
std::vector<unsigned char> readGlowMaterials(const std::string& gltfPath) {
    std::vector<unsigned char> flags;
    std::ifstream f(gltfPath);
    if (!f.is_open()) return flags;
    nlohmann::json j;
    try { f >> j; } catch (...) { return flags; }
    if (!j.contains("materials") || !j["materials"].is_array()) return flags;
    for (const auto& m : j["materials"]) {
        bool glow = m.contains("extras") && m["extras"].is_object() && m["extras"].value("glow", false);
        flags.push_back(glow ? 1 : 0);
    }
    return flags;
}
}  // namespace

Object3DRenderer::~Object3DRenderer() {
    destroy();
}

void Object3DRenderer::build(SceneRenderer* renderer, const std::vector<ObjectDefinition>& defs,
                             const std::string& assetPath) {
    if (built_) return;
    for (const ObjectDefinition& d : defs) {
        if (d.model.empty()) continue;                 // no mesh (e.g. a marker-only def)
        if (models_.count(d.renderIndex)) continue;    // shared model
        // Note: shadow-only defs ARE loaded (their mesh casts a shadow) but are skipped in render().
        Model m = LoadModel((assetPath + "/" + d.model).c_str());
        if (m.meshCount == 0) {
            TraceLog(LOG_WARNING, "Object3DRenderer: failed to load %s", d.model.c_str());
            continue;
        }
        sceneRendererApplyShader(renderer, &m);
        models_[d.renderIndex] = m;
        // Glow is per-material: cache which of this model's materials self-illuminate (from the
        // glTF material extras). Only relevant for glow defs.
        if (d.drawType == ObjectDrawType::Glow) {
            std::vector<unsigned char> flags = readGlowMaterials(assetPath + "/" + d.model);
            // Raylib's glTF loader may prepend a default material at index 0, shifting the glTF
            // materials to 1..N. meshMaterial[] indexes raylib's array, so align the (glTF-indexed)
            // flags to it by prepending a non-glow entry when the counts differ by exactly one.
            if ((int)flags.size() + 1 == m.materialCount) flags.insert(flags.begin(), 0);
            bool any = false;
            for (unsigned char b : flags) any = any || b;
            if (!any) TraceLog(LOG_WARNING, "Object3DRenderer: glow def '%s' has no material flagged "
                               "\"glow\":true in %s — nothing will glow", d.id.c_str(), d.model.c_str());
            glowMats_[d.renderIndex] = std::move(flags);
        }
    }
    shader_ = sceneRendererGetShader(renderer);
    emissiveLoc_ = GetShaderLocation(shader_, "emissive");
    emissiveColorLoc_ = GetShaderLocation(shader_, "emissiveColor");
    emissiveTintLoc_ = GetShaderLocation(shader_, "emissiveTint");
    built_ = true;
    TraceLog(LOG_INFO, "Object3DRenderer: loaded %zu object models", models_.size());
}

void Object3DRenderer::render(const std::vector<ObjectInstance>& instances) const {
    if (!built_) return;
    for (const ObjectInstance& inst : instances) {
        if (!inst.def || !inst.alive) continue;
        if (inst.def->drawType == ObjectDrawType::ShadowOnly) continue;  // shadow pass only
        auto it = models_.find(inst.def->renderIndex);
        if (it == models_.end()) continue;
        const Model& model = it->second;

        // Replicate DrawModelEx's transform (scale -> rotate about +Y -> translate, then
        // model.transform) so we can DrawMesh each submesh individually and toggle glow PER MATERIAL.
        // The instance position already carries height (objects float), so no floor-seating. Facing is
        // a game-frame angle about up; the game->render depth negation flips its sign (see
        // render_frame_rotation_inversion), so rotate about +Y by -(facing + spin).
        float rotDeg = -(inst.facingRad + inst.spinAngle) * RAD2DEG;
        float s = inst.def->scale;
        Matrix mtx = MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s),
                                    MatrixRotate((Vector3){0, 1, 0}, rotDeg * DEG2RAD)),
                                    MatrixTranslate(inst.position.x, inst.position.y, inst.position.z));
        mtx = MatrixMultiply(model.transform, mtx);

        // Glow is per-material: only the flagged submeshes self-illuminate (e.g. the alert light's
        // blue-glass lens, not its metal housing). Alert-sourced defs tint with the ship's pulsing
        // band colour; other glow defs use their own material colour.
        const bool defGlow = (inst.def->drawType == ObjectDrawType::Glow);
        const bool alert = defGlow && (inst.def->glowSource == GlowSource::Alert);
        const std::vector<unsigned char>* flags = nullptr;
        if (defGlow) {
            auto gi = glowMats_.find(inst.def->renderIndex);
            if (gi != glowMats_.end()) flags = &gi->second;
        }

        for (int i = 0; i < model.meshCount; ++i) {
            int matIdx = model.meshMaterial[i];
            bool matGlows = flags && matIdx >= 0 && matIdx < (int)flags->size() && (*flags)[matIdx];
            if (emissiveLoc_ >= 0) {
                float e = matGlows ? inst.def->glowIntensity : 0.0f;
                SetShaderValue(shader_, emissiveLoc_, &e, SHADER_UNIFORM_FLOAT);
                if (matGlows) {
                    int tint = alert ? 1 : 0;
                    if (emissiveTintLoc_ >= 0) SetShaderValue(shader_, emissiveTintLoc_, &tint, SHADER_UNIFORM_INT);
                    if (alert && emissiveColorLoc_ >= 0)
                        SetShaderValue(shader_, emissiveColorLoc_, &alertGlow_, SHADER_UNIFORM_VEC3);
                }
            }
            DrawMesh(model.meshes[i], model.materials[matIdx], mtx);
        }
    }
    // Leave emissive at 0 so the shared scene shader doesn't glow other geometry (tiles, units).
    if (emissiveLoc_ >= 0) {
        float zero = 0.0f;
        SetShaderValue(shader_, emissiveLoc_, &zero, SHADER_UNIFORM_FLOAT);
    }
}

void Object3DRenderer::renderDepth(const std::vector<ObjectInstance>& instances) const {
    if (!built_) return;
    for (const ObjectInstance& inst : instances) {
        if (!inst.def || !inst.alive || !inst.def->castsShadow) continue;
        auto it = models_.find(inst.def->renderIndex);
        if (it == models_.end()) continue;   // no model loaded (e.g. fan.gltf missing)
        float rotDeg = -(inst.facingRad + inst.spinAngle) * RAD2DEG;   // same transform as render()
        float s = inst.def->scale;
        DrawModelEx(it->second, inst.position, (Vector3){0, 1, 0}, rotDeg, (Vector3){s, s, s}, WHITE);
    }
}

void Object3DRenderer::destroy() {
    if (!built_) return;
    for (auto& [idx, m] : models_) UnloadModel(m);
    models_.clear();
    built_ = false;
}
