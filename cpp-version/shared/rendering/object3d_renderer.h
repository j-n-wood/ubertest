#ifndef OBJECT3D_RENDERER_H
#define OBJECT3D_RENDERER_H

#include "raylib.h"
#include "level/object_manager.h"      // ObjectDefinition, ObjectInstance
#include "rendering/scene_renderer.h"  // SceneRenderer
#include "rendering/shadow_renderer.h" // ShadowRenderer
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

    void render(const std::vector<ObjectInstance>& instances) const;

    // Cast each shadow-enabled instance's silhouette via the shadow pass (call between sr.begin/end).
    // Includes shadow-only defs (fan) — their mesh is drawn ONLY here, never in render().
    void castShadows(const ShadowRenderer& sr, const std::vector<ObjectInstance>& instances) const;

    void destroy();

private:
    std::unordered_map<int, Model> models_;   // keyed by def renderIndex
    bool built_ = false;
};

#endif // OBJECT3D_RENDERER_H
