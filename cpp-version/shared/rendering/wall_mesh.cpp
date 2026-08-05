#include "wall_mesh.h"

#include "tinyxml2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using namespace tinyxml2;

namespace {

//------------------------------------------------------------------------------
// Ported standard profile cross-sections (geometryGen.cpp: loadStandard).
// Each point is {lateral, height} in game units; texT is the per-point t coord.
//------------------------------------------------------------------------------
struct StandardShape {
    std::vector<Vector2> points;   // {lateral, height}
    std::vector<float> texT;
    float dsdx;
    bool cap;
    std::vector<Vector2> capPoints;   // {lateral, height} filling the cross-section
    std::vector<int> capTriangles;    // index triples into capPoints
};

StandardShape standardShape(int def) {
    switch (def) {
        case 0: {  // Default Wall — curved arch, 32 wide x 60 tall
            StandardShape s;
            s.points = {{16, 0}, {13, 25}, {8, 50}, {0, 60}, {-8, 50}, {-13, 25}, {-16, 0}};
            s.texT = {2.0f, 1.55f, 1.1f, 1.0f, 0.9f, 0.45f, 0.0f};
            s.dsdx = 1.0f / 80.0f; s.cap = true;
            s.capPoints = {{0, 0}, {16, 0}, {8, 50}, {0, 60}, {-8, 50}, {-16, 0}};
            s.capTriangles = {0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4};  // fan
            return s;
        }
        case 1:  // Default Border Left — thin floor trim (no cap)
            return {{{32, 0.2f}, {16, 0.3f}}, {1.0f, 0.9f}, 1.0f / 64.0f, false, {}, {}};
        case 2:  // Default Border Right (no cap)
            return {{{-16, 0.3f}, {-32, 0.2f}}, {0.9f, 1.0f}, 1.0f / 64.0f, false, {}, {}};
        case 3: {  // Default Curved Wall — flat-topped curved tunnel
            StandardShape s;
            s.points = {{16, 0}, {6, 15}, {2, 30}, {6, 45}, {16, 60},
                        {0, 60}, {-16, 60}, {-26, 45}, {-30, 30}, {-26, 15}, {-16, 0}};
            s.texT = {1.0f, 0.8f, 0.55f, 0.3f, 0.05f, 0.0f, -0.05f, -0.3f, -0.55f, -0.8f, -1.0f};
            s.dsdx = 1.0f / 80.0f; s.cap = true;
            s.capPoints = {{16, 0}, {6, 15}, {2, 30}, {6, 45}, {16, 60},
                           {-16, 60}, {-26, 45}, {-30, 30}, {-26, 15}, {-16, 0}};
            s.capTriangles = {0, 9, 8, 0, 8, 1, 1, 8, 7, 1, 7, 2, 2, 7, 6,
                              2, 6, 3, 3, 6, 5, 3, 5, 4};
            return s;
        }
        default:  // Fallback: a plain vertical wall 32 wide x 60 tall (no cap).
            return {{{16, 0}, {16, 60}, {-16, 60}, {-16, 0}}, {1.0f, 0.0f, 0.0f, 1.0f},
                    1.0f / 80.0f, false, {}, {}};
    }
}

// Accumulate face normals into vertices (smooth), then normalise.
void computeSmoothNormals(GeometryMesh& mesh) {
    for (auto& v : mesh.vertices) v.normal = {0, 0, 0};
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        const Vector3& a = mesh.vertices[ia].position;
        const Vector3& b = mesh.vertices[ib].position;
        const Vector3& c = mesh.vertices[ic].position;
        Vector3 ab = {b.x - a.x, b.y - a.y, b.z - a.z};
        Vector3 ac = {c.x - a.x, c.y - a.y, c.z - a.z};
        Vector3 n = {ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        for (uint32_t idx : {ia, ib, ic}) {
            mesh.vertices[idx].normal.x += n.x;
            mesh.vertices[idx].normal.y += n.y;
            mesh.vertices[idx].normal.z += n.z;
        }
    }
    for (auto& v : mesh.vertices) {
        float len = std::sqrt(v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z);
        if (len > 1e-6f) { v.normal.x /= len; v.normal.y /= len; v.normal.z /= len; }
        else v.normal = {0, 1, 0};
    }
}

// Tangent in the render XZ (horizontal) plane at path point i, returned as {dx, dz}.
// Ear-clip a simple 2D polygon (here the profile cross-section: x = lateral, y = height).
std::vector<std::array<int, 3>> triangulate2D(const std::vector<Vector2>& poly) {
    std::vector<std::array<int, 3>> tris;
    const int n = static_cast<int>(poly.size());
    if (n < 3) return tris;
    auto cross = [](Vector2 a, Vector2 b, Vector2 c) {
        return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    };
    // Normalize to CCW (positive shoelace) so convex = left turn (cross > 0).
    float area = 0;
    for (int i = 0; i < n; ++i) { int j = (i + 1) % n; area += poly[i].x * poly[j].y - poly[j].x * poly[i].y; }
    std::vector<int> idx(n);
    if (area >= 0) for (int i = 0; i < n; ++i) idx[i] = i;
    else           for (int i = 0; i < n; ++i) idx[i] = n - 1 - i;
    auto inside = [&](Vector2 p, Vector2 a, Vector2 b, Vector2 c) {
        float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
        bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(neg && pos);
    };
    int guard = 0;
    while (static_cast<int>(idx.size()) > 3 && guard++ < 2 * n) {
        bool clipped = false;
        int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            int ip = (i - 1 + m) % m, in = (i + 1) % m;
            Vector2 a = poly[idx[ip]], b = poly[idx[i]], c = poly[idx[in]];
            if (cross(a, b, c) <= 0) continue;  // reflex/degenerate
            bool ok = true;
            for (int j = 0; j < m; ++j) {
                if (j == ip || j == i || j == in) continue;
                if (inside(poly[idx[j]], a, b, c)) { ok = false; break; }
            }
            if (!ok) continue;
            tris.push_back({idx[ip], idx[i], idx[in]});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {  // degenerate fallback (consistent winding)
            for (size_t k = 1; k + 1 < idx.size(); ++k)
                tris.push_back({idx[0], idx[static_cast<int>(k)], idx[static_cast<int>(k + 1)]});
            idx.clear();
            break;
        }
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    return tris;
}

// Per-side miter offset vectors at a junction end (left = +lateral, right = -lateral). For a
// degree-2 corner both are equal; at degree-3+ each side mitres with its angular neighbour.
struct MiterSides {
    bool has = false;
    Vector2 left{};
    Vector2 right{};
};

Vector2 tangentXZ(const std::vector<Vector3>& path, size_t i) {
    Vector2 d;
    if (path.size() < 2) return {1, 0};
    if (i == 0) d = {path[1].x - path[0].x, path[1].z - path[0].z};
    else if (i + 1 >= path.size()) d = {path[i].x - path[i - 1].x, path[i].z - path[i - 1].z};
    else d = {path[i + 1].x - path[i - 1].x, path[i + 1].z - path[i - 1].z};
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    return (len > 1e-6f) ? Vector2{d.x / len, d.y / len} : Vector2{1, 0};
}

}  // namespace

//------------------------------------------------------------------------------
// materials.xml -> resolved profile table
//------------------------------------------------------------------------------
bool loadWallProfiles(const char* materialsXmlPath, WallProfileTable& out) {
    out.profiles.clear();
    out.loaded = false;

    XMLDocument doc;
    if (doc.LoadFile(materialsXmlPath) != XML_SUCCESS) {
        TraceLog(LOG_WARNING, "WALLS: could not load %s", materialsXmlPath);
        return false;
    }
    XMLElement* root = doc.FirstChildElement("MaterialData");
    if (!root) return false;

    // Materials: id -> {texture0, texture1}
    struct Mat { int tex0 = -1; int tex1 = -1; };
    std::unordered_map<int, Mat> materials;
    if (XMLElement* matsEl = root->FirstChildElement("Materials")) {
        for (XMLElement* m = matsEl->FirstChildElement("Material"); m; m = m->NextSiblingElement("Material")) {
            Mat mat;
            mat.tex0 = m->IntAttribute("texture0", -1);
            mat.tex1 = m->IntAttribute("texture1", -1);
            materials[m->IntAttribute("id", -1)] = mat;
        }
    }

    // Profiles: id -> {default shape, materialID, occlusionHeight}
    if (XMLElement* profsEl = root->FirstChildElement("Profiles")) {
        for (XMLElement* p = profsEl->FirstChildElement("Profile"); p; p = p->NextSiblingElement("Profile")) {
            WallProfile wp;
            wp.id = p->IntAttribute("id", -1);
            int def = p->IntAttribute("default", 0);
            wp.materialId = p->IntAttribute("materialID", -1);
            wp.occlusionHeight = p->FloatAttribute("occlusionHeight", 0.0f);

            StandardShape shape = standardShape(def);
            wp.points = shape.points;
            wp.texcoordT = shape.texT;
            wp.dsdx = shape.dsdx;
            wp.cap = shape.cap;
            wp.capPoints = shape.capPoints;
            wp.capTriangles = shape.capTriangles;

            auto it = materials.find(wp.materialId);
            if (it != materials.end()) {
                wp.diffuseTextureIndex = it->second.tex0;
                wp.normalTextureIndex = it->second.tex1;
            }
            wp.valid = !wp.points.empty();
            out.profiles[wp.id] = wp;
        }
    }

    out.loaded = !out.profiles.empty();
    TraceLog(LOG_INFO, "WALLS: loaded %zu wall profiles from %s", out.profiles.size(), materialsXmlPath);
    return out.loaded;
}

//------------------------------------------------------------------------------
// Sweep one link-profile into a GeometryMesh.
//------------------------------------------------------------------------------
// Append an end cap at a node: the wall's own cross-section outline (prof.points) triangulated and
// placed in the section plane, so the cap edges align exactly with the swept wall sides. `dir` is
// the cross-section direction at the node; each triangle is wound to face `outward` (the exposed
// direction away from the wall body), robustly regardless of the triangulator winding / reflection.
static void generateCap(GeometryMesh& mesh, const Vector3& nodePos, Vector2 dir,
                        const WallProfile& prof, float scale, Vector3 outward) {
    if (prof.points.size() < 3) return;
    std::vector<std::array<int, 3>> tris = triangulate2D(prof.points);
    Vector2 perp = {-dir.y, dir.x};
    Vector3 tan = {dir.x, 0.0f, dir.y};
    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

    for (size_t k = 0; k < prof.points.size(); ++k) {
        float latS = prof.points[k].x * scale, htS = prof.points[k].y * scale;
        GeometryVertex v;
        v.position = {nodePos.x + latS * perp.x, nodePos.y + htS, nodePos.z + latS * perp.y};
        v.uv = {(prof.points[k].x + 32.0f) / 64.0f, prof.points[k].y / 64.0f};
        v.tangent[0] = tan.x; v.tangent[1] = tan.y; v.tangent[2] = tan.z; v.tangent[3] = 1.0f;
        v.color = WHITE;
        v.normal = {0, 1, 0};
        mesh.vertices.push_back(v);
    }
    for (const auto& t : tris) {
        const Vector3& p0 = mesh.vertices[base + t[0]].position;
        const Vector3& p1 = mesh.vertices[base + t[1]].position;
        const Vector3& p2 = mesh.vertices[base + t[2]].position;
        Vector3 e1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        Vector3 e2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        Vector3 nrm = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        float facing = nrm.x * outward.x + nrm.y * outward.y + nrm.z * outward.z;
        mesh.indices.push_back(base + t[0]);
        if (facing >= 0.0f) {  // already faces outward
            mesh.indices.push_back(base + t[1]);
            mesh.indices.push_back(base + t[2]);
        } else {
            mesh.indices.push_back(base + t[2]);
            mesh.indices.push_back(base + t[1]);
        }
    }
}

// The path is in RENDER space (nodes/control are already game->render transformed by the JSON
// round-trip). Only the profile's local {lateral, height} offsets are raw game units, so only those
// are multiplied by `scale`; the path coords are used as-is. capStart/capEnd close an open link end.
static void sweepProfile(const std::vector<Vector3>& path, const WallProfile& prof, float scale,
                         bool capStart, bool capEnd,
                         const MiterSides& startMiter, const MiterSides& endMiter,
                         GeometryMeshCollection& out) {
    const int N = static_cast<int>(prof.points.size());
    if (N < 2 || path.size() < 2) return;

    GeometryMesh mesh;
    mesh.materialId = prof.diffuseTextureIndex;  // for downstream texture binding/grouping

    // Emit one cross-section per path point.
    float cumLen = 0.0f;  // render-space horizontal path length
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            float dx = path[i].x - path[i - 1].x, dz = path[i].z - path[i - 1].z;
            cumLen += std::sqrt(dx * dx + dz * dz);
        }
        Vector2 dir = tangentXZ(path, i);   // {dx, dz}
        Vector2 basePerp = {-dir.y, dir.x}; // lateral in the XZ plane (unit)
        // At a junction end, replace the perpendicular offset with per-side miter vectors so the
        // corner extends to the true intersection. Each side (+/- lateral) mitres with its own
        // angular neighbour, so this works for degree-2 corners and degree-3+ T-junctions alike.
        const MiterSides* junction = nullptr;
        if (i == 0 && startMiter.has) junction = &startMiter;
        else if (i + 1 == path.size() && endMiter.has) junction = &endMiter;
        // s along the path: dsdx is per game unit, so convert the render length back to game units.
        float s = (scale > 0.0f ? cumLen / scale : cumLen) * prof.dsdx;
        Vector3 tan = {dir.x, 0.0f, dir.y};

        for (int k = 0; k < N; ++k) {
            float lateral = prof.points[k].x;
            Vector2 perp = basePerp;
            if (junction) perp = (lateral >= 0.0f) ? junction->left : junction->right;
            float latS = lateral * scale;                   // lateral offset (game units -> render)
            float htS = prof.points[k].y * scale;           // height (game units -> render)

            GeometryVertex v;
            v.position = {path[i].x + latS * perp.x, path[i].y + htS, path[i].z + latS * perp.y};
            v.uv = {s, prof.texcoordT[k]};
            v.tangent[0] = tan.x; v.tangent[1] = tan.y; v.tangent[2] = tan.z; v.tangent[3] = 1.0f;
            v.color = WHITE;
            v.normal = {0, 1, 0};
            mesh.vertices.push_back(v);
        }
    }

    // Stitch consecutive sections. The game Y-axis negation in gameToRenderCoords reflects the
    // geometry (reverses handedness), so this winding is chosen to face the profiles outward under
    // raylib/OpenGL front-face (CCW) culling. computeSmoothNormals reads it, so normals follow.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        uint32_t a = static_cast<uint32_t>(i * N);
        uint32_t b = static_cast<uint32_t>((i + 1) * N);
        for (int k = 0; k < N - 1; ++k) {
            mesh.indices.push_back(a + k);
            mesh.indices.push_back(b + k);
            mesh.indices.push_back(b + k + 1);
            mesh.indices.push_back(a + k);
            mesh.indices.push_back(b + k + 1);
            mesh.indices.push_back(a + k + 1);
        }
    }

    // Close open (dead-end) ends. `outward` is the exposed direction away from the wall body: at the
    // start the wall extends toward path[1] (forward), so outward = -forward; at the end, +forward.
    if (prof.cap && capStart) {
        Vector2 t0 = tangentXZ(path, 0);
        generateCap(mesh, path.front(), t0, prof, scale, Vector3{-t0.x, 0.0f, -t0.y});
    }
    if (prof.cap && capEnd) {
        Vector2 tl = tangentXZ(path, path.size() - 1);
        generateCap(mesh, path.back(), tl, prof, scale, Vector3{tl.x, 0.0f, tl.y});
    }

    if (mesh.vertices.empty() || mesh.indices.empty()) return;
    computeSmoothNormals(mesh);
    out.meshes.push_back(std::move(mesh));
}

GeometryMeshCollection createWallMeshes(const PathGeometry& geometry, float scale,
                                        const WallProfileTable& table,
                                        bool enableCaps, bool enableMiter) {
    GeometryMeshCollection result = {};
    result.success = true;
    if (!table.loaded) { result.success = false; result.error = "wall profiles not loaded"; return result; }

    // Node id -> position.
    std::unordered_map<int, Vector3> nodePos;
    for (const auto& n : geometry.nodes) nodePos[n.id] = n.position;

    // Default profile set: the ids listed in <Profiles>. Profile-less links that didn't opt out
    // (defaultProfiles="0") use this set — that's how interior walls are declared.
    std::vector<int> defaultSet;
    for (const auto& p : geometry.profiles) defaultSet.push_back(p.id);

    // Dedupe exact-duplicate wall links (same endpoints + profiles + control). Redundant source data
    // — e.g. lvl7 links 23 & 43 are both node 42->43 — would otherwise draw two coincident walls
    // (z-fighting, slightly different normals) and, being a zero-angle "neighbour", break the miter
    // and border resolution at those nodes. The second occurrence is skipped.
    auto rawWall = [&](const PathLink& l) {
        return !l.profiles.empty() || (l.useDefaultProfiles && !defaultSet.empty());
    };
    std::unordered_set<long long> seenKey;
    std::unordered_set<int> dupSkip;  // link ids to skip
    for (const auto& l : geometry.links) {
        if (!rawWall(l)) continue;
        long long key = (static_cast<long long>(std::min(l.start, l.finish)) << 20) | std::max(l.start, l.finish);
        long long pk = l.control ? 1 : 0;
        for (int p : l.profiles) pk = pk * 131 + (p + 1);
        key = key * 1000003 + pk;
        if (!seenKey.insert(key).second) dupSkip.insert(l.id);
    }
    if (!dupSkip.empty()) {
        TraceLog(LOG_WARNING, "WALLS: skipped %zu duplicate wall link(s) in source geometry",
                 dupSkip.size());
    }

    // Node degree over wall-producing links: an end is "open" (gets a cap) when its node has a
    // single incident wall link. Junctions (degree >= 2) are left for the mitre pass.
    auto producesWall = [&](const PathLink& l) {
        return rawWall(l) && dupSkip.find(l.id) == dupSkip.end();
    };
    std::unordered_map<int, int> wallDegree;
    for (const auto& link : geometry.links) {
        if (!producesWall(link)) continue;
        wallDegree[link.start]++;
        wallDegree[link.finish]++;
    }

    // Per-link end tangents (unit, pointing away from each node) + node adjacency, for miter joins.
    auto norm2 = [](Vector2 v) {
        float l = std::sqrt(v.x * v.x + v.y * v.y);
        return (l > 1e-6f) ? Vector2{v.x / l, v.y / l} : Vector2{1.0f, 0.0f};
    };
    std::vector<std::pair<Vector2, Vector2>> linkAways(geometry.links.size());  // {startAway, endAway}
    std::unordered_map<int, std::vector<std::pair<int, Vector2>>> nodeAways;     // node -> [(linkIdx, away)]
    for (size_t li = 0; li < geometry.links.size(); ++li) {
        const auto& link = geometry.links[li];
        if (!producesWall(link)) continue;
        auto s = nodePos.find(link.start), f = nodePos.find(link.finish);
        if (s == nodePos.end() || f == nodePos.end()) continue;
        Vector2 P0{s->second.x, s->second.z}, P1{f->second.x, f->second.z};
        Vector2 startAway, endAway;
        if (link.control) {  // quadratic Bézier: end tangents point toward the control point
            Vector2 CP{link.control->position.x, link.control->position.z};
            startAway = norm2({CP.x - P0.x, CP.y - P0.y});
            endAway = norm2({CP.x - P1.x, CP.y - P1.y});
        } else {
            startAway = norm2({P1.x - P0.x, P1.y - P0.y});
            endAway = norm2({P0.x - P1.x, P0.y - P1.y});
        }
        linkAways[li] = {startAway, endAway};
        nodeAways[link.start].push_back({static_cast<int>(li), startAway});
        nodeAways[link.finish].push_back({static_cast<int>(li), endAway});
    }

    // Standard 2D stroke miter between THIS link (forward `tFwd`, `isStart` = flows out of the node)
    // and one neighbour (its away-dir). Result aligns with leftNormal(tFwd) (dot == 1), so it slots
    // in as a per-side perpendicular replacement without twisting. nullopt on a near-degenerate turn.
    auto miter2 = [](Vector2 tFwd, bool isStart, Vector2 otherAway) -> std::optional<Vector2> {
        Vector2 din, dout;
        if (isStart) { dout = tFwd; din = {-otherAway.x, -otherAway.y}; }
        else         { din = tFwd; dout = otherAway; }
        Vector2 nin = {-din.y, din.x};
        Vector2 nout = {-dout.y, dout.x};
        float denom = 1.0f + (nin.x * nout.x + nin.y * nout.y);
        if (std::fabs(denom) < 0.2f) return std::nullopt;
        return Vector2{(nin.x + nout.x) / denom, (nin.y + nout.y) / denom};
    };

    // Per-side miter at a node (works for degree 2 and degree 3+). Sort incident wall links by
    // angle; each side of this link mitres with its angular neighbour. For degree 2 both neighbours
    // are the same link, so left == right (reduces to the symmetric miter). A side with no valid
    // miter falls back to the plain perpendicular (square).
    auto computeMiterSides = [&](int node, int selfLink, Vector2 tFwd, bool isStart) -> MiterSides {
        MiterSides ms;
        Vector2 baseN = {-tFwd.y, tFwd.x};
        auto it = nodeAways.find(node);
        if (it == nodeAways.end() || it->second.size() < 2) return ms;  // dead end -> cap, no miter
        std::vector<std::pair<int, Vector2>> lst = it->second;
        std::sort(lst.begin(), lst.end(), [](const auto& a, const auto& b) {
            return std::atan2(a.second.y, a.second.x) < std::atan2(b.second.y, b.second.x);
        });
        int m = static_cast<int>(lst.size()), self = -1;
        for (int i = 0; i < m; ++i) if (lst[i].first == selfLink) { self = i; break; }
        if (self < 0) return ms;
        // Left of forward = leftNormal(tFwd). In away-angle order that is the CCW neighbour when the
        // link flows out (isStart), and the CW neighbour when it flows in (end).
        Vector2 leftN = isStart ? lst[(self + 1) % m].second : lst[(self - 1 + m) % m].second;
        Vector2 rightN = isStart ? lst[(self - 1 + m) % m].second : lst[(self + 1) % m].second;
        auto Ml = miter2(tFwd, isStart, leftN);
        auto Mr = miter2(tFwd, isStart, rightN);
        ms.left = Ml ? *Ml : baseN;
        ms.right = Mr ? *Mr : baseN;
        ms.has = true;
        return ms;
    };

    for (size_t li = 0; li < geometry.links.size(); ++li) {
        const auto& link = geometry.links[li];
        if (dupSkip.find(link.id) != dupSkip.end()) continue;  // duplicate wall link
        // Resolve which profiles this link sweeps.
        const std::vector<int>* profileIds = nullptr;
        if (!link.profiles.empty()) profileIds = &link.profiles;
        else if (link.useDefaultProfiles && !defaultSet.empty()) profileIds = &defaultSet;
        else continue;

        const bool capStart = enableCaps && (wallDegree[link.start] == 1);
        const bool capEnd = enableCaps && (wallDegree[link.finish] == 1);
        // Forward travel at each end: startAway points forward; endAway points backward, so negate.
        Vector2 fwdStart = linkAways[li].first;
        Vector2 fwdEnd = {-linkAways[li].second.x, -linkAways[li].second.y};
        MiterSides mStart, mEnd;
        if (enableMiter) {
            mStart = computeMiterSides(link.start, static_cast<int>(li), fwdStart, /*isStart=*/true);
            mEnd = computeMiterSides(link.finish, static_cast<int>(li), fwdEnd, /*isStart=*/false);
        }

        auto sIt = nodePos.find(link.start);
        auto fIt = nodePos.find(link.finish);
        if (sIt == nodePos.end() || fIt == nodePos.end()) continue;

        // Build the render-space path: straight, or a subdivided quadratic Bézier. Nodes and the
        // control point are already in render space (game->render done during JSON serialization).
        std::vector<Vector3> path;
        if (link.control) {
            const Vector3& p0 = sIt->second;
            const Vector3& cp = link.control->position;
            const Vector3& p1 = fIt->second;
            const int steps = 10;
            for (int i = 0; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps, u = 1.0f - t;
                Vector3 p;
                p.x = u * u * p0.x + 2 * u * t * cp.x + t * t * p1.x;
                p.y = u * u * p0.y + 2 * u * t * cp.y + t * t * p1.y;
                p.z = u * u * p0.z + 2 * u * t * cp.z + t * t * p1.z;
                path.push_back(p);
            }
        } else {
            path.push_back(sIt->second);
            path.push_back(fIt->second);
        }

        for (int profileId : *profileIds) {
            auto pIt = table.profiles.find(profileId);
            if (pIt == table.profiles.end() || !pIt->second.valid) continue;
            sweepProfile(path, pIt->second, scale, capStart, capEnd, mStart, mEnd, result);
        }
    }

    return result;
}

GeometryMeshCollection createDomainWallMeshes(const Domain& domain, float scale,
                                              const WallProfileTable& table,
                                              bool enableCaps, bool enableMiter) {
    GeometryMeshCollection result = {};
    result.success = true;
    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            GeometryMeshCollection sub = createWallMeshes(geom, scale, table, enableCaps, enableMiter);
            for (auto& mesh : sub.meshes) result.meshes.push_back(std::move(mesh));
        }
    }
    return result;
}
