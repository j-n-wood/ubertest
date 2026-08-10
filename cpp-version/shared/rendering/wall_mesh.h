#ifndef WALL_MESH_H
#define WALL_MESH_H

#include "raylib.h"
#include "rendering/geometry_mesh.h"
#include "scene_convert/scene_types.h"
#include <string>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------------
// Wall Mesh Generation — ports the legacy uberdroid geometry generator
// (geometryGen.cpp: geogenProfile_t::loadStandard + geogenPath_t::buildLink).
//
// A wall is a PathLink's cross-section *profile* swept along the link path. The
// profile cross-sections are the engine's "standard" profiles (a curved arch wall,
// floor borders, etc.), keyed via materials.xml <Profiles> (default shape + material +
// occlusion height). Stage 1: sweep + strip stitching + UVs + smooth normals/tangents;
// corner mitering (clipProfile) and end caps are deferred.
//------------------------------------------------------------------------------

// One resolved level profile (level profile id -> standard shape + material).
struct WallProfile {
    int id = 0;
    std::vector<Vector2> points;    // cross-section: x = lateral offset, y = height (game units)
    std::vector<float> texcoordT;   // per-point t texture coordinate
    int materialId = -1;
    int diffuseTextureIndex = -1;   // texture0 from materials.xml Material
    int normalTextureIndex = -1;    // texture1
    float dsdx = 1.0f / 64.0f;      // along-path texture scale (per game unit), for tile texgen
    int texgenType = 0;             // material TexGen0 mode: 0=tile (uniform density), 1=stretch, 2=fixed
    int solidType = 0;              // collision: 0 = footprint (st_quad), 1 = outer-edge walls (st_walls, tunnel)
    int drawtype = 0;               // material drawtype: 7 = bump; 5 = glass (bump + env + transparent)
    float occlusionHeight = 0.0f;
    bool cap = false;
    // End-cap geometry (mCapPoints/mCapTriangles): a flat fill of the cross-section, placed at a
    // link's open (dead-end) ends. `capPoints` are {lateral, height} in game units.
    std::vector<Vector2> capPoints;
    std::vector<int> capTriangles;
    bool valid = false;
};

// Resolved profile table, keyed by the level's profile id (0,1,2,...).
struct WallProfileTable {
    std::unordered_map<int, WallProfile> profiles;
    bool loaded = false;
};

// Parse materials.xml (<Materials> + <Profiles>) and resolve each profile against the
// ported standard cross-sections. Returns true on success.
bool loadWallProfiles(const char* materialsXmlPath, WallProfileTable& out);

// Build swept wall meshes for one PathGeometry (one GeometryMesh per link-profile). Each
// mesh's materialId is set to the profile's diffuse texture index for downstream binding.
// `enableCaps`/`enableMiter` toggle end caps and corner miter joins (for A/B comparison).
GeometryMeshCollection createWallMeshes(const PathGeometry& geometry, float scale,
                                        const WallProfileTable& table,
                                        bool enableCaps = true, bool enableMiter = true);

// Build swept wall meshes for a whole domain.
GeometryMeshCollection createDomainWallMeshes(const Domain& domain, float scale,
                                              const WallProfileTable& table,
                                              bool enableCaps = true, bool enableMiter = true);

// One wall-collision footprint quad, corners in the game's 2D physics plane (render X, render Z).
struct WallCollisionQuad { Vector2 v[4]; };

// Build wall-collision quads for a whole domain (shared by the export + the viewer's collision
// wireframe, so both agree). A normal profile emits one quad per path segment spanning its lateral
// extent (real thickness). An st_walls profile (the glass tunnel) instead emits TWO edge quads along
// the OUTER lateral edges (±extent, ~9.5-game-unit half-thickness), leaving the interior walkable.
// Trim/border profiles (near-zero height) are skipped. Coordinates are render-metric.
std::vector<WallCollisionQuad> buildWallCollision(const Domain& domain, const WallProfileTable& table,
                                                  float scale);

#endif // WALL_MESH_H
