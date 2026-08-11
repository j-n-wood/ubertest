#include "rendering/shadow_renderer.h"
#include "raymath.h"
#include "rlgl.h"

ShadowRenderer::~ShadowRenderer() {
    destroy();
}

void ShadowRenderer::build(const std::string& shaderDir) {
    if (built_) return;
    shader_ = LoadShader((shaderDir + "shadow.vs").c_str(), (shaderDir + "shadow.fs").c_str());
    if (shader_.id == 0) {
        TraceLog(LOG_WARNING, "ShadowRenderer: failed to load shadow shaders from %s", shaderDir.c_str());
        return;
    }
    // The vertex shader needs world/view/projection separately (to flatten in world space).
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_VIEW] = GetShaderLocation(shader_, "matView");
    shader_.locs[SHADER_LOC_MATRIX_PROJECTION] = GetShaderLocation(shader_, "matProjection");
    locLightDir_ = GetShaderLocation(shader_, "shadowLightDir");
    locFloorY_ = GetShaderLocation(shader_, "shadowFloorY");
    locColor_ = GetShaderLocation(shader_, "shadowColor");

    material_ = LoadMaterialDefault();
    material_.shader = shader_;
    built_ = true;
    TraceLog(LOG_INFO, "ShadowRenderer: loaded planar-shadow shader");
}

void ShadowRenderer::begin(Vector3 lightDir, float floorY, Color color) {
    if (!built_) return;
    Vector3 d = Vector3Normalize(lightDir);
    if (d.y > -0.05f) d.y = -0.05f;   // keep it pointing down so the plane intersection is finite
    float col[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    SetShaderValue(shader_, locLightDir_, &d, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, locFloorY_, &floorY, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locColor_, col, SHADER_UNIFORM_VEC4);

    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();   // flattened silhouettes can wind either way
    rlDisableDepthMask();         // blend onto the floor without writing depth (units draw on top)
}

void ShadowRenderer::drawModel(const Model& model, Vector3 position, float rotationDeg, Vector3 scale) const {
    if (!built_) return;
    // Mirror DrawModelEx's transform: scale, then Y-rotation, then translation, composed with the
    // model's own transform. The shadow shader flattens this onto the floor.
    Matrix mScale = MatrixScale(scale.x, scale.y, scale.z);
    Matrix mRot = MatrixRotateY(rotationDeg * DEG2RAD);
    Matrix mTrans = MatrixTranslate(position.x, position.y, position.z);
    Matrix world = MatrixMultiply(model.transform, MatrixMultiply(MatrixMultiply(mScale, mRot), mTrans));
    for (int i = 0; i < model.meshCount; ++i) {
        DrawMesh(model.meshes[i], material_, world);
    }
}

void ShadowRenderer::end() const {
    if (!built_) return;
    rlDrawRenderBatchActive();     // flush the shadow draws under this state
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

void ShadowRenderer::destroy() {
    if (!built_) return;
    // UnloadMaterial would also unload material.shader — detach our shader first so we own its unload.
    material_.shader.id = rlGetShaderIdDefault();
    UnloadMaterial(material_);
    material_ = {};
    UnloadShader(shader_);
    shader_ = {};
    built_ = false;
}
