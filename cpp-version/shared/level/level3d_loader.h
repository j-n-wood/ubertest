#ifndef LEVEL3D_LOADER_H
#define LEVEL3D_LOADER_H

#include "level/level_types.h"
#include "level/door_manager.h"      // DoorSpec
#include "level/charger_manager.h"   // ChargerSpec
#include "level/console_manager.h"   // ConsoleSpec
#include "level/object_manager.h"    // ObjectSpec
#include "level/lift_manager.h"      // TransporterSpec
#include "effects/decal_manager.h"   // Decal
#include "rendering/scene_renderer.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Converted 3D-level bundle loader (Objects3D render mode).
//
// Loads a self-contained per-deck bundle from
//   <assetPath>/ships/ship1/levels3d/level_<n>/
//     level_<n>.gltf          -> data.tileModel (floor/wall/tile geometry, scene-shaded)
//     level_<n>.entities.json -> data.waypointPositions / waypointAdjacency / waypointLinks
// All in the domain metric frame, so spawns + AI (which read waypointPositions) place units in the
// same frame as the geometry. Collision + objects (doors/chargers) are loaded separately.
//
// Returns true if the geometry loaded (caller falls back to the TMX build otherwise).
//------------------------------------------------------------------------------
bool load3DLevel(const std::string& assetPath, int levelNumber, SceneRenderer* renderer,
                 LevelRenderData& data);

// Load ONLY the waypoints (positions + adjacency) for a level from its bundle entities.json — no
// geometry/GPU work. Lets the game place a deck's enemy roster in its own world without building the
// deck's full render data (eager population at ship load). Returns true if any waypoint was read.
bool load3DLevelWaypoints(const std::string& assetPath, int levelNumber, LevelRenderData& data);

// Read doors / chargers from the bundle's entities.json in the domain frame (positions in metres),
// so Objects3D places them on the 3D geometry rather than at TMX-derived positions.
void load3DLevelDoors(const std::string& assetPath, int levelNumber, std::vector<DoorSpec>& out);
void load3DLevelChargers(const std::string& assetPath, int levelNumber, std::vector<ChargerSpec>& out);
void load3DLevelConsoles(const std::string& assetPath, int levelNumber, std::vector<ConsoleSpec>& out);
void load3DLevelObjects(const std::string& assetPath, int levelNumber, std::vector<ObjectSpec>& out);

// Read level-authored (permanent) floor decals from the bundle's entities.json `decals[]` array.
// Each record's `type` (uber feature id 29..34) selects the decal texture, half-extent, and aspect;
// `pos`/`rot` are render-metric. Output decals are non-cleanable (caller adds them via
// DecalManager::addLevelDecal). See docs/decals.md.
void load3DLevelDecals(const std::string& assetPath, int levelNumber, std::vector<Decal>& out);

// Static wall collision from the bundle's level_<n>.collision.json, in the game's 2D physics plane
// (render X, render Z), already metric. Polygons are CCW convex solids; chains are wall outlines.
struct Collision3D {
    std::vector<std::vector<Vector2>> polygons;
    struct Chain { std::vector<Vector2> verts; bool loop = false; };
    std::vector<Chain> chains;
};
bool load3DLevelCollision(const std::string& assetPath, int levelNumber, Collision3D& out);

// Read the ship-wide lift network from <assetPath>/ships/ship1/levels3d/transporters.json
// (render-metric frame). Not per-level — the whole deck set shares one file. Returns false if
// the file is missing/unreadable (caller falls back to the TMX lift build).
bool load3DLevelTransporters(const std::string& assetPath, std::vector<TransporterSpec>& out);

#endif // LEVEL3D_LOADER_H
