#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "viewer_state.h"

//------------------------------------------------------------------------------
// Camera Controller
//
// Handles camera movement for orbit and free camera modes.
//------------------------------------------------------------------------------

// Process camera input and update camera state
void cameraControllerUpdate(LevelViewerState* state, float deltaTime);

// Reset camera to default view
void cameraControllerReset(LevelViewerState* state);

#endif // CAMERA_CONTROLLER_H
