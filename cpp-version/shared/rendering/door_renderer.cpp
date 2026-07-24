#include "rendering/door_renderer.h"
#include "level/level_renderer.h"  // createLevelTileMeshCustom / createLevelTileModel / freeLevelRenderData

#include <algorithm>
#include <cmath>

DoorRenderer::~DoorRenderer() {
    destroy();
}

void DoorRenderer::buildFrameTables() {
    hFrames_.clear();
    vFrames_.clear();
    for (const auto& [localId, tp] : tileset_.tileProperties) {
        if (!tp.isDoor()) continue;
        if (tp.orientation == "vertical") vFrames_.push_back({tp.closed, localId});
        else                               hFrames_.push_back({tp.closed, localId});
    }
    auto byClosed = [](const auto& a, const auto& b) { return a.first < b.first; };
    std::sort(hFrames_.begin(), hFrames_.end(), byClosed);
    std::sort(vFrames_.begin(), vFrames_.end(), byClosed);
}

int DoorRenderer::selectGid(DoorOrientation orientation, float openFraction,
                            int authoredRow) const {
    const auto& frames = (orientation == DoorOrientation::Vertical) ? vFrames_ : hFrames_;
    if (frames.empty()) return 0;  // no door art for this orientation
    const int cols = tileset_.columns > 0 ? tileset_.columns : 1;
    const float target = 1.0f - std::clamp(openFraction, 0.0f, 1.0f);

    // Prefer frames in the authored row (same colour); fall back to any row only if
    // that row has no matching frames (shouldn't happen with per-row door tiles).
    int best = -1;
    float bestErr = 1e9f;
    for (const auto& [closed, localId] : frames) {
        if (localId / cols != authoredRow) continue;
        float err = std::fabs(closed - target);
        if (err < bestErr) { bestErr = err; best = localId; }
    }
    if (best < 0) {
        for (const auto& [closed, localId] : frames) {
            float err = std::fabs(closed - target);
            if (err < bestErr) { bestErr = err; best = localId; }
        }
    }
    return best < 0 ? 0 : tileset_.firstGid + best;
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
    buildFrameTables();

    // Door-only level: same dimensions/tileset, every cell empty except door cells.
    doorLevel_ = level;
    std::fill(doorLevel_.tiles.begin(), doorLevel_.tiles.end(), 0);

    gidCache_.clear();
    cellIndex_.clear();
    authoredRow_.clear();
    const int cols = tileset_.columns > 0 ? tileset_.columns : 1;
    for (const DoorView& v : views) {
        int idx = v.row * level.width + v.col;
        // Row (colour) of the door tile the map authored — animation stays on it.
        int authoredGid = (idx >= 0 && idx < static_cast<int>(level.tiles.size()))
                              ? level.tiles[idx] : 0;
        int authoredRow = (authoredGid - tileset_.firstGid) / cols;
        int gid = selectGid(v.orientation, v.openFraction, authoredRow);
        if (idx >= 0 && idx < static_cast<int>(doorLevel_.tiles.size())) {
            doorLevel_.tiles[idx] = gid;
        }
        gidCache_.push_back(gid);
        cellIndex_.push_back(idx);
        authoredRow_.push_back(authoredRow);
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
    if (views.size() != gidCache_.size()) return;

    bool changed = false;
    for (size_t i = 0; i < views.size(); i++) {
        int gid = selectGid(views[i].orientation, views[i].openFraction, authoredRow_[i]);
        if (gid == gidCache_[i]) continue;
        gidCache_[i] = gid;
        int idx = cellIndex_[i];
        if (idx >= 0 && idx < static_cast<int>(doorLevel_.tiles.size())) {
            doorLevel_.tiles[idx] = gid;
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
