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

json vec2Array(const Vector2& v, float scale) {
    // Match the mesh space: game Y (forward) is negated in gameToRenderCoords, so the collision's
    // forward axis is negated too (Box2D plane = render X, render Z).
    return json::array({v.x * scale, -v.y * scale});
}

// Aggregate + write Box2D-ready collision. Coordinates are in the game's 2D plane (X, Y-forward),
// pre-scaled to match the exported mesh; polygons are CCW convex (<=8 verts), chains carry a loop flag.
void writeCollision(const Domain& domain, float scale, int level, const std::string& path) {
    json doc;
    doc["level"] = level;
    doc["scale"] = scale;
    doc["space"] = "game-2d (x, y-forward), pre-scaled";
    doc["polygons"] = json::array();
    doc["chains"] = json::array();

    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            CollisionData coll;
            generateCollisionFromGeometry(geom, coll);
            for (const auto& poly : coll.polygons) {
                json verts = json::array();
                for (const auto& v : poly.vertices) verts.push_back(vec2Array(v, scale));
                doc["polygons"].push_back({{"vertices", verts}});
            }
            for (const auto& chain : coll.chains) {
                json verts = json::array();
                for (const auto& v : chain.vertices) verts.push_back(vec2Array(v, scale));
                bool loop = false;
                if (chain.vertices.size() >= 2) {
                    const Vector2& f = chain.vertices.front();
                    const Vector2& l = chain.vertices.back();
                    loop = (std::fabs(f.x - l.x) < 1e-4f && std::fabs(f.y - l.y) < 1e-4f);
                }
                doc["chains"].push_back({{"vertices", verts}, {"loop", loop}});
            }
        }
    }

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

    // 4. Texture URIs per material, resolved from the legacy texture table.
    GLTFExportOptions opts = GLTFDefaultOptions();
    opts.texture_dir = "textures";
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
    writeCollision(viewer->loadedDomain, viewer->scale, level, collPath);

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
