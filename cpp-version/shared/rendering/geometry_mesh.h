#ifndef GEOMETRY_MESH_H
#define GEOMETRY_MESH_H

#include "raylib.h"
#include "scene_convert/scene_types.h"
#include <vector>
#include <cstdint>

//------------------------------------------------------------------------------
// Geometry Mesh Generation - Creates Raylib meshes from PathGeometry areas
//
// Design:
// - Reads PathGeometry XML data (nodes, links, areas)
// - Tessellates floor areas into triangle meshes
// - Handles Bezier curves for smooth boundaries
// - Uses same lighting shader as tile rendering (bump mapping)
// - Groups meshes by material for efficient rendering
//------------------------------------------------------------------------------

// Scale factor for converting original game units to meters
constexpr float GEOMETRY_SCALE_UNITS_TO_METERS = 0.0254f;

//------------------------------------------------------------------------------
// Data Structures
//------------------------------------------------------------------------------

// Vertex with all attributes for lighting shader
struct GeometryVertex {
    Vector3 position;
    Vector3 normal;      // {0,1,0} for floors (UP)
    float tangent[4];    // {1,0,0,1} for floors (+X, right-handed)
    Vector2 uv;          // Planar-projected texture coordinates
    Color color;         // From material (default WHITE)
};

// Single mesh (one per material)
struct GeometryMesh {
    std::vector<GeometryVertex> vertices;
    std::vector<uint32_t> indices;
    int materialId;              // diffuse texture index (texture0)
    int normalMaterialId = -1;   // bump/normal texture index (texture1); -1 = none
    bool glass = false;          // material drawtype 5: transparent + env-mapped (glass tunnel)
    BoundingBox bounds;
};

// Collection from one PathGeometry
struct GeometryMeshCollection {
    std::vector<GeometryMesh> meshes;
    BoundingBox totalBounds;
    bool success;
    const char* error;
};

// Material definition for geometry (texgen scales from materials.xml)
struct GeometryMaterial {
    int id;
    int diffuseTextureIndex;   // texture0
    int normalTextureIndex;    // texture1
    float texgenScaleS;        // UV scale for S (X)
    float texgenScaleT;        // UV scale for T (Z)
    const char* name;
};

// Rendered geometry state (for viewer)
struct GeometryBatchState {
    Model model;
    int materialId;
    int triangleCount;
    bool valid;
    bool glass = false;   // drawtype 5 (tunnel): drawn in the transparent env-mapped pass
};

struct GeometryMeshState {
    std::vector<GeometryBatchState> batches;
    bool loaded;
    int totalTriangles;
    Vector3 boundsMin;
    Vector3 boundsMax;
};

//------------------------------------------------------------------------------
// Default Materials (from materials.xml)
//------------------------------------------------------------------------------

// Common material definitions
// These match the original game's materials.xml values
static const GeometryMaterial DEFAULT_FLOOR_MATERIAL = {
    0,      // id
    79,     // diffuseTextureIndex (floor texture)
    1,      // normalTextureIndex (bump map)
    0.015625f, // texgenScaleS (1/64 = 64 units per texture repeat)
    0.015625f, // texgenScaleT
    "floor"
};

static const GeometryMaterial DEFAULT_WALL_MATERIAL = {
    1,      // id
    80,     // diffuseTextureIndex (wall texture)
    1,      // normalTextureIndex
    0.02f,  // texgenScaleS (1/50 = 50 units per texture repeat)
    0.02f,  // texgenScaleT
    "wall"
};

//------------------------------------------------------------------------------
// Main API
//------------------------------------------------------------------------------

// Generate meshes from PathGeometry
// geometry: Parsed PathGeometry from XML
// scale: Scale factor (default GEOMETRY_SCALE_UNITS_TO_METERS)
// Returns collection of meshes grouped by material
GeometryMeshCollection createGeometryMeshes(
    const PathGeometry& geometry,
    float scale = GEOMETRY_SCALE_UNITS_TO_METERS
);

// Generate meshes from all geometry in a domain
GeometryMeshCollection createDomainGeometryMeshes(
    const Domain& domain,
    float scale = GEOMETRY_SCALE_UNITS_TO_METERS
);

// Convert mesh collection to raylib Mesh and upload to GPU
// Returns a Mesh that can be used with LoadModelFromMesh
Mesh geometryMeshToRaylibMesh(const GeometryMesh& geoMesh);

// Free mesh collection resources
void freeGeometryMeshCollection(GeometryMeshCollection* collection);

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

// Transform from game space to render space
// Game: X horizontal, Y horizontal (forward), Z vertical (height)
// Render: X horizontal, Y vertical (up), Z horizontal (depth)
inline Vector3 geometryGameToRenderCoords(float gameX, float gameY, float gameZ, float scale) {
    return {
        gameX * scale,      // X unchanged
        gameZ * scale,      // Game Z (height) -> Render Y (up)
        gameY * scale       // Game Y (forward) -> Render Z (depth)
    };
}

inline Vector3 geometryGameToRenderCoords(const Vector3& gamePos, float scale) {
    return geometryGameToRenderCoords(gamePos.x, gamePos.y, gamePos.z, scale);
}

// Generate planar UV coordinates for floor surfaces
// Matches original game's TexGen type 1
inline Vector2 generatePlanarUV(const Vector3& renderPos, float scaleS, float scaleT) {
    // Project onto X-Z plane (floor is horizontal in render space)
    return { renderPos.x * scaleS, renderPos.z * scaleT };
}

// Evaluate quadratic Bezier curve at parameter t
Vector3 evaluateQuadraticBezier(const Vector3& p0, const Vector3& control, const Vector3& p1, float t);

// Subdivide a Bezier curve into linear segments
// segments: number of segments (default 8)
// Returns vector of points along the curve (including start and end)
std::vector<Vector3> subdivideBezierCurve(
    const Vector3& start,
    const Vector3& control,
    const Vector3& end,
    int segments = 8
);

#endif // GEOMETRY_MESH_H
