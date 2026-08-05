// level_manager.cpp — in-app deck (level) cycling + validity report for the incremental viewer.
#include "viewer.h"
#include "rendering/geometry_mesh.h"
#include "rendering/texture_loader.h"
#include "scene_convert/domain_parser.h"
#include "scene_convert/geometry_xml_parser.h"
#include "scene_convert/scene_json.h"
#include "scene_convert/scene_types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Scan a source directory for xmapfile{N}.txt decks.
//------------------------------------------------------------------------------
void viewerScanLevels(Viewer* viewer, const char* sourceDir, const char* tilesPath) {
    if (!viewer) return;
    viewer->sourceDir = sourceDir ? sourceDir : "";
    viewer->tilesPath = tilesPath ? tilesPath : "";
    viewer->levelNumbers.clear();
    viewer->currentLevelIdx = -1;

    std::error_code ec;
    if (viewer->sourceDir.empty() || !fs::is_directory(viewer->sourceDir, ec)) return;

    std::set<int> found;
    for (const auto& entry : fs::directory_iterator(viewer->sourceDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        // Match exactly "xmapfile<digits>.txt" (skip the o*/p* variants).
        const std::string prefix = "xmapfile";
        const std::string suffix = ".txt";
        if (name.size() <= prefix.size() + suffix.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), ::isdigit)) continue;
        found.insert(std::stoi(digits));
    }
    viewer->levelNumbers.assign(found.begin(), found.end());
    TraceLog(LOG_INFO, "VIEWER: scanned %zu decks in %s", viewer->levelNumbers.size(),
             viewer->sourceDir.c_str());
}

//------------------------------------------------------------------------------
// Load a deck by number: convert + reload + validate.
//------------------------------------------------------------------------------
// Path of the saved (edited) JSON copy for a deck, or "" if no saveDir configured.
static std::string editedDeckPath(const Viewer* viewer, int levelNumber) {
    if (!viewer || viewer->saveDir.empty()) return "";
    return (fs::path(viewer->saveDir) / ("level_" + std::to_string(levelNumber) + ".json")).string();
}

bool viewerLoadLevel(Viewer* viewer, int levelNumber) {
    if (!viewer || viewer->sourceDir.empty()) return false;

    bool ok = false;
    // Prefer a previously-saved edited JSON copy (work on the new set; originals stay untouched).
    const std::string edited = editedDeckPath(viewer, levelNumber);
    if (!edited.empty() && fs::exists(edited)) {
        viewer->outputDir = viewer->outputDir;  // keep
        ok = viewerReloadFromJson(viewer, edited.c_str());
        viewer->loadedFromEdited = ok;
        if (ok) TraceLog(LOG_INFO, "VIEWER: loaded EDITED deck %s", edited.c_str());
    } else {
        const std::string path = (fs::path(viewer->sourceDir) /
                                  ("xmapfile" + std::to_string(levelNumber) + ".txt")).string();
        if (!fs::exists(path)) {
            TraceLog(LOG_ERROR, "VIEWER: deck file not found: %s", path.c_str());
            return false;
        }
        ok = viewerConvertAndLoad(viewer, path.c_str(), viewer->tilesPath.c_str(),
                                  viewer->outputDir.c_str(), viewer->scale);
        viewer->loadedFromEdited = false;
    }

    if (ok) {
        auto it = std::find(viewer->levelNumbers.begin(), viewer->levelNumbers.end(), levelNumber);
        viewer->currentLevelIdx = (it != viewer->levelNumbers.end())
                                       ? static_cast<int>(it - viewer->levelNumbers.begin()) : -1;
        viewerValidate(viewer);
    }
    return ok;
}

//------------------------------------------------------------------------------
// Save the edited deck to JSON in saveDir (originals untouched).
//------------------------------------------------------------------------------
bool viewerSaveEdited(Viewer* viewer) {
    if (!viewer || !viewer->domainLoaded) return false;
    if (viewer->saveDir.empty()) {
        TraceLog(LOG_WARNING, "VIEWER: no --save-dir set; cannot save");
        return false;
    }
    if (viewer->currentLevelIdx < 0) return false;
    int level = viewer->levelNumbers[viewer->currentLevelIdx];

    std::error_code ec;
    fs::create_directories(viewer->saveDir, ec);
    const std::string out = editedDeckPath(viewer, level);
    // loadedDomain is already render space (loaded from JSON), so do NOT re-apply the game->render
    // transform, or the reloaded file would be double-transformed (tiny / wrong geometry).
    if (saveDomainToFile(out, viewer->loadedDomain, /*pretty=*/true, /*transformToRender=*/false)) {
        viewer->loadedFromEdited = true;
        TraceLog(LOG_INFO, "VIEWER: saved edited deck -> %s", out.c_str());
        return true;
    }
    TraceLog(LOG_ERROR, "VIEWER: failed to save %s", out.c_str());
    return false;
}

//------------------------------------------------------------------------------
// Revert the current deck to the original XML: discard its saved edit and reload.
//------------------------------------------------------------------------------
bool viewerRevertToOriginal(Viewer* viewer) {
    if (!viewer || viewer->currentLevelIdx < 0) return false;
    int level = viewer->levelNumbers[viewer->currentLevelIdx];
    const std::string edited = editedDeckPath(viewer, level);
    if (!edited.empty() && fs::exists(edited)) {
        std::error_code ec;
        fs::remove(edited, ec);
        TraceLog(LOG_INFO, "VIEWER: reverted deck %d — discarded saved edit %s", level, edited.c_str());
    }
    return viewerLoadLevel(viewer, level);  // no edited copy now -> loads original XML
}

//------------------------------------------------------------------------------
// Save the whole ship: current deck's in-memory edits, plus a JSON for every deck that doesn't
// already have one (converted from its original XML). Leaves existing edited copies untouched and
// does not disturb the loaded deck/meshes. Returns the number of decks written.
//------------------------------------------------------------------------------
int viewerSaveAll(Viewer* viewer) {
    if (!viewer || viewer->saveDir.empty()) return 0;
    std::error_code ec;
    fs::create_directories(viewer->saveDir, ec);

    int count = 0;
    if (viewer->currentLevelIdx >= 0 && viewerSaveEdited(viewer)) count++;  // current in-memory edits

    for (int level : viewer->levelNumbers) {
        const std::string out = editedDeckPath(viewer, level);
        if (fs::exists(out)) continue;  // keep existing edited/previous copies
        const std::string xmap = (fs::path(viewer->sourceDir) /
                                  ("xmapfile" + std::to_string(level) + ".txt")).string();
        if (!fs::exists(xmap)) continue;
        Domain temp;
        fs::path base = fs::path(xmap).parent_path();
        if (parseDomainFile(xmap, temp, base, fs::path(viewer->tilesPath)) &&
            saveDomainToFile(out, temp)) {
            count++;
        }
    }
    TraceLog(LOG_INFO, "VIEWER: save-all wrote %d deck(s) to %s", count, viewer->saveDir.c_str());
    return count;
}

void viewerCycleLevel(Viewer* viewer, int delta) {
    if (!viewer || viewer->levelNumbers.empty()) return;
    int idx = viewer->currentLevelIdx;
    const int n = static_cast<int>(viewer->levelNumbers.size());
    if (idx < 0) idx = 0;
    else idx = ((idx + delta) % n + n) % n;  // wrap both directions
    viewerLoadLevel(viewer, viewer->levelNumbers[idx]);
}

//------------------------------------------------------------------------------
// Validity report — re-derives geometry + collision from the loaded domain and
// checks for the failure modes that matter downstream (Box2D, GLTF, culling).
//------------------------------------------------------------------------------
static bool finite3(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

//------------------------------------------------------------------------------
// Per-link profile assignment dump (diagnosis for manual XML edits).
//------------------------------------------------------------------------------
void viewerDumpProfiles(Viewer* viewer) {
    if (!viewer || !viewer->domainLoaded) return;
    int level = (viewer->currentLevelIdx >= 0) ? viewer->levelNumbers[viewer->currentLevelIdx] : -1;
    printf("\n=== Deck %d: wall-link profile assignment ===\n", level);
    printf("Profiles are chosen per link: explicit <Profile> children if present; else the deck's\n"
           "default set (the <Profiles> ids, e.g. {0,1,2}) unless the link has defaultProfiles=\"0\".\n"
           "Trim side follows path winding: Border-Left = profile 1 (LEFT of start->finish),\n"
           "Border-Right = profile 2 (RIGHT). Reverse start/finish to flip the trim to the other side.\n\n");
    for (const auto& area : viewer->loadedDomain.areas) {
        for (const auto& geom : area.geometry) {
            std::vector<int> defaultSet;
            for (const auto& p : geom.profiles) defaultSet.push_back(p.id);
            printf("geometry '%s'  default set = {", geom.sourceFile.c_str());
            for (size_t i = 0; i < defaultSet.size(); ++i) printf("%s%d", i ? "," : "", defaultSet[i]);
            printf("}\n");
            for (const auto& link : geom.links) {
                std::vector<int> ids;
                const char* how;
                if (!link.profiles.empty()) { ids = link.profiles; how = "explicit"; }
                else if (link.useDefaultProfiles && !defaultSet.empty()) { ids = defaultSet; how = "default "; }
                else { how = "NONE(dp=0)"; }
                std::string plist, trim;
                for (int id : ids) {
                    plist += (plist.empty() ? "" : ",") + std::to_string(id);
                    if (id == 1) trim += "L";
                    else if (id == 2) trim += "R";
                }
                printf("  link %3d: %3d->%-3d  %s [%s]%s%s\n",
                       link.id, link.start, link.finish, how, plist.c_str(),
                       trim.empty() ? "" : "  trim=", trim.c_str());
            }
        }
    }
    fflush(stdout);
}

void viewerValidate(Viewer* viewer) {
    if (!viewer) return;
    ValidationReport r;
    r.run = true;

    const Domain& domain = viewer->loadedDomain;

    // Count source floor areas + validate the tessellated geometry (CPU-side).
    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            r.areas += static_cast<int>(geom.areas.size());

            GeometryMeshCollection col = createGeometryMeshes(geom, viewer->scale);
            for (const auto& mesh : col.meshes) {
                r.floorMeshes++;
                r.triangles += static_cast<int>(mesh.indices.size() / 3);
                for (const auto& v : mesh.vertices) {
                    if (!finite3(v.position)) r.nonFiniteVerts++;
                }
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const Vector3& a = mesh.vertices[mesh.indices[i]].position;
                    const Vector3& b = mesh.vertices[mesh.indices[i + 1]].position;
                    const Vector3& c = mesh.vertices[mesh.indices[i + 2]].position;
                    // Twice the triangle area via cross product magnitude.
                    Vector3 ab = {b.x - a.x, b.y - a.y, b.z - a.z};
                    Vector3 ac = {c.x - a.x, c.y - a.y, c.z - a.z};
                    Vector3 cr = {ab.y * ac.z - ab.z * ac.y,
                                  ab.z * ac.x - ab.x * ac.z,
                                  ab.x * ac.y - ab.y * ac.x};
                    float area2 = std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z);
                    if (area2 < 1e-9f) r.degenerateTris++;
                }
            }
            if (!geom.areas.empty() && col.meshes.empty()) {
                r.emptyAreas += static_cast<int>(geom.areas.size());
            }

            // Collision: Box2D wants CCW convex polys of <= 8 verts, and closed chains.
            CollisionData coll;
            generateCollisionFromGeometry(geom, coll);
            r.collisionPolys += static_cast<int>(coll.polygons.size());
            r.collisionChains += static_cast<int>(coll.chains.size());
            for (const auto& poly : coll.polygons) {
                if (poly.vertices.size() > 8 || poly.vertices.size() < 3) r.oversizePolys++;
            }
            for (const auto& chain : coll.chains) {
                if (chain.vertices.size() >= 2) {
                    const Vector2& f = chain.vertices.front();
                    const Vector2& l = chain.vertices.back();
                    if (std::fabs(f.x - l.x) > 1e-4f || std::fabs(f.y - l.y) > 1e-4f) r.openChains++;
                }
            }
        }
    }

    r.tileBatches = viewer->tileMesh.loaded ? static_cast<int>(viewer->tileMesh.batches.size()) : 0;
    if (viewer->tileMesh.loaded) r.triangles += viewer->tileMesh.triangleCount;

    // Textures referenced by tile batches that don't resolve.
    if (viewer->texturesLoaded) {
        for (const auto& batch : viewer->tileMesh.batches) {
            if (batch.textureIndex1 > 0 && !isTextureValid(viewer->textureLookup, batch.textureIndex1)) {
                r.missingTextures++;
            }
        }
    }

    r.boundsFinite = finite3(viewer->tileMesh.boundsMin) && finite3(viewer->tileMesh.boundsMax);

    // Roll up human-readable warnings.
    auto warn = [&](bool cond, const std::string& msg) { if (cond) r.warnings.push_back(msg); };
    warn(r.areas == 0 && r.tileBatches == 0, "no geometry or tiles loaded");
    warn(r.emptyAreas > 0, TextFormat("%d area(s) produced no mesh", r.emptyAreas));
    warn(r.degenerateTris > 0, TextFormat("%d degenerate (zero-area) triangle(s)", r.degenerateTris));
    warn(r.nonFiniteVerts > 0, TextFormat("%d non-finite vertex position(s)", r.nonFiniteVerts));
    warn(r.oversizePolys > 0, TextFormat("%d collision poly(s) invalid for Box2D (>8 or <3 verts)", r.oversizePolys));
    // Open chains are valid Box2D chain shapes (walls need not loop) -> reported as info, not a fault.
    warn(r.missingTextures > 0, TextFormat("%d tile batch texture(s) missing", r.missingTextures));
    warn(!r.boundsFinite, "geometry bounds are non-finite");

    viewer->validation = std::move(r);

    // Console summary.
    const ValidationReport& v = viewer->validation;
    TraceLog(v.ok() ? LOG_INFO : LOG_WARNING,
             "VALIDATION: areas=%d floorMeshes=%d tileBatches=%d tris=%d collPolys=%d collChains=%d -> %s",
             v.areas, v.floorMeshes, v.tileBatches, v.triangles, v.collisionPolys, v.collisionChains,
             v.ok() ? "OK" : "ISSUES");
    for (const auto& w : v.warnings) TraceLog(LOG_WARNING, "  - %s", w.c_str());
}
