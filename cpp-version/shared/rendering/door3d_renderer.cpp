#include "rendering/door3d_renderer.h"
#include "raymath.h"

namespace {
// A fully-open door keeps its top this far above the floor rather than sinking fully under it
// (as in the uber engine) — prevents it vanishing under the floor tiles / z-fighting in the gap.
constexpr float DOOR_OPEN_RESIDUAL = 0.05f;
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
    minY_ = bb.min.y;   // the model's base may not sit at local y=0; seat it on the floor explicitly
    built_ = true;
    TraceLog(LOG_INFO, "Door3DRenderer: loaded %s (height %.2f m, %d meshes)",
             modelPath, height_, model_.meshCount);
}

void Door3DRenderer::render(const std::vector<DoorView>& views) const {
    if (!built_) return;

    for (const DoorView& d : views) {
        // Seat the model's base on the floor (y=0) regardless of where its local origin sits
        // (bb.min.y), then slide it down as the door opens. At fully-open the top is left
        // DOOR_OPEN_RESIDUAL above the floor so it never vanishes under the floor tiles.
        //   rest  (openFraction 0): base at 0,   top at height_
        //   open  (openFraction 1): base at -(height_-RESIDUAL), top at RESIDUAL
        float slideDown = (height_ - DOOR_OPEN_RESIDUAL) * d.openFraction;
        Vector3 pos = {d.worldPos.x, -minY_ - slideDown, d.worldPos.y};

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
