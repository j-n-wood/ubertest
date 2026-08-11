#ifndef CONSOLE3D_RENDERER_H
#define CONSOLE3D_RENDERER_H

#include "raylib.h"
#include "level/console_manager.h"     // ConsoleSpec
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <vector>

//------------------------------------------------------------------------------
// Console presentation (3D). Draws the converted console.gltf (metal body + a
// computer-screen top, standard glTF diffuse) at each console position, facing its
// authored orientation. A consumer of ConsoleManager::consoles(), kept separate from
// the console sim (the "use" proximity check). Used only in the game's Objects3D mode;
// the 2D tile modes render consoles as plain tiles. Mirrors Door3DRenderer.
//------------------------------------------------------------------------------

class Console3DRenderer {
public:
    Console3DRenderer() = default;
    ~Console3DRenderer();
    Console3DRenderer(const Console3DRenderer&) = delete;
    Console3DRenderer& operator=(const Console3DRenderer&) = delete;

    // Load the console model (glTF) and bind the scene shader. Idempotent (safe to call
    // per level); needs a live GL context and an initialised SceneRenderer.
    void build(SceneRenderer* renderer, const char* modelPath);

    // Draw one console model per spec, seated on the floor and rotated to its facing.
    void render(const std::vector<ConsoleSpec>& consoles) const;

    void destroy();

private:
    Model model_{};        // converted console.gltf
    float minY_ = 0.0f;    // model's local base Y (bb.min.y) — used to seat the base on the floor
    bool built_ = false;
};

#endif // CONSOLE3D_RENDERER_H
