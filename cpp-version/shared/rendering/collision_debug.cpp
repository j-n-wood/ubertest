#include "rendering/collision_debug.h"
#include "rlgl.h"

void drawCollisionWireframe(const std::vector<std::vector<Vector2>>& polygons, float y, Color color) {
    // rlgl batches geometry, so flush the scene, disable depth test, draw, then flush the debug WHILE
    // depth test is still off (otherwise the toggle is applied at EndMode3D with depth restored).
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (const auto& poly : polygons) {
        const int n = static_cast<int>(poly.size());
        for (int k = 0; k < n; ++k) {
            const Vector2& a = poly[k];
            const Vector2& b = poly[(k + 1) % n];
            DrawCylinderEx((Vector3){a.x, y, a.y}, (Vector3){b.x, y, b.y}, 0.04f, 0.04f, 5, color);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}
