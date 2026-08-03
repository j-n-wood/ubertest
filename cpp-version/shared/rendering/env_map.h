#ifndef ENV_MAP_H
#define ENV_MAP_H

#include "raylib.h"
#include <string>
#include <vector>

// One material's env-map declaration, parsed from glTF `extras` (pure data, no GPU work).
struct EnvMapEntry {
    int gltfMaterialIndex;   // index into the glTF `materials` array (Raylib's model index is +1)
    std::string texturePath; // env texture path exactly as written in extras (relative to gltf dir)
    float color[4];          // envColor (legacy SPECULARITY); defaults to {1,1,1,1}
    float intensity;         // envIntensity additive strength; defaults to 1.0
};

// Parse a glTF file's per-material env-map extras. Returns one entry per material that declares a
// non-empty `envTexture`. Does no GPU work (testable headless); a missing/binary/malformed file
// yields an empty vector.
std::vector<EnvMapEntry> envMapReadExtras(const std::string& gltfPath);

//------------------------------------------------------------------------------
// Environment mapping (legacy DRAWTYPE ENVMAP / EFFECTTEXTURE).
//
// Old-school spherical env mapping is NOT a standard glTF feature — an env map is
// view-relative *context*, not part of the model. We carry the reference in each
// material's glTF `extras`:
//
//   "materials": [ { ..., "extras": {
//       "envTexture":   "textures/envmapgold.png",  // path, relative to the .gltf dir
//       "envColor":     [0.8, 0.4, 0.2, 1.0],        // optional: legacy SPECULARITY modulation
//       "envIntensity": 1.0                          // optional: additive strength (default 1)
//   } } ]
//
// `envMapApplyExtras` reads those extras (Raylib's glTF loader ignores them) and binds the env
// texture into the material's MATERIAL_MAP_METALNESS slot — which the lighting shader samples as
// `texture1` for the env term. `envColor` is written to that map's color (surfaced to the shader
// as `colSpecular`, the modulation), and `envIntensity` to the map's scalar `value` (read per-mesh
// in the unit draw loop and pushed to the `envIntensity` uniform). See docs/env_mapping.md.
//
// Loaded env textures become owned by the Model (freed by the matching UnloadModel); a fresh copy
// is loaded per material so shared files never double-free.
//------------------------------------------------------------------------------
void envMapApplyExtras(Model& model, const std::string& gltfPath);

#endif // ENV_MAP_H
