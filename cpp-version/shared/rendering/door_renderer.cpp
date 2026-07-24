#include "rendering/door_renderer.h"
#include "level/level_renderer.h"  // createLevelTileMeshCustom / createLevelTileModel / freeLevelRenderData

#include <algorithm>
#include <cmath>

namespace {
// 0-based tileset (local) IDs of the closed door tile for each orientation; frames
// run base..base+4 (closed -> open). GID = firstGid + base + frame.
constexpr int DOOR_H_BASE_LOCAL = 18;
constexpr int DOOR_V_BASE_LOCAL = 27;
constexpr int DOOR_FRAME_MAX = 4;  // frames 0..4
}  // namespace

DoorRenderer::~DoorRenderer() {
    destroy();
}

int DoorRenderer::frameFor(const DoorView& v) const {
    int f = static_cast<int>(std::lround(v.openFraction * DOOR_FRAME_MAX));
    return std::clamp(f, 0, DOOR_FRAME_MAX);
}

void DoorRenderer::build(const TmxLevel& level, const TmxTileset& tileset,
                         const TilePropertiesConfig& props, Texture2D atlas,
                         Texture2D bump, SceneRenderer* renderer,
                         const std::vector<DoorView>& views) {
    destroy();
    tileset_ = tileset;
    props_ = props;
    atlas_ = atlas;
    bump_ = bump;
    renderer_ = renderer;

    // Door-only level: same dimensions/tileset, every cell empty except door cells.
    doorLevel_ = level;
    std::fill(doorLevel_.tiles.begin(), doorLevel_.tiles.end(), 0);

    frameCache_.clear();
    cellIndex_.clear();
    orient_.clear();
    for (const DoorView& v : views) {
        int idx = v.row * level.width + v.col;
        int frame = frameFor(v);
        int base = (v.orientation == DoorOrientation::Horizontal) ? DOOR_H_BASE_LOCAL
                                                                  : DOOR_V_BASE_LOCAL;
        if (idx >= 0 && idx < static_cast<int>(doorLevel_.tiles.size())) {
            doorLevel_.tiles[idx] = tileset_.firstGid + base + frame;
        }
        frameCache_.push_back(frame);
        cellIndex_.push_back(idx);
        orient_.push_back(v.orientation);
    }

    rebuildModel();
}

void DoorRenderer::rebuildModel() {
    // UnloadModel keeps the shared atlas/bump/shader alive (raylib only frees the
    // mesh + maps array), so this is safe to call repeatedly.
    freeLevelRenderData(&data_);

    int bumpW = bump_.id > 0 ? bump_.width : 0;
    int bumpH = bump_.id > 0 ? bump_.height : 0;
    data_.tileMesh = createLevelTileMeshCustom(doorLevel_, tileset_, props_, bumpW, bumpH, 1.0f);
    if (data_.tileMesh.vertexCount > 0) {
        data_.tileModel = createLevelTileModel(data_.tileMesh, atlas_, bump_, renderer_);
        data_.meshValid = true;
    }
}

void DoorRenderer::update(const std::vector<DoorView>& views) {
    // A count mismatch means the door set changed (level switch) — build() handles
    // that; skip here to keep the per-view arrays aligned.
    if (views.size() != frameCache_.size()) return;

    bool changed = false;
    for (size_t i = 0; i < views.size(); i++) {
        int f = frameFor(views[i]);
        if (f == frameCache_[i]) continue;
        frameCache_[i] = f;
        int base = (orient_[i] == DoorOrientation::Horizontal) ? DOOR_H_BASE_LOCAL
                                                               : DOOR_V_BASE_LOCAL;
        int idx = cellIndex_[i];
        if (idx >= 0 && idx < static_cast<int>(doorLevel_.tiles.size())) {
            doorLevel_.tiles[idx] = tileset_.firstGid + base + f;
        }
        changed = true;
    }
    if (changed) rebuildModel();
}

void DoorRenderer::render() const {
    if (data_.meshValid) {
        DrawModel(data_.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
    }
}

void DoorRenderer::destroy() {
    freeLevelRenderData(&data_);
}
