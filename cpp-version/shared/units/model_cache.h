#ifndef MODEL_CACHE_H
#define MODEL_CACHE_H

#include "raylib.h"
#include <string>
#include <unordered_map>

//------------------------------------------------------------------------------
// ModelCache — shares GLTF model data across unit instances so a droid type's
// heavy vertex/index buffers and textures load once, not per instance.
//
// Static models (no skeletal animation) are loaded once and returned as a shallow
// `Model` that shares the underlying GPU mesh/material buffers — callers must NOT
// UnloadModel a shared model (the cache owns it). Animated models are NOT shared: a
// single Model holds one skinned pose (CPU skinning writes the shared mesh VBOs), so
// the cache reports them as `animated` and the caller loads its own per-instance copy.
//
// World-agnostic: models carry no physics/world state, so one cache serves every
// level's world and the droid library. Destroy before the GL context closes.
//------------------------------------------------------------------------------

// Set smooth (bilinear) filtering on a model's diffuse/base-colour textures. Raylib's glTF loader
// leaves textures on the default POINT filter, which looks blocky when a model is drawn larger than
// its texture (e.g. the small copper disk). Unit models want linear magnification. Call once after
// LoadModel. (Tile textures use a separate pixel-art pipeline and are intentionally point-filtered.)
void modelSetSmoothTextureFilter(Model& model);

class ModelCache {
public:
    ~ModelCache();
    void destroy();  // UnloadModel every shared model; safe to call once, before window close

    struct Entry {
        Model model;    // valid only when `shared`
        bool animated;  // path has skeletal animation -> not shareable
        bool shared;    // true: use `model` (do not unload); false: caller loads its own
    };

    // Resolve `resolvedPath` (already base-path-joined) to a shared static Model, or report
    // it as animated so the caller loads a per-instance copy.
    Entry get(const std::string& resolvedPath);

    int sharedCount() const { return static_cast<int>(cache_.size()); }  // tests/logging

private:
    struct Cached {
        Model model{};
        bool animated = false;
        bool hasModel = false;
    };
    std::unordered_map<std::string, Cached> cache_;
};

#endif // MODEL_CACHE_H
