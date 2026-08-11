#include "domain_parser.h"
#include "archetile_parser.h"
#include "geometry_xml_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

static std::string getCurrentDateTimeISO() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string normalizePathSeparators(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

//------------------------------------------------------------------------------
// Archetile Loading
//------------------------------------------------------------------------------

bool ensureArchetilesLoaded(const fs::path& tilesPath) {
    if (ArchetileCache::instance().isLoaded()) {
        return true;
    }
    return ArchetileCache::instance().load(tilesPath.string());
}

//------------------------------------------------------------------------------
// Tile Parser
//------------------------------------------------------------------------------

// Parse a Tile when vertex count is already known
static bool parseTileWithCount(std::istream& stream, Tile& outTile, int vertexCount) {
    std::string line;

    if (vertexCount <= 0) {
        std::cerr << "TILE_PARSER: Invalid vertex count: " << vertexCount << std::endl;
        return false;
    }

    outTile.vertices.resize(vertexCount);

    // Read vertex positions
    for (int i = 0; i < vertexCount; ++i) {
        if (!std::getline(stream, line)) {
            std::cerr << "TILE_PARSER: Failed to read position line " << i << std::endl;
            return false;
        }
        line = trim(line);
        std::istringstream vss(line);
        vss >> outTile.vertices[i].position.x
            >> outTile.vertices[i].position.y
            >> outTile.vertices[i].position.z;
    }

    // Read first UV set
    for (int i = 0; i < vertexCount; ++i) {
        if (!std::getline(stream, line)) {
            std::cerr << "TILE_PARSER: Failed to read UV1 line " << i << std::endl;
            return false;
        }
        line = trim(line);
        std::istringstream uvss(line);
        uvss >> outTile.vertices[i].uv1.x
             >> outTile.vertices[i].uv1.y;
    }

    // Read second UV set
    for (int i = 0; i < vertexCount; ++i) {
        if (!std::getline(stream, line)) {
            std::cerr << "TILE_PARSER: Failed to read UV2 line " << i << std::endl;
            return false;
        }
        line = trim(line);
        std::istringstream uvss(line);
        uvss >> outTile.vertices[i].uv2.x
             >> outTile.vertices[i].uv2.y;
    }

    // Texture indices line: texindex bumpindex specularIndex
    if (!std::getline(stream, line)) {
        std::cerr << "TILE_PARSER: Failed to read texture indices line" << std::endl;
        return false;
    }
    line = trim(line);
    std::istringstream texIss(line);
    int specularIndex = 0;
    texIss >> outTile.textureIndex1 >> outTile.textureIndex2 >> specularIndex;

    // Read optional properties until "End"
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == "End") break;

        std::istringstream propIss(line);
        std::string propName;
        propIss >> propName;

        if (propName == "DiffuseColour") {
            propIss >> outTile.properties.diffuseColour.x
                    >> outTile.properties.diffuseColour.y
                    >> outTile.properties.diffuseColour.z;
        }
        else if (propName == "SpecularColour") {
            propIss >> outTile.properties.specularColour.x
                    >> outTile.properties.specularColour.y
                    >> outTile.properties.specularColour.z;
        }
        else if (propName == "EffectTexture") {
            propIss >> outTile.properties.effectTexture;
        }
        else if (propName == "EffectRenderMode") {
            propIss >> outTile.properties.effectRenderMode;
        }
        else if (propName == "EffectBlendSource") {
            propIss >> outTile.properties.effectBlendSource;
        }
        else if (propName == "EffectBlendDest") {
            propIss >> outTile.properties.effectBlendDest;
        }
        else if (propName == "AdditiveBlend") {
            outTile.properties.additiveBlend = true;
        }
        else if (propName == "AlphaBlend") {
            outTile.properties.alphaBlend = true;
        }
        else if (propName == "TEXROTATE") {
            outTile.properties.texRotate = true;
        }
        else if (propName == "TileType") {
            int tt;
            propIss >> tt;
            outTile.properties.tileType = tt;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Feature Parser
//------------------------------------------------------------------------------

static bool parseFeature(std::istream& stream, Feature& outFeature) {
    std::string line;

    // Position line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream posIss(line);
    posIss >> outFeature.position.x >> outFeature.position.y >> outFeature.position.z;

    // Rotation line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream rotIss(line);
    rotIss >> outFeature.rotation.x >> outFeature.rotation.y >> outFeature.rotation.z;

    // Flags line: indestructible solid fullbright
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream flagsIss(line);
    int indestructible, solid, fullbright;
    flagsIss >> indestructible >> solid >> fullbright;
    outFeature.flags.indestructible = (indestructible != 0);
    outFeature.flags.solid = (solid != 0);
    outFeature.flags.fullbright = (fullbright != 0);

    // Render index line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outFeature.renderIndex = std::stoi(line);

    // Generate collision for solid features
    if (outFeature.flags.solid) {
        FeatureCollision collision;
        collision.type = FeatureCollision::Type::Box;
        collision.halfExtents = {32.0f, 32.0f};  // Default size, would be better from model bounds
        outFeature.collision = collision;
    }

    return true;
}

//------------------------------------------------------------------------------
// Waypoint Parser
//------------------------------------------------------------------------------

// A waypoint is a 4-line record; `firstLine` is the "Waypoint <id>" line already read by the
// dispatch loop, and the position/flags/neighbors follow on the next three lines of `stream`:
//   Waypoint <id>
//   <x> <y> <z>
//   <start> <console> <recharge> <lift> <transmat>
//   <n0> <n1> <n2> <n3> <n4> <n5>
static bool parseWaypoint(std::istream& stream, const std::string& firstLine, Waypoint& outWaypoint) {
    std::istringstream idss(firstLine);
    std::string keyword;
    idss >> keyword;  // "Waypoint"
    idss >> outWaypoint.id;

    std::string line;

    // Position line
    if (!std::getline(stream, line)) return false;
    std::istringstream pss(trim(line));
    pss >> outWaypoint.position.x >> outWaypoint.position.y >> outWaypoint.position.z;

    // Flags line: start console recharge lift transmat
    if (!std::getline(stream, line)) return false;
    std::istringstream fss(trim(line));
    int start = 0, console = 0, recharge = 0, lift = 0, transmat = 0;
    fss >> start >> console >> recharge >> lift >> transmat;
    outWaypoint.flags.start = (start != 0);
    outWaypoint.flags.console = (console != 0);
    outWaypoint.flags.recharge = (recharge != 0);
    outWaypoint.flags.lift = (lift != 0);
    outWaypoint.flags.transmat = (transmat != 0);

    // Neighbors line: up to 6 ids, 0-padded (0 = no neighbor)
    if (!std::getline(stream, line)) return false;
    std::istringstream nss(trim(line));
    for (int i = 0; i < 6; ++i) {
        nss >> outWaypoint.neighbors[i];
    }

    return true;
}

//------------------------------------------------------------------------------
// Object Parsers
//------------------------------------------------------------------------------

static bool parseDoor(std::istream& stream, const std::string& firstLine, Door& outDoor) {
    std::istringstream idIss(firstLine);
    std::string keyword;
    idIss >> keyword;  // "Door"
    // Door ID is on next line actually
    std::string line;

    // ID line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outDoor.id = std::stoi(line);

    // Position line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream posIss(line);
    posIss >> outDoor.position.x >> outDoor.position.y >> outDoor.position.z;

    // Rotation line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream rotIss(line);
    rotIss >> outDoor.rotation.x >> outDoor.rotation.y >> outDoor.rotation.z;

    // Size line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream sizeIss(line);
    sizeIss >> outDoor.size.x >> outDoor.size.y;

    // State line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outDoor.state = std::stoi(line);

    // Read properties until END
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == "END") break;

        std::istringstream propIss(line);
        std::string propName;
        propIss >> propName;

        if (propName == "MASS") {
            propIss >> outDoor.properties.mass;
        }
        else if (propName == "ALWAYSRENDER") {
            outDoor.properties.alwaysRender = true;
        }
        else if (propName == "SPIN") {
            propIss >> outDoor.properties.spin.x
                    >> outDoor.properties.spin.y
                    >> outDoor.properties.spin.z;
        }
    }

    // Generate collision from door size
    outDoor.collision.type = FeatureCollision::Type::Box;
    outDoor.collision.halfExtents = {outDoor.size.x / 2.0f, outDoor.size.y / 2.0f};

    return true;
}

static bool parseConsole(std::istream& stream, Console& outConsole) {
    std::string line;

    // ID line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outConsole.id = std::stoi(line);

    // Position line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream posIss(line);
    posIss >> outConsole.position.x >> outConsole.position.y >> outConsole.position.z;

    // Rotation line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream rotIss(line);
    rotIss >> outConsole.rotation.x >> outConsole.rotation.y >> outConsole.rotation.z;

    // Size line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream sizeIss(line);
    sizeIss >> outConsole.size.x >> outConsole.size.y;

    // Target object ID line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outConsole.targetObjectId = std::stoi(line);

    // Read properties until END
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == "END") break;

        std::istringstream propIss(line);
        std::string propName;
        propIss >> propName;

        if (propName == "MASS") {
            propIss >> outConsole.mass;
        }
        else if (propName == "ALWAYSRENDER") {
            outConsole.alwaysRender = true;
        }
        else if (propName == "FIXED") {
            outConsole.fixed = true;
        }
    }

    return true;
}

// Charger blocks have the same layout as Console (id / position / rotation / size / index /
// properties-until-END); the Charger struct only needs id + position + rotation.
static bool parseCharger(std::istream& stream, Charger& outCharger) {
    std::string line;

    if (!std::getline(stream, line)) return false;
    outCharger.id = std::stoi(trim(line));

    if (!std::getline(stream, line)) return false;
    { std::istringstream iss(trim(line)); iss >> outCharger.position.x >> outCharger.position.y >> outCharger.position.z; }

    if (!std::getline(stream, line)) return false;
    { std::istringstream iss(trim(line)); iss >> outCharger.rotation.x >> outCharger.rotation.y >> outCharger.rotation.z; }

    // Size + render-index lines (unused for chargers).
    if (!std::getline(stream, line)) return false;
    if (!std::getline(stream, line)) return false;

    // Properties until END (MASS, SPIN, … — unused).
    while (std::getline(stream, line)) {
        if (trim(line) == "END") break;
    }
    return true;
}

static bool parseGenericObject(std::istream& stream, GenericObject& outObject) {
    std::string line;

    // ID line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outObject.id = std::stoi(line);

    // Position line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream posIss(line);
    posIss >> outObject.position.x >> outObject.position.y >> outObject.position.z;

    // Rotation line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream rotIss(line);
    rotIss >> outObject.rotation.x >> outObject.rotation.y >> outObject.rotation.z;

    // Size line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    std::istringstream sizeIss(line);
    sizeIss >> outObject.size.x >> outObject.size.y;

    // Type ID line
    if (!std::getline(stream, line)) return false;
    line = trim(line);
    outObject.typeId = std::stoi(line);

    // Read properties until END
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == "END") break;

        std::istringstream propIss(line);
        std::string propName;
        propIss >> propName;

        if (propName == "MASS") {
            propIss >> outObject.mass;
        }
        else if (propName == "ALWAYSRENDER") {
            outObject.alwaysRender = true;
        }
        else if (propName == "SPIN") {
            propIss >> outObject.spin.x >> outObject.spin.y >> outObject.spin.z;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Domain Footer Parser
//------------------------------------------------------------------------------

static void parseDomainFooter(std::istream& stream, Domain& domain) {
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "NAME") {
            std::getline(iss, domain.name);
            domain.name = trim(domain.name);
        }
        else if (keyword == "AMBIENCE") {
            iss >> domain.ambience;
        }
        else if (keyword == "PROFILE") {
            for (int i = 0; i < 9; ++i) {
                iss >> domain.profile[i];
            }
        }
        else if (keyword == "PLACEDROID") {
            Spawn spawn;
            std::string token;
            while (iss >> token) {
                if (token == "CLASS") {
                    iss >> spawn.droidClass;
                }
                else if (token == "WAYPOINT" || token == "INDEX") {  // xmapfile uses WAYPOINT
                    iss >> spawn.waypointIndex;
                }
                else if (token == "ANGLE") {
                    iss >> spawn.angle;
                }
            }
            domain.spawns.push_back(spawn);
        }
    }
}

//------------------------------------------------------------------------------
// Main Domain Parser
//------------------------------------------------------------------------------

bool parseDomainFile(std::string_view path, Domain& outDomain,
                     const fs::path& basePath, const fs::path& tilesPath) {
    std::cout << "DOMAIN_PARSER: Opening file: " << path << std::endl;

    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "DOMAIN_PARSER: Failed to open domain file: " << path << std::endl;
        return false;
    }

    fs::path filePath(path);
    fs::path resolveBase = basePath.empty() ? filePath.parent_path() : basePath;
    std::cout << "DOMAIN_PARSER: Resolve base path: " << resolveBase << std::endl;

    // Try to load archetiles if path provided
    if (!tilesPath.empty()) {
        ensureArchetilesLoaded(tilesPath);
    }

    outDomain = Domain{};
    outDomain.metadata.sourceFile = filePath.filename().string();
    outDomain.metadata.conversionDate = getCurrentDateTimeISO();

    Area* currentArea = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "Level") {
            iss >> outDomain.levelNumber;
        }
        else if (keyword == "Area") {
            // Area bounds: 4 vectors (12 floats)
            outDomain.areas.push_back(Area{});
            currentArea = &outDomain.areas.back();

            // Parse remaining bounds from current line and next lines
            Vector3 v1, v2, v3, v4;

            // First line might have first vector
            if (!(iss >> v1.x >> v1.y >> v1.z)) {
                // Read from next line
                if (std::getline(file, line)) {
                    std::istringstream v1ss(trim(line));
                    v1ss >> v1.x >> v1.y >> v1.z;
                }
            }

            // Read remaining 3 vectors
            for (int i = 0; i < 3; ++i) {
                if (std::getline(file, line)) {
                    std::istringstream vss(trim(line));
                    if (i == 0) vss >> v2.x >> v2.y >> v2.z;
                    else if (i == 1) vss >> v3.x >> v3.y >> v3.z;
                    else vss >> v4.x >> v4.y >> v4.z;
                }
            }

            // Compute bounds from 4 corner points
            currentArea->bounds.min.x = std::min({v1.x, v2.x, v3.x, v4.x});
            currentArea->bounds.min.y = std::min({v1.y, v2.y, v3.y, v4.y});
            currentArea->bounds.min.z = std::min({v1.z, v2.z, v3.z, v4.z});
            currentArea->bounds.max.x = std::max({v1.x, v2.x, v3.x, v4.x});
            currentArea->bounds.max.y = std::max({v1.y, v2.y, v3.y, v4.y});
            currentArea->bounds.max.z = std::max({v1.z, v2.z, v3.z, v4.z});
        }
        else if (keyword == "Tile" && currentArea) {
            // Tile format: "Tile N" where N is vertex count on same line
            int vertexCount = 0;
            iss >> vertexCount;

            Tile tile;
            if (parseTileWithCount(file, tile, vertexCount)) {
                currentArea->tiles.push_back(std::move(tile));
            }
        }
        else if (keyword == "Archetile" && currentArea) {
            int archetypeIndex;
            float xOffset, yOffset;
            iss >> archetypeIndex >> xOffset >> yOffset;

            const Archetile* archetype = ArchetileCache::instance().get(archetypeIndex);
            if (archetype) {
                Tile tile = expandArchetile(*archetype, xOffset, yOffset);
                currentArea->tiles.push_back(std::move(tile));
            } else {
                std::cerr << "Warning: Unknown archetile index " << archetypeIndex << std::endl;
            }
        }
        else if (keyword == "Feature" && currentArea) {
            Feature feature;
            if (parseFeature(file, feature)) {
                currentArea->features.push_back(std::move(feature));
            }
        }
        else if (keyword == "Geometry" && currentArea) {
            std::string geomPath;
            iss >> geomPath;
            geomPath = normalizePathSeparators(geomPath);

            // Geometry paths in xmapfile are relative to the data root (parent of ship directories)
            // e.g., "ship1/lvl0section0.xml" when xmapfile is in ship1/
            // So we go up one level from the xmapfile's directory
            fs::path dataRoot = resolveBase.parent_path();
            fs::path fullPath = dataRoot / geomPath;

            // Fallback: if not found, try relative to xmapfile directory
            if (!fs::exists(fullPath)) {
                fullPath = resolveBase / geomPath;
            }

            std::cout << "DOMAIN_PARSER: Loading geometry from: " << fullPath << std::endl;

            PathGeometry geom;
            if (parseGeometryXml(fullPath.string(), geom)) {
                std::cout << "DOMAIN_PARSER: Loaded geometry with " << geom.areas.size() << " areas, "
                          << geom.nodes.size() << " nodes, " << geom.links.size() << " links" << std::endl;

                // Merge every geometry "section" of this area into ONE PathGeometry with a unified id
                // space — sections number from 0 independently, so keeping them separate collides on
                // node/link/area ids (broken id labels + link table for the 2nd+ section). Gross
                // culling can be done per floor-area/path instead of per section.
                if (currentArea->geometry.empty()) {
                    currentArea->geometry.push_back(std::move(geom));
                } else {
                    mergePathGeometry(currentArea->geometry[0], std::move(geom));
                }

                // Regenerate collision from the merged geometry (covers all sections).
                currentArea->collision = CollisionData{};
                generateCollisionFromGeometry(currentArea->geometry[0], currentArea->collision);

                const PathGeometry& merged = currentArea->geometry[0];
                std::cout << "DOMAIN_PARSER: merged geometry now " << merged.nodes.size() << " nodes, "
                          << merged.links.size() << " links, " << merged.areas.size() << " areas" << std::endl;
            } else {
                std::cerr << "Warning: Failed to load geometry: " << fullPath << std::endl;
            }
        }
        else if (keyword == "EndArea") {
            currentArea = nullptr;
        }
        else if (keyword == "Waypoint") {
            Waypoint wp;
            if (parseWaypoint(file, line, wp)) {
                outDomain.waypoints.push_back(wp);
            }
        }
        else if (keyword == "Door") {
            Door door;
            if (parseDoor(file, line, door)) {
                outDomain.objects.doors.push_back(std::move(door));
            }
        }
        else if (keyword == "Console") {
            Console console;
            if (parseConsole(file, console)) {
                outDomain.objects.consoles.push_back(std::move(console));
            }
        }
        else if (keyword == "Charger") {
            Charger charger;
            if (parseCharger(file, charger)) {
                outDomain.objects.chargers.push_back(std::move(charger));
            }
        }
        else if (keyword == "Object" || keyword == "Destructible" || keyword == "Organic") {
            // All three share object::load's record layout (parseGenericObject captures id/pos/rot/
            // renderIndex/spin and ignores the extra EXPLODE*/FIXED lines). Destructible/Organic gain
            // their gameplay behaviour from the object DEFINITION (keyed by renderIndex), not per-record
            // flags, so they route here uniformly. See docs/scenery_entities.md.
            GenericObject obj;
            if (parseGenericObject(file, obj)) {
                outDomain.objects.generic.push_back(std::move(obj));
            }
        }
        else if (keyword == "EndDomain") {
            // Parse footer (NAME, AMBIENCE, PROFILE, PLACEDROID)
            parseDomainFooter(file, outDomain);
            break;
        }
    }

    // Calculate overall domain bounds from all areas
    if (!outDomain.areas.empty()) {
        outDomain.bounds = outDomain.areas[0].bounds;
        for (size_t i = 1; i < outDomain.areas.size(); ++i) {
            const auto& ab = outDomain.areas[i].bounds;
            outDomain.bounds.min.x = std::min(outDomain.bounds.min.x, ab.min.x);
            outDomain.bounds.min.y = std::min(outDomain.bounds.min.y, ab.min.y);
            outDomain.bounds.min.z = std::min(outDomain.bounds.min.z, ab.min.z);
            outDomain.bounds.max.x = std::max(outDomain.bounds.max.x, ab.max.x);
            outDomain.bounds.max.y = std::max(outDomain.bounds.max.y, ab.max.y);
            outDomain.bounds.max.z = std::max(outDomain.bounds.max.z, ab.max.z);
        }
    }

    return true;
}
