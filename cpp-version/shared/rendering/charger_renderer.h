#ifndef CHARGER_RENDERER_H
#define CHARGER_RENDERER_H

#include "raylib.h"
#include "level/level_types.h"
#include "level/charger_manager.h"     // ChargerView
#include "rendering/scene_renderer.h"  // SceneRenderer
#include <vector>
#include <map>

//------------------------------------------------------------------------------
// Charger presentation (interim 2D). Consumes ChargerManager::views(); draws the
// charger tiles with a FREE-RUNNING animation (cycle the frames of the authored
// tile's row by time, independent of IDLE/CHARGING state). Same "door-only level"
// reuse of the tile mesh/model path as DoorRenderer, and the same row-preserving
// rule (a row is a colour variant — animation only moves in column/X). See
// docs/charger.md.
//------------------------------------------------------------------------------

class ChargerRenderer {
public:
    ChargerRenderer() = default;
    ~ChargerRenderer();
    ChargerRenderer(const ChargerRenderer&) = delete;
    ChargerRenderer& operator=(const ChargerRenderer&) = delete;

    // rowOffset shifts the diffuse atlas by whole colour rows to match the level's tileset row.
    void build(const TmxLevel& level, const TmxTileset& tileset,
               const TilePropertiesConfig& props, Texture2D atlas, Texture2D bump,
               SceneRenderer* renderer, const std::vector<ChargerView>& views, int rowOffset = 0);
    void update(float dt, const std::vector<ChargerView>& views);
    // Switch the tileset colour row (e.g. "lights out"): re-bake the model if it changed.
    void setRowOffset(int rowOffset);
    void render() const;
    void destroy();

private:
    void rebuildModel();
    void buildFrameTables();
    // GID for a charger in `authoredRow` at animation `frameIndex` (wraps by that
    // row's charger-frame count). Returns 0 if the row has no charger frames.
    int gidFor(int authoredRow, int frameIndex) const;

    TmxLevel chargerLevel_{};
    TmxTileset tileset_{};
    TilePropertiesConfig props_{};
    Texture2D atlas_{};
    Texture2D bump_{};
    SceneRenderer* renderer_ = nullptr;

    // Tileset row -> that row's charger local ids, sorted by column (animation order).
    std::map<int, std::vector<int>> rowFrames_;

    int rowOffset_ = 0;            // diffuse atlas colour-row shift (level tileset row / lights-out)
    LevelRenderData data_{};
    float time_ = 0.0f;            // free-running animation clock
    std::vector<int> gidCache_;    // per-view currently-shown GID
    std::vector<int> cellIndex_;   // per-view tile index (row*width+col)
    std::vector<int> authoredRow_; // per-view tileset row of the authored tile
};

#endif // CHARGER_RENDERER_H
