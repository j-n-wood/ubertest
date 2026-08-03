#ifndef DOOR_RENDERER_H
#define DOOR_RENDERER_H

#include "raylib.h"
#include "level/level_types.h"
#include "level/door_manager.h"        // DoorView, DoorOrientation
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <vector>
#include <utility>

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

    // Build the door model from the current door set (call on level load/switch). rowOffset
    // shifts the diffuse atlas by whole colour rows to match the level's tileset row selection.
    void build(const TmxLevel& level, const TmxTileset& tileset,
               const TilePropertiesConfig& props, Texture2D atlas, Texture2D bump,
               SceneRenderer* renderer, const std::vector<DoorView>& views, int rowOffset = 0);

    // Refresh animation: rebuild the model if any door's tile frame changed.
    void update(const std::vector<DoorView>& views);

    // Switch the tileset colour row (e.g. "lights out"): re-bake the model if it changed.
    void setRowOffset(int rowOffset);

    void render() const;
    void destroy();

private:
    void rebuildModel();
    void buildFrameTables();
    // Pick the door-frame GID whose `closed` value best matches this openFraction,
    // restricted to the authored tileset row (colour) so animation only moves in X.
    int selectGid(DoorOrientation orientation, float openFraction, int authoredRow) const;

    TmxLevel doorLevel_{};        // all cells 0 except door cells = current-frame GID
    TmxTileset tileset_{};
    TilePropertiesConfig props_{};
    Texture2D atlas_{};
    Texture2D bump_{};
    SceneRenderer* renderer_ = nullptr;

    // (closed-value, local tile id) frame tables per orientation, from TSX properties.
    std::vector<std::pair<float, int>> hFrames_;
    std::vector<std::pair<float, int>> vFrames_;

    int rowOffset_ = 0;            // diffuse atlas colour-row shift (level tileset row / lights-out)
    LevelRenderData data_{};       // holds the door mesh + model
    std::vector<int> gidCache_;    // per-view currently-shown GID
    std::vector<int> cellIndex_;   // per-view tile index (row*width+col)
    std::vector<int> authoredRow_; // per-view tileset row of the authored door tile
};

#endif // DOOR_RENDERER_H
