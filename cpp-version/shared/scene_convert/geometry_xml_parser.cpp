#include "geometry_xml_parser.h"
#include <tinyxml2.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

using namespace tinyxml2;

//------------------------------------------------------------------------------
// Geometry XML Parser
//------------------------------------------------------------------------------

bool parseGeometryXml(std::string_view path, PathGeometry& outGeometry) {
    XMLDocument doc;
    XMLError result = doc.LoadFile(std::string(path).c_str());

    if (result != XML_SUCCESS) {
        std::cerr << "Failed to load geometry XML: " << path << std::endl;
        return false;
    }

    outGeometry = PathGeometry{};
    outGeometry.sourceFile = std::string(path);

    XMLElement* pathRoot = doc.FirstChildElement("Path");
    if (!pathRoot) {
        std::cerr << "No <Path> root element found in: " << path << std::endl;
        return false;
    }

    // Parse Nodes
    XMLElement* nodesElem = pathRoot->FirstChildElement("Nodes");
    if (nodesElem) {
        for (XMLElement* nodeElem = nodesElem->FirstChildElement("Node");
             nodeElem != nullptr;
             nodeElem = nodeElem->NextSiblingElement("Node")) {

            PathNode node;
            node.id = nodeElem->IntAttribute("id", 0);
            node.position.x = nodeElem->FloatAttribute("x", 0.0f);
            node.position.y = nodeElem->FloatAttribute("y", 0.0f);
            node.position.z = nodeElem->FloatAttribute("z", 0.0f);

            outGeometry.nodes.push_back(node);
        }
    }

    // Parse Links
    XMLElement* linksElem = pathRoot->FirstChildElement("Links");
    if (linksElem) {
        for (XMLElement* linkElem = linksElem->FirstChildElement("Link");
             linkElem != nullptr;
             linkElem = linkElem->NextSiblingElement("Link")) {

            PathLink link;
            link.id = linkElem->IntAttribute("id", 0);
            link.start = linkElem->IntAttribute("start", 0);
            link.finish = linkElem->IntAttribute("finish", 0);
            // defaultProfiles="0" opts a profile-less link out of the default wall set.
            link.useDefaultProfiles = (linkElem->IntAttribute("defaultProfiles", 1) != 0);

            // Parse optional Control point
            XMLElement* controlElem = linkElem->FirstChildElement("Control");
            if (controlElem) {
                ControlPoint cp;
                cp.position.x = controlElem->FloatAttribute("x", 0.0f);
                cp.position.y = controlElem->FloatAttribute("y", 0.0f);
                cp.position.z = controlElem->FloatAttribute("z", 0.0f);
                link.control = cp;
            }

            // Parse Profile references
            for (XMLElement* profileElem = linkElem->FirstChildElement("Profile");
                 profileElem != nullptr;
                 profileElem = profileElem->NextSiblingElement("Profile")) {

                int profileId = profileElem->IntAttribute("id", 0);
                link.profiles.push_back(profileId);
            }

            outGeometry.links.push_back(link);
        }
    }

    // Parse Profiles
    XMLElement* profilesElem = pathRoot->FirstChildElement("Profiles");
    if (profilesElem) {
        for (XMLElement* profileElem = profilesElem->FirstChildElement("Profile");
             profileElem != nullptr;
             profileElem = profileElem->NextSiblingElement("Profile")) {

            PathProfile profile;
            profile.id = profileElem->IntAttribute("id", 0);

            // Parse profile points (if any)
            for (XMLElement* pointElem = profileElem->FirstChildElement("Point");
                 pointElem != nullptr;
                 pointElem = pointElem->NextSiblingElement("Point")) {

                Vector2 point;
                point.x = pointElem->FloatAttribute("x", 0.0f);
                point.y = pointElem->FloatAttribute("y", 0.0f);
                profile.points.push_back(point);
            }

            outGeometry.profiles.push_back(profile);
        }
    }

    // Parse Areas
    XMLElement* areasElem = pathRoot->FirstChildElement("Areas");
    if (areasElem) {
        for (XMLElement* areaElem = areasElem->FirstChildElement("Area");
             areaElem != nullptr;
             areaElem = areaElem->NextSiblingElement("Area")) {

            PathArea area;
            area.id = areaElem->IntAttribute("id", 0);
            area.materialId = areaElem->IntAttribute("materialID", 0);

            // Parse Link references
            for (XMLElement* linkRef = areaElem->FirstChildElement("Link");
                 linkRef != nullptr;
                 linkRef = linkRef->NextSiblingElement("Link")) {

                int linkId = linkRef->IntAttribute("id", 0);
                area.links.push_back(linkId);
            }

            outGeometry.areas.push_back(area);
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Collision Generation
//------------------------------------------------------------------------------

// Helper: Build node lookup map
static std::unordered_map<int, const PathNode*> buildNodeMap(const PathGeometry& geom) {
    std::unordered_map<int, const PathNode*> map;
    for (const auto& node : geom.nodes) {
        map[node.id] = &node;
    }
    return map;
}

// Helper: Build link lookup map
static std::unordered_map<int, const PathLink*> buildLinkMap(const PathGeometry& geom) {
    std::unordered_map<int, const PathLink*> map;
    for (const auto& link : geom.links) {
        map[link.id] = &link;
    }
    return map;
}

// Helper: Check if polygon is convex (simplified, assumes 2D)
static bool isConvexPolygon(const std::vector<Vector2>& vertices) {
    if (vertices.size() < 3) return false;

    bool sign = false;
    size_t n = vertices.size();

    for (size_t i = 0; i < n; ++i) {
        float dx1 = vertices[(i + 2) % n].x - vertices[(i + 1) % n].x;
        float dy1 = vertices[(i + 2) % n].y - vertices[(i + 1) % n].y;
        float dx2 = vertices[i].x - vertices[(i + 1) % n].x;
        float dy2 = vertices[i].y - vertices[(i + 1) % n].y;
        float cross = dx1 * dy2 - dy1 * dx2;

        if (i == 0) {
            sign = (cross > 0);
        } else {
            if ((cross > 0) != sign) return false;
        }
    }
    return true;
}

// Helper: Simple ear-clipping triangulation for non-convex polygons
// Returns triangles as triplets of indices
static std::vector<std::array<int, 3>> triangulatePolygon(const std::vector<Vector2>& vertices) {
    std::vector<std::array<int, 3>> triangles;

    if (vertices.size() < 3) return triangles;

    // Simple fan triangulation (works for convex polygons)
    // For non-convex, this is a simplification
    for (size_t i = 1; i + 1 < vertices.size(); ++i) {
        triangles.push_back({0, static_cast<int>(i), static_cast<int>(i + 1)});
    }

    return triangles;
}

void generateCollisionFromGeometry(const PathGeometry& geometry, CollisionData& outCollision) {
    outCollision = CollisionData{};

    auto nodeMap = buildNodeMap(geometry);
    auto linkMap = buildLinkMap(geometry);

    // Track which links are used as area boundaries
    std::unordered_set<int> areaBoundaryLinks;

    // Process each area into a collision polygon
    for (const auto& area : geometry.areas) {
        if (area.links.empty()) continue;

        // Collect vertices from area boundary links
        std::vector<Vector2> vertices;
        std::unordered_set<int> visitedNodes;

        // Follow the links in order to build the polygon boundary
        for (int linkId : area.links) {
            areaBoundaryLinks.insert(linkId);

            auto linkIt = linkMap.find(linkId);
            if (linkIt == linkMap.end()) continue;

            const PathLink* link = linkIt->second;

            // Add start node if not visited
            if (visitedNodes.find(link->start) == visitedNodes.end()) {
                auto nodeIt = nodeMap.find(link->start);
                if (nodeIt != nodeMap.end()) {
                    vertices.push_back({nodeIt->second->position.x, nodeIt->second->position.z});
                    visitedNodes.insert(link->start);
                }
            }

            // For curved links with control points, we could add intermediate points
            // For simplicity, we just use the endpoints

            // Add finish node if not visited
            if (visitedNodes.find(link->finish) == visitedNodes.end()) {
                auto nodeIt = nodeMap.find(link->finish);
                if (nodeIt != nodeMap.end()) {
                    vertices.push_back({nodeIt->second->position.x, nodeIt->second->position.z});
                    visitedNodes.insert(link->finish);
                }
            }
        }

        if (vertices.size() < 3) continue;

        // Check if convex and small enough for Box2D (max 8 vertices)
        if (isConvexPolygon(vertices) && vertices.size() <= 8) {
            CollisionPolygon poly;
            poly.vertices = vertices;
            outCollision.polygons.push_back(poly);
        }
        else {
            // Decompose into triangles
            auto triangles = triangulatePolygon(vertices);
            for (const auto& tri : triangles) {
                CollisionPolygon poly;
                poly.vertices = {
                    vertices[tri[0]],
                    vertices[tri[1]],
                    vertices[tri[2]]
                };
                outCollision.polygons.push_back(poly);
            }
        }
    }

    // Create edge chains for wall segments. A wall is any link that carries a profile (it gets
    // extruded into a wall mesh) — INCLUDING links that also bound a floor area (the shared edge
    // between a room and the outside/next room is still a wall units must collide with). The floor
    // polygons above describe walkable rooms, so they are not solid collision; the walls are.
    (void)areaBoundaryLinks;
    // A link is a wall if it carries an explicit profile OR uses the geometry's default profile set
    // — this must match the wall-MESH selection (createDomainWallMeshes), otherwise interior walls
    // (which rely on the default profiles) render but get no collision.
    const bool hasDefaultProfiles = !geometry.profiles.empty();
    for (const auto& link : geometry.links) {
        const bool isWall = !link.profiles.empty() || (link.useDefaultProfiles && hasDefaultProfiles);
        if (isWall) {
            auto startIt = nodeMap.find(link.start);
            auto finishIt = nodeMap.find(link.finish);

            if (startIt != nodeMap.end() && finishIt != nodeMap.end()) {
                CollisionChain chain;
                chain.loop = false;

                chain.vertices.push_back({startIt->second->position.x, startIt->second->position.z});

                // If there's a control point, add it as an intermediate vertex
                if (link.control) {
                    chain.vertices.push_back({link.control->position.x, link.control->position.z});
                }

                chain.vertices.push_back({finishIt->second->position.x, finishIt->second->position.z});

                outCollision.chains.push_back(chain);
            }
        }
    }
}

void mergePathGeometry(PathGeometry& dst, PathGeometry src) {
    // Offsets = one past the current maximum id in each space (0 when dst is empty).
    int nodeOff = 0, linkOff = 0, areaOff = 0;
    for (const auto& n : dst.nodes) if (n.id + 1 > nodeOff) nodeOff = n.id + 1;
    for (const auto& l : dst.links) if (l.id + 1 > linkOff) linkOff = l.id + 1;
    for (const auto& a : dst.areas) if (a.id + 1 > areaOff) areaOff = a.id + 1;

    for (auto& n : src.nodes) { n.id += nodeOff; dst.nodes.push_back(n); }
    for (auto& l : src.links) {
        l.id += linkOff;
        l.start += nodeOff;
        l.finish += nodeOff;
        dst.links.push_back(std::move(l));
    }
    // Profile ids reference the global materials.xml profile table — union by id, do not offset.
    for (auto& p : src.profiles) {
        bool exists = false;
        for (const auto& q : dst.profiles) if (q.id == p.id) { exists = true; break; }
        if (!exists) dst.profiles.push_back(std::move(p));
    }
    for (auto& a : src.areas) {
        a.id += areaOff;
        for (auto& lid : a.links) lid += linkOff;
        dst.areas.push_back(std::move(a));
    }
    if (!src.sourceFile.empty())
        dst.sourceFile += (dst.sourceFile.empty() ? std::string() : " + ") + src.sourceFile;
}

void mergeDomainSections(Domain& domain) {
    for (auto& area : domain.areas) {
        if (area.geometry.size() <= 1) continue;
        PathGeometry merged = std::move(area.geometry[0]);
        for (size_t i = 1; i < area.geometry.size(); ++i)
            mergePathGeometry(merged, std::move(area.geometry[i]));
        area.geometry.clear();
        area.geometry.push_back(std::move(merged));
    }
}
