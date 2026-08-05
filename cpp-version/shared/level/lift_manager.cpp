#include "lift_manager.h"
#include "raymath.h"
#include "world_scale.h"  // lift physicsCenter is a tile position → metric

#include <algorithm>

void LiftManager::build(const std::vector<TmxLevel>& levels) {
    stops_.clear();
    elevators_.clear();
    currentStop_ = nullptr;

    // Flatten every level's lift markers into stops. Tile (col,row) -> physics centre
    // uses the same origin-centred formula as the door/charger/console detectors.
    for (int li = 0; li < static_cast<int>(levels.size()); ++li) {
        const TmxLevel& lvl = levels[li];
        const float halfW = lvl.width * 0.5f;
        const float halfH = lvl.height * 0.5f;
        for (const TmxLift& lift : lvl.lifts) {
            LiftStop s;
            s.level = li;
            s.levelNumber = lvl.number;
            s.col = lift.col;
            s.row = lift.row;
            s.elevator = lift.elevator;
            s.stopIndex = lift.stopIndex;
            s.physicsCenter = {(lift.col + 0.5f - halfW) * WORLD_SCALE, (lift.row + 0.5f - halfH) * WORLD_SCALE};
            stops_.push_back(s);
        }
    }

    // Group stop indices by elevator and order each chain by stopIndex (ascending = up).
    for (int i = 0; i < static_cast<int>(stops_.size()); ++i) {
        elevators_[stops_[i].elevator].push_back(i);
    }
    for (auto& kv : elevators_) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [this](int a, int b) { return stops_[a].stopIndex < stops_[b].stopIndex; });
    }
}

void LiftManager::destroy() {
    stops_.clear();
    elevators_.clear();
    currentStop_ = nullptr;
}

void LiftManager::update(Vector2 playerPos, int currentLevel) {
    currentStop_ = nullptr;
    for (const LiftStop& s : stops_) {
        if (s.level != currentLevel) continue;
        if (Vector2Distance(playerPos, s.physicsCenter) <= LIFT_USE_RADIUS) {
            currentStop_ = &s;
            break;
        }
    }
}

const LiftStop* LiftManager::stepStop(const LiftStop* from, int delta) const {
    if (!from) return nullptr;
    auto it = elevators_.find(from->elevator);
    if (it == elevators_.end()) return nullptr;
    const std::vector<int>& chain = it->second;
    // Find `from`'s position in the ordered chain (pointer identity into stops_).
    for (int pos = 0; pos < static_cast<int>(chain.size()); ++pos) {
        if (&stops_[chain[pos]] == from) {
            int next = pos + delta;
            if (next < 0 || next >= static_cast<int>(chain.size())) return nullptr;
            return &stops_[chain[next]];
        }
    }
    return nullptr;
}
