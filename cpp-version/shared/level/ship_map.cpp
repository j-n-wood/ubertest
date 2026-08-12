#include "ship_map.h"

#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace {
// Read a fractional rect {x,y,w,h} (defaults 0). Accepts "w"/"h" or "width"/"height".
Rectangle parseRect(const json& j) {
    Rectangle r;
    r.x = j.value("x", 0.0f);
    r.y = j.value("y", 0.0f);
    r.width = j.contains("width") ? j.value("width", 0.0f) : j.value("w", 0.0f);
    r.height = j.contains("height") ? j.value("height", 0.0f) : j.value("h", 0.0f);
    return r;
}
}  // namespace

bool ShipMap::load(const std::string& path) {
    loaded_ = false;
    elevators_.clear();
    decks_.clear();

    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded()) return false;

    name_ = j.value("name", "");
    image_ = j.value("image", "");
    imageLit_ = j.value("imageLit", "");

    if (j.contains("elevators") && j["elevators"].is_array()) {
        for (const auto& e : j["elevators"]) {
            elevators_.push_back(parseRect(e));
        }
    }

    if (j.contains("decks") && j["decks"].is_array()) {
        for (const auto& d : j["decks"]) {
            int level = d.value("level", -1);
            if (level < 0 || !d.contains("rects") || !d["rects"].is_array()) continue;
            std::vector<Rectangle>& rects = decks_[level];
            for (const auto& r : d["rects"]) {
                rects.push_back(parseRect(r));
            }
        }
    }

    loaded_ = true;
    return true;
}

const Rectangle* ShipMap::elevatorRect(int elevatorId) const {
    if (elevatorId < 0 || elevatorId >= static_cast<int>(elevators_.size())) return nullptr;
    return &elevators_[elevatorId];
}

const std::vector<Rectangle>* ShipMap::deckRects(int levelNumber) const {
    auto it = decks_.find(levelNumber);
    return (it != decks_.end()) ? &it->second : nullptr;
}
