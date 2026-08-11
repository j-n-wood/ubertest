#include "level/object_manager.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
ObjectDrawType parseDrawType(const std::string& s) {
    if (s == "glow") return ObjectDrawType::Glow;
    if (s == "shadowOnly") return ObjectDrawType::ShadowOnly;
    return ObjectDrawType::Diffuse;
}
}  // namespace

void ObjectManager::loadDefinitions(const std::string& dir) {
    defs_.clear();
    byRenderIndex_.clear();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        TraceLog(LOG_WARNING, "ObjectManager: no object definitions dir: %s", dir.c_str());
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        if (!f.is_open()) continue;
        json j;
        try { f >> j; } catch (...) {
            TraceLog(LOG_WARNING, "ObjectManager: bad JSON: %s", entry.path().string().c_str());
            continue;
        }
        ObjectDefinition d;
        d.id = j.value("id", entry.path().stem().string());
        d.renderIndex = j.value("renderIndex", -1);
        d.model = j.value("model", "");
        d.scale = j.value("scale", 1.0f);
        d.floating = j.value("floating", true);
        d.collisionRadius = j.value("collisionRadius", 0.0f);
        d.destructible = j.value("destructible", false);
        d.health = j.value("health", 50);
        d.explodeSize = j.value("explodeSize", 0.0f);
        d.drawType = parseDrawType(j.value("drawtype", "diffuse"));
        d.castsShadow = j.value("castsShadow", false);
        if (j.contains("spin") && j["spin"].is_array() && j["spin"].size() >= 3) {
            d.spin = {j["spin"][0].get<float>(), j["spin"][1].get<float>(), j["spin"][2].get<float>()};
        }
        if (d.renderIndex >= 0) byRenderIndex_[d.renderIndex] = static_cast<int>(defs_.size());
        defs_.push_back(std::move(d));
    }
    TraceLog(LOG_INFO, "ObjectManager: loaded %zu object definitions from %s", defs_.size(), dir.c_str());
}

const ObjectDefinition* ObjectManager::defForRenderIndex(int renderIndex) const {
    auto it = byRenderIndex_.find(renderIndex);
    return (it != byRenderIndex_.end()) ? &defs_[it->second] : nullptr;
}

void ObjectManager::setInstances(const std::vector<ObjectSpec>& specs) {
    instances_.clear();
    for (const ObjectSpec& s : specs) {
        const ObjectDefinition* def = defForRenderIndex(s.renderIndex);
        if (!def) continue;   // unknown renderIndex (e.g. MD2 organic — no definition): skip
        ObjectInstance inst;
        inst.def = def;
        inst.position = s.position;
        inst.facingRad = s.rotation.z;
        // Instance spin (from the map record) wins; fall back to the definition's default spin.
        inst.spinRad = (std::fabs(s.spin.z) > 1e-6f) ? s.spin.z : def->spin.z;
        inst.health = def->health;
        instances_.push_back(inst);
    }
}

void ObjectManager::update(float dt) {
    for (ObjectInstance& inst : instances_) {
        if (inst.spinRad != 0.0f) inst.spinAngle += inst.spinRad * dt;
    }
}
