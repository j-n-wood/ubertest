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
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// One mesh destined for the exported GLTF, tagged for the manifest.
struct ShapeMesh {
    Mesh mesh;          // raylib CPU mesh (owned here; unloaded after export)
    std::string kind;   // "floor" | "tile"
    int id;             // area index or tile-batch index
    int textureIndex;   // legacy texture index (for material grouping / URIs)
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
        em.textureIndex = gm.materialId;  // diffuse texture index
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
            em.textureIndex = batch.textureIndex1;
        out.push_back(em);
        }
        batchId++;
    }
}

// Write Box2D-ready wall collision as footprint POLYGONS. A wall is any link that produces a wall
// mesh (explicit profile, or the geometry's default profile set — matching createDomainWallMeshes).
// For each wall we rebuild the render-space path exactly as the mesh does (straight, or a subdivided
// quadratic Bézier) and sweep it by the profile's lateral extent, emitting one convex quad per path
// segment. So the collision has the wall's real THICKNESS and follows its curves — not a thin
// centreline. Node positions are already render-metric (scale applied at serialization); the
// profile's lateral offset is in game units, so it is scaled here. Output is the game's 2D physics
// plane (render X, render Z). Floor areas are walkable, so they are NOT emitted as solids.
void writeCollision(const Domain& domain, const WallProfileTable& table, float scale, int level,
                    const std::string& path) {
    json doc;
    doc["level"] = level;
    doc["space"] = "render-metric wall footprint polygons (x, z)";
    json polys = json::array();

    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            std::unordered_map<int, Vector3> nodePos;
            for (const auto& n : geom.nodes) nodePos[n.id] = n.position;
            std::vector<int> defaultSet;
            for (const auto& p : geom.profiles) defaultSet.push_back(p.id);

            for (const auto& link : geom.links) {
                const std::vector<int>* profileIds = nullptr;
                if (!link.profiles.empty()) profileIds = &link.profiles;
                else if (link.useDefaultProfiles && !defaultSet.empty()) profileIds = &defaultSet;
                else continue;  // not a wall

                // Wall thickness = the lateral extent of the swept profile(s). Skip trim/border
                // profiles (near-zero height, e.g. the floor borders) — they are decorative and
                // extend laterally past the wall, so they must not widen or create collision.
                constexpr float MIN_WALL_HEIGHT = 2.0f;    // game units; trim ~0.3, walls ~60
                float latMin = 1e9f, latMax = -1e9f;
                for (int pid : *profileIds) {
                    auto it = table.profiles.find(pid);
                    if (it == table.profiles.end() || !it->second.valid) continue;
                    float yMin = 1e9f, yMax = -1e9f;
                    for (const auto& pt : it->second.points) {
                        yMin = std::min(yMin, pt.y); yMax = std::max(yMax, pt.y);
                    }
                    if (yMax - yMin < MIN_WALL_HEIGHT) continue;   // trim/border — not collision
                    for (const auto& pt : it->second.points) {
                        latMin = std::min(latMin, pt.x);
                        latMax = std::max(latMax, pt.x);
                    }
                }
                if (latMin > latMax) continue;             // no wall-height profile on this link
                latMin *= scale; latMax *= scale;          // game units -> render metres
                const float minThick = 0.12f;              // keep thin profiles collidable
                if (latMax - latMin < minThick) {
                    float c = 0.5f * (latMin + latMax);
                    latMin = c - 0.5f * minThick; latMax = c + 0.5f * minThick;
                }

                auto s = nodePos.find(link.start), f = nodePos.find(link.finish);
                if (s == nodePos.end() || f == nodePos.end()) continue;

                // Render-space path in the X-Z plane (matches the wall mesh: 10-step Bézier).
                std::vector<Vector2> pts;
                if (link.control) {
                    Vector2 p0{s->second.x, s->second.z};
                    Vector2 cp{link.control->position.x, link.control->position.z};
                    Vector2 p1{f->second.x, f->second.z};
                    const int steps = 10;
                    for (int i = 0; i <= steps; ++i) {
                        float t = (float)i / steps, u = 1.0f - t;
                        pts.push_back({u*u*p0.x + 2*u*t*cp.x + t*t*p1.x,
                                       u*u*p0.y + 2*u*t*cp.y + t*t*p1.y});
                    }
                } else {
                    pts.push_back({s->second.x, s->second.z});
                    pts.push_back({f->second.x, f->second.z});
                }

                // One convex quad per path segment, offset ±lateral perpendicular to the segment.
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    Vector2 p0 = pts[i], p1 = pts[i + 1];
                    float dx = p1.x - p0.x, dz = p1.y - p0.y;
                    float len = std::sqrt(dx*dx + dz*dz);
                    if (len < 1e-5f) continue;
                    Vector2 perp = {-dz / len, dx / len};
                    auto pt = [&](const Vector2& p, float lat) {
                        return json::array({p.x + perp.x * lat, p.y + perp.y * lat});
                    };
                    polys.push_back({{"vertices", json::array({
                        pt(p0, latMin), pt(p1, latMin), pt(p1, latMax), pt(p0, latMax)})}});
                }
            }
        }
    }

    doc["polygons"] = polys;
    doc["chains"] = json::array();  // walls are footprint polygons now; no chains

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
                            {"waypointId", c.waypointId}});
    doc["consoles"] = consoles;

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

    // 2. Group materials by distinct legacy texture index (keeps material count small).
    std::unordered_map<int, int> texToMaterial;  // textureIndex -> material index
    std::vector<int> materialTexIndex;           // material index -> textureIndex
    for (const auto& em : meshes) {
        if (!texToMaterial.count(em.textureIndex)) {
            texToMaterial[em.textureIndex] = static_cast<int>(materialTexIndex.size());
            materialTexIndex.push_back(em.textureIndex);
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
        model.meshMaterial[i] = texToMaterial[meshes[i].textureIndex];
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
    std::vector<std::string> texPaths(model.materialCount);
    opts.texture_count = model.materialCount;
    for (int i = 0; i < model.materialCount && i < GLTF_MAX_TEXTURES; ++i) {
        texPaths[i] = getTextureFullPath(viewer->textureLookup, materialTexIndex[i]);
        opts.texture_paths[i] = texPaths[i].empty() ? nullptr : texPaths[i].c_str();
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
