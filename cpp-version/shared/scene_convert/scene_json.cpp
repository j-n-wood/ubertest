#include "scene_json.h"
#include "rendering/tile_mesh.h"  // For coordinate transform and scale constant
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>  // For std::min, std::max

using json = nlohmann::json;

//------------------------------------------------------------------------------
// JSON Conversion Helpers - Vector Types
//------------------------------------------------------------------------------

static json vector2ToJson(const Vector2& v) {
    return json::array({v.x, v.y});
}

static json vector3ToJson(const Vector3& v) {
    return json::array({v.x, v.y, v.z});
}

static Vector2 jsonToVector2(const json& j) {
    return {j[0].get<float>(), j[1].get<float>()};
}

static Vector3 jsonToVector3(const json& j) {
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// Coordinate transform applied to positions on save. The JSON stores render space, and the loader
// reads it verbatim (no transform). The convert path feeds game-space data, so it must transform
// (game -> render). But re-saving an already-render-space domain (e.g. the viewer's loadedDomain)
// must NOT transform again — otherwise coordinates are scaled/permuted twice and reload is garbage.
// saveDomainToFile sets this per call; default true preserves the original convert behaviour.
static bool g_saveTransformCoords = true;
static Vector3 saveCoords(const Vector3& p) {
    return g_saveTransformCoords ? gameToRenderCoords(p, SCALE_UNITS_TO_METERS) : p;
}

//------------------------------------------------------------------------------
// JSON Conversion - Bounds and Rect
//------------------------------------------------------------------------------

// Transform bounds from game space to render space during serialization
static json boundsToJson(const Bounds& b) {
    Vector3 renderMin = saveCoords(b.min);
    Vector3 renderMax = saveCoords(b.max);
    // After axis swap, min/max may need recalculation
    return {
        {"min", vector3ToJson({
            std::min(renderMin.x, renderMax.x),
            std::min(renderMin.y, renderMax.y),
            std::min(renderMin.z, renderMax.z)
        })},
        {"max", vector3ToJson({
            std::max(renderMin.x, renderMax.x),
            std::max(renderMin.y, renderMax.y),
            std::max(renderMin.z, renderMax.z)
        })}
    };
}

static Bounds jsonToBounds(const json& j) {
    Bounds b;
    b.min = jsonToVector3(j["min"]);
    b.max = jsonToVector3(j["max"]);
    return b;
}

static json rectToJson(const Rect& r) {
    return {{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
}

static Rect jsonToRect(const json& j) {
    return {
        j.value("x", 0),
        j.value("y", 0),
        j.value("w", 0),
        j.value("h", 0)
    };
}

//------------------------------------------------------------------------------
// JSON Conversion - Collision
//------------------------------------------------------------------------------

static json collisionPolygonToJson(const CollisionPolygon& p) {
    json vertices = json::array();
    for (const auto& v : p.vertices) {
        vertices.push_back(vector2ToJson(v));
    }
    return {{"vertices", vertices}};
}

static CollisionPolygon jsonToCollisionPolygon(const json& j) {
    CollisionPolygon p;
    for (const auto& v : j["vertices"]) {
        p.vertices.push_back(jsonToVector2(v));
    }
    return p;
}

static json collisionChainToJson(const CollisionChain& c) {
    json vertices = json::array();
    for (const auto& v : c.vertices) {
        vertices.push_back(vector2ToJson(v));
    }
    json result = {{"vertices", vertices}};
    if (c.loop) result["loop"] = true;
    return result;
}

static CollisionChain jsonToCollisionChain(const json& j) {
    CollisionChain c;
    for (const auto& v : j["vertices"]) {
        c.vertices.push_back(jsonToVector2(v));
    }
    c.loop = j.value("loop", false);
    return c;
}

static json collisionDataToJson(const CollisionData& cd) {
    json polygons = json::array();
    for (const auto& p : cd.polygons) {
        polygons.push_back(collisionPolygonToJson(p));
    }
    json chains = json::array();
    for (const auto& c : cd.chains) {
        chains.push_back(collisionChainToJson(c));
    }
    return {{"polygons", polygons}, {"chains", chains}};
}

static CollisionData jsonToCollisionData(const json& j) {
    CollisionData cd;
    if (j.contains("polygons")) {
        for (const auto& p : j["polygons"]) {
            cd.polygons.push_back(jsonToCollisionPolygon(p));
        }
    }
    if (j.contains("chains")) {
        for (const auto& c : j["chains"]) {
            cd.chains.push_back(jsonToCollisionChain(c));
        }
    }
    return cd;
}

static json featureCollisionToJson(const FeatureCollision& fc) {
    json result;
    if (fc.type == FeatureCollision::Type::Box) {
        result["type"] = "box";
        result["halfExtents"] = vector2ToJson(fc.halfExtents);
    } else {
        result["type"] = "circle";
        result["radius"] = fc.radius;
    }
    return result;
}

static FeatureCollision jsonToFeatureCollision(const json& j) {
    FeatureCollision fc;
    std::string type = j.value("type", "box");
    if (type == "circle") {
        fc.type = FeatureCollision::Type::Circle;
        fc.radius = j.value("radius", 0.5f);
    } else {
        fc.type = FeatureCollision::Type::Box;
        if (j.contains("halfExtents")) {
            fc.halfExtents = jsonToVector2(j["halfExtents"]);
        }
    }
    return fc;
}

//------------------------------------------------------------------------------
// JSON Conversion - Tile
//------------------------------------------------------------------------------

// Transform tile vertex position from game space to render space during serialization
// Game: X horizontal, Y horizontal (forward), Z vertical (height)
// Render: X horizontal, Y vertical (up), Z horizontal (depth)
static json tileVertexToJson(const TileVertex& v) {
    // Transform position to render space (Y-up) with scaling
    Vector3 renderPos = saveCoords(v.position);
    return {
        {"position", vector3ToJson(renderPos)},
        {"uv1", vector2ToJson(v.uv1)},
        {"uv2", vector2ToJson(v.uv2)}
    };
}

static TileVertex jsonToTileVertex(const json& j) {
    TileVertex v;
    v.position = jsonToVector3(j["position"]);
    v.uv1 = jsonToVector2(j["uv1"]);
    v.uv2 = jsonToVector2(j["uv2"]);
    return v;
}

static json tileTexturesToJson(const TileTextures& t) {
    json result;
    if (!t.diffuse.empty()) result["diffuse"] = t.diffuse;
    if (!t.bump.empty()) result["bump"] = t.bump;
    if (!t.effect.empty()) result["effect"] = t.effect;
    return result;
}

static TileTextures jsonToTileTextures(const json& j) {
    TileTextures t;
    t.diffuse = j.value("diffuse", "");
    t.bump = j.value("bump", "");
    t.effect = j.value("effect", "");
    return t;
}

static json tilePropertiesToJson(const TileProperties& p) {
    json result;
    // Only include non-default values
    if (p.diffuseColour.x != 1 || p.diffuseColour.y != 1 || p.diffuseColour.z != 1) {
        result["diffuseColour"] = vector3ToJson(p.diffuseColour);
    }
    if (p.specularColour.x != 0 || p.specularColour.y != 0 || p.specularColour.z != 0) {
        result["specularColour"] = vector3ToJson(p.specularColour);
    }
    if (p.tileType != 0) result["tileType"] = p.tileType;
    if (p.additiveBlend) result["additiveBlend"] = true;
    if (p.alphaBlend) result["alphaBlend"] = true;
    if (p.effectTexture >= 0) result["effectTexture"] = p.effectTexture;
    if (p.texRotate) result["texRotate"] = true;
    return result;
}

static TileProperties jsonToTileProperties(const json& j) {
    TileProperties p;
    if (j.contains("diffuseColour")) p.diffuseColour = jsonToVector3(j["diffuseColour"]);
    if (j.contains("specularColour")) p.specularColour = jsonToVector3(j["specularColour"]);
    p.tileType = j.value("tileType", 0);
    p.additiveBlend = j.value("additiveBlend", false);
    p.alphaBlend = j.value("alphaBlend", false);
    p.effectTexture = j.value("effectTexture", -1);
    p.texRotate = j.value("texRotate", false);
    return p;
}

static json tileToJson(const Tile& t) {
    json vertices = json::array();
    for (const auto& v : t.vertices) {
        vertices.push_back(tileVertexToJson(v));
    }
    json result = {{"vertices", vertices}};

    json textures = tileTexturesToJson(t.textures);
    if (!textures.empty()) result["textures"] = textures;

    json props = tilePropertiesToJson(t.properties);
    if (!props.empty()) result["properties"] = props;

    // Include original indices for reference
    result["textureIndex1"] = t.textureIndex1;
    result["textureIndex2"] = t.textureIndex2;

    return result;
}

static int tileLoadCounter = 0;

static Tile jsonToTile(const json& j) {
    Tile t;
    for (const auto& v : j["vertices"]) {
        t.vertices.push_back(jsonToTileVertex(v));
    }
    if (j.contains("textures")) t.textures = jsonToTileTextures(j["textures"]);
    if (j.contains("properties")) t.properties = jsonToTileProperties(j["properties"]);
    t.textureIndex1 = j.value("textureIndex1", 0);
    t.textureIndex2 = j.value("textureIndex2", 0);

    // Log first few tiles for debugging
    if (tileLoadCounter < 3) {
        std::cout << "JSON_LOADER: Tile " << tileLoadCounter << " loaded with "
                  << t.vertices.size() << " vertices:" << std::endl;
        for (size_t i = 0; i < t.vertices.size(); ++i) {
            std::cout << "  v[" << i << "] = (" << t.vertices[i].position.x << ", "
                      << t.vertices[i].position.y << ", " << t.vertices[i].position.z << ")"
                      << std::endl;
        }
    }
    tileLoadCounter++;

    return t;
}

//------------------------------------------------------------------------------
// JSON Conversion - Feature
//------------------------------------------------------------------------------

static json featureFlagsToJson(const FeatureFlags& f) {
    json result;
    if (f.indestructible) result["indestructible"] = true;
    if (!f.solid) result["solid"] = false;  // Default is true, only store false
    if (f.fullbright) result["fullbright"] = true;
    return result;
}

static FeatureFlags jsonToFeatureFlags(const json& j) {
    FeatureFlags f;
    f.indestructible = j.value("indestructible", false);
    f.solid = j.value("solid", true);
    f.fullbright = j.value("fullbright", false);
    return f;
}

static json featureToJson(const Feature& f) {
    // Transform position to render space
    Vector3 renderPos = saveCoords(f.position);
    json result = {
        {"position", vector3ToJson(renderPos)},
        {"rotation", vector3ToJson(f.rotation)},  // Rotation angles preserved as-is
        {"renderIndex", f.renderIndex}
    };

    json flags = featureFlagsToJson(f.flags);
    if (!flags.empty()) result["flags"] = flags;

    if (!f.model.empty()) result["model"] = f.model;
    if (f.collision) result["collision"] = featureCollisionToJson(*f.collision);

    return result;
}

static Feature jsonToFeature(const json& j) {
    Feature f;
    f.position = jsonToVector3(j["position"]);
    f.rotation = jsonToVector3(j["rotation"]);
    f.renderIndex = j.value("renderIndex", 0);
    if (j.contains("flags")) f.flags = jsonToFeatureFlags(j["flags"]);
    f.model = j.value("model", "");
    if (j.contains("collision")) f.collision = jsonToFeatureCollision(j["collision"]);
    return f;
}

//------------------------------------------------------------------------------
// JSON Conversion - PathGeometry
//------------------------------------------------------------------------------

static json pathNodeToJson(const PathNode& n) {
    Vector3 renderPos = saveCoords(n.position);
    return {{"id", n.id}, {"position", vector3ToJson(renderPos)}};
}

static PathNode jsonToPathNode(const json& j) {
    PathNode n;
    n.id = j["id"];
    n.position = jsonToVector3(j["position"]);
    return n;
}

static json pathLinkToJson(const PathLink& l) {
    json result = {
        {"id", l.id},
        {"start", l.start},
        {"finish", l.finish}
    };
    if (l.control) {
        // Transform the Bézier control point to render space, same as the nodes (pathNodeToJson).
        // Without this the control stays in raw game units while endpoints are render-space, which
        // balloons curved-link floor/wall geometry to huge coordinates.
        Vector3 renderControl = saveCoords(l.control->position);
        result["control"] = {{"position", vector3ToJson(renderControl)}};
    }
    if (!l.profiles.empty()) {
        result["profiles"] = l.profiles;
    }
    if (!l.useDefaultProfiles) {
        result["useDefaultProfiles"] = false;  // only emit when opted out (default is true)
    }
    return result;
}

static PathLink jsonToPathLink(const json& j) {
    PathLink l;
    l.id = j["id"];
    l.start = j["start"];
    l.finish = j["finish"];
    if (j.contains("control")) {
        ControlPoint cp;
        cp.position = jsonToVector3(j["control"]["position"]);
        l.control = cp;
    }
    if (j.contains("profiles")) {
        l.profiles = j["profiles"].get<std::vector<int>>();
    }
    l.useDefaultProfiles = j.value("useDefaultProfiles", true);
    return l;
}

static json pathProfileToJson(const PathProfile& p) {
    json points = json::array();
    for (const auto& pt : p.points) {
        points.push_back(vector2ToJson(pt));
    }
    return {{"id", p.id}, {"points", points}};
}

static PathProfile jsonToPathProfile(const json& j) {
    PathProfile p;
    p.id = j["id"];
    for (const auto& pt : j["points"]) {
        p.points.push_back(jsonToVector2(pt));
    }
    return p;
}

static json pathAreaToJson(const PathArea& a) {
    return {
        {"id", a.id},
        {"materialId", a.materialId},
        {"links", a.links}
    };
}

static PathArea jsonToPathArea(const json& j) {
    PathArea a;
    a.id = j["id"];
    a.materialId = j.value("materialId", 0);
    a.links = j["links"].get<std::vector<int>>();
    return a;
}

static json pathGeometryToJson(const PathGeometry& g) {
    json nodes = json::array();
    for (const auto& n : g.nodes) {
        nodes.push_back(pathNodeToJson(n));
    }
    json links = json::array();
    for (const auto& l : g.links) {
        links.push_back(pathLinkToJson(l));
    }
    json profiles = json::array();
    for (const auto& p : g.profiles) {
        profiles.push_back(pathProfileToJson(p));
    }
    json areas = json::array();
    for (const auto& a : g.areas) {
        areas.push_back(pathAreaToJson(a));
    }
    return {
        {"nodes", nodes},
        {"links", links},
        {"profiles", profiles},
        {"areas", areas},
        {"sourceFile", g.sourceFile}
    };
}

static PathGeometry jsonToPathGeometry(const json& j) {
    PathGeometry g;
    for (const auto& n : j["nodes"]) {
        g.nodes.push_back(jsonToPathNode(n));
    }
    for (const auto& l : j["links"]) {
        g.links.push_back(jsonToPathLink(l));
    }
    if (j.contains("profiles")) {
        for (const auto& p : j["profiles"]) {
            g.profiles.push_back(jsonToPathProfile(p));
        }
    }
    if (j.contains("areas")) {
        for (const auto& a : j["areas"]) {
            g.areas.push_back(jsonToPathArea(a));
        }
    }
    g.sourceFile = j.value("sourceFile", "");
    return g;
}

//------------------------------------------------------------------------------
// JSON Conversion - Waypoint
//------------------------------------------------------------------------------

static json waypointFlagsToJson(const WaypointFlags& f) {
    json result;
    if (f.start) result["start"] = true;
    if (f.console) result["console"] = true;
    if (f.recharge) result["recharge"] = true;
    if (f.lift) result["lift"] = true;
    if (f.transmat) result["transmat"] = true;
    return result;
}

static WaypointFlags jsonToWaypointFlags(const json& j) {
    WaypointFlags f;
    f.start = j.value("start", false);
    f.console = j.value("console", false);
    f.recharge = j.value("recharge", false);
    f.lift = j.value("lift", false);
    f.transmat = j.value("transmat", false);
    return f;
}

static json waypointToJson(const Waypoint& w) {
    Vector3 renderPos = saveCoords(w.position);
    json result = {
        {"id", w.id},
        {"position", vector3ToJson(renderPos)},
        {"neighbors", w.neighbors}
    };
    json flags = waypointFlagsToJson(w.flags);
    if (!flags.empty()) result["flags"] = flags;
    return result;
}

static Waypoint jsonToWaypoint(const json& j) {
    Waypoint w;
    w.id = j["id"];
    w.position = jsonToVector3(j["position"]);
    if (j.contains("flags")) w.flags = jsonToWaypointFlags(j["flags"]);
    auto neighbors = j["neighbors"].get<std::vector<int>>();
    for (size_t i = 0; i < 6 && i < neighbors.size(); ++i) {
        w.neighbors[i] = neighbors[i];
    }
    return w;
}

//------------------------------------------------------------------------------
// JSON Conversion - Objects
//------------------------------------------------------------------------------

static json doorToJson(const Door& d) {
    Vector3 renderPos = saveCoords(d.position);
    json result = {
        {"id", d.id},
        {"position", vector3ToJson(renderPos)},
        {"rotation", vector3ToJson(d.rotation)},
        {"size", vector2ToJson(d.size)},
        {"state", d.state},
        {"waypoints", d.waypoints}
    };

    json props;
    if (d.properties.mass != 1.0f) props["mass"] = d.properties.mass;
    if (d.properties.alwaysRender) props["alwaysRender"] = true;
    if (!props.empty()) result["properties"] = props;

    result["collision"] = featureCollisionToJson(d.collision);
    return result;
}

static Door jsonToDoor(const json& j) {
    Door d;
    d.id = j["id"];
    d.position = jsonToVector3(j["position"]);
    d.rotation = jsonToVector3(j["rotation"]);
    d.size = jsonToVector2(j["size"]);
    d.state = j.value("state", 0);
    auto waypoints = j["waypoints"].get<std::vector<int>>();
    for (size_t i = 0; i < 2 && i < waypoints.size(); ++i) {
        d.waypoints[i] = waypoints[i];
    }
    if (j.contains("properties")) {
        d.properties.mass = j["properties"].value("mass", 1.0f);
        d.properties.alwaysRender = j["properties"].value("alwaysRender", false);
    }
    if (j.contains("collision")) d.collision = jsonToFeatureCollision(j["collision"]);
    return d;
}

static json consoleToJson(const Console& c) {
    Vector3 renderPos = saveCoords(c.position);
    return {
        {"id", c.id},
        {"position", vector3ToJson(renderPos)},
        {"rotation", vector3ToJson(c.rotation)},
        {"waypointId", c.waypointId},
        {"targetObjectId", c.targetObjectId}
    };
}

static Console jsonToConsole(const json& j) {
    Console c;
    c.id = j["id"];
    c.position = jsonToVector3(j["position"]);
    c.rotation = jsonToVector3(j["rotation"]);
    c.waypointId = j.value("waypointId", 0);
    c.targetObjectId = j.value("targetObjectId", 0);
    return c;
}

static json chargerToJson(const Charger& c) {
    Vector3 renderPos = saveCoords(c.position);
    return {
        {"id", c.id},
        {"position", vector3ToJson(renderPos)},
        {"rotation", vector3ToJson(c.rotation)}
    };
}

static Charger jsonToCharger(const json& j) {
    Charger c;
    c.id = j["id"];
    c.position = jsonToVector3(j["position"]);
    c.rotation = jsonToVector3(j["rotation"]);
    return c;
}

static json destructibleToJson(const Destructible& d) {
    Vector3 renderPos = saveCoords(d.position);
    json result = {
        {"id", d.id},
        {"position", vector3ToJson(renderPos)},
        {"rotation", vector3ToJson(d.rotation)},
        {"renderIndex", d.renderIndex},
        {"hitPoints", d.hitPoints}
    };
    if (!d.model.empty()) result["model"] = d.model;
    if (d.collision) result["collision"] = featureCollisionToJson(*d.collision);
    return result;
}

static Destructible jsonToDestructible(const json& j) {
    Destructible d;
    d.id = j["id"];
    d.position = jsonToVector3(j["position"]);
    d.rotation = jsonToVector3(j["rotation"]);
    d.renderIndex = j.value("renderIndex", 0);
    d.model = j.value("model", "");
    d.hitPoints = j.value("hitPoints", 50);
    if (j.contains("collision")) d.collision = jsonToFeatureCollision(j["collision"]);
    return d;
}

static json objectsToJson(const Objects& o) {
    json doors = json::array();
    for (const auto& d : o.doors) {
        doors.push_back(doorToJson(d));
    }
    json consoles = json::array();
    for (const auto& c : o.consoles) {
        consoles.push_back(consoleToJson(c));
    }
    json chargers = json::array();
    for (const auto& c : o.chargers) {
        chargers.push_back(chargerToJson(c));
    }
    json destructibles = json::array();
    for (const auto& d : o.destructibles) {
        destructibles.push_back(destructibleToJson(d));
    }
    return {
        {"doors", doors},
        {"consoles", consoles},
        {"chargers", chargers},
        {"destructibles", destructibles}
    };
}

static Objects jsonToObjects(const json& j) {
    Objects o;
    if (j.contains("doors")) {
        for (const auto& d : j["doors"]) {
            o.doors.push_back(jsonToDoor(d));
        }
    }
    if (j.contains("consoles")) {
        for (const auto& c : j["consoles"]) {
            o.consoles.push_back(jsonToConsole(c));
        }
    }
    if (j.contains("chargers")) {
        for (const auto& c : j["chargers"]) {
            o.chargers.push_back(jsonToCharger(c));
        }
    }
    if (j.contains("destructibles")) {
        for (const auto& d : j["destructibles"]) {
            o.destructibles.push_back(jsonToDestructible(d));
        }
    }
    return o;
}

//------------------------------------------------------------------------------
// JSON Conversion - Spawn
//------------------------------------------------------------------------------

static json spawnToJson(const Spawn& s) {
    return {
        {"droidClass", s.droidClass},
        {"waypointIndex", s.waypointIndex},
        {"angle", s.angle}
    };
}

static Spawn jsonToSpawn(const json& j) {
    return {
        j.value("droidClass", 0),
        j.value("waypointIndex", 0),
        j.value("angle", 0.0f)
    };
}

//------------------------------------------------------------------------------
// JSON Conversion - Area
//------------------------------------------------------------------------------

static json areaToJson(const Area& a) {
    json tiles = json::array();
    for (const auto& t : a.tiles) {
        tiles.push_back(tileToJson(t));
    }
    json features = json::array();
    for (const auto& f : a.features) {
        features.push_back(featureToJson(f));
    }
    json geometry = json::array();
    for (const auto& g : a.geometry) {
        geometry.push_back(pathGeometryToJson(g));
    }

    json result = {
        {"bounds", boundsToJson(a.bounds)},
        {"tiles", tiles},
        {"features", features}
    };

    if (!geometry.empty()) result["geometry"] = geometry;
    if (!a.collision.polygons.empty() || !a.collision.chains.empty()) {
        result["collision"] = collisionDataToJson(a.collision);
    }

    return result;
}

static Area jsonToArea(const json& j) {
    Area a;
    a.bounds = jsonToBounds(j["bounds"]);
    for (const auto& t : j["tiles"]) {
        a.tiles.push_back(jsonToTile(t));
    }
    for (const auto& f : j["features"]) {
        a.features.push_back(jsonToFeature(f));
    }
    if (j.contains("geometry")) {
        for (const auto& g : j["geometry"]) {
            a.geometry.push_back(jsonToPathGeometry(g));
        }
    }
    if (j.contains("collision")) {
        a.collision = jsonToCollisionData(j["collision"]);
    }
    return a;
}

//------------------------------------------------------------------------------
// JSON Conversion - Domain
//------------------------------------------------------------------------------

std::string domainToJson(const Domain& domain, bool pretty) {
    json areas = json::array();
    for (const auto& a : domain.areas) {
        areas.push_back(areaToJson(a));
    }

    json waypoints = json::array();
    for (const auto& w : domain.waypoints) {
        waypoints.push_back(waypointToJson(w));
    }

    json spawns = json::array();
    for (const auto& s : domain.spawns) {
        spawns.push_back(spawnToJson(s));
    }

    json result = {
        {"version", domain.version},
        {"levelNumber", domain.levelNumber},
        {"bounds", boundsToJson(domain.bounds)},
        {"areas", areas},
        {"waypoints", waypoints},
        {"objects", objectsToJson(domain.objects)},
        {"metadata", {
            {"sourceFile", domain.metadata.sourceFile},
            {"conversionDate", domain.metadata.conversionDate}
        }}
    };

    if (!domain.name.empty()) result["name"] = domain.name;
    if (domain.ambience >= 0) result["ambience"] = domain.ambience;

    // Only include non-zero profile
    bool hasProfile = false;
    for (int i = 0; i < 9; ++i) {
        if (domain.profile[i] != 0) {
            hasProfile = true;
            break;
        }
    }
    if (hasProfile) result["profile"] = domain.profile;

    if (!spawns.empty()) result["spawns"] = spawns;

    return pretty ? result.dump(2) : result.dump();
}

bool jsonToDomain(std::string_view jsonStr, Domain& outDomain) {
    try {
        json j = json::parse(jsonStr);

        outDomain = Domain{};
        outDomain.version = j.value("version", "1.0");
        outDomain.levelNumber = j.value("levelNumber", 0);
        outDomain.name = j.value("name", "");
        outDomain.ambience = j.value("ambience", -1);
        outDomain.bounds = jsonToBounds(j["bounds"]);

        if (j.contains("profile")) {
            auto profile = j["profile"].get<std::vector<int>>();
            for (size_t i = 0; i < 9 && i < profile.size(); ++i) {
                outDomain.profile[i] = profile[i];
            }
        }

        for (const auto& a : j["areas"]) {
            outDomain.areas.push_back(jsonToArea(a));
        }

        for (const auto& w : j["waypoints"]) {
            outDomain.waypoints.push_back(jsonToWaypoint(w));
        }

        if (j.contains("objects")) {
            outDomain.objects = jsonToObjects(j["objects"]);
        }

        if (j.contains("spawns")) {
            for (const auto& s : j["spawns"]) {
                outDomain.spawns.push_back(jsonToSpawn(s));
            }
        }

        if (j.contains("metadata")) {
            outDomain.metadata.sourceFile = j["metadata"].value("sourceFile", "");
            outDomain.metadata.conversionDate = j["metadata"].value("conversionDate", "");
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

//------------------------------------------------------------------------------
// JSON Conversion - Transporter
//------------------------------------------------------------------------------

static json transporterToJson(const Transporter& t) {
    Vector3 renderPos = saveCoords(t.position);
    return {
        {"id", t.id},
        {"domainIndex", t.domainIndex},
        {"position", vector3ToJson(renderPos)},
        {"levelUp", t.levelUp},
        {"levelDown", t.levelDown},
        {"liftRow", t.liftRow}
    };
}

static Transporter jsonToTransporter(const json& j) {
    Transporter t;
    t.id = j["id"];
    t.domainIndex = j.value("domainIndex", 0);
    t.position = jsonToVector3(j["position"]);
    t.levelUp = j.value("levelUp", -1);
    t.levelDown = j.value("levelDown", -1);
    t.liftRow = j.value("liftRow", 0);
    return t;
}

//------------------------------------------------------------------------------
// JSON Conversion - Decks
//------------------------------------------------------------------------------

static json elevatorToJson(const Elevator& e) {
    return {
        {"id", e.id},
        {"rect", rectToJson(e.rect)}
    };
}

static Elevator jsonToElevator(const json& j) {
    Elevator e;
    e.id = j["id"];
    e.rect = jsonToRect(j["rect"]);
    return e;
}

static json domainRectToJson(const DomainRect& dr) {
    return {
        {"domainIndex", dr.domainIndex},
        {"rectNumber", dr.rectNumber},
        {"rect", rectToJson(dr.rect)}
    };
}

static DomainRect jsonToDomainRect(const json& j) {
    DomainRect dr;
    dr.domainIndex = j["domainIndex"];
    dr.rectNumber = j.value("rectNumber", 0);
    dr.rect = jsonToRect(j["rect"]);
    return dr;
}

static json decksToJson(const Decks& d) {
    json elevators = json::array();
    for (const auto& e : d.elevators) {
        elevators.push_back(elevatorToJson(e));
    }
    json domainRects = json::array();
    for (const auto& dr : d.domainRects) {
        domainRects.push_back(domainRectToJson(dr));
    }
    return {{"elevators", elevators}, {"domainRects", domainRects}};
}

static Decks jsonToDecks(const json& j) {
    Decks d;
    if (j.contains("elevators")) {
        for (const auto& e : j["elevators"]) {
            d.elevators.push_back(jsonToElevator(e));
        }
    }
    if (j.contains("domainRects")) {
        for (const auto& dr : j["domainRects"]) {
            d.domainRects.push_back(jsonToDomainRect(dr));
        }
    }
    return d;
}

//------------------------------------------------------------------------------
// JSON Conversion - Ship
//------------------------------------------------------------------------------

std::string shipToJson(const Ship& ship, bool pretty) {
    json transporters = json::array();
    for (const auto& t : ship.transporters) {
        transporters.push_back(transporterToJson(t));
    }

    json result = {
        {"version", ship.version},
        {"name", ship.name},
        {"domains", ship.domainPaths},
        {"metadata", {
            {"sourceFile", ship.metadata.sourceFile},
            {"conversionDate", ship.metadata.conversionDate},
            {"toolVersion", ship.metadata.toolVersion}
        }}
    };

    if (ship.crew > 0) result["crew"] = ship.crew;
    if (ship.capacity > 0) result["capacity"] = ship.capacity;
    if (!ship.description.empty()) result["description"] = ship.description;
    if (!transporters.empty()) result["transporters"] = transporters;
    if (!ship.decks.elevators.empty() || !ship.decks.domainRects.empty()) {
        result["decks"] = decksToJson(ship.decks);
    }

    return pretty ? result.dump(2) : result.dump();
}

bool jsonToShip(std::string_view jsonStr, Ship& outShip) {
    try {
        json j = json::parse(jsonStr);

        outShip = Ship{};
        outShip.version = j.value("version", "1.0");
        outShip.name = j.value("name", "");
        outShip.crew = j.value("crew", 0);
        outShip.capacity = j.value("capacity", 0);

        if (j.contains("description")) {
            outShip.description = j["description"].get<std::vector<std::string>>();
        }

        if (j.contains("domains")) {
            outShip.domainPaths = j["domains"].get<std::vector<std::string>>();
        }

        if (j.contains("transporters")) {
            for (const auto& t : j["transporters"]) {
                outShip.transporters.push_back(jsonToTransporter(t));
            }
        }

        if (j.contains("decks")) {
            outShip.decks = jsonToDecks(j["decks"]);
        }

        if (j.contains("metadata")) {
            outShip.metadata.sourceFile = j["metadata"].value("sourceFile", "");
            outShip.metadata.conversionDate = j["metadata"].value("conversionDate", "");
            outShip.metadata.toolVersion = j["metadata"].value("toolVersion", "1.0.0");
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

//------------------------------------------------------------------------------
// File I/O
//------------------------------------------------------------------------------

bool saveShipToFile(std::string_view path, const Ship& ship, bool pretty) {
    std::ofstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return false;
    }
    file << shipToJson(ship, pretty);
    return file.good();
}

bool saveDomainToFile(std::string_view path, const Domain& domain, bool pretty,
                      bool transformToRender) {
    std::ofstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return false;
    }
    g_saveTransformCoords = transformToRender;
    file << domainToJson(domain, pretty);
    g_saveTransformCoords = true;  // restore default (convert path relies on it)
    return file.good();
}

bool loadShipFromFile(std::string_view path, Ship& outShip) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << std::endl;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return jsonToShip(buffer.str(), outShip);
}

bool loadDomainFromFile(std::string_view path, Domain& outDomain) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << std::endl;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return jsonToDomain(buffer.str(), outDomain);
}
