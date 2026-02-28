#include "unit_json.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

//------------------------------------------------------------------------------
// Helper Functions
//------------------------------------------------------------------------------

namespace {

Vector2 parseVector2(const json& j, Vector2 defaultVal = {0, 0}) {
    if (j.is_array() && j.size() >= 2) {
        return {j[0].get<float>(), j[1].get<float>()};
    }
    return defaultVal;
}

Vector3 parseVector3(const json& j, Vector3 defaultVal = {1, 1, 1}) {
    if (j.is_array() && j.size() >= 3) {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    }
    return defaultVal;
}

json vector2ToJson(Vector2 v) {
    return json::array({v.x, v.y});
}

json vector3ToJson(Vector3 v) {
    return json::array({v.x, v.y, v.z});
}

PropertyValue parsePropertyValue(const json& j) {
    if (j.is_boolean()) {
        return j.get<bool>();
    } else if (j.is_number_integer()) {
        return j.get<int>();
    } else if (j.is_number_float()) {
        return j.get<float>();
    } else if (j.is_string()) {
        return j.get<std::string>();
    } else if (j.is_array()) {
        if (j.size() == 2) {
            return parseVector2(j);
        } else if (j.size() == 3) {
            return parseVector3(j);
        }
    }
    return std::string{};  // Default to empty string
}

json propertyValueToJson(const PropertyValue& value) {
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Vector2>) {
            return vector2ToJson(v);
        } else if constexpr (std::is_same_v<T, Vector3>) {
            return vector3ToJson(v);
        } else {
            return v;
        }
    }, value);
}

PropertyMap parseProperties(const json& j) {
    PropertyMap props;
    if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            props[key] = parsePropertyValue(value);
        }
    }
    return props;
}

json propertiesToJson(const PropertyMap& props) {
    json j = json::object();
    for (const auto& [key, value] : props) {
        j[key] = propertyValueToJson(value);
    }
    return j;
}

DroidProperties parseDroidProperties(const json& j) {
    DroidProperties props;
    if (!j.is_object()) return props;
    props.classId     = j.value("classId", -1);
    props.typeCode    = j.value("typeCode", 0);
    props.energy      = j.value("energy", 0);
    props.armour      = j.value("armour", 0.0f);
    props.weapon      = j.value("weapon", -1);
    props.droidType   = j.value("droidType", 0);
    props.driveType   = j.value("driveType", 0);
    props.brainType   = j.value("brainType", 0);
    props.hasTurret   = j.value("hasTurret", false);
    props.description = j.value("description", std::string{});
    return props;
}

json droidPropertiesToJson(const DroidProperties& props) {
    json j = json::object();
    j["classId"]     = props.classId;
    j["typeCode"]    = props.typeCode;
    j["energy"]      = props.energy;
    j["armour"]      = props.armour;
    j["weapon"]      = props.weapon;
    j["droidType"]   = props.droidType;
    j["driveType"]   = props.driveType;
    j["brainType"]   = props.brainType;
    j["hasTurret"]   = props.hasTurret;
    j["description"] = props.description;
    return j;
}

PhysicsProperties parsePhysics(const json& j) {
    PhysicsProperties phys;

    if (j.contains("shape")) {
        const auto& shape = j["shape"];
        std::string type = shape.value("type", "none");

        if (type == "circle") {
            phys.shapeType = PhysicsShapeType::Circle;
            phys.circle.radius = shape.value("radius", 0.5f);
            if (shape.contains("offset")) {
                phys.circle.offset = parseVector2(shape["offset"]);
            }
        } else if (type == "box") {
            phys.shapeType = PhysicsShapeType::Box;
            phys.box.width = shape.value("width", 1.0f);
            phys.box.height = shape.value("height", 1.0f);
            if (shape.contains("offset")) {
                phys.box.offset = parseVector2(shape["offset"]);
            }
        } else if (type == "polygon") {
            phys.shapeType = PhysicsShapeType::Polygon;
            if (shape.contains("vertices") && shape["vertices"].is_array()) {
                for (const auto& vert : shape["vertices"]) {
                    phys.polygon.vertices.push_back(parseVector2(vert));
                }
            }
        }
    }

    phys.density = j.value("density", 1.0f);
    phys.friction = j.value("friction", 0.3f);
    phys.restitution = j.value("restitution", 0.0f);
    phys.linearDamping = j.value("linearDamping", 4.0f);
    phys.angularDamping = j.value("angularDamping", 8.0f);
    phys.isSensor = j.value("isSensor", false);

    return phys;
}

json physicsToJson(const PhysicsProperties& phys) {
    json j = json::object();

    json shape = json::object();
    switch (phys.shapeType) {
        case PhysicsShapeType::Circle:
            shape["type"] = "circle";
            shape["radius"] = phys.circle.radius;
            if (phys.circle.offset.x != 0 || phys.circle.offset.y != 0) {
                shape["offset"] = vector2ToJson(phys.circle.offset);
            }
            break;
        case PhysicsShapeType::Box:
            shape["type"] = "box";
            shape["width"] = phys.box.width;
            shape["height"] = phys.box.height;
            if (phys.box.offset.x != 0 || phys.box.offset.y != 0) {
                shape["offset"] = vector2ToJson(phys.box.offset);
            }
            break;
        case PhysicsShapeType::Polygon:
            shape["type"] = "polygon";
            {
                json verts = json::array();
                for (const auto& v : phys.polygon.vertices) {
                    verts.push_back(vector2ToJson(v));
                }
                shape["vertices"] = verts;
            }
            break;
        default:
            shape["type"] = "none";
            break;
    }
    j["shape"] = shape;

    j["density"] = phys.density;
    j["friction"] = phys.friction;
    j["restitution"] = phys.restitution;
    j["linearDamping"] = phys.linearDamping;
    j["angularDamping"] = phys.angularDamping;
    j["isSensor"] = phys.isSensor;

    return j;
}

SectionRotationMode parseRotationMode(const std::string& str) {
    if (str == "FollowFacing") return SectionRotationMode::FollowFacing;
    if (str == "Fixed") return SectionRotationMode::Fixed;
    return SectionRotationMode::FollowUnit;  // Default
}

std::string rotationModeToString(SectionRotationMode mode) {
    switch (mode) {
        case SectionRotationMode::FollowFacing: return "FollowFacing";
        case SectionRotationMode::Fixed: return "Fixed";
        default: return "FollowUnit";
    }
}

void parseSection(const json& j, SectionDefinition& section) {
    section.name = j.value("name", "");
    section.modelPath = j.value("model", "");

    if (j.contains("offset")) {
        section.offset = parseVector3(j["offset"], {0, 0, 0});
    }
    section.localRotation = j.value("localRotation", 0.0f);

    if (j.contains("scale")) {
        section.scale = parseVector3(j["scale"], {1, 1, 1});
    }

    // Parse rotation mode
    if (j.contains("rotationMode")) {
        section.rotationMode = parseRotationMode(j["rotationMode"].get<std::string>());
    }

    // Physics (used for debris when unit is dismantled)
    if (j.contains("physics")) {
        section.physics = parsePhysics(j["physics"]);
    }

    if (j.contains("properties")) {
        section.properties = parseProperties(j["properties"]);
    }

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& childJson : j["children"]) {
            SectionDefinition child;
            parseSection(childJson, child);
            section.children.push_back(std::move(child));
        }
    }
}

json sectionToJson(const SectionDefinition& section) {
    json j = json::object();

    j["name"] = section.name;
    if (!section.modelPath.empty()) {
        j["model"] = section.modelPath;
    }

    if (section.offset.x != 0 || section.offset.y != 0 || section.offset.z != 0) {
        j["offset"] = vector3ToJson(section.offset);
    }
    if (section.localRotation != 0) {
        j["localRotation"] = section.localRotation;
    }
    if (section.scale.x != 1 || section.scale.y != 1 || section.scale.z != 1) {
        j["scale"] = vector3ToJson(section.scale);
    }

    // Output rotation mode if not default
    if (section.rotationMode != SectionRotationMode::FollowUnit) {
        j["rotationMode"] = rotationModeToString(section.rotationMode);
    }

    // Physics (used for debris when unit is dismantled)
    if (section.physics.has_value()) {
        j["physics"] = physicsToJson(*section.physics);
    }

    if (!section.properties.empty()) {
        j["properties"] = propertiesToJson(section.properties);
    }

    if (!section.children.empty()) {
        json children = json::array();
        for (const auto& child : section.children) {
            children.push_back(sectionToJson(child));
        }
        j["children"] = children;
    }

    return j;
}

} // anonymous namespace

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

bool loadUnitDefinitionFromFile(std::string_view path, UnitDefinition& outDefinition) {
    std::string pathStr{path};
    std::ifstream file{pathStr};
    if (!file) {
        std::cerr << "Failed to open unit definition file: " << path << std::endl;
        return false;
    }

    try {
        json j;
        file >> j;

        outDefinition.name = j.value("name", "");
        outDefinition.id = j.value("id", "");

        // Unit-level physics radii
        outDefinition.collisionRadius = j.value("collisionRadius", 0.5f);
        outDefinition.proximityRadius = j.value("proximityRadius", 1.0f);

        if (j.contains("properties")) {
            outDefinition.properties = parseDroidProperties(j["properties"]);
        }

        if (j.contains("rootSection")) {
            parseSection(j["rootSection"], outDefinition.rootSection);
        } else {
            std::cerr << "Unit definition missing rootSection: " << path << std::endl;
            return false;
        }

        return true;

    } catch (const json::exception& e) {
        std::cerr << "JSON parse error in " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool saveUnitDefinitionToFile(std::string_view path, const UnitDefinition& definition) {
    try {
        json j = json::object();

        j["name"] = definition.name;
        j["id"] = definition.id;

        // Unit-level physics radii
        j["collisionRadius"] = definition.collisionRadius;
        j["proximityRadius"] = definition.proximityRadius;

        j["properties"] = droidPropertiesToJson(definition.properties);

        j["rootSection"] = sectionToJson(definition.rootSection);

        std::string pathStr{path};
        std::ofstream file{pathStr};
        if (!file) {
            std::cerr << "Failed to open file for writing: " << path << std::endl;
            return false;
        }

        file << j.dump(2);  // Pretty print with 2-space indent
        return true;

    } catch (const json::exception& e) {
        std::cerr << "JSON serialization error: " << e.what() << std::endl;
        return false;
    }
}

bool parseUnitDefinitionFromString(std::string_view jsonString, UnitDefinition& outDefinition) {
    try {
        json j = json::parse(jsonString);

        outDefinition.name = j.value("name", "");
        outDefinition.id = j.value("id", "");

        // Unit-level physics radii
        outDefinition.collisionRadius = j.value("collisionRadius", 0.5f);
        outDefinition.proximityRadius = j.value("proximityRadius", 1.0f);

        if (j.contains("properties")) {
            outDefinition.properties = parseDroidProperties(j["properties"]);
        }

        if (j.contains("rootSection")) {
            parseSection(j["rootSection"], outDefinition.rootSection);
        } else {
            return false;
        }

        return true;

    } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

std::string serializeUnitDefinitionToString(const UnitDefinition& definition, bool pretty) {
    try {
        json j = json::object();

        j["name"] = definition.name;
        j["id"] = definition.id;

        // Unit-level physics radii
        j["collisionRadius"] = definition.collisionRadius;
        j["proximityRadius"] = definition.proximityRadius;

        j["properties"] = droidPropertiesToJson(definition.properties);

        j["rootSection"] = sectionToJson(definition.rootSection);

        return pretty ? j.dump(2) : j.dump();

    } catch (const json::exception& e) {
        std::cerr << "JSON serialization error: " << e.what() << std::endl;
        return "{}";
    }
}

void scaleDefinitionOffsets(SectionDefinition& section, float scale) {
    section.offset.x *= scale;
    section.offset.y *= scale;
    section.offset.z *= scale;
    for (auto& child : section.children) {
        scaleDefinitionOffsets(child, scale);
    }
}
