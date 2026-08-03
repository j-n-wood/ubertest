#include "model_cache.h"
#include "../rendering/env_map.h"

void modelSetSmoothTextureFilter(Model& model) {
    for (int i = 0; i < model.materialCount; ++i) {
        Texture2D diffuse = model.materials[i].maps[MATERIAL_MAP_ALBEDO].texture;
        // Skip the shared 1x1 default (untextured materials) — only filter real image textures.
        if (diffuse.id > 0 && diffuse.width > 1) {
            SetTextureFilter(diffuse, TEXTURE_FILTER_BILINEAR);
        }
    }
}

ModelCache::~ModelCache() {
    destroy();
}

void ModelCache::destroy() {
    for (auto& kv : cache_) {
        if (kv.second.hasModel) {
            UnloadModel(kv.second.model);
            kv.second.hasModel = false;
        }
    }
    cache_.clear();
}

ModelCache::Entry ModelCache::get(const std::string& resolvedPath) {
    auto it = cache_.find(resolvedPath);
    if (it == cache_.end()) {
        Cached c;
        // Probe for skeletal animation without keeping the animation data (the cache never
        // owns animations; animated models are loaded per-instance by the caller).
        int animCount = 0;
        ModelAnimation* anims = LoadModelAnimations(resolvedPath.c_str(), &animCount);
        if (anims) UnloadModelAnimations(anims, animCount);
        c.animated = (animCount > 0);
        if (!c.animated) {
            c.model = LoadModel(resolvedPath.c_str());
            c.hasModel = IsModelValid(c.model);
            // Bind any env maps declared in the glTF material `extras` (Raylib ignores extras).
            // The shared Model owns these textures; ModelCache::destroy's UnloadModel frees them.
            if (c.hasModel) {
                modelSetSmoothTextureFilter(c.model);
                envMapApplyExtras(c.model, resolvedPath);
            }
        }
        it = cache_.emplace(resolvedPath, c).first;
    }

    const Cached& c = it->second;
    if (c.animated || !c.hasModel) {
        return Entry{Model{}, c.animated, false};  // caller loads its own
    }
    return Entry{c.model, false, true};  // shared — do not unload
}
