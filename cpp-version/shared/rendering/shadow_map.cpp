#include "rendering/shadow_map.h"
#include "raymath.h"
#include "rlgl.h"

namespace {
// Texture unit for the shadow map. Must be >= MAX_MATERIAL_MAPS (12) so per-mesh material binding in
// DrawMesh never clobbers it.
constexpr int SHADOW_SLOT = 15;

// Depth-only, SAMPLEABLE framebuffer (raylib's LoadRenderTexture depth is a renderbuffer, not a
// texture). Mirrors the raylib shadowmap example.
RenderTexture2D loadDepthFBO(int width, int height) {
    RenderTexture2D t = {0};
    t.id = rlLoadFramebuffer();
    t.texture.width = width;
    t.texture.height = height;
    if (t.id > 0) {
        rlEnableFramebuffer(t.id);
        t.depth.id = rlLoadTextureDepth(width, height, false);   // false => real texture
        t.depth.width = width;
        t.depth.height = height;
        t.depth.format = 19;   // DEPTH_COMPONENT_24BIT
        t.depth.mipmaps = 1;
        rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING, "ShadowMap: framebuffer incomplete");
        rlDisableFramebuffer();
    }
    return t;
}
}  // namespace

ShadowMap::~ShadowMap() {
    destroy();
}

void ShadowMap::build(int resolution) {
    if (built_) return;
    fbo_ = loadDepthFBO(resolution, resolution);
    built_ = (fbo_.id != 0);
    if (built_) TraceLog(LOG_INFO, "ShadowMap: %dx%d depth target ready", resolution, resolution);
    else TraceLog(LOG_WARNING, "ShadowMap: failed to create depth target");
}

bool ShadowMap::ready() const { return built_; }

void ShadowMap::beginDepth(Vector3 center, float extent, float height) {
    if (!built_) return;
    Camera3D lc = {0};
    lc.position = {center.x, height, center.z};
    lc.target = {center.x, 0.0f, center.z};
    lc.up = {0.0f, 0.0f, -1.0f};        // looking straight down; up cannot be the view axis
    lc.fovy = extent;                    // orthographic: fovy = view height in world units
    lc.projection = CAMERA_ORTHOGRAPHIC;

    BeginTextureMode(fbo_);
    ClearBackground(WHITE);              // clears depth to far (1.0)
    BeginMode3D(lc);
    lightVP_ = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
}

void ShadowMap::endDepth() {
    if (!built_) return;
    EndMode3D();
    EndTextureMode();
}

void ShadowMap::apply(Shader shader, float bias) const {
    if (!built_) return;
    SetShaderValueMatrix(shader, GetShaderLocation(shader, "lightVP"), lightVP_);
    int one = 1;
    SetShaderValue(shader, GetShaderLocation(shader, "useShadows"), &one, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "shadowBias"), &bias, SHADER_UNIFORM_FLOAT);
    // Bind the depth texture to a dedicated slot and point the sampler at it.
    int slot = SHADOW_SLOT;
    SetShaderValue(shader, GetShaderLocation(shader, "shadowMap"), &slot, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(slot);
    rlEnableTexture(fbo_.depth.id);
}

void ShadowMap::disable(Shader shader) {
    int zero = 0;
    SetShaderValue(shader, GetShaderLocation(shader, "useShadows"), &zero, SHADER_UNIFORM_INT);
}

void ShadowMap::destroy() {
    if (!built_) return;
    UnloadRenderTexture(fbo_);
    fbo_ = {};
    built_ = false;
}
