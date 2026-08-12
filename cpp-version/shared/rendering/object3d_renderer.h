#ifndef OBJECT3D_RENDERER_H
#define OBJECT3D_RENDERER_H

#include "raylib.h"
#include "level/object_manager.h"      // ObjectDefinition, ObjectInstance
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <string>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------------
// Scenery-object presentation (3D). Loads one model per visible object definition (keyed by
// renderIndex) and draws each ObjectInstance at its full 3D position (objects can be ceiling-height,
// not floor-seated) + facing + spin. `shadowOnly` definitions are skipped here (they belong to the
// future shadow pass). Used only in Objects3D mode. Mirrors the console/door renderers. See
// docs/scenery_entities.md.
//------------------------------------------------------------------------------

class Object3DRenderer {
public:
    Object3DRenderer() = default;
    ~Object3DRenderer();
    Object3DRenderer(const Object3DRenderer&) = delete;
    Object3DRenderer& operator=(const Object3DRenderer&) = delete;

    // Load each visible definition's model + bind the scene shader. Idempotent (built once).
    void build(SceneRenderer* renderer, const std::vector<ObjectDefinition>& defs,
               const std::string& assetPath);

    // The current ship alert glow colour (band colour × pulse), fed each frame by the game and used
    // for `glowSource: Alert` definitions (the alert lights). See scoring.h alert_band_color/pulse.
    void setAlertGlow(Vector3 rgb) { alertGlow_ = rgb; }

    void render(const std::vector<ObjectInstance>& instances) const;

    // Draw all shadow-casting instances' models normally (for the shadow-map depth pass), including
    // shadow-only defs (the fan) whose mesh is otherwise never drawn.
    void renderDepth(const std::vector<ObjectInstance>& instances) const;

    void destroy();

private:
    std::unordered_map<int, Model> models_;   // keyed by def renderIndex
    // Per-material glow flags for each glow model (keyed by def renderIndex; one byte per glTF
    // material, 1 = that submesh self-illuminates). Glow is a per-material property — only the
    // flagged materials emit (e.g. the alert light's blue-glass lens, not its metal housing) —
    // read from each material's glTF `extras.glow`. See docs/scenery_entities.md.
    std::unordered_map<int, std::vector<unsigned char>> glowMats_;
    Shader shader_{};                          // the shared scene shader (for the per-draw emissive uniforms)
    int emissiveLoc_ = -1;                     // cached "emissive" (strength) uniform location
    int emissiveColorLoc_ = -1;                // cached "emissiveColor" (tint) uniform location
    int emissiveTintLoc_ = -1;                 // cached "emissiveTint" (flag) uniform location
    Vector3 alertGlow_ = {0, 1, 0};            // current alert-band glow colour × pulse (green when calm)
    bool built_ = false;
};

#endif // OBJECT3D_RENDERER_H
