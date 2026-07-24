#ifndef DOOR_RENDERER_H
#define DOOR_RENDERER_H

#include "raylib.h"
#include "level/level_types.h"
#include "level/door_manager.h"        // DoorView, DoorOrientation
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <vector>

//------------------------------------------------------------------------------
// Door presentation (interim 2D). A consumer of DoorManager::views() — kept fully
// separate from the door simulation. It renders doors as animated tiles by building
// a small "door-only" tile model through the SAME mesh/model path as the level
// tiles (so doors get identical bump/lighting), and rebuilds it only when a door's
// discrete animation frame changes. See docs/doors.md.
//------------------------------------------------------------------------------

class DoorRenderer {
public:
    DoorRenderer() = default;
    ~DoorRenderer();
    DoorRenderer(const DoorRenderer&) = delete;
    DoorRenderer& operator=(const DoorRenderer&) = delete;

    // Build the door model from the current door set (call on level load/switch).
    void build(const TmxLevel& level, const TmxTileset& tileset,
               const TilePropertiesConfig& props, Texture2D atlas, Texture2D bump,
               SceneRenderer* renderer, const std::vector<DoorView>& views);

    // Refresh animation: rebuild the model if any door's tile frame changed.
    void update(const std::vector<DoorView>& views);

    void render() const;
    void destroy();

private:
    void rebuildModel();
    int frameFor(const DoorView& v) const;  // openFraction -> 0..4

    TmxLevel doorLevel_{};        // all cells 0 except door cells = current-frame GID
    TmxTileset tileset_{};
    TilePropertiesConfig props_{};
    Texture2D atlas_{};
    Texture2D bump_{};
    SceneRenderer* renderer_ = nullptr;

    LevelRenderData data_{};      // holds the door mesh + model
    std::vector<int> frameCache_; // per-view current frame
    std::vector<int> cellIndex_;  // per-view tile index (row*width+col)
    std::vector<DoorOrientation> orient_;
};

#endif // DOOR_RENDERER_H
