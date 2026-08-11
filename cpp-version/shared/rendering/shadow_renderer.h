#ifndef SHADOW_RENDERER_H
#define SHADOW_RENDERER_H

#include "raylib.h"
#include <string>

//------------------------------------------------------------------------------
// Planar projected shadows. Casters (units, scenery objects, shadow-only geometry) are drawn with a
// shadow shader that flattens their world-space silhouette onto the floor plane along a light
// direction and fills it with a flat dark, alpha-blended colour. Cheap, works on the flat floor, and
// matches the uber SHADOWCAST intent. Shared across units + objects. See docs/scenery_entities.md.
//
// Usage:  begin(lightDir, floorY, color);  drawModel(m, pos, deg, scale)*;  end();
//------------------------------------------------------------------------------

class ShadowRenderer {
public:
    ShadowRenderer() = default;
    ~ShadowRenderer();
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    void build(const std::string& shaderDir);   // load shaders/shadow.{vs,fs}
    bool ready() const { return built_; }

    // Set the projection + blend/depth state for a batch of caster draws.
    void begin(Vector3 lightDir, float floorY, Color color);
    // Draw one model's silhouette (transform mirrors DrawModelEx: scale, Y-rotation deg, translation).
    void drawModel(const Model& model, Vector3 position, float rotationDeg, Vector3 scale) const;
    void end() const;

    void destroy();

private:
    Shader shader_{};
    Material material_{};   // default material carrying the shadow shader (for DrawMesh)
    int locLightDir_ = -1;
    int locFloorY_ = -1;
    int locColor_ = -1;
    bool built_ = false;
};

#endif // SHADOW_RENDERER_H
