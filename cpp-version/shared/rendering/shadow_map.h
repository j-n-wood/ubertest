#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

#include "raylib.h"

//------------------------------------------------------------------------------
// Shadow mapping. A depth buffer is rendered from the light's point of view (an orthographic view
// straight down, following the camera), then the lighting shader projects each fragment into that
// light space and compares depths to decide if it's occluded. Unlike planar shadows this lands on
// every surface — so a ceiling fan/light shadows units passing under it. Straight-down light matches
// the scene's directional light. See shaders/lighting.fs shadowFactor() + docs/scenery_entities.md.
//------------------------------------------------------------------------------

class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void build(int resolution);
    bool ready() const;

    // Depth pass: draw all shadow casters between beginDepth()/endDepth(). The light is an
    // orthographic top-down view `extent` metres wide, centred at `center`, from `height` up.
    void beginDepth(Vector3 center, float extent, float height);
    void endDepth();

    // Bind the shadow map + light matrix onto the scene shader and turn shadowing on.
    void apply(Shader shader, float bias) const;
    // Turn shadowing off on a shader (for the depth pass itself and any non-shadowed draws).
    static void disable(Shader shader);

    void destroy();

private:
    RenderTexture2D fbo_{};   // depth-only, sampleable
    Matrix lightVP_{};
    bool built_ = false;
};

#endif // SHADOW_MAP_H
