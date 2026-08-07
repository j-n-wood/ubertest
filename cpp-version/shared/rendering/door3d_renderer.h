#ifndef DOOR3D_RENDERER_H
#define DOOR3D_RENDERER_H

#include "raylib.h"
#include "level/door_manager.h"        // DoorView, DoorOrientation
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <vector>

//------------------------------------------------------------------------------
// Door presentation (3D). A consumer of DoorManager::views(), kept fully separate
// from the door simulation — like the old uber engine, each door is a metal block
// (the converted door.gltf: diffuse + tangent-space normal/bump, standard glTF) that
// slides DOWN into the floor as it opens (openFraction 0 = closed/up, 1 = open,
// leaving a thin residual sliver above the floor so it never vanishes under the floor
// tiles). The model is rendered as-is — no bespoke bump handling. Used by the game's
// Objects3D mode; the 2D tile modes keep the animated-tile DoorRenderer.
//------------------------------------------------------------------------------

class Door3DRenderer {
public:
    Door3DRenderer() = default;
    ~Door3DRenderer();
    Door3DRenderer(const Door3DRenderer&) = delete;
    Door3DRenderer& operator=(const Door3DRenderer&) = delete;

    // Load the door model (glTF) and bind the scene shader. Idempotent (safe to call
    // per level); needs a live GL context and an initialised SceneRenderer.
    void build(SceneRenderer* renderer, const char* modelPath);

    // Draw one block per door from the current view set.
    void render(const std::vector<DoorView>& views) const;

    void destroy();

private:
    Model model_{};         // converted door.gltf (metal + bump)
    float height_ = 2.0f;   // model height (world Y), for the open-slide distance
    float minY_ = 0.0f;     // model's local base Y (bb.min.y) — used to seat the base on the floor
    bool built_ = false;
};

#endif // DOOR3D_RENDERER_H
