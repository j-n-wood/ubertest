#include "test_scene.h"
#include "units/unit_json.h"
#include "raymath.h"
#include <cmath>
#include <iostream>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

constexpr float CAMERA_MOVE_SPEED = 5.0f;
constexpr float CAMERA_ROTATE_SPEED = 0.003f;
constexpr float HEIGHT_OFFSET_STEP = 0.025f;  // Per-section height adjustment step (25mm)
constexpr float ROTATION_STEP = 0.05f;        // Manual rotation step (~3 degrees)
constexpr float ROTATION_SPEED_STEP = 0.5f;   // Rotation speed adjustment (rad/sec)
constexpr float FACING_ANGLE_STEP = 0.1f;     // Facing angle adjustment (~6 degrees)

// Default camera setup: 45 degree angle, 3m from origin
constexpr float DEFAULT_CAMERA_DISTANCE = 3.0f;
constexpr float DEFAULT_CAMERA_ANGLE = 45.0f * DEG2RAD;  // 45 degrees from horizontal

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------

bool testSceneInit(TestScene* scene, const char* shaderPath, const char* modelsBasePath) {
    // Store models base path for resolving model references
    if (modelsBasePath && modelsBasePath[0] != '\0') {
        scene->modelsBasePath = modelsBasePath;
    }

    // Initialize scene renderer with lighting shader
    if (!sceneRendererInit(&scene->renderer, shaderPath)) {
        std::cerr << "Failed to initialize scene renderer" << std::endl;
        return false;
    }

    // Configure lighting - directional light from above
    // Light direction = target - position, so position at origin and target above gives upward direction
    sceneRendererAddDirectionalLight(&scene->renderer,
        {0, 0, 0},    // Position (reference point)
        {0, 50, 0},   // Target (light shines toward this, so direction is UP)
        WHITE);

    // Set effective eye height for specular calculations
    sceneRendererSetEffectiveEyeHeight(&scene->renderer, 1.0f);

    // Set ambient light - same as main game
    sceneRendererSetAmbient(&scene->renderer, 0.15f, 0.15f, 0.15f, 1.0f);

    // Create physics world with zero gravity (top-down)
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0, 0};
    scene->worldId = b2CreateWorld(&worldDef);

    // Initialize unit manager with optional models base path
    scene->units.init(scene->worldId, scene->modelsBasePath.c_str());

    // Setup camera - 45 degree angle from top-left-front, 3m from target
    // Position: looking from front-left at 45 degrees down
    float dist = DEFAULT_CAMERA_DISTANCE;
    float pitch = DEFAULT_CAMERA_ANGLE;
    float yaw = -45.0f * DEG2RAD;  // Front-left corner

    scene->camera.position = {
        dist * std::cos(pitch) * std::sin(yaw),   // X
        dist * std::sin(pitch),                    // Y (height)
        dist * std::cos(pitch) * std::cos(yaw)    // Z
    };
    scene->camera.target = {0, 0, 0};
    scene->camera.up = {0, 1, 0};
    scene->camera.fovy = 45.0f;
    scene->camera.projection = CAMERA_PERSPECTIVE;

    // Create simple ground plane
    scene->groundModel = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 1, 1));
    scene->hasGroundModel = IsModelValid(scene->groundModel);

    // Apply shader to ground model
    if (scene->hasGroundModel) {
        sceneRendererApplyShader(&scene->renderer, &scene->groundModel);
    }

    scene->paused = false;
    scene->showDebug = true;
    scene->showInfo = true;

    return true;
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

    sceneRendererDestroy(&scene->renderer);
}

//------------------------------------------------------------------------------
// Unit Management
//------------------------------------------------------------------------------

bool testSceneLoadUnit(TestScene* scene, const char* path) {
    // Clear any debris from previous unit
    scene->units.clearDebris();

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
    std::cout << "  Collision radius: " << def->collisionRadius << std::endl;
    std::cout << "  Proximity radius: " << def->proximityRadius << std::endl;
    std::cout << "  Sections: " << scene->currentUnit->allSections.size() << std::endl;

    // Apply lighting shader to unit models
    scene->units.applyShaderToModels(sceneRendererGetShader(&scene->renderer));

    // Initialize per-section height offsets (all zeros initially)
    scene->sectionHeightOffsets.clear();
    scene->sectionHeightOffsets.resize(scene->currentUnit->allSections.size(), 0.0f);
    scene->selectedSection = 0;
    scene->heightsModified = false;

    // Reset rotation
    scene->manualRotation = 0.0f;
    scene->rotationSpeed = 0.0f;
    scene->facingAngle = 0.0f;

    return true;
}

void testSceneResetUnit(TestScene* scene) {
    if (scene->currentUnitPath.empty()) return;
    testSceneLoadUnit(scene, scene->currentUnitPath.c_str());
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void testSceneUpdate(TestScene* scene, float dt) {
    // Handle input first
    testSceneHandleInput(scene);

    // Update rotation
    if (!scene->paused && scene->rotationSpeed != 0.0f) {
        scene->manualRotation += scene->rotationSpeed * dt;
    }

    // Update physics and unit transforms
    if (!scene->paused) {
        b2World_Step(scene->worldId, dt, 4);

        // Set unit rotation via physics body
        if (scene->currentUnit && b2Body_IsValid(scene->currentUnit->bodyId)) {
            b2Vec2 pos = b2Body_GetPosition(scene->currentUnit->bodyId);
            b2Body_SetTransform(scene->currentUnit->bodyId, pos, b2MakeRot(scene->manualRotation));
        }

        // Update facing angle for FollowFacing sections
        if (scene->currentUnit) {
            for (auto* section : scene->currentUnit->allSections) {
                section->facingAngle = scene->facingAngle;
            }
        }

        // Update unit manager (syncs transforms from physics)
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
        dist = std::clamp(dist - wheel * 0.5f, 1.0f, 50.0f);
        dir = Vector3Normalize(dir);
        scene->camera.position = Vector3Add(scene->camera.target, Vector3Scale(dir, dist));
    }
}

void testSceneHandleInput(TestScene* scene) {
    // Left/Right arrows - Rotate unit
    if (IsKeyPressed(KEY_LEFT)) {
        scene->manualRotation += ROTATION_STEP;
    }
    if (IsKeyPressed(KEY_RIGHT)) {
        scene->manualRotation -= ROTATION_STEP;
    }

    // Up/Down arrows - Adjust rotation speed
    if (IsKeyPressed(KEY_UP)) {
        scene->rotationSpeed += ROTATION_SPEED_STEP;
        std::cout << "Rotation speed: " << scene->rotationSpeed << " rad/sec" << std::endl;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        scene->rotationSpeed -= ROTATION_SPEED_STEP;
        std::cout << "Rotation speed: " << scene->rotationSpeed << " rad/sec" << std::endl;
    }

    // 0 - Reset rotation
    if (IsKeyPressed(KEY_ZERO)) {
        scene->manualRotation = 0.0f;
        scene->rotationSpeed = 0.0f;
        std::cout << "Rotation reset" << std::endl;
    }

    // F/G - Adjust facing angle for FollowFacing sections
    if (IsKeyPressed(KEY_F)) {
        scene->facingAngle += FACING_ANGLE_STEP;
        std::cout << "Facing angle: " << (scene->facingAngle * RAD2DEG) << " deg" << std::endl;
    }
    if (IsKeyPressed(KEY_G)) {
        scene->facingAngle -= FACING_ANGLE_STEP;
        std::cout << "Facing angle: " << (scene->facingAngle * RAD2DEG) << " deg" << std::endl;
    }

    // X - Dismantle unit into debris
    if (IsKeyPressed(KEY_X) && scene->currentUnit) {
        std::vector<DebrisObject> debris = scene->units.dismantleUnit(scene->currentUnit);
        std::cout << "Dismantled unit into " << debris.size() << " debris objects" << std::endl;
        scene->currentUnit = nullptr;  // Unit was destroyed
    }

    // R - Reset unit
    if (IsKeyPressed(KEY_R)) {
        testSceneResetUnit(scene);
        std::cout << "Reset unit" << std::endl;
    }

    // P - Toggle pause
    if (IsKeyPressed(KEY_P)) {
        scene->paused = !scene->paused;
        std::cout << (scene->paused ? "Paused" : "Resumed") << std::endl;
    }

    // F1 - Toggle debug draw
    if (IsKeyPressed(KEY_F1)) {
        scene->showDebug = !scene->showDebug;
    }

    // I - Toggle info overlay
    if (IsKeyPressed(KEY_I)) {
        scene->showInfo = !scene->showInfo;
    }

    // Section selection with Tab/Shift+Tab
    if (IsKeyPressed(KEY_TAB) && scene->currentUnit) {
        int numSections = (int)scene->currentUnit->allSections.size();
        if (numSections > 0) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                scene->selectedSection = (scene->selectedSection - 1 + numSections) % numSections;
            } else {
                scene->selectedSection = (scene->selectedSection + 1) % numSections;
            }
            std::cout << "Selected section: " << scene->selectedSection << std::endl;
        }
    }

    // Per-section height adjustment
    // ] - Increase selected section height
    if (IsKeyPressed(KEY_RIGHT_BRACKET) && scene->currentUnit) {
        if (scene->selectedSection >= 0 && scene->selectedSection < (int)scene->sectionHeightOffsets.size()) {
            scene->sectionHeightOffsets[scene->selectedSection] += HEIGHT_OFFSET_STEP;
            scene->heightsModified = true;
            std::cout << "Section " << scene->selectedSection << " height offset: "
                      << scene->sectionHeightOffsets[scene->selectedSection] << std::endl;
        }
    }

    // [ - Decrease selected section height
    if (IsKeyPressed(KEY_LEFT_BRACKET) && scene->currentUnit) {
        if (scene->selectedSection >= 0 && scene->selectedSection < (int)scene->sectionHeightOffsets.size()) {
            scene->sectionHeightOffsets[scene->selectedSection] -= HEIGHT_OFFSET_STEP;
            scene->heightsModified = true;
            std::cout << "Section " << scene->selectedSection << " height offset: "
                      << scene->sectionHeightOffsets[scene->selectedSection] << std::endl;
        }
    }

    // Ctrl+S - Save unit with adjusted heights
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S)) {
        if (testSceneSaveUnit(scene)) {
            std::cout << "Saved unit with adjusted heights" << std::endl;
        } else {
            std::cout << "Failed to save unit" << std::endl;
        }
    }
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void testSceneRender(TestScene* scene) {
    // Update camera position for specular calculations
    sceneRendererUpdateCamera(&scene->renderer, scene->camera.position);

    BeginMode3D(scene->camera);

    // Draw ground plane
    if (scene->hasGroundModel) {
        DrawModel(scene->groundModel, {0, -0.01f, 0}, 1.0f, DARKGRAY);
    }

    // Draw grid
    DrawGrid(20, 0.5f);

    // Render units with per-section height offsets
    const std::vector<float>* offsets = scene->sectionHeightOffsets.empty() ? nullptr : &scene->sectionHeightOffsets;
    scene->units.renderAll(offsets);

    // Render debris
    scene->units.renderDebris();

    // Debug visualization
    if (scene->showDebug) {
        scene->units.renderDebug(offsets);
        scene->units.renderDebrisDebug();
    }

    EndMode3D();

    // Info overlay
    if (scene->showInfo) {
        testSceneRenderInfo(scene);
    }
}

void testSceneRenderInfo(TestScene* scene) {
    int y = 10;
    int lineHeight = 18;

    DrawText("Unit Test Tool", 10, y, 20, WHITE);
    y += lineHeight + 10;

    // Controls
    DrawText("Controls:", 10, y, 16, LIGHTGRAY);
    y += lineHeight;
    DrawText("  WASD/QE - Move camera", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Right Mouse - Orbit camera", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Scroll - Zoom", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Left/Right - Rotate unit", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Up/Down - Rotation speed", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  0 - Reset rotation", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  F/G - Adjust facing angle", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  X - Dismantle to debris", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  R - Reset unit", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  P - Pause", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Tab - Select section", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  [/] - Adjust height", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  Ctrl+S - Save", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  F1 - Toggle debug", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  I - Toggle info", 10, y, 14, GRAY);
    y += lineHeight;
    DrawText("  ESC - Exit", 10, y, 14, GRAY);
    y += lineHeight + 10;

    // Rotation info
    DrawText(TextFormat("Rotation: %.1f deg", scene->manualRotation * RAD2DEG), 10, y, 14, YELLOW);
    y += lineHeight;
    if (scene->rotationSpeed != 0.0f) {
        DrawText(TextFormat("Speed: %.2f rad/s", scene->rotationSpeed), 10, y, 14, YELLOW);
        y += lineHeight;
    }
    DrawText(TextFormat("Facing: %.1f deg", scene->facingAngle * RAD2DEG), 10, y, 14, SKYBLUE);
    y += lineHeight + 5;

    // Modified indicator
    if (scene->heightsModified) {
        DrawText("Heights modified *", 10, y, 14, ORANGE);
        y += lineHeight + 5;
    }

    // Status
    if (scene->paused) {
        DrawText("PAUSED", 10, y, 18, YELLOW);
        y += lineHeight + 5;
    }

    // Debris count
    size_t debrisCount = scene->units.getDebris().size();
    if (debrisCount > 0) {
        DrawText(TextFormat("Debris: %d", (int)debrisCount), 10, y, 14, RED);
        y += lineHeight + 5;
    }

    // Unit info
    if (scene->currentUnit && scene->currentUnit->definition) {
        const auto* def = scene->currentUnit->definition;
        DrawText(TextFormat("Unit: %s", def->name.c_str()), 10, y, 16, WHITE);
        y += lineHeight;
        DrawText(TextFormat("ID: %s", def->id.c_str()), 10, y, 14, LIGHTGRAY);
        y += lineHeight;
        DrawText(TextFormat("Collision: %.2f  Proximity: %.2f", def->collisionRadius, def->proximityRadius), 10, y, 14, LIGHTGRAY);
        y += lineHeight;
        DrawText(TextFormat("Sections: %d", (int)scene->currentUnit->allSections.size()), 10, y, 14, LIGHTGRAY);
        y += lineHeight;

        // Root position
        if (scene->currentUnit->rootSection) {
            auto& pos = scene->currentUnit->rootSection->worldPosition;
            DrawText(TextFormat("Position: (%.2f, %.2f)", pos.x, pos.y), 10, y, 14, LIGHTGRAY);
            y += lineHeight;
        }

        // Section list with selection
        y += 5;
        DrawText("Sections (Tab to select, [/] to adjust):", 10, y, 14, LIGHTGRAY);
        y += lineHeight;
        int idx = 0;
        for (auto* section : scene->currentUnit->allSections) {
            bool isSelected = (idx == scene->selectedSection);
            float heightOffset = (idx < (int)scene->sectionHeightOffsets.size())
                               ? scene->sectionHeightOffsets[idx] : 0.0f;
            float totalHeight = section->definition->offset.z + heightOffset;

            Color textColor = isSelected ? YELLOW : GRAY;
            const char* selector = isSelected ? ">" : " ";

            // Show rotation mode
            const char* modeStr = "";
            if (section->definition->rotationMode == SectionRotationMode::FollowFacing) {
                modeStr = " [F]";
            } else if (section->definition->rotationMode == SectionRotationMode::Fixed) {
                modeStr = " [X]";
            }

            DrawText(TextFormat("%s[%d] %s h:%.3f%s%s", selector, idx,
                     section->definition->name.c_str(), totalHeight,
                     (heightOffset != 0.0f) ? "*" : "", modeStr),
                     10, y, 14, textColor);
            y += lineHeight;
            idx++;
            if (idx >= 10) break;  // Only show first 10
        }
    } else if (debrisCount == 0) {
        DrawText("No unit loaded", 10, y, 16, RED);
    }

    // FPS
    DrawFPS(GetScreenWidth() - 100, 10);
}

//------------------------------------------------------------------------------
// Save Unit
//------------------------------------------------------------------------------

// Helper to apply height offsets to a section tree (matches allSections order)
static void applyHeightOffsets(SectionDefinition& section, const std::vector<float>& offsets, int& index) {
    if (index < (int)offsets.size()) {
        section.offset.z += offsets[index];
    }
    ++index;

    for (auto& child : section.children) {
        applyHeightOffsets(child, offsets, index);
    }
}

bool testSceneSaveUnit(TestScene* scene) {
    if (!scene->currentUnit || !scene->currentUnit->definition) {
        return false;
    }

    if (scene->currentUnitPath.empty()) {
        std::cerr << "No unit path to save to" << std::endl;
        return false;
    }

    // Make a copy of the definition to modify
    UnitDefinition modifiedDef = *scene->currentUnit->definition;

    // Apply per-section height offsets
    int index = 0;
    applyHeightOffsets(modifiedDef.rootSection, scene->sectionHeightOffsets, index);

    // Save to file
    if (!saveUnitDefinitionToFile(scene->currentUnitPath, modifiedDef)) {
        return false;
    }

    // Unload cached definition and reload from the saved file
    std::string defId = scene->currentUnit->definition->id;
    std::string path = scene->currentUnitPath;
    scene->units.unloadDefinition(defId);
    testSceneLoadUnit(scene, path.c_str());

    return true;
}
