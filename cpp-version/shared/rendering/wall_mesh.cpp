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
    int solidType = 0;                // collision: 0 = footprint (st_quad), 1 = outer-edge walls (st_walls)
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
        case 5: {  // Tunnel — cubic-Bézier glass arch, 160 wide x 120 tall, straddling the centreline
                   // (uber loadStandard(5)). Swept along the link it forms the glass tube; collision is
                   // st_walls (two outer-edge walls, not the footprint).
            StandardShape s;
            const Vector2 P0{80, 0}, P1{80, 120}, P2{-80, 120}, P3{-80, 0};
            const int steps = 13;
            std::vector<float> arc(steps + 1, 0.0f);
            for (int i = 0; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps, u = 1.0f - t;
                Vector2 p = {u*u*u*P0.x + 3*u*u*t*P1.x + 3*u*t*t*P2.x + t*t*t*P3.x,
                             u*u*u*P0.y + 3*u*u*t*P1.y + 3*u*t*t*P2.y + t*t*t*P3.y};
                s.points.push_back(p);
                if (i > 0) {
                    float dx = p.x - s.points[i - 1].x, dy = p.y - s.points[i - 1].y;
                    arc[i] = arc[i - 1] + std::sqrt(dx * dx + dy * dy);
                }
            }
            float total = arc[steps] > 0.0f ? arc[steps] : 1.0f;
            for (int i = 0; i <= steps; ++i) s.texT.push_back(2.0f - 2.0f * (arc[i] / total));
            s.dsdx = 1.0f / 80.0f;
            s.cap = false;
            s.solidType = 1;  // st_walls
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

// Per-vertex tangent frame from position + UV gradients (Lengyel's method) — the same
// position/uv-direction derivation the uber engine used. raylib's GenMeshTangents is a coarser,
// non-mikktspace variant and is skipped once a mesh already has tangents, so we compute them here.
// Accumulates a per-triangle tangent/bitangent, then Gram-Schmidt orthonormalises the tangent
// against the vertex normal and derives handedness (w) from the bitangent sign. This keeps
// tangent-space normal mapping consistent across path joins and winding, instead of flipping with an
// explicit path-direction tangent.
void computeVertexTangents(GeometryMesh& mesh) {
    const size_t n = mesh.vertices.size();
    std::vector<Vector3> tanAcc(n, {0, 0, 0}), bitAcc(n, {0, 0, 0});
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
        const Vector3& p0 = mesh.vertices[i0].position;
        const Vector3& p1 = mesh.vertices[i1].position;
        const Vector3& p2 = mesh.vertices[i2].position;
        const Vector2& w0 = mesh.vertices[i0].uv;
        const Vector2& w1 = mesh.vertices[i1].uv;
        const Vector2& w2 = mesh.vertices[i2].uv;
        Vector3 e1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        Vector3 e2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        float du1 = w1.x - w0.x, dv1 = w1.y - w0.y;
        float du2 = w2.x - w0.x, dv2 = w2.y - w0.y;
        float denom = du1 * dv2 - du2 * dv1;
        float r = (std::fabs(denom) > 1e-9f) ? (1.0f / denom) : 0.0f;
        Vector3 sdir = {(dv2 * e1.x - dv1 * e2.x) * r, (dv2 * e1.y - dv1 * e2.y) * r, (dv2 * e1.z - dv1 * e2.z) * r};
        Vector3 tdir = {(du1 * e2.x - du2 * e1.x) * r, (du1 * e2.y - du2 * e1.y) * r, (du1 * e2.z - du2 * e1.z) * r};
        for (uint32_t idx : {i0, i1, i2}) {
            tanAcc[idx].x += sdir.x; tanAcc[idx].y += sdir.y; tanAcc[idx].z += sdir.z;
            bitAcc[idx].x += tdir.x; bitAcc[idx].y += tdir.y; bitAcc[idx].z += tdir.z;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        Vector3 Nn = mesh.vertices[i].normal;
        Vector3 T = tanAcc[i];
        float d = Nn.x * T.x + Nn.y * T.y + Nn.z * T.z;    // Gram-Schmidt: drop the normal component
        T = {T.x - Nn.x * d, T.y - Nn.y * d, T.z - Nn.z * d};
        float len = std::sqrt(T.x * T.x + T.y * T.y + T.z * T.z);
        if (len > 1e-6f) { T = {T.x / len, T.y / len, T.z / len}; }
        else { T = {1.0f, 0.0f, 0.0f}; }
        Vector3 cr = {Nn.y * T.z - Nn.z * T.y, Nn.z * T.x - Nn.x * T.z, Nn.x * T.y - Nn.y * T.x};  // N x T
        float hw = (cr.x * bitAcc[i].x + cr.y * bitAcc[i].y + cr.z * bitAcc[i].z) < 0.0f ? -1.0f : 1.0f;
        mesh.vertices[i].tangent[0] = T.x; mesh.vertices[i].tangent[1] = T.y;
        mesh.vertices[i].tangent[2] = T.z; mesh.vertices[i].tangent[3] = hw;
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

    // Materials: id -> {texture0, texture1, TexGen0 mode}. The child <TexGen0 type=".."/> selects the
    // along-path texture generation: 0=tile (uniform density per world length), 1=stretch (fit one
    // texture span per path section, corner miters ignored), 2=fixed. Default 0 when absent.
    struct Mat { int tex0 = -1; int tex1 = -1; int texgen0 = 0; int drawtype = 0; };
    std::unordered_map<int, Mat> materials;
    if (XMLElement* matsEl = root->FirstChildElement("Materials")) {
        for (XMLElement* m = matsEl->FirstChildElement("Material"); m; m = m->NextSiblingElement("Material")) {
            Mat mat;
            mat.tex0 = m->IntAttribute("texture0", -1);
            mat.tex1 = m->IntAttribute("texture1", -1);
            mat.drawtype = m->IntAttribute("drawtype", 0);
            if (XMLElement* tg = m->FirstChildElement("TexGen0")) mat.texgen0 = tg->IntAttribute("type", 0);
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
            wp.solidType = shape.solidType;

            auto it = materials.find(wp.materialId);
            if (it != materials.end()) {
                wp.diffuseTextureIndex = it->second.tex0;
                wp.normalTextureIndex = it->second.tex1;
                wp.texgenType = it->second.texgen0;
                wp.drawtype = it->second.drawtype;
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
    mesh.materialId = prof.diffuseTextureIndex;        // diffuse (texture0)
    mesh.normalMaterialId = prof.normalTextureIndex;   // bump/normal (texture1)
    mesh.glass = (prof.drawtype == 5);                 // dt_glass: transparent + env-mapped

    // Trim/border profiles (near-zero height, e.g. floor borders) are flat, laterally-wide strips.
    // Sweeping them with along-path U + down-profile V skews the texture badly (very visible once a
    // detailed/number texture is on it). Instead, generate PLANAR UVs from the XZ position, exactly
    // like the floor's texgen — rectilinear and independent of path direction. Wall profiles keep the
    // swept tile UVs.
    float profH = 0.0f;
    {
        float yMin = 1e9f, yMax = -1e9f;
        for (int k = 0; k < N; ++k) { yMin = std::min(yMin, prof.points[k].y); yMax = std::max(yMax, prof.points[k].y); }
        profH = yMax - yMin;
    }
    const bool isTrim = profH < 2.0f;                                    // game units; trim ~0.3, walls ~60
    const float planarScale = (scale > 0.0f) ? (0.015625f / scale) : 0.015625f;  // 1/64 in game units (floor texgen)

    // Emit one cross-section per path point.
    std::vector<Vector2> ringPerp(path.size());  // per-ring lateral axis, for the outward-winding test
    float cumLen = 0.0f;  // render-space horizontal path length
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            float dx = path[i].x - path[i - 1].x, dz = path[i].z - path[i - 1].z;
            cumLen += std::sqrt(dx * dx + dz * dz);
        }
        Vector2 dir = tangentXZ(path, i);   // {dx, dz}
        Vector2 basePerp = {-dir.y, dir.x}; // lateral in the XZ plane (unit)
        ringPerp[i] = basePerp;
        // At a junction end, replace the perpendicular offset with per-side miter vectors so the
        // corner extends to the true intersection. Each side (+/- lateral) mitres with its own
        // angular neighbour, so this works for degree-2 corners and degree-3+ T-junctions alike.
        const MiterSides* junction = nullptr;
        if (i == 0 && startMiter.has) junction = &startMiter;
        else if (i + 1 == path.size() && endMiter.has) junction = &endMiter;
        // U (texture s), uniform per ring per the material's TexGen0 mode. Both modes take the value
        // from the CENTRELINE (which the corner miter does NOT lengthen), so miters are ignored for UV
        // and never introduce the propagating per-side skew:
        //   tile    (0): u = centreline length * dsdx  -> uniform texel density along the wall.
        //   stretch (1): u = section index             -> one texture span fitted per path section.
        // (fixed (2) has no known use here; treat as tile.)
        float s = (prof.texgenType == 1)
                      ? static_cast<float>(i)
                      : (scale > 0.0f ? cumLen / scale : cumLen) * prof.dsdx;
        Vector3 tan = {dir.x, 0.0f, dir.y};

        for (int k = 0; k < N; ++k) {
            float lateral = prof.points[k].x;
            Vector2 perp = basePerp;
            if (junction) perp = (lateral >= 0.0f) ? junction->left : junction->right;
            float latS = lateral * scale;                   // lateral offset (game units -> render)
            float htS = prof.points[k].y * scale;           // height (game units -> render)

            GeometryVertex v;
            v.position = {path[i].x + latS * perp.x, path[i].y + htS, path[i].z + latS * perp.y};
            if (isTrim) {
                // Planar (XZ) UVs + world-X tangent — matches the floor, no path-direction skew.
                v.uv = {v.position.x * planarScale, v.position.z * planarScale};
                v.tangent[0] = 1.0f; v.tangent[1] = 0.0f; v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
            } else {
                v.uv = {s, prof.texcoordT[k]};
                v.tangent[0] = tan.x; v.tangent[1] = tan.y; v.tangent[2] = tan.z; v.tangent[3] = 1.0f;
            }
            v.color = WHITE;
            v.normal = {0, 1, 0};
            mesh.vertices.push_back(v);
        }
    }


    // The swept triangle winding is fixed in index order, but its world orientation depends on the
    // path direction (via `perp`). Opposite-winding paths would flip the geometric normals inward,
    // which reverses both back-face culling and the normal-map lighting. Detect the actual winding
    // (compare the first quad's normal to the profile's known-outward direction at its highest edge,
    // whose outward is ~ +Y) and flip all swept triangles if it faces inward — so normals are always
    // outward regardless of winding.
    bool flipWinding = false;
    if (path.size() >= 2 && N >= 2) {
        int kTop = 0; float bestH = -1e30f;
        for (int k = 0; k + 1 < N; ++k) {
            float mh = 0.5f * (prof.points[k].y + prof.points[k + 1].y);
            if (mh > bestH) { bestH = mh; kTop = k; }
        }
        float tdx = prof.points[kTop + 1].x - prof.points[kTop].x;
        float tdy = prof.points[kTop + 1].y - prof.points[kTop].y;
        float nlat = tdy, nht = -tdx;                       // profile 2D normal (rotate tangent -90)
        if (nht < 0.0f) { nlat = -nlat; nht = -nht; }       // orient outward = up at the top edge
        Vector2 p0 = ringPerp[0];
        Vector3 outward = {nlat * p0.x, nht, nlat * p0.y};
        uint32_t a = static_cast<uint32_t>(kTop), b = static_cast<uint32_t>(N + kTop), c = static_cast<uint32_t>(N + kTop + 1);
        const Vector3& pa = mesh.vertices[a].position;
        const Vector3& pb = mesh.vertices[b].position;
        const Vector3& pc = mesh.vertices[c].position;
        Vector3 ab = {pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
        Vector3 ac = {pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
        Vector3 gn = {ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        flipWinding = (gn.x * outward.x + gn.y * outward.y + gn.z * outward.z) < 0.0f;
    }
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        uint32_t a = static_cast<uint32_t>(i * N);
        uint32_t b = static_cast<uint32_t>((i + 1) * N);
        for (int k = 0; k < N - 1; ++k) {
            if (!flipWinding) {
                mesh.indices.push_back(a + k); mesh.indices.push_back(b + k); mesh.indices.push_back(b + k + 1);
                mesh.indices.push_back(a + k); mesh.indices.push_back(b + k + 1); mesh.indices.push_back(a + k + 1);
            } else {
                mesh.indices.push_back(a + k); mesh.indices.push_back(b + k + 1); mesh.indices.push_back(b + k);
                mesh.indices.push_back(a + k); mesh.indices.push_back(a + k + 1); mesh.indices.push_back(b + k + 1);
            }
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
    computeVertexTangents(mesh);   // proper tangent frame from UV+position (replaces the path-dir tangent)

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

        // Build the render-space path, subdivided into limited-length sections — the uber subdivide()
        // pattern: it both evaluates the Bézier and caps every section's length, so straight and
        // curved links are handled identically and at consistent resolution. Section count is driven
        // by length (~one tile per section) so stretch texgen tiles evenly and tile texgen keeps a
        // uniform density. Nodes/control are already in render space (transformed at serialization).
        const float maxSec = (scale > 0.0f ? 64.0f * scale : 64.0f);  // ~one tile of arc per section
        auto dist3 = [](const Vector3& a, const Vector3& b) {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        std::vector<Vector3> path;
        if (link.control) {
            const Vector3& p0 = sIt->second;
            const Vector3& cp = link.control->position;
            const Vector3& p1 = fIt->second;
            float est = dist3(p0, cp) + dist3(cp, p1);        // control-polygon length (>= arc length)
            int steps = std::max(2, static_cast<int>(std::ceil(est / maxSec)));
            for (int i = 0; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps, u = 1.0f - t;
                path.push_back({u * u * p0.x + 2 * u * t * cp.x + t * t * p1.x,
                                u * u * p0.y + 2 * u * t * cp.y + t * t * p1.y,
                                u * u * p0.z + 2 * u * t * cp.z + t * t * p1.z});
            }
        } else {
            const Vector3& p0 = sIt->second;
            const Vector3& p1 = fIt->second;
            int steps = std::max(1, static_cast<int>(std::ceil(dist3(p0, p1) / maxSec)));
            for (int i = 0; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps;
                path.push_back({p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t, p0.z + (p1.z - p0.z) * t});
            }
        }

        const bool explicitProfiles = !link.profiles.empty();
        for (int profileId : *profileIds) {
            // Swap the border trims (1 = left, 2 = right) for EXPLICIT (non-default) link profiles:
            // the render-space Y-negation flips the lateral (perpendicular) axis, so a single-side
            // trim otherwise lands on the wrong side. The default set carries both, so it's unaffected.
            if (explicitProfiles) {
                if (profileId == 1) profileId = 2;
                else if (profileId == 2) profileId = 1;
            }
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

std::vector<WallCollisionQuad> buildWallCollision(const Domain& domain, const WallProfileTable& table,
                                                  float scale) {
    std::vector<WallCollisionQuad> out;
    constexpr float MIN_WALL_HEIGHT = 2.0f;                 // game units; trim ~0.3, walls ~60
    const float minThick = 0.12f;                          // keep thin footprints collidable (render m)
    const float halfThick = 9.5f * scale;                  // uber st_walls edge half-width (game units)
    const float maxSec = (scale > 0.0f ? 64.0f * scale : 64.0f);

    auto emit = [&](Vector2 p0, Vector2 p1, Vector2 perp, float a, float b) {
        WallCollisionQuad q;
        q.v[0] = {p0.x + perp.x * a, p0.y + perp.y * a};
        q.v[1] = {p1.x + perp.x * a, p1.y + perp.y * a};
        q.v[2] = {p1.x + perp.x * b, p1.y + perp.y * b};
        q.v[3] = {p0.x + perp.x * b, p0.y + perp.y * b};
        out.push_back(q);
    };
    auto dist2 = [](Vector2 a, Vector2 b) { float dx = a.x - b.x, dy = a.y - b.y; return std::sqrt(dx * dx + dy * dy); };

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
                else continue;

                float latMin = 1e9f, latMax = -1e9f;
                bool stWalls = false;
                for (int pid : *profileIds) {
                    auto it = table.profiles.find(pid);
                    if (it == table.profiles.end() || !it->second.valid) continue;
                    float yMin = 1e9f, yMax = -1e9f;
                    for (const auto& pt : it->second.points) { yMin = std::min(yMin, pt.y); yMax = std::max(yMax, pt.y); }
                    if (yMax - yMin < MIN_WALL_HEIGHT) continue;   // trim/border — not collision
                    for (const auto& pt : it->second.points) { latMin = std::min(latMin, pt.x); latMax = std::max(latMax, pt.x); }
                    if (it->second.solidType == 1) stWalls = true;
                }
                if (latMin > latMax) continue;
                latMin *= scale; latMax *= scale;
                // Pull both edges slightly toward the centre-line: the swept profile's lateral extent
                // slightly overshoots the visible wall face, which leaves a thin gap where a wall meets
                // neighbouring geometry (e.g. the tunnel beside its parallel wall). This trims that.
                constexpr float COLLISION_INSET = 0.05f;   // render metres
                if (latMax - latMin > 2.0f * COLLISION_INSET) { latMin += COLLISION_INSET; latMax -= COLLISION_INSET; }
                if (!stWalls && latMax - latMin < minThick) {
                    float c = 0.5f * (latMin + latMax);
                    latMin = c - 0.5f * minThick; latMax = c + 0.5f * minThick;
                }

                auto s = nodePos.find(link.start), f = nodePos.find(link.finish);
                if (s == nodePos.end() || f == nodePos.end()) continue;

                // Path in the render X-Z plane, uniformly subdivided (matches createWallMeshes).
                std::vector<Vector2> pts;
                if (link.control) {
                    Vector2 p0{s->second.x, s->second.z}, cp{link.control->position.x, link.control->position.z},
                            p1{f->second.x, f->second.z};
                    int steps = std::max(2, static_cast<int>(std::ceil((dist2(p0, cp) + dist2(cp, p1)) / maxSec)));
                    for (int i = 0; i <= steps; ++i) {
                        float t = static_cast<float>(i) / steps, u = 1.0f - t;
                        pts.push_back({u*u*p0.x + 2*u*t*cp.x + t*t*p1.x, u*u*p0.y + 2*u*t*cp.y + t*t*p1.y});
                    }
                } else {
                    Vector2 p0{s->second.x, s->second.z}, p1{f->second.x, f->second.z};
                    int steps = std::max(1, static_cast<int>(std::ceil(dist2(p0, p1) / maxSec)));
                    for (int i = 0; i <= steps; ++i) {
                        float t = static_cast<float>(i) / steps;
                        pts.push_back({p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t});
                    }
                }

                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    Vector2 p0 = pts[i], p1 = pts[i + 1];
                    float dx = p1.x - p0.x, dz = p1.y - p0.y, len = std::sqrt(dx * dx + dz * dz);
                    if (len < 1e-5f) continue;
                    Vector2 perp = {-dz / len, dx / len};
                    if (stWalls) {
                        emit(p0, p1, perp, latMax - halfThick, latMax + halfThick);  // outer edge (+lateral)
                        emit(p0, p1, perp, latMin - halfThick, latMin + halfThick);  // outer edge (-lateral)
                    } else {
                        emit(p0, p1, perp, latMin, latMax);                          // full footprint
                    }
                }
            }
        }
    }
    return out;
}
