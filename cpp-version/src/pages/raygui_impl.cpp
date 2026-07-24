// Single translation unit that compiles the raygui implementation. All other
// files include "raygui.h" WITHOUT defining RAYGUI_IMPLEMENTATION.
#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
