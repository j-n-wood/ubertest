#include "geometry_mesh.h"
#include "raymath.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <cmath>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Up normal for all floor surfaces
static constexpr Vector3 UP_NORMAL = {0.0f, 1.0f, 0.0f};

// Tangent for floor surfaces (aligned with +X / U texture coordinate)
// vec4 format: xyz = tangent direction, w = handedness (+1 for right-handed)
static constexpr float FLOOR_TANGENT[4] = {1.0f, 0.0f, 0.0f, 1.0f};

// Number of segments for Bezier curve subdivision
static constexpr int BEZIER_SEGMENTS = 8;

//------------------------------------------------------------------------------
// Bezier Curve Functions
//------------------------------------------------------------------------------

Vector3 evaluateQuadraticBezier(const Vector3& p0, const Vector3& control, const Vector3& p1, float t) {
    float u = 1.0f - t;
    return {
        p0.x * (u * u) + control.x * (2 * u * t) + p1.x * (t * t),
        p0.y * (u * u) + control.y * (2 * u * t) + p1.y * (t * t),
        p0.z * (u * u) + control.z * (2 * u * t) + p1.z * (t * t)
    };
}

std::vector<Vector3> subdivideBezierCurve(
    const Vector3& start,
    const Vector3& control,
    const Vector3& end,
    int segments
) {
    std::vector<Vector3> points;
    points.reserve(segments + 1);

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(evaluateQuadraticBezier(start, control, end, t));
    }

    return points;
}

//------------------------------------------------------------------------------
// Helper: Build lookup maps
//------------------------------------------------------------------------------

static std::unordered_map<int, const PathNode*> buildNodeMap(const PathGeometry& geom) {
    std::unordered_map<int, const PathNode*> map;
    for (const auto& node : geom.nodes) {
        map[node.id] = &node;
    }
    return map;
}

static std::unordered_map<int, const PathLink*> buildLinkMap(const PathGeometry& geom) {
    std::unordered_map<int, const PathLink*> map;
    for (const auto& link : geom.links) {
        map[link.id] = &link;
    }
    return map;
}

//------------------------------------------------------------------------------
// Helper: Collect boundary vertices from area's link chain
//------------------------------------------------------------------------------

static std::vector<Vector3> collectAreaBoundary(
    const PathArea& area,
    const std::unordered_map<int, const PathNode*>& nodeMap,
    const std::unordered_map<int, const PathLink*>& linkMap
) {
    std::vector<Vector3> boundary;

    if (area.links.empty()) return boundary;

    // Track the expected next node to ensure proper link chaining
    int expectedStartNode = -1;

    for (size_t i = 0; i < area.links.size(); ++i) {
        int linkId = area.links[i];
        auto linkIt = linkMap.find(linkId);
        if (linkIt == linkMap.end()) {
            TraceLog(LOG_WARNING, "GEOMETRY: Link %d not found for area %d", linkId, area.id);
            continue;
        }

        const PathLink* link = linkIt->second;

        // Determine if we need to traverse this link in reverse
        bool reverse = false;
        if (expectedStartNode >= 0) {
            if (link->finish == expectedStartNode) {
                reverse = true;
            } else if (link->start != expectedStartNode) {
                TraceLog(LOG_WARNING, "GEOMETRY: Link chain broken at link %d for area %d", linkId, area.id);
            }
        }

        int startNodeId = reverse ? link->finish : link->start;
        int endNodeId = reverse ? link->start : link->finish;

        auto startIt = nodeMap.find(startNodeId);
        auto endIt = nodeMap.find(endNodeId);

        if (startIt == nodeMap.end() || endIt == nodeMap.end()) {
            TraceLog(LOG_WARNING, "GEOMETRY: Node not found for link %d", linkId);
            continue;
        }

        Vector3 startPos = startIt->second->position;
        Vector3 endPos = endIt->second->position;

        // Handle Bezier curves
        if (link->control.has_value()) {
            Vector3 ctrlPos = link->control->position;

            // Subdivide the Bezier curve
            std::vector<Vector3> curvePoints;
            if (reverse) {
                curvePoints = subdivideBezierCurve(endPos, ctrlPos, startPos, BEZIER_SEGMENTS);
            } else {
                curvePoints = subdivideBezierCurve(startPos, ctrlPos, endPos, BEZIER_SEGMENTS);
            }

            // Add all points except the last (to avoid duplicating with next link's start)
            for (size_t j = 0; j < curvePoints.size() - 1; ++j) {
                boundary.push_back(curvePoints[j]);
            }
        } else {
            // Straight line - just add start point
            boundary.push_back(startPos);
        }

        expectedStartNode = endNodeId;
    }

    return boundary;
}

//------------------------------------------------------------------------------
// Helper: Ear-clipping triangulation for non-convex polygons
//------------------------------------------------------------------------------

// Check if point is inside triangle (2D, using XZ plane for floor polygons in render space)
// Note: In render space, Y is up (height), X-Z is the floor plane
static bool pointInTriangle2D(
    float px, float pz,
    float ax, float az,
    float bx, float bz,
    float cx, float cz
) {
    float v0x = cx - ax, v0z = cz - az;
    float v1x = bx - ax, v1z = bz - az;
    float v2x = px - ax, v2z = pz - az;

    float dot00 = v0x * v0x + v0z * v0z;
    float dot01 = v0x * v1x + v0z * v1z;
    float dot02 = v0x * v2x + v0z * v2z;
    float dot11 = v1x * v1x + v1z * v1z;
    float dot12 = v1x * v2x + v1z * v2z;

    float denom = dot00 * dot11 - dot01 * dot01;
    if (fabsf(denom) < 1e-10f) return false;  // Degenerate triangle

    float invDenom = 1.0f / denom;
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

// Calculate signed area of triangle (2D in XZ plane)
static float triangleArea2D(float ax, float az, float bx, float bz, float cx, float cz) {
    return (bx - ax) * (cz - az) - (cx - ax) * (bz - az);
}

// Check if vertex at index i is an ear (can be clipped)
// Uses XZ plane for floor polygon triangulation in render space
static bool isEar(const std::vector<Vector3>& polygon, const std::vector<int>& indices, int i) {
    int n = static_cast<int>(indices.size());
    if (n < 3) return false;

    int prev = (i - 1 + n) % n;
    int next = (i + 1) % n;

    const Vector3& a = polygon[indices[prev]];
    const Vector3& b = polygon[indices[i]];
    const Vector3& c = polygon[indices[next]];

    // Convex vertex test. The polygon is normalized to positive shoelace area (see
    // triangulatePolygon), and triangleArea2D shares that sign convention, so a convex vertex has
    // area > 0. (The previous `>= 0 -> reject` inverted this, so no ear was ever found and every
    // polygon fell back to a fan — which is wrong for concave areas.)
    float area = triangleArea2D(a.x, a.z, b.x, b.z, c.x, c.z);
    if (area <= 0) return false;  // reflex or degenerate

    // Check if any other vertex is inside this triangle
    for (int j = 0; j < n; ++j) {
        if (j == prev || j == i || j == next) continue;

        const Vector3& p = polygon[indices[j]];
        if (pointInTriangle2D(p.x, p.z, a.x, a.z, b.x, b.z, c.x, c.z)) {
            return false;
        }
    }

    return true;
}

// Ear-clipping triangulation
// Uses XZ plane for floor polygon triangulation in render space
static std::vector<std::array<int, 3>> triangulatePolygon(const std::vector<Vector3>& polygon) {
    std::vector<std::array<int, 3>> triangles;

    if (polygon.size() < 3) return triangles;

    // Initialize index list
    std::vector<int> indices;
    indices.reserve(polygon.size());

    // Calculate polygon winding (using XZ plane in render space)
    float signedArea = 0;
    for (size_t i = 0; i < polygon.size(); ++i) {
        size_t j = (i + 1) % polygon.size();
        signedArea += polygon[i].x * polygon[j].z;
        signedArea -= polygon[j].x * polygon[i].z;
    }

    // Ensure CCW winding for ear clipping (when looking down -Y axis)
    if (signedArea > 0) {
        for (size_t i = 0; i < polygon.size(); ++i) {
            indices.push_back(static_cast<int>(i));
        }
    } else {
        for (int i = static_cast<int>(polygon.size()) - 1; i >= 0; --i) {
            indices.push_back(i);
        }
    }

    // Ear clipping loop
    int iterations = 0;
    int maxIterations = static_cast<int>(polygon.size()) * 2;

    while (indices.size() > 3 && iterations < maxIterations) {
        bool earFound = false;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (isEar(polygon, indices, static_cast<int>(i))) {
                int n = static_cast<int>(indices.size());
                int prev = (static_cast<int>(i) - 1 + n) % n;
                int next = (static_cast<int>(i) + 1) % n;

                triangles.push_back({indices[prev], indices[i], indices[next]});
                indices.erase(indices.begin() + i);
                earFound = true;
                break;
            }
        }

        if (!earFound) {
            // Rare (numerical/degenerate) fallback: fan the REMAINING indices, which are already
            // CCW-normalized, so the winding stays consistent — and keep the ears clipped so far.
            for (size_t k = 1; k + 1 < indices.size(); ++k) {
                triangles.push_back({indices[0], indices[static_cast<int>(k)], indices[static_cast<int>(k + 1)]});
            }
            indices.clear();
            break;
        }

        iterations++;
    }

    // Handle remaining triangle
    if (indices.size() == 3) {
        triangles.push_back({indices[0], indices[1], indices[2]});
    }

    return triangles;
}

//------------------------------------------------------------------------------
// Mesh Generation
//------------------------------------------------------------------------------

GeometryMeshCollection createGeometryMeshes(const PathGeometry& geometry, float scale) {
    GeometryMeshCollection result = {};
    result.success = false;

    if (geometry.areas.empty()) {
        result.success = true;
        return result;
    }

    auto nodeMap = buildNodeMap(geometry);
    auto linkMap = buildLinkMap(geometry);

    // Group areas by material
    std::unordered_map<int, std::vector<const PathArea*>> areasByMaterial;
    for (const auto& area : geometry.areas) {
        areasByMaterial[area.materialId].push_back(&area);
    }

    // Track overall bounds
    bool firstVertex = true;
    Vector3 totalMin = {0, 0, 0};
    Vector3 totalMax = {0, 0, 0};

    // Process each material group
    for (const auto& [materialId, areas] : areasByMaterial) {
        GeometryMesh geoMesh;
        geoMesh.materialId = materialId;
        geoMesh.bounds = {{0, 0, 0}, {0, 0, 0}};

        bool meshFirstVertex = true;

        // Get material texgen scales (use default floor material for now).
        // texgenScale is defined per *game unit* (e.g. 1/64 = one repeat per 64 units), but the
        // boundary positions are already in render space (metres = game units * scale). Divide by
        // scale so the texture still repeats every N game units — otherwise it repeats every N
        // metres, magnifying the texture ~1/scale (~40x) across the floor and flattening the bump.
        const float uvScale = (scale > 0.0f) ? scale : 1.0f;
        float scaleS = DEFAULT_FLOOR_MATERIAL.texgenScaleS / uvScale;
        float scaleT = DEFAULT_FLOOR_MATERIAL.texgenScaleT / uvScale;

        for (const PathArea* area : areas) {
            // Collect boundary vertices
            // NOTE: boundary points are already in render space (from JSON)
            // because pathNodeToJson transforms game->render when writing JSON
            // and jsonToPathNode loads render coords directly when reading
            std::vector<Vector3> boundary = collectAreaBoundary(*area, nodeMap, linkMap);

            if (boundary.size() < 3) {
                TraceLog(LOG_WARNING, "GEOMETRY: Area %d has fewer than 3 boundary vertices", area->id);
                continue;
            }

            // Boundary is already in render space from JSON, no transform needed
            // The scale factor was already applied during JSON serialization

            // Triangulate the polygon (using XZ plane in render space)
            auto triangles = triangulatePolygon(boundary);

            if (triangles.empty()) {
                TraceLog(LOG_WARNING, "GEOMETRY: Failed to triangulate area %d", area->id);
                continue;
            }

            // Generate vertices and indices
            uint32_t baseIndex = static_cast<uint32_t>(geoMesh.vertices.size());

            // Add all boundary vertices (already in render space)
            for (size_t i = 0; i < boundary.size(); ++i) {
                GeometryVertex vertex;
                vertex.position = boundary[i];
                vertex.normal = UP_NORMAL;
                vertex.tangent[0] = FLOOR_TANGENT[0];
                vertex.tangent[1] = FLOOR_TANGENT[1];
                vertex.tangent[2] = FLOOR_TANGENT[2];
                vertex.tangent[3] = FLOOR_TANGENT[3];
                vertex.uv = generatePlanarUV(boundary[i], scaleS, scaleT);
                vertex.color = WHITE;

                geoMesh.vertices.push_back(vertex);

                // Update bounds
                if (meshFirstVertex) {
                    geoMesh.bounds.min = geoMesh.bounds.max = vertex.position;
                    meshFirstVertex = false;
                } else {
                    geoMesh.bounds.min.x = std::min(geoMesh.bounds.min.x, vertex.position.x);
                    geoMesh.bounds.min.y = std::min(geoMesh.bounds.min.y, vertex.position.y);
                    geoMesh.bounds.min.z = std::min(geoMesh.bounds.min.z, vertex.position.z);
                    geoMesh.bounds.max.x = std::max(geoMesh.bounds.max.x, vertex.position.x);
                    geoMesh.bounds.max.y = std::max(geoMesh.bounds.max.y, vertex.position.y);
                    geoMesh.bounds.max.z = std::max(geoMesh.bounds.max.z, vertex.position.z);
                }

                // Update total bounds
                if (firstVertex) {
                    totalMin = totalMax = vertex.position;
                    firstVertex = false;
                } else {
                    totalMin.x = std::min(totalMin.x, vertex.position.x);
                    totalMin.y = std::min(totalMin.y, vertex.position.y);
                    totalMin.z = std::min(totalMin.z, vertex.position.z);
                    totalMax.x = std::max(totalMax.x, vertex.position.x);
                    totalMax.y = std::max(totalMax.y, vertex.position.y);
                    totalMax.z = std::max(totalMax.z, vertex.position.z);
                }
            }

            // Emit each triangle with the winding that makes it face up (+Y), matching the hardcoded
            // UP_NORMAL. This is robust to the triangulator's winding and the source Y reflection: a
            // triangle's normal.y = -triangleArea2D, so area < 0 means it already faces up.
            for (const auto& tri : triangles) {
                const Vector3& p0 = boundary[tri[0]];
                const Vector3& p1 = boundary[tri[1]];
                const Vector3& p2 = boundary[tri[2]];
                float a2 = triangleArea2D(p0.x, p0.z, p1.x, p1.z, p2.x, p2.z);
                geoMesh.indices.push_back(baseIndex + tri[0]);
                if (a2 < 0.0f) {
                    geoMesh.indices.push_back(baseIndex + tri[1]);
                    geoMesh.indices.push_back(baseIndex + tri[2]);
                } else {
                    geoMesh.indices.push_back(baseIndex + tri[2]);
                    geoMesh.indices.push_back(baseIndex + tri[1]);
                }
            }
        }

        if (!geoMesh.vertices.empty()) {
            result.meshes.push_back(std::move(geoMesh));
        }
    }

    result.totalBounds = {totalMin, totalMax};
    result.success = true;

    TraceLog(LOG_INFO, "GEOMETRY: Generated %zu meshes from %zu areas",
             result.meshes.size(), geometry.areas.size());

    return result;
}

GeometryMeshCollection createDomainGeometryMeshes(const Domain& domain, float scale) {
    GeometryMeshCollection result = {};
    result.success = true;

    bool firstVertex = true;
    Vector3 totalMin = {0, 0, 0};
    Vector3 totalMax = {0, 0, 0};

    // Process geometry from all areas
    for (const auto& area : domain.areas) {
        for (const auto& geom : area.geometry) {
            GeometryMeshCollection areaResult = createGeometryMeshes(geom, scale);

            if (areaResult.success) {
                // Merge meshes into result
                for (auto& mesh : areaResult.meshes) {
                    result.meshes.push_back(std::move(mesh));
                }

                // Update bounds
                if (!areaResult.meshes.empty()) {
                    if (firstVertex) {
                        totalMin = areaResult.totalBounds.min;
                        totalMax = areaResult.totalBounds.max;
                        firstVertex = false;
                    } else {
                        totalMin.x = std::min(totalMin.x, areaResult.totalBounds.min.x);
                        totalMin.y = std::min(totalMin.y, areaResult.totalBounds.min.y);
                        totalMin.z = std::min(totalMin.z, areaResult.totalBounds.min.z);
                        totalMax.x = std::max(totalMax.x, areaResult.totalBounds.max.x);
                        totalMax.y = std::max(totalMax.y, areaResult.totalBounds.max.y);
                        totalMax.z = std::max(totalMax.z, areaResult.totalBounds.max.z);
                    }
                }
            }
        }
    }

    result.totalBounds = {totalMin, totalMax};

    TraceLog(LOG_INFO, "GEOMETRY: Generated %zu total meshes from domain",
             result.meshes.size());

    return result;
}

//------------------------------------------------------------------------------
// Convert to Raylib Mesh
//------------------------------------------------------------------------------

Mesh geometryMeshToRaylibMesh(const GeometryMesh& geoMesh) {
    Mesh mesh = {0};

    if (geoMesh.vertices.empty() || geoMesh.indices.empty()) {
        return mesh;
    }

    int vertexCount = static_cast<int>(geoMesh.vertices.size());
    int indexCount = static_cast<int>(geoMesh.indices.size());

    mesh.vertexCount = vertexCount;
    mesh.triangleCount = indexCount / 3;

    // Allocate arrays
    mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.tangents = (float*)MemAlloc(vertexCount * 4 * sizeof(float));
    mesh.colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));
    mesh.indices = (unsigned short*)MemAlloc(indexCount * sizeof(unsigned short));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals ||
        !mesh.tangents || !mesh.colors || !mesh.indices) {
        if (mesh.vertices) MemFree(mesh.vertices);
        if (mesh.texcoords) MemFree(mesh.texcoords);
        if (mesh.normals) MemFree(mesh.normals);
        if (mesh.tangents) MemFree(mesh.tangents);
        if (mesh.colors) MemFree(mesh.colors);
        if (mesh.indices) MemFree(mesh.indices);
        return {0};
    }

    // Fill vertex data
    for (int i = 0; i < vertexCount; ++i) {
        const GeometryVertex& v = geoMesh.vertices[i];

        mesh.vertices[i * 3 + 0] = v.position.x;
        mesh.vertices[i * 3 + 1] = v.position.y;
        mesh.vertices[i * 3 + 2] = v.position.z;

        mesh.texcoords[i * 2 + 0] = v.uv.x;
        mesh.texcoords[i * 2 + 1] = v.uv.y;

        mesh.normals[i * 3 + 0] = v.normal.x;
        mesh.normals[i * 3 + 1] = v.normal.y;
        mesh.normals[i * 3 + 2] = v.normal.z;

        mesh.tangents[i * 4 + 0] = v.tangent[0];
        mesh.tangents[i * 4 + 1] = v.tangent[1];
        mesh.tangents[i * 4 + 2] = v.tangent[2];
        mesh.tangents[i * 4 + 3] = v.tangent[3];

        mesh.colors[i * 4 + 0] = v.color.r;
        mesh.colors[i * 4 + 1] = v.color.g;
        mesh.colors[i * 4 + 2] = v.color.b;
        mesh.colors[i * 4 + 3] = v.color.a;
    }

    // Fill index data
    for (int i = 0; i < indexCount; ++i) {
        mesh.indices[i] = static_cast<unsigned short>(geoMesh.indices[i]);
    }

    // Upload to GPU
    UploadMesh(&mesh, false);

    return mesh;
}

//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------

void freeGeometryMeshCollection(GeometryMeshCollection* collection) {
    if (!collection) return;

    collection->meshes.clear();
    collection->success = false;
    collection->error = nullptr;
}
