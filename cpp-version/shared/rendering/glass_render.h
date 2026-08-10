#ifndef GLASS_RENDER_H
#define GLASS_RENDER_H

#include "raylib.h"

//------------------------------------------------------------------------------
// Glass material rendering (drawtype 5 — the converted glass tunnel). The scene
// `lighting` shader already does everything we need: tangent-space bump, spherical
// environment reflection (texture1 / metalness slot, sampled via the bump-perturbed
// eye-space normal), and per-fragment alpha. So glass is just:
//   - a material reconfig (move the env texture to the env slot, tint the diffuse
//     blue with alpha, keep the bump) — configureGlassMaterial, once after load, and
//   - a transparent pass (alpha blend + depth-write off + env on) — begin/endGlassPass,
//     drawn AFTER the opaque geometry so it tests against walls but doesn't occlude them.
// Shared so the game and the viewer render glass identically.
//------------------------------------------------------------------------------

// The tunnel's stored diffuse texture is actually the ENV image (envmapblue). Reconfigure the
// material for glass: bind that texture into the env (metalness) slot, replace the diffuse with a
// flat blue tint (alpha < 1), keep the normal/bump, and set specular white so the env shows at full.
// Idempotent-ish; call once per glass material after LoadModel.
void configureGlassMaterial(Material* material);

// Enter/leave the transparent glass pass on the scene shader: alpha blend, depth-write off (test on),
// and env mapping on at `envIntensity`. Draw glass meshes between them (DrawMesh / DrawModel).
void beginGlassPass(Shader shader, float envIntensity = 1.0f);
void endGlassPass(Shader shader);

#endif // GLASS_RENDER_H
