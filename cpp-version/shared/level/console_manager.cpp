#include "console_manager.h"
#include "raymath.h"
#include <cmath>

void ConsoleManager::init(const std::vector<ConsoleSpec>& specs) {
    consoles_ = specs;
    playerInRange_ = false;
}

ConsoleManager::~ConsoleManager() {
    destroy();
}

void ConsoleManager::destroy() {
    consoles_.clear();
    playerInRange_ = false;
}

Vector2 consoleUseZoneCenter(const ConsoleSpec& c) {
    const float a = consoleFacingAngle(c.facingRad);
    // Front (accessible/room) direction = the footprint short-axis, on the FAR side of the box from
    // the console's back. Offset by 2*halfX so the zone sits adjacent to the front face.
    return {c.physicsCenter.x - 2.0f * CONSOLE_HALF_X * std::cos(a),
            c.physicsCenter.y + 2.0f * CONSOLE_HALF_X * std::sin(a)};
}

std::array<Vector2, 4> consoleUseZoneCorners(const ConsoleSpec& c) {
    const float a = consoleFacingAngle(c.facingRad);
    const float ca = std::cos(a), sa = std::sin(a);
    const Vector2 zc = consoleUseZoneCenter(c);
    std::array<Vector2, 4> out;
    const float sx[4] = {-1, 1, 1, -1}, sz[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        float x = sx[i] * CONSOLE_HALF_X, z = sz[i] * CONSOLE_HALF_Z;
        out[i] = {zc.x + x * ca + z * sa, zc.y - x * sa + z * ca};   // rotate about up (matches DrawModelEx)
    }
    return out;
}

void ConsoleManager::update(Vector2 playerPos) {
    // The console body blocks its own centre, so the "use" zone is a footprint-sized box placed in
    // front of the console (see consoleUseZoneCenter). Test the player against that oriented box.
    playerInRange_ = false;
    for (const ConsoleSpec& c : consoles_) {
        const float a = consoleFacingAngle(c.facingRad);
        const float ca = std::cos(a), sa = std::sin(a);
        const Vector2 zc = consoleUseZoneCenter(c);
        const float rx = playerPos.x - zc.x, ry = playerPos.y - zc.y;   // player in the zone's local frame
        const float lx = rx * ca - ry * sa;
        const float lz = rx * sa + ry * ca;
        if (std::fabs(lx) <= CONSOLE_HALF_X && std::fabs(lz) <= CONSOLE_HALF_Z) {
            playerInRange_ = true;
            break;
        }
    }
}
