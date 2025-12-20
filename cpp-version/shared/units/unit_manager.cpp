#include "unit_manager.h"
#include "unit_json.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

UnitManager::~UnitManager() {
    destroy();
}

void UnitManager::init(b2WorldId worldId) {
    m_worldId = worldId;
}

void UnitManager::destroy() {
    // Clear instances - SectionInstance destructors handle cleanup via RAII
    m_instances.clear();
    m_definitions.clear();
    m_worldId = b2_nullWorldId;
}

//------------------------------------------------------------------------------
// Definition Management
//------------------------------------------------------------------------------

const UnitDefinition* UnitManager::loadDefinition(std::string_view path) {
    // Check if already loaded by scanning existing definitions
    for (const auto& [id, def] : m_definitions) {
        // Could check by path if we stored it, but for now just load
    }

    auto definition = std::make_unique<UnitDefinition>();
    if (!loadUnitDefinitionFromFile(path, *definition)) {
        return nullptr;
    }

    std::string id = definition->id;
    if (id.empty()) {
        // Use filename stem as fallback ID
        id = fs::path(path).stem().string();
        definition->id = id;
    }

    // Check if this ID already exists
    auto existing = m_definitions.find(id);
    if (existing != m_definitions.end()) {
        return existing->second.get();
    }

    const UnitDefinition* ptr = definition.get();
    m_definitions[id] = std::move(definition);
    return ptr;
}

const UnitDefinition* UnitManager::getDefinition(std::string_view id) const {
    auto it = m_definitions.find(std::string(id));
    if (it != m_definitions.end()) {
        return it->second.get();
    }
    return nullptr;
}

void UnitManager::preloadDefinitions(std::string_view directory) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            loadDefinition(entry.path().string());
        }
    }
}

std::vector<std::string> UnitManager::getDefinitionIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_definitions.size());
    for (const auto& [id, def] : m_definitions) {
        ids.push_back(id);
    }
    return ids;
}

//------------------------------------------------------------------------------
// Instance Management
//------------------------------------------------------------------------------

UnitInstance* UnitManager::createInstance(
    std::string_view definitionId,
    Vector2 position,
    float rotation
) {
    const UnitDefinition* def = getDefinition(definitionId);
    if (!def) {
        return nullptr;
    }
    return createInstance(def, position, rotation);
}

UnitInstance* UnitManager::createInstance(
    const UnitDefinition* definition,
    Vector2 position,
    float rotation
) {
    if (!definition || B2_IS_NULL(m_worldId)) {
        return nullptr;
    }

    auto instance = std::make_unique<UnitInstance>();
    instance->definition = definition;
    instance->active = true;
    instance->collisionGroupId = m_nextCollisionGroup--;

    // Create root section
    SectionInstance* root = createSectionInstance(
        definition->rootSection,
        nullptr,
        position,
        rotation,
        instance.get()
    );

    if (!root) {
        return nullptr;
    }

    instance->rootSection.reset(root);

    UnitInstance* ptr = instance.get();
    m_instances.push_back(std::move(instance));
    return ptr;
}

void UnitManager::destroyInstance(UnitInstance* instance) {
    if (!instance) return;

    auto it = std::find_if(m_instances.begin(), m_instances.end(),
        [instance](const auto& ptr) { return ptr.get() == instance; });

    if (it != m_instances.end()) {
        // Erase triggers unique_ptr destruction, which calls SectionInstance destructors
        m_instances.erase(it);
    }
}

const std::vector<std::unique_ptr<UnitInstance>>& UnitManager::getInstances() const {
    return m_instances;
}

//------------------------------------------------------------------------------
// Section Instance Creation
//------------------------------------------------------------------------------

SectionInstance* UnitManager::createSectionInstance(
    const SectionDefinition& def,
    SectionInstance* parent,
    Vector2 parentWorldPos,
    float parentWorldRot,
    UnitInstance* unit
) {
    auto section = new SectionInstance();
    section->definition = &def;
    section->parent = parent;
    section->attached = true;

    // Calculate world position
    float cosR = std::cos(parentWorldRot);
    float sinR = std::sin(parentWorldRot);
    section->worldPosition = {
        parentWorldPos.x + def.localOffset.x * cosR - def.localOffset.y * sinR,
        parentWorldPos.y + def.localOffset.x * sinR + def.localOffset.y * cosR
    };
    section->worldRotation = parentWorldRot + def.localRotation;

    // Create physics body if defined
    if (def.physics.has_value() && def.physics->shapeType != PhysicsShapeType::None) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {section->worldPosition.x, section->worldPosition.y};
        bodyDef.rotation = b2MakeRot(section->worldRotation);
        bodyDef.linearDamping = def.physics->linearDamping;
        bodyDef.angularDamping = def.physics->angularDamping;

        section->bodyId = b2CreateBody(m_worldId, &bodyDef);
        section->hasPhysics = true;

        // Create shape
        const auto& phys = *def.physics;
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = phys.density;
        shapeDef.friction = phys.friction;
        shapeDef.restitution = phys.restitution;
        shapeDef.isSensor = phys.isSensor;

        // Set collision filtering - negative group index prevents self-collision within unit
        shapeDef.filter.groupIndex = unit->collisionGroupId;

        switch (phys.shapeType) {
            case PhysicsShapeType::Circle: {
                b2Circle circle;
                circle.center = {phys.circle.offset.x, phys.circle.offset.y};
                circle.radius = phys.circle.radius;
                b2CreateCircleShape(section->bodyId, &shapeDef, &circle);
                break;
            }
            case PhysicsShapeType::Box: {
                b2Polygon box = b2MakeOffsetBox(
                    phys.box.width / 2.0f,
                    phys.box.height / 2.0f,
                    {phys.box.offset.x, phys.box.offset.y},
                    0.0f  // angle in radians
                );
                b2CreatePolygonShape(section->bodyId, &shapeDef, &box);
                break;
            }
            case PhysicsShapeType::Polygon: {
                if (!phys.polygon.vertices.empty() && phys.polygon.vertices.size() <= 8) {
                    b2Vec2 verts[8];
                    int count = std::min((int)phys.polygon.vertices.size(), 8);
                    for (int i = 0; i < count; ++i) {
                        verts[i] = {phys.polygon.vertices[i].x, phys.polygon.vertices[i].y};
                    }
                    b2Hull hull = b2ComputeHull(verts, count);
                    b2Polygon poly = b2MakePolygon(&hull, 0);
                    b2CreatePolygonShape(section->bodyId, &shapeDef, &poly);
                }
                break;
            }
            default:
                break;
        }

        // Create weld joint to parent if this is not the root
        if (parent && parent->hasPhysics) {
            b2WeldJointDef jointDef = b2DefaultWeldJointDef();
            jointDef.bodyIdA = parent->bodyId;
            jointDef.bodyIdB = section->bodyId;

            // Local anchors
            jointDef.localAnchorA = {def.localOffset.x, def.localOffset.y};
            jointDef.localAnchorB = {0, 0};
            jointDef.referenceAngle = def.localRotation;

            section->parentJoint = b2CreateWeldJoint(m_worldId, &jointDef);
            unit->allJoints.push_back(section->parentJoint);
        }
    }

    // Load model if specified
    if (!def.modelPath.empty()) {
        section->model = LoadModel(def.modelPath.c_str());
        section->hasModel = IsModelValid(section->model);
    }

    // Add to unit's flat list
    unit->allSections.push_back(section);

    // Create children recursively
    for (const auto& childDef : def.children) {
        SectionInstance* child = createSectionInstance(
            childDef,
            section,
            section->worldPosition,
            section->worldRotation,
            unit
        );
        if (child) {
            section->children.emplace_back(child);
        }
    }

    return section;
}

//------------------------------------------------------------------------------
// Deconstruction
//------------------------------------------------------------------------------

void UnitManager::breakJoint(SectionInstance* section) {
    if (!section || !section->attached) return;

    if (b2Joint_IsValid(section->parentJoint)) {
        b2DestroyJoint(section->parentJoint);
    }
    section->parentJoint = b2_nullJointId;
    section->attached = false;
}

void UnitManager::breakAllJoints(UnitInstance* unit) {
    if (!unit) return;

    for (auto& jointId : unit->allJoints) {
        if (b2Joint_IsValid(jointId)) {
            b2DestroyJoint(jointId);
        }
    }
    unit->allJoints.clear();

    for (auto* section : unit->allSections) {
        section->attached = (section->parent == nullptr);  // Root stays "attached"
        section->parentJoint = b2_nullJointId;
    }
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void UnitManager::update(float dt) {
    (void)dt;

    for (auto& instance : m_instances) {
        if (!instance || !instance->active) continue;

        // Check for joint breaking
        checkJointBreaking(instance.get());

        // Update transforms
        if (instance->rootSection) {
            // Get root position from physics if it has a body
            Vector2 rootPos = {0, 0};
            float rootRot = 0.0f;

            if (instance->rootSection->hasPhysics &&
                b2Body_IsValid(instance->rootSection->bodyId)) {
                b2Vec2 pos = b2Body_GetPosition(instance->rootSection->bodyId);
                rootPos = {pos.x, pos.y};
                rootRot = b2Rot_GetAngle(b2Body_GetRotation(instance->rootSection->bodyId));
            }

            instance->rootSection->worldPosition = rootPos;
            instance->rootSection->worldRotation = rootRot;

            // Update children
            for (auto& child : instance->rootSection->children) {
                updateSectionTransforms(child.get(), rootPos, rootRot);
            }
        }
    }
}

void UnitManager::checkJointBreaking(UnitInstance* unit) {
    for (auto* section : unit->allSections) {
        if (!section->attached || !b2Joint_IsValid(section->parentJoint)) continue;

        const auto* def = section->definition;
        if (!def) continue;

        // Check if break thresholds are set
        if (def->jointBreakForce <= 0 && def->jointBreakTorque <= 0) continue;

        b2Vec2 force = b2Joint_GetConstraintForce(section->parentJoint);
        float torque = b2Joint_GetConstraintTorque(section->parentJoint);

        float forceMag = b2Length(force);

        bool shouldBreak = false;
        if (def->jointBreakForce > 0 && forceMag > def->jointBreakForce) {
            shouldBreak = true;
        }
        if (def->jointBreakTorque > 0 && std::abs(torque) > def->jointBreakTorque) {
            shouldBreak = true;
        }

        if (shouldBreak) {
            breakJoint(section);
        }
    }
}

void UnitManager::updateSectionTransforms(
    SectionInstance* section,
    Vector2 parentWorldPos,
    float parentWorldRot
) {
    if (!section) return;

    if (section->attached && section->parent) {
        // Compute from parent transform
        const auto& offset = section->definition->localOffset;
        float cosR = std::cos(parentWorldRot);
        float sinR = std::sin(parentWorldRot);
        section->worldPosition = {
            parentWorldPos.x + offset.x * cosR - offset.y * sinR,
            parentWorldPos.y + offset.x * sinR + offset.y * cosR
        };
        section->worldRotation = parentWorldRot + section->definition->localRotation;
    } else if (section->hasPhysics && b2Body_IsValid(section->bodyId)) {
        // Detached: get from physics
        b2Vec2 pos = b2Body_GetPosition(section->bodyId);
        section->worldPosition = {pos.x, pos.y};
        section->worldRotation = b2Rot_GetAngle(b2Body_GetRotation(section->bodyId));
    }

    // Update children
    for (auto& child : section->children) {
        updateSectionTransforms(child.get(), section->worldPosition, section->worldRotation);
    }
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void UnitManager::renderAll() {
    for (auto& instance : m_instances) {
        if (!instance || !instance->active) continue;
        if (instance->rootSection) {
            renderSection(instance->rootSection.get());
        }
    }
}

void UnitManager::renderSection(SectionInstance* section) {
    if (!section) return;

    if (section->hasModel) {
        // Map 2D physics to 3D rendering
        Vector3 position = {
            section->worldPosition.x,
            section->definition->height,
            section->worldPosition.y
        };

        DrawModelEx(
            section->model,
            position,
            {0, 1, 0},
            section->worldRotation * RAD2DEG,
            section->definition->scale,
            WHITE
        );
    }

    // Render children
    for (auto& child : section->children) {
        renderSection(child.get());
    }
}

void UnitManager::renderDebug() {
    for (auto& instance : m_instances) {
        if (!instance || !instance->active) continue;
        if (instance->rootSection) {
            renderSectionDebug(instance->rootSection.get());
        }
    }
}

void UnitManager::renderSectionDebug(SectionInstance* section) {
    if (!section) return;

    float y = section->definition->height + 0.1f;

    // Draw physics shape outline
    if (section->hasPhysics && section->definition->physics.has_value()) {
        const auto& phys = *section->definition->physics;
        Color shapeColor = section->attached ? GREEN : RED;

        Vector3 center = {
            section->worldPosition.x,
            y,
            section->worldPosition.y
        };

        switch (phys.shapeType) {
            case PhysicsShapeType::Circle:
                DrawCircle3D(center, phys.circle.radius, {1, 0, 0}, 90.0f, shapeColor);
                break;
            case PhysicsShapeType::Box: {
                // Draw box outline
                float hw = phys.box.width / 2.0f;
                float hh = phys.box.height / 2.0f;
                float cosR = std::cos(section->worldRotation);
                float sinR = std::sin(section->worldRotation);

                Vector3 corners[4];
                float localX[] = {-hw, hw, hw, -hw};
                float localY[] = {-hh, -hh, hh, hh};

                for (int i = 0; i < 4; ++i) {
                    float rx = localX[i] * cosR - localY[i] * sinR;
                    float ry = localX[i] * sinR + localY[i] * cosR;
                    corners[i] = {
                        section->worldPosition.x + rx,
                        y,
                        section->worldPosition.y + ry
                    };
                }

                for (int i = 0; i < 4; ++i) {
                    DrawLine3D(corners[i], corners[(i + 1) % 4], shapeColor);
                }
                break;
            }
            default:
                break;
        }
    }

    // Draw joint connection to parent
    if (section->parent) {
        Vector3 from = {
            section->parent->worldPosition.x,
            section->parent->definition->height + 0.1f,
            section->parent->worldPosition.y
        };
        Vector3 to = {
            section->worldPosition.x,
            y,
            section->worldPosition.y
        };
        Color jointColor = section->attached ? LIME : MAROON;
        DrawLine3D(from, to, jointColor);
    }

    // Render children debug
    for (auto& child : section->children) {
        renderSectionDebug(child.get());
    }
}
