#ifndef TEST_SCENE_H
#define TEST_SCENE_H

#include "units/unit_manager.h"
#include "raylib.h"
#include "box2d/box2d.h"

//------------------------------------------------------------------------------
// Test Scene - Manages the unit test environment
//------------------------------------------------------------------------------

struct TestScene {
    // Physics
    b2WorldId worldId = b2_nullWorldId;

    // Unit system
    UnitManager units;
    UnitInstance* currentUnit = nullptr;
    std::string currentUnitPath;

    // Camera
    Camera3D camera = {};

    // State
    bool paused = false;
    bool showDebug = true;
    bool showInfo = true;

    // Ground plane
    Model groundModel = {};
    bool hasGroundModel = false;

    // Enclosing walls for collision testing
    b2BodyId wallBodies[4] = {b2_nullBodyId, b2_nullBodyId, b2_nullBodyId, b2_nullBodyId};
    float wallBounds = 10.0f;  // Half-size of enclosure
};

// Initialize the test scene
void testSceneInit(TestScene* scene);

// Destroy the test scene
void testSceneDestroy(TestScene* scene);

// Load and spawn a unit from a JSON file
bool testSceneLoadUnit(TestScene* scene, const char* path);

// Reset the current unit (destroy and respawn)
void testSceneResetUnit(TestScene* scene);

// Update the scene (physics, input, camera)
void testSceneUpdate(TestScene* scene, float dt);

// Render the scene
void testSceneRender(TestScene* scene);

// Render debug information
void testSceneRenderDebug(TestScene* scene);

// Render info overlay
void testSceneRenderInfo(TestScene* scene);

// Handle input
void testSceneHandleInput(TestScene* scene);

// Create/update enclosing walls based on unit size
void testSceneCreateWalls(TestScene* scene, float unitRadius);

#endif // TEST_SCENE_H
