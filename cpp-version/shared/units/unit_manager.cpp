#include "unit_manager.h"
#include "unit_json.h"
#include "movement_tuning.h"
#include "../rendering/env_map.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

UnitManager::~UnitManager() {
    destroy();
}

void UnitManager::init(b2WorldId worldId, const char* modelsBasePath) {
    m_worldId = worldId;
    if (modelsBasePath && modelsBasePath[0] != '\0') {
        m_modelsBasePath = modelsBasePath;
    }
    // Static anchor at the world origin — bodyA for every unit's motor joint.
    m_originBody = unit_create_origin_body(m_worldId);
}

void UnitManager::destroy() {
    // Clear debris physics bodies
    clearDebris();

    // Clear instances - destroy unit physics bodies
    for (auto& instance : m_instances) {
        if (instance && b2Body_IsValid(instance->bodyId)) {
            b2DestroyBody(instance->bodyId);
        }
    }
    m_instances.clear();

    m_definitions.clear();
    m_modelsBasePath.clear();
    // The origin body is owned by the world; it is freed when the world is
    // destroyed elsewhere. Just drop our handle.
    m_originBody = b2_nullBodyId;
    m_worldId = b2_nullWorldId;
}

//------------------------------------------------------------------------------
// Definition Management
//------------------------------------------------------------------------------

const UnitDefinition* UnitManager::loadDefinition(std::string_view path) {
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

UnitDefinition* UnitManager::getDefinitionMutable(std::string_view id) {
    auto it = m_definitions.find(std::string(id));
    return (it != m_definitions.end()) ? it->second.get() : nullptr;
}

void UnitManager::unloadDefinition(std::string_view id) {
    m_definitions.erase(std::string(id));
}

UnitDefinition* UnitManager::reloadDefinition(std::string_view id, std::string_view path) {
    auto it = m_definitions.find(std::string(id));
    if (it == m_definitions.end()) return nullptr;

    // Parse into a fresh object first; only swap on success so a bad file leaves the cache intact.
    auto fresh = std::make_unique<UnitDefinition>();
    if (!loadUnitDefinitionFromFile(path, *fresh)) return nullptr;

    // Retire the old object (keep it alive — live instances still point into its section tree),
    // then install the reloaded one so getDefinition/getDefinitionMutable/new spawns see it.
    m_retiredDefinitions.push_back(std::move(it->second));
    it->second = std::move(fresh);
    return it->second.get();
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
    float rotation,
    b2WorldId world,
    b2BodyId origin
) {
    const UnitDefinition* def = getDefinition(definitionId);
    if (!def) {
        return nullptr;
    }
    return createInstance(def, position, rotation, world, origin);
}

UnitInstance* UnitManager::createInstance(
    const UnitDefinition* definition,
    Vector2 position,
    float rotation,
    b2WorldId world,
    b2BodyId origin
) {
    // Per-level worlds pass an explicit world/origin; default to the init() world.
    b2WorldId targetWorld = B2_IS_NULL(world) ? m_worldId : world;
    b2BodyId targetOrigin = B2_IS_NULL(origin) ? m_originBody : origin;
    if (!definition || B2_IS_NULL(targetWorld)) {
        return nullptr;
    }

    auto instance = std::make_unique<UnitInstance>();
    instance->definition = definition;
    instance->active = true;
    instance->collisionGroupId = m_nextCollisionGroup--;
    instance->combatState = initCombatState(definition->properties);

    // Set body user data for contact event identification
    instance->bodyUserData.tag = BodyTag::Unit;
    instance->bodyUserData.owner = instance.get();

    // Create single physics body for the entire unit using collisionRadius
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {position.x, position.y};
    bodyDef.rotation = b2MakeRot(rotation);
    // Per-type linear damping sets terminal velocity. The motor saturates at
    // (mass * acceleration * MOVEMENT_UNIT_SCALE * UNIT_MOTOR_AUTHORITY), so
    // terminal speed = force / (mass * damping). Choosing
    // damping = acceleration * UNIT_MOTOR_AUTHORITY / maxSpeed makes the unit
    // cruise at exactly maxSpeed*MOVEMENT_UNIT_SCALE while the authority factor
    // still boosts absolute force (it cancels out of terminal speed). Units with
    // no movement data fall back to the global damping constant.
    bodyDef.linearDamping = unit_base_linear_damping(definition->maxSpeed,
                                                     definition->acceleration);
    bodyDef.angularDamping = UNIT_ANGULAR_DAMPING;
    // Never sleep: units are driven by a motor joint whose target we update every
    // frame, and b2MotorJoint_Set*Offset does NOT wake a sleeping body. A resting
    // unit would otherwise sleep (~0.5s) and then ignore new move/face targets,
    // freezing until something else woke it (e.g. swapping unit type).
    bodyDef.enableSleep = false;
    bodyDef.userData = &instance->bodyUserData;

    instance->bodyId = b2CreateBody(targetWorld, &bodyDef);

    // Create circle shape using unit's collision radius
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    // Near-frictionless so a unit that hits a wall at an angle SLIDES along it
    // (the contact solver truncates the into-wall velocity; low friction lets the
    // motor's tangential component carry it along). This is the primary escape when
    // a straight path to a waypoint clips an obstacle. Deceleration comes from
    // linearDamping, not contact friction, so dropping this doesn't affect feel.
    shapeDef.friction = UNIT_CONTACT_FRICTION;
    shapeDef.restitution = 0.0f;
    shapeDef.filter.categoryBits = CATEGORY_UNIT;
    shapeDef.filter.maskBits = MASK_UNIT;
    shapeDef.filter.groupIndex = instance->collisionGroupId;

    b2Circle circle;
    circle.center = {0, 0};
    circle.radius = definition->collisionRadius;
    if (circle.radius <= 0.0f) {
        TraceLog(LOG_WARNING, "Unit '%s' has zero collisionRadius, using default 0.2", definition->id.c_str());
        circle.radius = 0.2f;
    }
    b2CreateCircleShape(instance->bodyId, &shapeDef, &circle);

    // Attach the motor joint that drives this unit toward a target position/facing.
    // Identical for AI-driven units and the player — only the target source differs.
    unit_attach_motor_joint(instance.get(), targetWorld, targetOrigin);

    // Create section instances (rendering only, no per-section physics)
    SectionInstance* root = createSectionInstance(
        definition->rootSection,
        nullptr,
        instance.get()
    );

    if (!root) {
        b2DestroyBody(instance->bodyId);
        return nullptr;
    }

    instance->rootSection.reset(root);

    // Initialize section transforms
    root->worldPosition = position;
    root->worldRotation = rotation;
    for (auto& child : root->children) {
        updateSectionTransforms(child.get(), root->worldPosition, root->worldRotation);
    }

    UnitInstance* ptr = instance.get();
    m_instances.push_back(std::move(instance));
    return ptr;
}

void UnitManager::destroyInstance(UnitInstance* instance) {
    if (!instance) return;

    auto it = std::find_if(m_instances.begin(), m_instances.end(),
        [instance](const auto& ptr) { return ptr.get() == instance; });

    if (it != m_instances.end()) {
        // Destroy the motor joint before its body (explicit; Box2D would also
        // clean it up with the body, but be deterministic).
        if (b2Joint_IsValid(instance->motorJoint)) {
            b2DestroyJoint(instance->motorJoint);
            instance->motorJoint = b2_nullJointId;
        }
        // Destroy physics body
        if (b2Body_IsValid(instance->bodyId)) {
            b2DestroyBody(instance->bodyId);
        }
        // Erase triggers unique_ptr destruction, which calls SectionInstance destructors
        m_instances.erase(it);
    }
}

const std::vector<std::unique_ptr<UnitInstance>>& UnitManager::getInstances() const {
    return m_instances;
}

//------------------------------------------------------------------------------
// Section Instance Creation (Rendering Only)
//------------------------------------------------------------------------------

SectionInstance* UnitManager::createSectionInstance(
    const SectionDefinition& def,
    SectionInstance* parent,
    UnitInstance* unit
) {
    auto section = new SectionInstance();
    section->definition = &def;
    section->parent = parent;

    // Load model if specified
    if (!def.modelPath.empty()) {
        std::string resolvedPath = def.modelPath;

        // If we have a models base path and the model path is relative, resolve it
        if (!m_modelsBasePath.empty() && !fs::path(def.modelPath).is_absolute()) {
            // Strip "models/" prefix from the model path if present (conventional structure)
            std::string_view modelPath = def.modelPath;
            if (modelPath.substr(0, 7) == "models/") {
                modelPath = modelPath.substr(7);
            }
            resolvedPath = (fs::path(m_modelsBasePath) / modelPath).string();
        }

        // Prefer a shared model from the cache for static meshes; animated models (and the
        // no-cache fallback) load per-instance so each can hold its own skinned pose.
        ModelCache::Entry entry{Model{}, true, false};
        if (m_modelCache) {
            entry = m_modelCache->get(resolvedPath);
        }

        if (entry.shared) {
            section->model = entry.model;   // shared handle — do NOT unload in the section
            section->hasModel = true;
            section->ownsModel = false;
        } else {
            section->model = LoadModel(resolvedPath.c_str());
            section->hasModel = IsModelValid(section->model);
            section->ownsModel = true;
            // Bind env maps from glTF material `extras` for per-instance (animated/no-cache) models
            // too; this Model owns the textures and frees them on its own UnloadModel.
            if (section->hasModel) {
                modelSetSmoothTextureFilter(section->model);
                envMapApplyExtras(section->model, resolvedPath);
            }
            // Load animations only for per-instance (animated) models.
            if (section->hasModel) {
                section->animations = LoadModelAnimations(resolvedPath.c_str(), &section->animCount);
                if (section->animCount > 0) {
                    std::cout << "  Loaded " << section->animCount << " animations for " << def.name << std::endl;
                }
            }
        }
    }

    // Add to unit's flat list
    unit->allSections.push_back(section);

    // Create children recursively
    for (const auto& childDef : def.children) {
        SectionInstance* child = createSectionInstance(
            childDef,
            section,
            unit
        );
        if (child) {
            section->children.emplace_back(child);
        }
    }

    return section;
}

//------------------------------------------------------------------------------
// Debris Management
//------------------------------------------------------------------------------

// Helper to calculate accumulated height for a section by walking up parent chain
static float getAccumulatedHeight(SectionInstance* section) {
    float height = 0.0f;
    while (section) {
        height += section->definition->offset.z;
        section = section->parent;
    }
    return height;
}

std::vector<DebrisObject> UnitManager::dismantleUnit(UnitInstance* unit) {
    std::vector<DebrisObject> debris;

    if (!unit || !unit->rootSection) {
        return debris;
    }

    // Get current unit velocity before destruction
    b2Vec2 unitVel = {0, 0};
    float unitAngVel = 0.0f;
    if (b2Body_IsValid(unit->bodyId)) {
        unitVel = b2Body_GetLinearVelocity(unit->bodyId);
        unitAngVel = b2Body_GetAngularVelocity(unit->bodyId);
    }

    Vector2 unitPos = unit->rootSection->worldPosition;

    // Create debris for each section that has physics properties defined
    for (auto* section : unit->allSections) {
        if (!section->definition->physics.has_value()) {
            continue;  // Skip sections without debris physics
        }

        // Calculate velocity contribution from rotation
        Vector2 relPos = {
            section->worldPosition.x - unitPos.x,
            section->worldPosition.y - unitPos.y
        };
        b2Vec2 debrisVel = {
            unitVel.x - unitAngVel * relPos.y,
            unitVel.y + unitAngVel * relPos.x
        };

        // Calculate accumulated height for this section (relative heights sum up the chain)
        float sectionHeight = getAccumulatedHeight(section);

        DebrisObject obj = createDebrisFromSection(
            *section->definition,
            section->model,
            section->hasModel,
            section->worldPosition,
            section->worldRotation,
            sectionHeight,
            debrisVel,
            unitAngVel
        );

        // Clear model from section so it's not unloaded when unit is destroyed
        if (section->hasModel) {
            section->hasModel = false;  // Ownership transferred to debris
        }

        debris.push_back(obj);
        m_debris.push_back(obj);
    }

    // Destroy the unit
    destroyInstance(unit);

    return debris;
}

DebrisObject UnitManager::createDebrisFromSection(
    const SectionDefinition& section,
    const Model& model,
    bool hasModel,
    Vector2 position,
    float rotation,
    float accumulatedHeight,
    b2Vec2 velocity,
    float angularVelocity
) {
    DebrisObject obj;
    obj.model = model;
    obj.hasModel = hasModel;
    obj.height = accumulatedHeight;  // Use pre-computed accumulated height
    obj.collisionGroup = m_nextCollisionGroup--;

    if (!section.physics.has_value()) {
        return obj;
    }

    const auto& phys = *section.physics;

    // Set body user data for contact event identification
    obj.bodyUserData.tag = BodyTag::Debris;
    obj.bodyUserData.owner = nullptr;

    // Create physics body
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {position.x, position.y};
    bodyDef.rotation = b2MakeRot(rotation);
    bodyDef.linearDamping = phys.linearDamping;
    bodyDef.angularDamping = phys.angularDamping;
    bodyDef.userData = &obj.bodyUserData;

    obj.bodyId = b2CreateBody(m_worldId, &bodyDef);

    // Create shape
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = phys.density;
    shapeDef.friction = phys.friction;
    shapeDef.restitution = phys.restitution;
    shapeDef.filter.categoryBits = CATEGORY_DEBRIS;
    shapeDef.filter.maskBits = MASK_DEBRIS;
    shapeDef.filter.groupIndex = obj.collisionGroup;

    switch (phys.shapeType) {
        case PhysicsShapeType::Circle: {
            b2Circle circle;
            circle.center = {phys.circle.offset.x, phys.circle.offset.y};
            circle.radius = phys.circle.radius;
            b2CreateCircleShape(obj.bodyId, &shapeDef, &circle);
            break;
        }
        case PhysicsShapeType::Box: {
            b2Polygon box = b2MakeOffsetBox(
                phys.box.width / 2.0f,
                phys.box.height / 2.0f,
                {phys.box.offset.x, phys.box.offset.y},
                0.0f
            );
            b2CreatePolygonShape(obj.bodyId, &shapeDef, &box);
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
                b2CreatePolygonShape(obj.bodyId, &shapeDef, &poly);
            }
            break;
        }
        default:
            break;
    }

    // Set initial velocity
    b2Body_SetLinearVelocity(obj.bodyId, velocity);
    b2Body_SetAngularVelocity(obj.bodyId, angularVelocity);

    return obj;
}

const std::vector<DebrisObject>& UnitManager::getDebris() const {
    return m_debris;
}

void UnitManager::clearDebris() {
    for (auto& debris : m_debris) {
        if (b2Body_IsValid(debris.bodyId)) {
            b2DestroyBody(debris.bodyId);
        }
        if (debris.hasModel) {
            UnloadModel(debris.model);
        }
    }
    m_debris.clear();
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void UnitManager::update(float dt) {
    // Animation frame timing (30 fps target)
    constexpr float ANIM_FRAME_TIME = 1.0f / 30.0f;
    // Body speed (world units/s) above which an "anim_moving" section plays its walk clip.
    constexpr float ANIM_MOVING_SPEED = 0.25f;
    static float animTimer = 0.0f;
    animTimer += dt;
    bool advanceFrame = animTimer >= ANIM_FRAME_TIME;
    if (advanceFrame) {
        animTimer -= ANIM_FRAME_TIME;
    }

    for (auto& instance : m_instances) {
        if (!instance || !instance->active) continue;

        // Flush accumulated realtime damage (e.g. from explosions) on the 0.1s tick.
        updateRealtimeDamage(instance->combatState, dt);

        // Update transforms from unit's single physics body
        if (instance->rootSection && b2Body_IsValid(instance->bodyId)) {
            b2Vec2 pos = b2Body_GetPosition(instance->bodyId);
            float rot = b2Rot_GetAngle(b2Body_GetRotation(instance->bodyId));

            instance->rootSection->worldPosition = {pos.x, pos.y};
            instance->rootSection->worldRotation = rot;

            // Update children using code-based positioning
            for (auto& child : instance->rootSection->children) {
                updateSectionTransforms(child.get(), instance->rootSection->worldPosition, rot);
            }
        }

        // Is the unit moving? Drives idle/move animation selection below.
        bool moving = false;
        if (b2Body_IsValid(instance->bodyId)) {
            b2Vec2 v = b2Body_GetLinearVelocity(instance->bodyId);
            moving = (v.x * v.x + v.y * v.y) > (ANIM_MOVING_SPEED * ANIM_MOVING_SPEED);
        }

        // Update animations for all sections
        for (auto* section : instance->allSections) {
            if (section->animCount <= 0) continue;

            // "anim_moving" sections: clip 0 is the walk cycle, clip 1 the idle pose (the
            // legs.glb ordering — moving selects 0, idle selects 1). Falls back to clip 0 if
            // the model has only one clip. Switching clips restarts at frame 0; both loop.
            if (section->definition && section->definition->animMoving) {
                int desired = (moving || section->animCount < 2) ? 0 : 1;
                if (desired != section->currentAnim) {
                    section->currentAnim = desired;
                    section->currentFrame = 0;
                }
                section->animPlaying = true;
            }

            if (section->animPlaying && advanceFrame) {
                int animIdx = section->currentAnim % section->animCount;
                ModelAnimation& anim = section->animations[animIdx];

                section->currentFrame++;
                if (section->currentFrame >= anim.frameCount) {
                    section->currentFrame = 0;  // Loop animation
                }

                UpdateModelAnimation(section->model, anim, section->currentFrame);
            }
        }
    }
}

void UnitManager::updateSectionTransforms(
    SectionInstance* section,
    Vector2 parentWorldPos,
    float parentWorldRot
) {
    if (!section) return;

    const auto* def = section->definition;
    if (!def) return;

    // Compute world position (always follows parent offset)
    // offset.x/y are 2D physics coords, offset.z is vertical height (handled in rendering)
    float cosR = std::cos(parentWorldRot);
    float sinR = std::sin(parentWorldRot);

    section->worldPosition = {
        parentWorldPos.x + def->offset.x * cosR - def->offset.y * sinR,
        parentWorldPos.y + def->offset.x * sinR + def->offset.y * cosR
    };

    // Compute world rotation based on mode
    switch (def->rotationMode) {
        case SectionRotationMode::FollowUnit:
            section->worldRotation = parentWorldRot + def->localRotation;
            break;
        case SectionRotationMode::FollowFacing:
            section->worldRotation = section->facingAngle;
            break;
        case SectionRotationMode::Fixed:
            section->worldRotation = def->localRotation;
            break;
    }

    // Update children recursively
    for (auto& child : section->children) {
        updateSectionTransforms(child.get(), section->worldPosition, section->worldRotation);
    }
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void UnitManager::drawModelWithEnv(const Model& model, Vector3 position, float rotAngleDeg,
                                    Vector3 scale) {
    // Replicate DrawModelEx's transform (scale -> rotate about +Y -> translate, then model.transform).
    Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
    Matrix matRotation = MatrixRotate({0.0f, 1.0f, 0.0f}, rotAngleDeg * DEG2RAD);
    Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
    transform = MatrixMultiply(model.transform, transform);

    const bool haveEnvLoc = (m_useEnvMapLoc >= 0);
    const int envOn = 1;
    const int envOff = 0;

    for (int i = 0; i < model.meshCount; i++) {
        const Material& material = model.materials[model.meshMaterial[i]];

        // A material is env-mapped iff envMapApplyExtras bound a texture into its metalness slot.
        if (haveEnvLoc) {
            const MaterialMap& envSlot = material.maps[MATERIAL_MAP_METALNESS];
            if (envSlot.texture.id > 0) {
                SetShaderValue(m_envShader, m_useEnvMapLoc, &envOn, SHADER_UNIFORM_INT);
                if (m_envIntensityLoc >= 0) {
                    SetShaderValue(m_envShader, m_envIntensityLoc, &envSlot.value, SHADER_UNIFORM_FLOAT);
                }
            } else {
                SetShaderValue(m_envShader, m_useEnvMapLoc, &envOff, SHADER_UNIFORM_INT);
            }
        }

        DrawMesh(model.meshes[i], material, transform);
    }

    // Restore env-off so any later draws sharing this shader (tiles, debris, other units' first
    // mesh before its own toggle) are never left with a stray env term.
    if (haveEnvLoc) SetShaderValue(m_envShader, m_useEnvMapLoc, &envOff, SHADER_UNIFORM_INT);
}

void UnitManager::applyShaderToModels(Shader shader) {
    // Capture the shader and (once per distinct program) resolve the env-map uniform locations
    // used by drawModelWithEnv.
    m_envShader = shader;
    if (shader.id != m_envLocsShaderId) {
        m_useEnvMapLoc = GetShaderLocation(shader, "useEnvMap");
        m_envIntensityLoc = GetShaderLocation(shader, "envIntensity");
        m_envLocsShaderId = shader.id;
    }

    for (auto& instance : m_instances) {
        if (!instance) continue;
        for (auto* section : instance->allSections) {
            if (section->hasModel) {
                for (int i = 0; i < section->model.materialCount; ++i) {
                    section->model.materials[i].shader = shader;
                }
            }
        }
    }

    // Also apply to debris
    for (auto& debris : m_debris) {
        if (debris.hasModel) {
            for (int i = 0; i < debris.model.materialCount; ++i) {
                debris.model.materials[i].shader = shader;
            }
        }
    }
}

void UnitManager::renderAll(const std::vector<float>* heightOffsets) {
    for (auto& instance : m_instances) {
        if (!instance || !instance->active || !instance->visible) continue;
        if (instance->rootSection) {
            // renderHeightOffset lifts the whole unit (device-on-top overlay); 0 normally.
            renderSection(instance->rootSection.get(), heightOffsets, instance->allSections,
                          instance->renderHeightOffset);
        }
    }
}

int UnitManager::getSectionIndex(SectionInstance* section, const std::vector<SectionInstance*>& allSections) {
    for (size_t i = 0; i < allSections.size(); ++i) {
        if (allSections[i] == section) return static_cast<int>(i);
    }
    return -1;
}

void UnitManager::renderSection(SectionInstance* section, const std::vector<float>* heightOffsets,
                                 const std::vector<SectionInstance*>& allSections, float parentHeight) {
    if (!section) return;

    // Calculate this section's absolute height by adding its relative offset.z to parent's height
    float height = parentHeight + section->definition->offset.z;

    // Debug: print heights on first render
    static int frameCount = 0;
    if (parentHeight == 0.0f) frameCount++;
    if (frameCount == 1) {
        std::cout << "[RENDER] " << section->definition->name
                  << ": parent=" << parentHeight
                  << " + offset.z=" << section->definition->offset.z
                  << " = " << height << std::endl;
    }
    if (heightOffsets) {
        int idx = getSectionIndex(section, allSections);
        if (idx >= 0 && idx < (int)heightOffsets->size()) {
            height += (*heightOffsets)[idx];
        }
    }

    if (section->hasModel) {
        // Map 2D physics to 3D rendering
        // Physics: X right, Y forward (into screen)
        // World: X right, Y up (height), Z into screen
        // Physics Y -> World Z (no negation)
        Vector3 position = {
            section->worldPosition.x,
            height,
            section->worldPosition.y
        };

        // Physics rotation is CCW in physics XY plane
        // When mapping to 3D (physics Y -> world Z), the rotation direction flips
        // visually because we're looking at the XZ plane from +Y (above)
        // Negate the angle to get correct visual rotation
        drawModelWithEnv(
            section->model,
            position,
            -section->worldRotation * RAD2DEG,
            section->definition->scale
        );
    }

    // Render children, passing this section's accumulated height
    for (auto& child : section->children) {
        renderSection(child.get(), heightOffsets, allSections, height);
    }

}

void UnitManager::renderDebris() {
    for (auto& debris : m_debris) {
        if (!debris.hasModel || !b2Body_IsValid(debris.bodyId)) continue;

        b2Vec2 pos = b2Body_GetPosition(debris.bodyId);
        float rot = b2Rot_GetAngle(b2Body_GetRotation(debris.bodyId));

        Vector3 position = {pos.x, debris.height, pos.y};

        drawModelWithEnv(debris.model, position, -rot * RAD2DEG, {1, 1, 1});
    }
}

void UnitManager::renderDebug(const std::vector<float>* heightOffsets) {
    for (auto& instance : m_instances) {
        if (!instance || !instance->active) continue;

        // Draw unit collision circle
        if (b2Body_IsValid(instance->bodyId)) {
            b2Vec2 pos = b2Body_GetPosition(instance->bodyId);
            float y = 0.1f;

            Vector3 center = {pos.x, y, pos.y};
            float radius = instance->definition->collisionRadius;

            DrawCircle3D(center, radius, {1, 0, 0}, 90.0f, GREEN);

            // Draw proximity radius (fainter)
            DrawCircle3D(center, instance->definition->proximityRadius, {1, 0, 0}, 90.0f,
                        Fade(SKYBLUE, 0.3f));
        }

        // Render section debug
        if (instance->rootSection) {
            renderSectionDebug(instance->rootSection.get(), heightOffsets, instance->allSections);
        }
    }
}

void UnitManager::renderSectionDebug(SectionInstance* section, const std::vector<float>* heightOffsets,
                                      const std::vector<SectionInstance*>& allSections, float parentHeight) {
    if (!section) return;

    // Calculate this section's absolute height by adding its relative offset.z to parent's height
    float height = parentHeight + section->definition->offset.z;
    int sectionIdx = getSectionIndex(section, allSections);
    if (heightOffsets && sectionIdx >= 0 && sectionIdx < (int)heightOffsets->size()) {
        height += (*heightOffsets)[sectionIdx];
    }

    float y = height + 0.1f;

    // Draw joint connection to parent
    if (section->parent) {
        Vector3 from = {
            section->parent->worldPosition.x,
            parentHeight + 0.1f,
            section->parent->worldPosition.y
        };
        Vector3 to = {
            section->worldPosition.x,
            y,
            section->worldPosition.y
        };
        DrawLine3D(from, to, LIME);
    }

    // Render children debug, passing this section's accumulated height
    for (auto& child : section->children) {
        renderSectionDebug(child.get(), heightOffsets, allSections, height);
    }
}

void UnitManager::renderDebrisDebug() {
    for (auto& debris : m_debris) {
        if (!b2Body_IsValid(debris.bodyId)) continue;

        b2Vec2 pos = b2Body_GetPosition(debris.bodyId);
        float y = debris.height + 0.1f;

        Vector3 center = {pos.x, y, pos.y};

        // Draw a small circle for debris
        DrawCircle3D(center, 0.1f, {1, 0, 0}, 90.0f, RED);
    }
}
