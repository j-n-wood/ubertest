#include "rendering/door3d_renderer.h"
#include "raymath.h"

namespace {
// A fully-open door keeps its top this far above the floor rather than sinking fully under it
// (as in the uber engine) — prevents it vanishing under the floor tiles / z-fighting in the gap.
constexpr float DOOR_OPEN_RESIDUAL = 0.02f;
}

Door3DRenderer::~Door3DRenderer() {
    destroy();
}

void Door3DRenderer::build(SceneRenderer* renderer, const char* modelPath) {
    if (built_) return;

    model_ = LoadModel(modelPath);
    if (model_.meshCount == 0) {
        TraceLog(LOG_WARNING, "Door3DRenderer: failed to load door model: %s", modelPath);
        return;
    }
    // Standard glTF: raylib loads baseColor into MATERIAL_MAP_DIFFUSE and normalTexture into
    // MATERIAL_MAP_NORMAL, plus the TANGENT attribute — the scene shader bumps/lights it with no
    // special handling here.
    sceneRendererApplyShader(renderer, &model_);

    BoundingBox bb = GetModelBoundingBox(model_);
    height_ = bb.max.y - bb.min.y;
    built_ = true;
    TraceLog(LOG_INFO, "Door3DRenderer: loaded %s (height %.2f m, %d meshes)",
             modelPath, height_, model_.meshCount);
}

void Door3DRenderer::render(const std::vector<DoorView>& views) const {
    if (!built_) return;

    for (const DoorView& d : views) {
        // Slide the model down into the floor as the door opens, leaving DOOR_OPEN_RESIDUAL of its
        // top above the floor at fully-open (so it never disappears under the floor tiles).
        float slideDown = (height_ - DOOR_OPEN_RESIDUAL) * d.openFraction;
        Vector3 pos = {d.worldPos.x, -slideDown, d.worldPos.y};

        // The model's leaf runs along its local Z. A horizontal door's opening spans world X, so
        // rotate it 90° about Y to lay the leaf across the opening; a vertical door needs none.
        float rotDeg = (d.orientation == DoorOrientation::Horizontal) ? 90.0f : 0.0f;
        DrawModelEx(model_, pos, (Vector3){0, 1, 0}, rotDeg, (Vector3){1, 1, 1}, WHITE);
    }
}

void Door3DRenderer::destroy() {
    if (!built_) return;
    UnloadModel(model_);
    model_ = {};
    built_ = false;
}
