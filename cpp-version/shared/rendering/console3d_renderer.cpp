#include "rendering/console3d_renderer.h"
#include "raymath.h"

Console3DRenderer::~Console3DRenderer() {
    destroy();
}

void Console3DRenderer::build(SceneRenderer* renderer, const char* modelPath) {
    if (built_) return;

    model_ = LoadModel(modelPath);
    if (model_.meshCount == 0) {
        TraceLog(LOG_WARNING, "Console3DRenderer: failed to load console model: %s", modelPath);
        return;
    }
    // Standard glTF: raylib loads baseColor into MATERIAL_MAP_DIFFUSE; the scene shader lights it
    // with no special handling here (the console has no bump map).
    sceneRendererApplyShader(renderer, &model_);

    BoundingBox bb = GetModelBoundingBox(model_);
    minY_ = bb.min.y;   // the model's base may not sit at local y=0; seat it on the floor explicitly
    built_ = true;
    TraceLog(LOG_INFO, "Console3DRenderer: loaded %s (%d meshes)", modelPath, model_.meshCount);
}

void Console3DRenderer::render(const std::vector<ConsoleSpec>& consoles) const {
    if (!built_) return;

    for (const ConsoleSpec& c : consoles) {
        // Seat the base on the floor (y=0) regardless of the model's local origin.
        Vector3 pos = {c.physicsCenter.x, -minY_, c.physicsCenter.y};
        float rotDeg = consoleFacingAngle(c.facingRad) * RAD2DEG;   // shared with the collision footprint
        DrawModelEx(model_, pos, (Vector3){0, 1, 0}, rotDeg, (Vector3){1, 1, 1}, WHITE);
    }
}

void Console3DRenderer::destroy() {
    if (!built_) return;
    UnloadModel(model_);
    model_ = {};
    built_ = false;
}
