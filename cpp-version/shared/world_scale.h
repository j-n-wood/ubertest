#ifndef WORLD_SCALE_H
#define WORLD_SCALE_H

//------------------------------------------------------------------------------
// Canonical world scale (GLTF-metric convention)
//------------------------------------------------------------------------------
// The sim/render frame is metric: one TMX tile spans 64 legacy game-units and one unit is
// 0.0254 m, so 1 tile = 64 * 0.0254 = 1.6256 m. This is the `worldScale` passed to the level
// renderer/collision, and the factor by which spatial quantities that were originally authored in
// the old "1 tile = 1 world unit" convention are rescaled to metres.
//
// Multiply a spatial constant that was tuned at "1 tile = 1 unit" by WORLD_SCALE to express it in
// metres. Do NOT apply it to angles, times, damage, coefficients, or to model/section geometry
// (unit GLTF models are authored at true metric scale and are the ground truth).
constexpr float WORLD_SCALE = 1.6256f;  // = 64 * SCALE_UNITS_TO_METERS(0.0254)

#endif // WORLD_SCALE_H
