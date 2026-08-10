#ifndef GEOMETRY_XML_PARSER_H
#define GEOMETRY_XML_PARSER_H

#include "scene_types.h"
#include <string_view>

//------------------------------------------------------------------------------
// Geometry XML Parser
//------------------------------------------------------------------------------

// Parse a geometry XML file (lvl{n}section{m}.xml)
// Returns true on success, false on failure
[[nodiscard]] bool parseGeometryXml(
    std::string_view path,
    PathGeometry& outGeometry
);

// Generate collision data from path geometry
// Creates convex polygons from areas and edge chains from links
void generateCollisionFromGeometry(
    const PathGeometry& geometry,
    CollisionData& outCollision
);

// Merge `src` geometry into `dst`, giving it a UNIFIED id space. Each geometry XML "section" numbers
// its nodes/links/areas from 0, so multiple sections in one area collide on ids (breaking id labels,
// the link table, and any id-based reference). This offsets src's node/link/area ids past dst's
// current maxima and rewrites link start/finish and area link lists to match. Profile ids are global
// (they reference the materials.xml profile table), so they are unioned by id, not offset.
void mergePathGeometry(PathGeometry& dst, PathGeometry src);

// Normalise a whole domain so every area holds at most ONE PathGeometry (all its sections merged
// with a unified id space via mergePathGeometry). Idempotent — a no-op for areas already at one
// section. Apply after loading a domain from JSON (source-XML parsing already merges inline), so
// pre-merge saved JSON with colliding per-section ids is healed on load.
void mergeDomainSections(Domain& domain);

#endif // GEOMETRY_XML_PARSER_H
