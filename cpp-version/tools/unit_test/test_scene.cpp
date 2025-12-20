#include "test_scene.h"
#include "units/unit_json.h"
#include "lighting/light.h"
#include "raymath.h"
#include <cmath>
#include <iostream>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

constexpr float CAMERA_MOVE_SPEED = 10.0f;
constexpr float CAMERA_ROTATE_SPEED = 0.003f;
constexpr float IMPULSE_STRENGTH = 50.0f;
constexpr float EXPLOSION_STRENGTH = 100.0f;
constexpr float EXPLOSION_JITTER = 20.0f;
constexpr float WALL_CLEARANCE = 5.0f;  // Extra space beyond unit radius
constexpr float WALL_THICKNESS = 1.0f;

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------

void testSceneInit(TestScene* scene) {
    // Create physics world with zero gravity (top-down)
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0, 0};
    scene->worldId = b2CreateWorld(&worldDef);

    // Initialize unit manager
    scene->units.init(scene->worldId);

    // Setup camera
    scene->camera.position = {0, 15, 10};
    scene->camera.target = {0, 0, 0};
    scene->camera.up = {0, 1, 0};
    scene->camera.fovy = 45.0f;
    scene->camera.projection = CAMERA_PERSPECTIVE;

    // Create simple ground plane
    scene->groundModel = LoadModelFromMesh(GenMeshPlane(50.0f, 50.0f, 1, 1));
    scene->hasGroundModel = IsModelValid(scene->groundModel);

    scene->paused = false;
    scene->showDebug = true;
    scene->showInfo = true;
}

void testSceneDestroy(TestScene* scene) {
    if (scene->currentUnit) {
        scene->units.destroyInstance(scene->currentUnit);
        scene->currentUnit = nullptr;
    }

    scene->units.destroy();

    if (b2World_IsValid(scene->worldId)) {
        b2DestroyWorld(scene->worldId);
        scene->worldId = b2_nullWorldId;
    }

    if (scene->hasGroundModel) {
        UnloadModel(scene->groundModel);
        scene->hasGroundModel = false;
    }
}

//------------------------------------------------------------------------------
// Unit Management
//------------------------------------------------------------------------------

bool testSceneLoadUnit(TestScene* scene, const char* path) {
    // Destroy existing unit
    if (scene->currentUnit) {
        scene->units.destroyInstance(scene->currentUnit);
        scene->currentUnit = nullptr;
    }

    // Load definition
    const UnitDefinition* def = scene->units.loadDefinition(path);
    if (!def) {
        std::cerr << "Failed to load unit definition: " << path << std::endl;
        return false;
    }

    // Create instance at origin
    scene->currentUnit = scene->units.createInstance(def, {0, 0}, 0);
    if (!scene->currentUnit) {
        std::cerr << "Failed to create unit instance" << std::endl;
        return false;
    }

    scene->currentUnitPath = path;
    std::cout << "Loaded unit: " << def->name << " (" << def->id << ")" << std::endl;

    // Calculate unit radius from all sections for wall placement
    float maxRadius = 0.0f;
    for (auto* section : scene->currentUnit->allSections) {
        if (section->definition && section->definition->physics.has_value()) {
            const auto& phys = *section->definition->physics;
            float sectionRadius = 0.0f;

            // Get offset from root
            float offsetDist = std::sqrt(
                section->definition->localOffset.x * section->definition->localOffset.x +
                section->definition->localOffset.y * section->definition->localOffset.y
            );

            switch (phys.shapeType) {
                case PhysicsShapeType::Circle:
                    sectionRadius = offsetDist + phys.circle.radius;
                    break;
                case PhysicsShapeType::Box:
                    sectionRadius = offsetDist + std::sqrt(phys.box.width * phys.box.width +
                                                           phys.box.height * phys.box.height) / 2.0f;
                    break;
                default:
                    sectionRadius = offsetDist + 1.0f;
                    break;
            }

            maxRadius = std::max(maxRadius, sectionRadius);
        }
    }

    // Create enclosing walls
    testSceneCreateWalls(scene, std::max(maxRadius, 3.0f));

    return true;
}

void testSceneResetUnit(TestScene* scene) {
    if (scene->currentUnitPath.empty()) return;
    testSceneLoadUnit(scene, scene->currentUnitPath.c_str());
}

//------------------------------------------------------------------------------
// Wall Creation
//------------------------------------------------------------------------------

void testSceneCreateWalls(TestScene* scene, float unitRadius) {
    // Destroy existing walls
    for (int i = 0; i < 4; ++i) {
        if (b2Body_IsValid(scene->wallBodies[i])) {
            b2DestroyBody(scene->wallBodies[i]);
            scene->wallBodies[i] = b2_nullBodyId;
        }
    }

    // Calculate wall bounds based on unit size
    scene->wallBounds = unitRadius + WALL_CLEARANCE;

    // Create static wall bodies (top, bottom, left, right)
    // Wall positions: offset from center by bounds + half thickness
    struct WallDef {
        float x, y;       // Position
        float hw, hh;     // Half-width, half-height
    };

    WallDef walls[4] = {
        {0, -scene->wallBounds - WALL_THICKNESS/2, scene->wallBounds + WALL_THICKNESS, WALL_THICKNESS/2},  // Top (negative Y in physics)
        {0, scene->wallBounds + WALL_THICKNESS/2, scene->wallBounds + WALL_THICKNESS, WALL_THICKNESS/2},   // Bottom
        {-scene->wallBounds - WALL_THICKNESS/2, 0, WALL_THICKNESS/2, scene->wallBounds + WALL_THICKNESS},  // Left
        {scene->wallBounds + WALL_THICKNESS/2, 0, WALL_THICKNESS/2, scene->wallBounds + WALL_THICKNESS},   // Right
    };

    for (int i = 0; i < 4; ++i) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {walls[i].x, walls[i].y};

        scene->wallBodies[i] = b2CreateBody(scene->worldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.friction = 0.5f;
        shapeDef.restitution = 0.3f;

        b2Polygon box = b2MakeBox(walls[i].hw, walls[i].hh);
        b2CreatePolygonShape(scene->wallBodies[i], &shapeDef, &box);
    }

    std::cout << "Created walls with bounds: " << scene->wallBounds << std::endl;
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void testSceneUpdate(TestScene* scene, float dt) {
    // Handle input first
    testSceneHandleInput(scene);

    // Update physics
    if (!scene->paused) {
        b2World_Step(scene->worldId, dt, 4);
        scene->units.update(dt);
    }

    // Update camera with mouse look when right button held
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();

        // Rotate camera around target
        float yaw = -delta.x * CAMERA_ROTATE_SPEED;
        float pitch = -delta.y * CAMERA_ROTATE_SPEED;

        // Calculate camera direction
        Vector3 dir = Vector3Subtract(scene->camera.position, scene->camera.target);
        float dist = Vector3Length(dir);

        // Spherical coordinates
        float theta = std::atan2(dir.x, dir.z) + yaw;
        float phi = std::acos(dir.y / dist);
        phi = std::clamp(phi + pitch, 0.1f, 3.0f);

        // Convert back to cartesian
        dir.x = dist * std::sin(phi) * std::sin(theta);
        dir.y = dist * std::cos(phi);
        dir.z = dist * std::sin(phi) * std::cos(theta);

        scene->camera.position = Vector3Add(scene->camera.target, dir);
    }

    // Camera movement with WASD
    Vector3 forward = Vector3Normalize(Vector3Subtract(scene->camera.target, scene->camera.position));
    forward.y = 0;
    forward = Vector3Normalize(forward);
    Vector3 right = Vector3CrossProduct(forward, scene->camera.up);

    Vector3 move = {0, 0, 0};
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_Q)) move.y -= 1;
    if (IsKeyDown(KEY_E)) move.y += 1;

    if (Vector3Length(move) > 0) {
        move = Vector3Normalize(move);
        move = Vector3Scale(move, CAMERA_MOVE_SPEED * dt);
        scene->camera.position = Vector3Add(scene->camera.position, move);
        scene->camera.target = Vector3Add(scene->camera.target, move);
    }

    // Zoom with scroll wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector3 dir = Vector3Subtract(scene->camera.position, scene->camera.target);
        float dist = Vector3Length(dir);
        dist = std::clamp(dist - wheel * 2.0f, 2.0f, 100.0f);
        dir = Vector3Normalize(dir);
        scene->camera.position = Vector3Add(scene->camera.target, Vector3Scale(dir, dist));
    }
}

void testSceneHandleInput(TestScene* scene) {
    // Space - Apply impulse to root body
    if (IsKeyPressed(KEY_SPACE) && scene->currentUnit && scene->currentUnit->rootSection) {
        auto* root = scene->currentUnit->rootSection.get();
        if (root->hasPhysics && b2Body_IsValid(root->bodyId)) {
            // Apply impulse in a random direction
            float angle = GetRandomValue(0, 360) * DEG2RAD;
            b2Vec2 impulse = {
                std::cos(angle) * IMPULSE_STRENGTH,
                std::sin(angle) * IMPULSE_STRENGTH
            };
            b2Body_ApplyLinearImpulseToCenter(root->bodyId, impulse, true);
            std::cout << "Applied impulse" << std::endl;
        }
    }

    // B - Break all joints
    if (IsKeyPressed(KEY_B) && scene->currentUnit) {
        scene->units.breakAllJoints(scene->currentUnit);
        std::cout << "Broke all joints" << std::endl;
    }

    // X - Explode: break all joints and apply explosive impulse from root
    if (IsKeyPressed(KEY_X) && scene->currentUnit && scene->currentUnit->rootSection) {
        // First break all joints
        scene->units.breakAllJoints(scene->currentUnit);

        // Get root position as explosion center
        auto& rootPos = scene->currentUnit->rootSection->worldPosition;

        // Apply explosive impulse to all sections
        for (auto* section : scene->currentUnit->allSections) {
            if (section->hasPhysics && b2Body_IsValid(section->bodyId)) {
                // Direction from root to this section
                float dx = section->worldPosition.x - rootPos.x;
                float dy = section->worldPosition.y - rootPos.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                // Normalize direction (default to random if at center)
                if (dist < 0.01f) {
                    float angle = GetRandomValue(0, 360) * DEG2RAD;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                } else {
                    dx /= dist;
                    dy /= dist;
                }

                // Add random jitter
                float jitterAngle = GetRandomValue(-30, 30) * DEG2RAD;
                float cosJ = std::cos(jitterAngle);
                float sinJ = std::sin(jitterAngle);
                float jdx = dx * cosJ - dy * sinJ;
                float jdy = dx * sinJ + dy * cosJ;

                // Add random magnitude variation
                float strength = EXPLOSION_STRENGTH + GetRandomValue(-20, 20);

                b2Vec2 impulse = {
                    jdx * strength + (GetRandomValue(-100, 100) / 100.0f) * EXPLOSION_JITTER,
                    jdy * strength + (GetRandomValue(-100, 100) / 100.0f) * EXPLOSION_JITTER
                };

                b2Body_ApplyLinearImpulseToCenter(section->bodyId, impulse, true);

                // Add some angular impulse too
                float torque = GetRandomValue(-50, 50) * 1.0f;
                b2Body_ApplyAngularImpulse(section->bodyId, torque, true);
            }
        }
        std::cout << "EXPLOSION!" << std::endl;
    }

    // R - Reset unit
    if (IsKeyPressed(KEY_R)) {
        testSceneResetUnit(scene);
        std::cout << "Reset unit" << std::endl;
    }

    // P - Toggle pause
    if (IsKeyPressed(KEY_P)) {
        scene->paused = !scene->paused;
        std::cout << "Physics " << (scene->paused ? "paused" : "resumed") << std::endl;
    }

    // F1 - Toggle debug draw
    if (IsKeyPressed(KEY_F1)) {
        scene->showDebug = !scene->showDebug;
    }

    // I - Toggle info overlay
    if (IsKeyPressed(KEY_I)) {
        scene->showInfo = !scene->showInfo;
    }

    // 1-9 - Break individual section joints
    for (int key = KEY_ONE; key <= KEY_NINE; ++key) {
        if (IsKeyPressed(key) && scene->currentUnit) {
            int index = key - KEY_ONE;  // 0-8
            if (index < (int)scene->currentUnit->allSections.size()) {
                SectionInstance* section = scene->currentUnit->allSections[index];
                if (section->attached && section->parent != nullptr) {
                    scene->units.breakJoint(section);
                    std::cout << "Broke joint for section [" << index << "]: "
                              << section->definition->name << std::endl;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void testSceneRender(TestScene* scene) {
    BeginMode3D(scene->camera);

    // Draw ground plane
    if (scene->hasGroundModel) {
        DrawModel(scene->groundModel, {0, -0.01f, 0}, 1.0f, DARKGRAY);
    }

    // Draw grid
    DrawGrid(50, 1.0f);

    // Render units
    scene->units.renderAll();

    // Debug visualization
    if (scene->showDebug) {
        scene->units.renderDebug();

        // Draw enclosing walls
        float wb = scene->wallBounds;
        float y = 0.5f;  // Wall height for visualization
        Color wallColor = BLUE;

        // Draw wall outlines (in 3D space: X is physics X, Z is physics Y)
        DrawLine3D({-wb, y, -wb}, {wb, y, -wb}, wallColor);   // Top
        DrawLine3D({-wb, y, wb}, {wb, y, wb}, wallColor);     // Bottom
        DrawLine3D({-wb, y, -wb}, {-wb, y, wb}, wallColor);   // Left
        DrawLine3D({wb, y, -wb}, {wb, y, wb}, wallColor);     // Right

        // Draw wall boxes
        DrawCubeWires({0, y/2, -wb - WALL_THICKNESS/2}, wb*2 + WALL_THICKNESS*2, y, WALL_THICKNESS, wallColor);
        DrawCubeWires({0, y/2, wb + WALL_THICKNESS/2}, wb*2 + WALL_THICKNESS*2, y, WALL_THICKNESS, wallColor);
        DrawCubeWires({-wb - WALL_THICKNESS/2, y/2, 0}, WALL_THICKNESS, y, wb*2 + WALL_THICKNESS*2, wallColor);
        DrawCubeWires({wb + WALL_THICKNESS/2, y/2, 0}, WALL_THICKNESS, y, wb*2 + WALL_THICKNESS*2, wallColor);
    }

    EndMode3D();

    // Info overlay
    if (scene->showInfo) {
        testSceneRenderInfo(scene);
    }
}

void testSceneRenderInfo(TestScene* scene) {
    int y = 10;
    int lineHeight = 20;

    DrawText("Unit Test Tool", 10, y, 20, WHITE);
    y += lineHeight + 10;

    // Controls
    DrawText("Controls:", 10, y, 16, LIGHTGRAY);
    y += lineHeight;
    DrawText("  WASD/QE - Move camera", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Right Mouse - Look around", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Scroll - Zoom", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Space - Apply impulse", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  B - Break all joints", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  X - Explode (break + impulse)", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  1-9 - Break section joint", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  R - Reset unit", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  P - Pause physics", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  F1 - Toggle debug", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  I - Toggle info", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  ESC - Exit", 10, y, 14, GRAY);
    y += lineHeight + 10;

    // Status
    if (scene->paused) {
        DrawText("PAUSED", 10, y, 20, YELLOW);
        y += lineHeight + 5;
    }

    // Unit info
    if (scene->currentUnit && scene->currentUnit->definition) {
        const auto* def = scene->currentUnit->definition;
        DrawText(TextFormat("Unit: %s", def->name.c_str()), 10, y, 16, WHITE);
        y += lineHeight;
        DrawText(TextFormat("ID: %s", def->id.c_str()), 10, y, 14, LIGHTGRAY);
        y += lineHeight;
        DrawText(TextFormat("Sections: %d", (int)scene->currentUnit->allSections.size()), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        // Count attached vs detached
        int attached = 0;
        int detached = 0;
        for (auto* section : scene->currentUnit->allSections) {
            if (section->attached || section->parent == nullptr) {
                attached++;
            } else {
                detached++;
            }
        }
        DrawText(TextFormat("Attached: %d  Detached: %d", attached, detached), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        // Root position
        if (scene->currentUnit->rootSection) {
            auto& pos = scene->currentUnit->rootSection->worldPosition;
            DrawText(TextFormat("Root pos: (%.1f, %.1f)", pos.x, pos.y), 10, y, 14, LIGHTGRAY);
            y += lineHeight;
        }

        // Section list with indices for deconstruction
        y += 5;
        DrawText("Sections (press 1-9 to break):", 10, y, 14, LIGHTGRAY);
        y += lineHeight;
        int idx = 0;
        for (auto* section : scene->currentUnit->allSections) {
            const char* status = section->parent == nullptr ? "root" :
                                (section->attached ? "attached" : "DETACHED");
            Color textColor = section->attached ? GRAY : RED;
            DrawText(TextFormat("  [%d] %s (%s)", idx, section->definition->name.c_str(), status),
                     10, y, 14, textColor);
            y += lineHeight;
            idx++;
            if (idx >= 9) break;  // Only show first 9
        }
    } else {
        DrawText("No unit loaded", 10, y, 16, RED);
    }

    // FPS
    DrawFPS(GetScreenWidth() - 100, 10);
}
