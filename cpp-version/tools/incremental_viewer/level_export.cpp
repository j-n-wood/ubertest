// level_export.cpp — export the loaded deck to a GLTF (one mesh per procedural shape + per tile
// batch), a per-mesh manifest for culling, and Box2D-ready collision shapes.
#include "viewer.h"
#include "rendering/geometry_mesh.h"
#include "rendering/wall_mesh.h"
#include "rendering/tile_mesh.h"
#include "rendering/texture_loader.h"
#include "scene_convert/geometry_xml_parser.h"
#include "scene_convert/scene_types.h"
#include "model_convert/gltf_export.h"

#include "raymath.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// One mesh destined for the exported GLTF, tagged for the manifest.
struct ShapeMesh {
    Mesh mesh;                    // raylib CPU mesh (owned here; unloaded after export)
    std::string kind;             // "floor" | "tile"
    int id;                       // area index or tile-batch index
    int textureIndex;             // legacy diffuse texture index (material grouping / URIs)
    int normalTextureIndex = -1;  // legacy bump/normal texture index (-1 = none)
    bool glass = false;           // material drawtype 5: transparent + env-mapped (glass tunnel)
};

// Build one GeometryMesh per PathArea by feeding a single-area copy through the existing
// tessellator (which groups by material -> exactly one mesh for one area).
void collectFloorMeshes(const Domain& domain, float scale, std::vector<ShapeMesh>& out) {
    int areaId = 0;
    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            for (const auto& pathArea : geom.areas) {
                PathGeometry single;
                single.nodes = geom.nodes;
                single.links = geom.links;
                single.profiles = geom.profiles;
                single.areas = {pathArea};

                GeometryMeshCollection col = createGeometryMeshes(single, scale);
                for (const auto& gm : col.meshes) {
                    if (gm.vertices.empty() || gm.indices.empty()) continue;
                    ShapeMesh em;
                    em.mesh = geometryMeshToRaylibMesh(gm);
                    em.kind = "floor";
                    em.id = areaId;
                    em.textureIndex = DEFAULT_FLOOR_MATERIAL.diffuseTextureIndex;
                    em.normalTextureIndex = DEFAULT_FLOOR_MATERIAL.normalTextureIndex;  // bump
                    out.push_back(em);
                }
                areaId++;
            }
        }
    }
}

// One mesh per swept link-profile (wall), tagged for the manifest.
void collectWallMeshes(const Domain& domain, float scale, const WallProfileTable& table,
                       bool caps, bool miter, std::vector<ShapeMesh>& out) {
    if (!table.loaded) return;
    GeometryMeshCollection col = createDomainWallMeshes(domain, scale, table, caps, miter);
    int wallId = 0;
    for (const auto& gm : col.meshes) {
        if (gm.vertices.empty() || gm.indices.empty()) { wallId++; continue; }
        ShapeMesh em;
        em.mesh = geometryMeshToRaylibMesh(gm);
        em.kind = "wall";
        em.id = wallId++;
        em.textureIndex = gm.materialId;              // diffuse texture index
        em.normalTextureIndex = gm.normalMaterialId;  // profile bump/normal (texture1)
        em.glass = gm.glass;                          // drawtype 5 -> transparent glass pass
        out.push_back(em);
    }
}

void collectTileMeshes(const Domain& domain, std::vector<ShapeMesh>& out,
                       TileBatchCollection& ownedBatches) {
    ownedBatches = createDomainBatchedMeshes(domain);
    int batchId = 0;
    for (const auto& batch : ownedBatches.batches) {
        if (batch.valid && batch.mesh.vertexCount > 0) {
            ShapeMesh em;
            em.mesh = batch.mesh;  // borrowed; freed via freeTileBatchCollection
            em.kind = "tile";
            em.id = batchId;
            em.textureIndex = batch.textureIndex1;       // archetile diffuse
            em.normalTextureIndex = batch.textureIndex2; // archetile bump (trio: diffuse bump spec)
        out.push_back(em);
        }
        batchId++;
    }
}

// Write Box2D-ready wall collision as footprint POLYGONS, via the shared `buildWallCollision` (so the
// export and the viewer's collision wireframe stay identical). Normal walls -> one quad per path
// segment spanning the profile lateral extent; st_walls profiles (glass tunnel) -> two outer-edge
// quads leaving the interior walkable. Coordinates are the game's 2D physics plane (render X, Z).
void writeCollision(const Domain& domain, const WallProfileTable& table, float scale, int level,
                    const std::string& path) {
    json doc;
    doc["level"] = level;
    doc["space"] = "render-metric wall footprint polygons (x, z)";
    json polys = json::array();
    for (const auto& q : buildWallCollision(domain, table, scale)) {
        json poly = {{"vertices", json::array({
            json::array({q.v[0].x, q.v[0].y}), json::array({q.v[1].x, q.v[1].y}),
            json::array({q.v[2].x, q.v[2].y}), json::array({q.v[3].x, q.v[3].y})})}};
        if (q.glass) poly["glass"] = true;   // LOS-transparent wall (CATEGORY_GLASS in the game)
        polys.push_back(std::move(poly));
    }
    doc["polygons"] = polys;
    doc["chains"] = json::array();  // walls are footprint polygons; no chains

    std::ofstream f(path);
    f << doc.dump(2) << "\n";
}

}  // namespace

// Write a single ShapeMesh to its own .gltf (one mesh, one material). Returns its bounds.
static BoundingBox writeSingleMeshGltf(const ShapeMesh& em, const std::string& texPath,
                                       const std::string& outPath) {
    Model model = {};
    model.transform = MatrixIdentity();
    model.meshCount = 1;
    model.meshes = (Mesh*)MemAlloc(sizeof(Mesh));
    model.meshes[0] = em.mesh;
    model.meshMaterial = (int*)MemAlloc(sizeof(int));
    model.meshMaterial[0] = 0;
    model.materialCount = 1;
    model.materials = (Material*)MemAlloc(sizeof(Material));
    model.materials[0] = LoadMaterialDefault();
    model.materials[0].maps[MATERIAL_MAP_ALBEDO].color = WHITE;

    GLTFExportOptions opts = GLTFDefaultOptions();
    opts.texture_dir = "textures";
    opts.copy_textures = true;
    opts.include_physics_shape = false;
    opts.texture_count = 1;
    opts.texture_paths[0] = texPath.empty() ? nullptr : texPath.c_str();

    ExportGLTFEx(model, outPath.c_str(), opts);
    BoundingBox bb = GetMeshBoundingBox(em.mesh);

    MemFree(model.meshes);
    MemFree(model.meshMaterial);
    MemFree(model.materials);
    return bb;
}

// Split export: one .gltf per procedural shape (floor area / wall / tile batch), named by kind +
// index, plus an _index.json listing per-file bounds and max coordinate magnitude so broken
// sections (blown-out coordinates) are obvious at a glance without opening each file.
bool viewerExportLevelSplit(Viewer* viewer, const char* dir) {
    if (!viewer || !viewer->domainLoaded) {
        TraceLog(LOG_WARNING, "EXPORT(split): no level loaded");
        return false;
    }
    const int level = (viewer->currentLevelIdx >= 0)
                          ? viewer->levelNumbers[viewer->currentLevelIdx] : 0;
    fs::path outDir = fs::path(dir) / ("level_" + std::to_string(level) + "_split");
    std::error_code ec;
    fs::create_directories(outDir, ec);

    std::vector<ShapeMesh> meshes;
    collectFloorMeshes(viewer->loadedDomain, viewer->scale, meshes);
    collectWallMeshes(viewer->loadedDomain, viewer->scale, viewer->wallProfiles,
                      viewer->toggles.enableCaps, viewer->toggles.enableMiter, meshes);
    TileBatchCollection tileBatches = {};
    collectTileMeshes(viewer->loadedDomain, meshes, tileBatches);

    json index;
    index["level"] = level;
    index["scale"] = viewer->scale;
    index["files"] = json::array();

    // Per-kind running index for stable, readable filenames.
    std::unordered_map<std::string, int> kindSeq;
    for (const auto& em : meshes) {
        int seq = kindSeq[em.kind]++;
        char name[64];
        snprintf(name, sizeof(name), "%s_%03d.gltf", em.kind.c_str(), em.id);
        std::string texPath = getTextureFullPath(viewer->textureLookup, em.textureIndex);
        BoundingBox bb = writeSingleMeshGltf(em, texPath, (outDir / name).string());

        float maxMag = 0.0f;
        for (float v : {bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z})
            maxMag = std::max(maxMag, std::fabs(v));

        index["files"].push_back({
            {"file", name}, {"kind", em.kind}, {"id", em.id}, {"seq", seq},
            {"textureIndex", em.textureIndex},
            {"min", {bb.min.x, bb.min.y, bb.min.z}},
            {"max", {bb.max.x, bb.max.y, bb.max.z}},
            {"maxMagnitude", maxMag},
        });
    }

    { std::ofstream f((outDir / "_index.json").string()); f << index.dump(2) << "\n"; }

    for (auto& em : meshes) if (em.kind != "tile") UnloadMesh(em.mesh);
    freeTileBatchCollection(&tileBatches);

    TraceLog(LOG_INFO, "EXPORT(split): level %d -> %s (%zu files)", level,
             outDir.string().c_str(), meshes.size());
    return true;
}

// Sidecar: AI waypoints + unit spawns + interactive objects (doors/chargers/consoles), in the SAME
// render-metric frame as level_N.gltf so the game's 3D-level path can consume geometry + entities
// together. Positions come from the loaded (render-space) domain verbatim.
static void writeEntities(const Domain& domain, float scale, int level, const std::string& path) {
    json doc;
    doc["level"] = level;
    doc["scale"] = scale;
    doc["space"] = "render-metric (matches level_<n>.gltf: X right, Y up, Z depth)";

    json wps = json::array();
    for (const auto& w : domain.waypoints) {
        json flags = json::object();
        if (w.flags.start)    flags["start"] = true;
        if (w.flags.console)  flags["console"] = true;
        if (w.flags.recharge) flags["recharge"] = true;
        if (w.flags.lift)     flags["lift"] = true;
        if (w.flags.transmat) flags["transmat"] = true;
        wps.push_back({{"id", w.id},
                       {"pos", {w.position.x, w.position.y, w.position.z}},
                       {"neighbors", w.neighbors},
                       {"flags", flags}});
    }
    doc["waypoints"] = wps;

    json sp = json::array();
    for (const auto& s : domain.spawns)
        sp.push_back({{"droidClass", s.droidClass}, {"waypointIndex", s.waypointIndex}, {"angle", s.angle}});
    doc["spawns"] = sp;

    // Positions come from the render-space domain (already metres); `size` is a raw game-unit
    // extent, so scale it to metres to keep the whole sidecar in the GLTF frame.
    json doors = json::array();
    for (const auto& d : domain.objects.doors)
        doors.push_back({{"id", d.id},
                         {"pos", {d.position.x, d.position.y, d.position.z}},
                         {"rot", {d.rotation.x, d.rotation.y, d.rotation.z}},
                         {"size", {d.size.x * scale, d.size.y * scale}},
                         {"state", d.state}});
    doc["doors"] = doors;

    json chargers = json::array();
    for (const auto& c : domain.objects.chargers)
        chargers.push_back({{"id", c.id}, {"pos", {c.position.x, c.position.y, c.position.z}}});
    doc["chargers"] = chargers;

    json consoles = json::array();
    for (const auto& c : domain.objects.consoles)
        consoles.push_back({{"id", c.id},
                            {"pos", {c.position.x, c.position.y, c.position.z}},
                            {"rot", {c.rotation.x, c.rotation.y, c.rotation.z}},
                            {"waypointId", c.waypointId}});
    doc["consoles"] = consoles;

    // Generic scenery objects (Object/Destructible/Organic records). `renderIndex` selects the object
    // DEFINITION (assets/objects/*.json) that gives it model/physics/health/shader; the full 3D pos
    // (these can be ceiling-height, not floor-seated), facing, and spin are per-instance. See
    // docs/scenery_entities.md.
    json objects = json::array();
    for (const auto& o : domain.objects.generic)
        objects.push_back({{"renderIndex", o.typeId},
                           {"pos", {o.position.x, o.position.y, o.position.z}},
                           {"rot", {o.rotation.x, o.rotation.y, o.rotation.z}},
                           {"spin", {o.spin.x, o.spin.y, o.spin.z}}});
    doc["objects"] = objects;

    // Level-authored floor decals: map Feature records whose type id is in the decal band (29..34 in
    // uber's features.txt — biohazard/storage/danger/processing markings). `type` selects the decal
    // texture + size + aspect in the loader; pos/rot are render-metric verbatim (same as objects).
    // These are permanent, non-cleanable, purely visual (no collision — writeCollision ignores
    // features). See docs/decals.md.
    json decals = json::array();
    for (const auto& area : domain.areas)
        for (const auto& fe : area.features)
            if (fe.renderIndex >= 29 && fe.renderIndex <= 34)
                decals.push_back({{"type", fe.renderIndex},
                                  {"pos", {fe.position.x, fe.position.y, fe.position.z}},
                                  {"rot", {fe.rotation.x, fe.rotation.y, fe.rotation.z}}});
    doc["decals"] = decals;

    std::ofstream f(path);
    f << doc.dump(2) << "\n";
}

bool viewerExportTransporters(const std::vector<Transporter>& transporters, float scale,
                              const char* dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);

    json doc;
    doc["space"] = "render-metric (matches level_<n>.gltf: X right, Y up, Z depth)";
    doc["scale"] = scale;

    json arr = json::array();
    for (const auto& t : transporters) {
        // Transporter.position is game units (tile*64 + centre, z=height); bring it into the same
        // render-metric frame as the geometry/waypoints so the game's lift stops land on the deck.
        Vector3 p = gameToRenderCoords(t.position, scale);
        arr.push_back({{"id", t.id},
                       {"deck", t.domainIndex},
                       {"pos", {p.x, p.y, p.z}},
                       {"levelUp", t.levelUp},
                       {"levelDown", t.levelDown},
                       {"liftRow", t.liftRow}});
    }
    doc["transporters"] = arr;

    std::ofstream f(fs::path(dir) / "transporters.json");
    if (!f.is_open()) return false;
    f << doc.dump(2) << "\n";
    return true;
}

bool viewerExportSpawns(const std::vector<DeckSpawnInfo>& decks, const char* dir) {
    // spawns.json lives one level up from the levels3d bundle dir (…/ship1/spawns.json — where the
    // game loads it), unlike transporters.json which sits inside the bundle dir.
    fs::path shipDir = fs::path(dir).parent_path();
    fs::path out = shipDir / "spawns.json";

    // Preserve the existing "name" (cosmetic; the game reads it as the ship label).
    std::string name = "Ship 1";
    {
        std::ifstream in(out);
        if (in) {
            json ex = json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (!ex.is_discarded() && ex.contains("name")) name = ex.value("name", name);
        }
    }

    // Stable output: order decks by level number.
    std::vector<const DeckSpawnInfo*> sorted;
    sorted.reserve(decks.size());
    for (const auto& d : decks) sorted.push_back(&d);
    std::sort(sorted.begin(), sorted.end(),
              [](const DeckSpawnInfo* a, const DeckSpawnInfo* b) { return a->level < b->level; });

    std::error_code ec;
    fs::create_directories(shipDir, ec);
    std::ofstream f(out);
    if (!f.is_open()) {
        TraceLog(LOG_WARNING, "EXPORT: could not write %s", out.string().c_str());
        return false;
    }

    // Emit one compact line per level (profile/placedDroids as inline arrays) — matches the original
    // layout and keeps git diffs readable, instead of dump(2)'s one-int-per-line expansion.
    f << "{\n";
    f << "  \"name\": " << json(name).dump() << ",\n";
    f << "  \"levels\": [\n";
    for (size_t i = 0; i < sorted.size(); ++i) {
        const DeckSpawnInfo* d = sorted[i];
        f << "    {\"level\": " << d->level
          << ", \"profile\": " << json(d->profile).dump();   // per-type spawn counts (from PROFILE)
        json pd = json::array();
        for (const Spawn& s : d->placed) {
            if (s.droidClass == 0) continue;   // classId 0 is the player device — never an AI enemy
            pd.push_back({{"classId", s.droidClass},
                          {"waypointIndex", s.waypointIndex},
                          {"angle", s.angle}});
        }
        if (!pd.empty()) f << ", \"placedDroids\": " << pd.dump();
        f << "}" << (i + 1 < sorted.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    TraceLog(LOG_INFO, "EXPORT: wrote %s (%zu decks) from xmapfile PROFILE/PLACEDROID",
             out.string().c_str(), decks.size());
    return true;
}

bool viewerExportLevel(Viewer* viewer, const char* dir) {
    if (!viewer || !viewer->domainLoaded) {
        TraceLog(LOG_WARNING, "EXPORT: no level loaded");
        return false;
    }

    const int level = (viewer->currentLevelIdx >= 0)
                          ? viewer->levelNumbers[viewer->currentLevelIdx] : 0;
    fs::path outDir = fs::path(dir) / ("level_" + std::to_string(level));
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // 1. Collect per-shape meshes (floors) + per-batch tile meshes.
    std::vector<ShapeMesh> meshes;
    collectFloorMeshes(viewer->loadedDomain, viewer->scale, meshes);
    collectWallMeshes(viewer->loadedDomain, viewer->scale, viewer->wallProfiles,
                      viewer->toggles.enableCaps, viewer->toggles.enableMiter, meshes);
    TileBatchCollection tileBatches = {};
    collectTileMeshes(viewer->loadedDomain, meshes, tileBatches);

    if (meshes.empty()) {
        TraceLog(LOG_WARNING, "EXPORT: level %d produced no meshes", level);
        freeTileBatchCollection(&tileBatches);
        return false;
    }

    // 2. Group materials by distinct (diffuse, normal) texture-index pair, so meshes that share a
    //    diffuse but differ in bump map get separate materials (keeps material count small otherwise).
    std::map<std::pair<int, int>, int> texToMaterial;      // (diffuse, normal) -> material index
    std::vector<std::pair<int, int>> materialTexIndex;     // material index -> (diffuse, normal)
    for (const auto& em : meshes) {
        std::pair<int, int> key{em.textureIndex, em.normalTextureIndex};
        if (!texToMaterial.count(key)) {
            texToMaterial[key] = static_cast<int>(materialTexIndex.size());
            materialTexIndex.push_back(key);
        }
    }

    // 3. Assemble a raylib Model referencing the CPU meshes.
    Model model = {};
    model.transform = MatrixIdentity();
    model.meshCount = static_cast<int>(meshes.size());
    model.meshes = (Mesh*)MemAlloc(sizeof(Mesh) * model.meshCount);
    model.meshMaterial = (int*)MemAlloc(sizeof(int) * model.meshCount);
    for (int i = 0; i < model.meshCount; ++i) {
        model.meshes[i] = meshes[i].mesh;
        model.meshMaterial[i] = texToMaterial[{meshes[i].textureIndex, meshes[i].normalTextureIndex}];
    }
    model.materialCount = static_cast<int>(materialTexIndex.size());
    model.materials = (Material*)MemAlloc(sizeof(Material) * model.materialCount);
    for (int i = 0; i < model.materialCount; ++i) {
        model.materials[i] = LoadMaterialDefault();
        model.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    }

    // 4. Texture URIs per material, resolved from the legacy texture table. The bundle GLTFs live in
    //    per-deck subfolders (levels3d/level_N/), but the deck textures are the same handful of
    //    images, so point them at ONE shared folder a level up (levels3d/textures/) instead of
    //    duplicating them into every deck. "../textures" is both the URI prefix (resolved by the
    //    loader relative to the .gltf) and the copy destination (output_dir + "/../textures").
    GLTFExportOptions opts = GLTFDefaultOptions();
    opts.texture_dir = "../textures";
    opts.copy_textures = true;
    opts.include_physics_shape = false;
    std::vector<std::string> texPaths(model.materialCount), normPaths(model.materialCount);
    opts.texture_count = model.materialCount;
    opts.normal_texture_count = model.materialCount;
    for (int i = 0; i < model.materialCount && i < GLTF_MAX_TEXTURES; ++i) {
        // Diffuse (texture0).
        texPaths[i] = getTextureFullPath(viewer->textureLookup, materialTexIndex[i].first);
        opts.texture_paths[i] = texPaths[i].empty() ? nullptr : texPaths[i].c_str();
        // Bump/normal (texture1) — assigns a tangent-space normalTexture so the game's scene shader
        // bumps floors/walls/tiles just like the door model.
        int normIdx = materialTexIndex[i].second;
        if (normIdx >= 0) {
            normPaths[i] = getTextureFullPath(viewer->textureLookup, normIdx);
            opts.normal_texture_paths[i] = normPaths[i].empty() ? nullptr : normPaths[i].c_str();
        } else {
            opts.normal_texture_paths[i] = nullptr;
        }
    }

    // 5. Export the GLTF.
    std::string gltfPath = (outDir / ("level_" + std::to_string(level) + ".gltf")).string();
    GLTFExportResult res = ExportGLTFEx(model, gltfPath.c_str(), opts);
    if (!res.success) {
        TraceLog(LOG_ERROR, "EXPORT: GLTF write failed: %s", res.error_msg);
    }

    // 6. Manifest: per-mesh kind/id/material/bounds so the engine can cull post-load (raylib drops
    //    node names, but per-mesh index + bounds is enough).
    json manifest;
    manifest["level"] = level;
    manifest["scale"] = viewer->scale;
    manifest["gltf"] = fs::path(gltfPath).filename().string();
    manifest["meshes"] = json::array();
    for (int i = 0; i < model.meshCount; ++i) {
        BoundingBox bb = GetMeshBoundingBox(model.meshes[i]);
        manifest["meshes"].push_back({
            {"index", i},
            {"kind", meshes[i].kind},
            {"id", meshes[i].id},
            {"material", model.meshMaterial[i]},
            {"textureIndex", meshes[i].textureIndex},
            {"glass", meshes[i].glass},   // drawtype 5 -> transparent env-mapped glass pass
            {"min", {bb.min.x, bb.min.y, bb.min.z}},
            {"max", {bb.max.x, bb.max.y, bb.max.z}},
        });
    }
    std::string manifestPath = (outDir / ("level_" + std::to_string(level) + ".manifest.json")).string();
    { std::ofstream f(manifestPath); f << manifest.dump(2) << "\n"; }

    // 7. Collision (separate, Box2D-ready).
    std::string collPath = (outDir / ("level_" + std::to_string(level) + ".collision.json")).string();
    writeCollision(viewer->loadedDomain, viewer->wallProfiles, viewer->scale, level, collPath);

    // 7b. Entities sidecar (waypoints / spawns / objects) — completes the self-contained bundle.
    std::string entPath = (outDir / ("level_" + std::to_string(level) + ".entities.json")).string();
    writeEntities(viewer->loadedDomain, viewer->scale, level, entPath);

    TraceLog(LOG_INFO, "EXPORT: level %d -> %s (%d meshes, %d materials)",
             level, outDir.string().c_str(), model.meshCount, model.materialCount);

    // 8. Cleanup: free the temporary GPU meshes we own. Floor meshes were created here; tile meshes
    //    belong to tileBatches. Unload floor meshes individually, then the tile batch collection.
    for (auto& em : meshes) {
        if (em.kind != "tile") UnloadMesh(em.mesh);  // floor + wall meshes are owned here
    }
    freeTileBatchCollection(&tileBatches);
    // Materials use the default shared texture; UnloadMaterial would free that shared texture, so
    // just free the arrays we MemAlloc'd.
    MemFree(model.meshes);
    MemFree(model.meshMaterial);
    MemFree(model.materials);

    return res.success;
}
