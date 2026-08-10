#include "rendering/glass_render.h"
#include "rlgl.h"

namespace {
// Blue-green glass tint (uber render.cpp: glColor4f(0, 0.1, 0.2, 0.4)) as 8-bit RGBA.
const Color GLASS_TINT = {0, 26, 51, 102};
}

void configureGlassMaterial(Material* material) {
    if (!material) return;
    MaterialMap& diffuse = material->maps[MATERIAL_MAP_DIFFUSE];

    // Idempotent: several glass meshes can share ONE material (e.g. all glass walls on a deck), so
    // the loader may call this repeatedly on the same Material. Once converted, the diffuse slot is
    // the 1x1 white default — use that as a sentinel and bail, otherwise a second call would take
    // the white default as the "env image" and clobber the real envmapblue reflection.
    if (diffuse.texture.id == rlGetTextureIdDefault()) return;

    // The exporter put the env image (envmapblue) in the diffuse slot; move it to the env slot
    // (texture1 in the shader) so it reads as a spherical reflection, not a base texture.
    Texture2D env = diffuse.texture;
    if (env.id > 0) {
        SetTextureFilter(env, TEXTURE_FILTER_BILINEAR);   // smooth env, no minification aliasing
        SetTextureWrap(env, TEXTURE_WRAP_CLAMP);          // avoid the eye-space-normal wrap seam
        material->maps[MATERIAL_MAP_METALNESS].texture = env;
        material->maps[MATERIAL_MAP_METALNESS].value = 1.0f;
    }

    // Diffuse = flat blue tint over a 1x1 white texture (texelColor = white -> diffuseColor = tint).
    diffuse.texture.id = rlGetTextureIdDefault();
    diffuse.texture.width = diffuse.texture.height = 1;
    diffuse.color = GLASS_TINT;                            // colDiffuse: rgb tint + a=0.4 transparency

    // Specular colour modulates the env sample in the shader; white -> env at full brightness.
    material->maps[MATERIAL_MAP_SPECULAR].color = WHITE;
}

void beginGlassPass(Shader shader, float envIntensity) {
    rlDrawRenderBatchActive();          // flush opaque geometry before switching state
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();               // write no depth (test still on) so glass layers blend

    int on = 1;
    int useLoc = GetShaderLocation(shader, "useEnvMap");
    if (useLoc >= 0) SetShaderValue(shader, useLoc, &on, SHADER_UNIFORM_INT);
    int intLoc = GetShaderLocation(shader, "envIntensity");
    if (intLoc >= 0) SetShaderValue(shader, intLoc, &envIntensity, SHADER_UNIFORM_FLOAT);
}

void endGlassPass(Shader shader) {
    rlDrawRenderBatchActive();          // flush the glass draws under the glass state
    int off = 0;
    int useLoc = GetShaderLocation(shader, "useEnvMap");
    if (useLoc >= 0) SetShaderValue(shader, useLoc, &off, SHADER_UNIFORM_INT);
    rlEnableDepthMask();
    EndBlendMode();
}
