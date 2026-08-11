#include "level/level3d_loader.h"
#include "world_scale.h"
#include "rendering/glass_render.h"
#include "raylib.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <filesystem>
#include <cmath>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string bundleDir(const std::string& assetPath, int n) {
    return assetPath + "/ships/ship1/levels3d/level_" + std::to_string(n);
}

// entities.json waypoints -> data.waypointPositions / waypointAdjacency / waypointLinks. Neighbours
// are stored as waypoint ids (0-padded); we map them to indices. `0` is padding unless a real
// waypoint has id 0.
void loadWaypoints(const std::string& path, LevelRenderData& data) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    json doc;
    try { f >> doc; } catch (...) { return; }
    if (!doc.contains("waypoints")) return;
    const json& wps = doc["waypoints"];

    std::unordered_map<int, int> idToIdx;
    for (int i = 0; i < (int)wps.size(); i++) idToIdx[wps[i].value("id", -1)] = i;
    const bool zeroIsReal = idToIdx.count(0) > 0;

    data.waypointPositions.clear();
    data.waypointLinks.clear();
    data.waypointAdjacency.assign(wps.size(), {});

    for (const auto& w : wps) {
        const json& p = w["pos"];
        data.waypointPositions.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
    }
    for (int i = 0; i < (int)wps.size(); i++) {
        if (!wps[i].contains("neighbors")) continue;
        for (const auto& n : wps[i]["neighbors"]) {
            int id = n.get<int>();
            if (id == 0 && !zeroIsReal) continue;        // padding
            auto it = idToIdx.find(id);
            if (it == idToIdx.end() || it->second == i) continue;
            data.waypointAdjacency[i].push_back(it->second);
            if (i < it->second) data.waypointLinks.push_back({i, it->second});  // dedupe undirected
        }
    }
}

}  // namespace

bool load3DLevel(const std::string& assetPath, int levelNumber, SceneRenderer* renderer,
                 LevelRenderData& data) {
    const std::string dir = bundleDir(assetPath, levelNumber);
    const std::string stem = "/level_" + std::to_string(levelNumber);
    const std::string gltf = dir + stem + ".gltf";
    if (!fs::exists(gltf)) return false;

    data.tileModel = LoadModel(gltf.c_str());
    if (data.tileModel.meshCount == 0) return false;
    sceneRendererApplyShader(renderer, &data.tileModel);
    data.meshValid = true;

    // Manifest: tag glass meshes (drawtype 5) + reconfigure their material for the transparent
    // env-mapped glass pass. The manifest mesh order matches the GLTF/tileModel mesh order.
    data.glassMeshIndices.clear();
    {
        std::ifstream mf(dir + stem + ".manifest.json");
        if (mf.is_open()) {
            json mdoc;
            try { mf >> mdoc; } catch (...) {}
            if (mdoc.contains("meshes")) {
                for (const auto& m : mdoc["meshes"]) {
                    if (!m.value("glass", false)) continue;
                    int idx = m.value("index", -1);
                    if (idx < 0 || idx >= data.tileModel.meshCount) continue;
                    data.glassMeshIndices.push_back(idx);
                    configureGlassMaterial(&data.tileModel.materials[data.tileModel.meshMaterial[idx]]);
                }
            }
        }
    }

    BoundingBox bb = GetModelBoundingBox(data.tileModel);
    data.boundsMin = bb.min;
    data.boundsMax = bb.max;

    loadWaypoints(dir + stem + ".entities.json", data);

    TraceLog(LOG_INFO, "Loaded 3D bundle for level %d: %d meshes, %zu waypoints, %zu glass",
             levelNumber, data.tileModel.meshCount, data.waypointPositions.size(),
             data.glassMeshIndices.size());
    return true;
}

void load3DLevelDoors(const std::string& assetPath, int levelNumber, std::vector<DoorSpec>& out) {
    std::ifstream f(bundleDir(assetPath, levelNumber) + "/level_" + std::to_string(levelNumber) + ".entities.json");
    if (!f.is_open()) return;
    json doc;
    try { f >> doc; } catch (...) { return; }
    if (!doc.contains("doors")) return;
    for (const auto& d : doc["doors"]) {
        const json& p = d["pos"];
        DoorSpec s;
        s.physicsCenter = {p[0].get<float>(), p[2].get<float>()};   // domain X, Z (metres)
        float rz = (d.contains("rot") && d["rot"].size() >= 3) ? d["rot"][2].get<float>() : 0.0f;
        s.orientation = (std::fabs(rz) > PI * 0.25f) ? DoorOrientation::Vertical
                                                     : DoorOrientation::Horizontal;
        // The domain door `size` is a small placeholder; use a tile-sized, oriented footprint so the
        // collision/sensor spans the doorway (matches the TMX doors' sizing).
        s.size = (s.orientation == DoorOrientation::Horizontal)
                     ? Vector2{1.0f * WORLD_SCALE, 0.5f * WORLD_SCALE}
                     : Vector2{0.5f * WORLD_SCALE, 1.0f * WORLD_SCALE};
        s.initialClosed = (d.value("state", 0) == 0);
        out.push_back(s);
    }
}

void load3DLevelChargers(const std::string& assetPath, int levelNumber, std::vector<ChargerSpec>& out) {
    std::ifstream f(bundleDir(assetPath, levelNumber) + "/level_" + std::to_string(levelNumber) + ".entities.json");
    if (!f.is_open()) return;
    json doc;
    try { f >> doc; } catch (...) { return; }
    if (!doc.contains("chargers")) return;
    for (const auto& c : doc["chargers"]) {
        const json& p = c["pos"];
        ChargerSpec s;
        s.physicsCenter = {p[0].get<float>(), p[2].get<float>()};   // domain X, Z (metres)
        s.size = {1.0f * WORLD_SCALE, 1.0f * WORLD_SCALE};          // one-tile footprint
        out.push_back(s);
    }
}

void load3DLevelConsoles(const std::string& assetPath, int levelNumber, std::vector<ConsoleSpec>& out) {
    std::ifstream f(bundleDir(assetPath, levelNumber) + "/level_" + std::to_string(levelNumber) + ".entities.json");
    if (!f.is_open()) return;
    json doc;
    try { f >> doc; } catch (...) { return; }
    if (!doc.contains("consoles")) return;
    for (const auto& c : doc["consoles"]) {
        const json& p = c["pos"];
        ConsoleSpec s;
        s.physicsCenter = {p[0].get<float>(), p[2].get<float>()};   // domain X, Z (metres)
        s.facingRad = (c.contains("rot") && c["rot"].size() >= 3) ? c["rot"][2].get<float>() : 0.0f;
        out.push_back(s);
    }
}

void load3DLevelObjects(const std::string& assetPath, int levelNumber, std::vector<ObjectSpec>& out) {
    std::ifstream f(bundleDir(assetPath, levelNumber) + "/level_" + std::to_string(levelNumber) + ".entities.json");
    if (!f.is_open()) return;
    json doc;
    try { f >> doc; } catch (...) { return; }
    if (!doc.contains("objects")) return;
    auto v3 = [](const json& a) -> Vector3 {
        return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
    };
    for (const auto& o : doc["objects"]) {
        ObjectSpec s;
        s.renderIndex = o.value("renderIndex", -1);
        if (o.contains("pos")) s.position = v3(o["pos"]);   // full render-space position (X, Y up, Z)
        if (o.contains("rot")) s.rotation = v3(o["rot"]);
        if (o.contains("spin")) s.spin = v3(o["spin"]);
        out.push_back(s);
    }
}

bool load3DLevelCollision(const std::string& assetPath, int levelNumber, Collision3D& out) {
    std::ifstream f(bundleDir(assetPath, levelNumber) + "/level_" + std::to_string(levelNumber) + ".collision.json");
    if (!f.is_open()) return false;
    json doc;
    try { f >> doc; } catch (...) { return false; }

    if (doc.contains("polygons")) {
        for (const auto& p : doc["polygons"]) {
            std::vector<Vector2> verts;
            for (const auto& v : p["vertices"]) verts.push_back({v[0].get<float>(), v[1].get<float>()});
            if (verts.size() >= 3) out.polygons.push_back(std::move(verts));
        }
    }
    if (doc.contains("chains")) {
        for (const auto& c : doc["chains"]) {
            Collision3D::Chain ch;
            for (const auto& v : c["vertices"]) ch.verts.push_back({v[0].get<float>(), v[1].get<float>()});
            ch.loop = c.value("loop", false);
            if (ch.verts.size() >= 2) out.chains.push_back(std::move(ch));
        }
    }
    return !out.polygons.empty() || !out.chains.empty();
}

bool load3DLevelTransporters(const std::string& assetPath, std::vector<TransporterSpec>& out) {
    std::ifstream f(assetPath + "/ships/ship1/levels3d/transporters.json");
    if (!f.is_open()) return false;
    json doc;
    try { f >> doc; } catch (...) { return false; }
    if (!doc.contains("transporters")) return false;
    for (const auto& t : doc["transporters"]) {
        const json& p = t["pos"];
        TransporterSpec s;
        s.id = t.value("id", 0);
        s.deck = t.value("deck", 0);
        s.pos = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
        s.levelUp = t.value("levelUp", -1);
        s.levelDown = t.value("levelDown", -1);
        s.liftRow = t.value("liftRow", 0);
        out.push_back(s);
    }
    return !out.empty();
}
