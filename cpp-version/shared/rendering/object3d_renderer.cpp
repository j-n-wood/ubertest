#include "rendering/object3d_renderer.h"
#include "raymath.h"

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
    }
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
        // The instance position already carries height (objects can float), so no floor-seating.
        // Facing is a game-frame angle about up; the game->render depth negation flips its sign
        // (see docs — render_frame_rotation_inversion), so rotate about +Y by -(facing + spin).
        float rotDeg = -(inst.facingRad + inst.spinAngle) * RAD2DEG;
        float s = inst.def->scale;
        DrawModelEx(it->second, inst.position, (Vector3){0, 1, 0}, rotDeg, (Vector3){s, s, s}, WHITE);
    }
}

void Object3DRenderer::castShadows(const ShadowRenderer& sr, const std::vector<ObjectInstance>& instances) const {
    if (!built_ || !sr.ready()) return;
    for (const ObjectInstance& inst : instances) {
        if (!inst.def || !inst.alive || !inst.def->castsShadow) continue;
        auto it = models_.find(inst.def->renderIndex);
        if (it == models_.end()) continue;   // no model loaded (e.g. fan.gltf not yet converted)
        float rotDeg = -(inst.facingRad + inst.spinAngle) * RAD2DEG;   // same transform as render()
        float s = inst.def->scale;
        sr.drawModel(it->second, inst.position, rotDeg, (Vector3){s, s, s});
    }
}

void Object3DRenderer::destroy() {
    if (!built_) return;
    for (auto& [idx, m] : models_) UnloadModel(m);
    models_.clear();
    built_ = false;
}
