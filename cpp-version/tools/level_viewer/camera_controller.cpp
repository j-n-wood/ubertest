#include "camera_controller.h"
#include <cmath>

//------------------------------------------------------------------------------
// Camera Controller
//------------------------------------------------------------------------------

void cameraControllerUpdate(LevelViewerState* state, float deltaTime) {
    // Orbit controls (Q/E)
    float orbitSpeed = 60.0f;  // degrees per second

    if (IsKeyDown(KEY_Q)) {
        state->cameraOrbitAngle -= orbitSpeed * deltaTime;
    }
    if (IsKeyDown(KEY_E)) {
        state->cameraOrbitAngle += orbitSpeed * deltaTime;
    }

    // Auto rotate
    if (state->autoRotate) {
        state->cameraOrbitAngle += 15.0f * deltaTime;
    }

    // Keep angle in range
    while (state->cameraOrbitAngle < 0) state->cameraOrbitAngle += 360.0f;
    while (state->cameraOrbitAngle >= 360.0f) state->cameraOrbitAngle -= 360.0f;

    // Zoom controls (Up/Down arrows or +/-)
    float zoomSpeed = 15.0f;  // units per second
    float minDist = 5.0f;
    float maxDist = 100.0f;

    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD)) {
        state->cameraOrbitDistance -= zoomSpeed * deltaTime;
        if (state->cameraOrbitDistance < minDist) state->cameraOrbitDistance = minDist;
    }
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT)) {
        state->cameraOrbitDistance += zoomSpeed * deltaTime;
        if (state->cameraOrbitDistance > maxDist) state->cameraOrbitDistance = maxDist;
    }

    // Height adjustment (Page Up/Down)
    float heightSpeed = 10.0f;
    float minHeight = 2.0f;
    float maxHeight = 50.0f;

    if (IsKeyDown(KEY_PAGE_UP)) {
        state->cameraHeight += heightSpeed * deltaTime;
        if (state->cameraHeight > maxHeight) state->cameraHeight = maxHeight;
    }
    if (IsKeyDown(KEY_PAGE_DOWN)) {
        state->cameraHeight -= heightSpeed * deltaTime;
        if (state->cameraHeight < minHeight) state->cameraHeight = minHeight;
    }

    // WASD camera movement - always available
    // Moves the camera target (orbit center) in the XZ plane
    float moveSpeed = 10.0f;

    // Get forward and right vectors based on current orbit angle
    // Forward is the direction the camera is looking (toward target)
    float angle = state->cameraOrbitAngle * DEG2RAD;
    Vector3 forward = {-sinf(angle), 0, -cosf(angle)};  // Points from camera toward target
    Vector3 right = {cosf(angle), 0, -sinf(angle)};     // Perpendicular to forward

    Vector3 move = {0, 0, 0};
    if (IsKeyDown(KEY_W)) {
        move.x += forward.x * moveSpeed * deltaTime;
        move.z += forward.z * moveSpeed * deltaTime;
    }
    if (IsKeyDown(KEY_S)) {
        move.x -= forward.x * moveSpeed * deltaTime;
        move.z -= forward.z * moveSpeed * deltaTime;
    }
    if (IsKeyDown(KEY_A)) {
        move.x -= right.x * moveSpeed * deltaTime;
        move.z -= right.z * moveSpeed * deltaTime;
    }
    if (IsKeyDown(KEY_D)) {
        move.x += right.x * moveSpeed * deltaTime;
        move.z += right.z * moveSpeed * deltaTime;
    }

    // Move the camera target (orbit center)
    state->cameraTarget.x += move.x;
    state->cameraTarget.z += move.z;

    // Update camera position based on orbit parameters
    viewerStateUpdateCamera(state);
}

void cameraControllerReset(LevelViewerState* state) {
    state->cameraOrbitAngle = 0.0f;
    viewerStateCenterCamera(state);
}
