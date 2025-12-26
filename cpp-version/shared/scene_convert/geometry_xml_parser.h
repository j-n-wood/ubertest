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

#endif // GEOMETRY_XML_PARSER_H
