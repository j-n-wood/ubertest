#include "rendering/charger_renderer.h"
#include "level/level_renderer.h"  // createLevelTileMeshCustom / createLevelTileModel / freeLevelRenderData

#include <algorithm>

ChargerRenderer::~ChargerRenderer() {
    destroy();
}

void ChargerRenderer::buildFrameTables() {
    rowFrames_.clear();
    const int cols = tileset_.columns > 0 ? tileset_.columns : 1;
    for (const auto& [localId, tp] : tileset_.tileProperties) {
        if (tp.type != "charger") continue;
        rowFrames_[localId / cols].push_back(localId);
    }
    for (auto& [row, ids] : rowFrames_) {
        std::sort(ids.begin(), ids.end());  // column order = animation order
    }
}

int ChargerRenderer::gidFor(int authoredRow, int frameIndex) const {
    auto it = rowFrames_.find(authoredRow);
    if (it == rowFrames_.end() || it->second.empty()) return 0;
    const std::vector<int>& ids = it->second;
    int idx = frameIndex % static_cast<int>(ids.size());
    return tileset_.firstGid + ids[idx];
}

void ChargerRenderer::build(const TmxLevel& level, const TmxTileset& tileset,
                            const TilePropertiesConfig& props, Texture2D atlas,
                            Texture2D bump, SceneRenderer* renderer,
                            const std::vector<ChargerView>& views) {
    destroy();
    tileset_ = tileset;
    props_ = props;
    atlas_ = atlas;
    bump_ = bump;
    renderer_ = renderer;
    time_ = 0.0f;
    buildFrameTables();

    chargerLevel_ = level;
    std::fill(chargerLevel_.tiles.begin(), chargerLevel_.tiles.end(), 0);

    gidCache_.clear();
    cellIndex_.clear();
    authoredRow_.clear();
    const int cols = tileset_.columns > 0 ? tileset_.columns : 1;
    for (const ChargerView& v : views) {
        int idx = v.row * level.width + v.col;
        int authoredGid = (idx >= 0 && idx < static_cast<int>(level.tiles.size()))
                              ? level.tiles[idx] : 0;
        int authoredRow = (authoredGid - tileset_.firstGid) / cols;
        int gid = gidFor(authoredRow, 0);
        if (idx >= 0 && idx < static_cast<int>(chargerLevel_.tiles.size())) {
            chargerLevel_.tiles[idx] = gid;
        }
        gidCache_.push_back(gid);
        cellIndex_.push_back(idx);
        authoredRow_.push_back(authoredRow);
    }

    rebuildModel();
}

void ChargerRenderer::rebuildModel() {
    freeLevelRenderData(&data_);  // UnloadModel keeps shared atlas/bump/shader alive
    int bumpW = bump_.id > 0 ? bump_.width : 0;
    int bumpH = bump_.id > 0 ? bump_.height : 0;
    data_.tileMesh = createLevelTileMeshCustom(chargerLevel_, tileset_, props_, bumpW, bumpH, 1.0f);
    if (data_.tileMesh.vertexCount > 0) {
        data_.tileModel = createLevelTileModel(data_.tileMesh, atlas_, bump_, renderer_);
        data_.meshValid = true;
    }
}

void ChargerRenderer::update(float dt, const std::vector<ChargerView>& views) {
    if (views.size() != gidCache_.size()) return;  // door set changed -> build() handles it
    time_ += dt;
    int frameIndex = static_cast<int>(time_ / CHARGER_FRAME_TIME);

    bool changed = false;
    for (size_t i = 0; i < views.size(); i++) {
        int gid = gidFor(authoredRow_[i], frameIndex);
        if (gid == gidCache_[i]) continue;
        gidCache_[i] = gid;
        int idx = cellIndex_[i];
        if (idx >= 0 && idx < static_cast<int>(chargerLevel_.tiles.size())) {
            chargerLevel_.tiles[idx] = gid;
        }
        changed = true;
    }
    if (changed) rebuildModel();
}

void ChargerRenderer::render() const {
    if (data_.meshValid) {
        DrawModel(data_.tileModel, (Vector3){0, 0, 0}, 1.0f, WHITE);
    }
}

void ChargerRenderer::destroy() {
    freeLevelRenderData(&data_);
}
