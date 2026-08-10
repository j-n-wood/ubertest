#ifndef COLLISION_DEBUG_H
#define COLLISION_DEBUG_H

#include "raylib.h"
#include <vector>

//------------------------------------------------------------------------------
// Shared collision-shape wireframe overlay. Each polygon is a list of vertices in
// the game's 2D physics plane (render X, render Z). Drawn as raised bars with the
// depth test disabled, so the outlines read over the rendered geometry rather than
// clipping into it. Used by BOTH the game's collision debug and the viewer's
// collision-wireframe view so they look identical.
//------------------------------------------------------------------------------
// y defaults near the floor (the collision is a wall footprint at ground level) so it aligns with
// the wall base under the top-down perspective camera instead of parallax-shifting like a raised bar.
void drawCollisionWireframe(const std::vector<std::vector<Vector2>>& polygons,
                            float y = 0.05f, Color color = ORANGE);

#endif // COLLISION_DEBUG_H
