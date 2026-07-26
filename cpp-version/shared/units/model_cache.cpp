#include "model_cache.h"

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
        }
        it = cache_.emplace(resolvedPath, c).first;
    }

    const Cached& c = it->second;
    if (c.animated || !c.hasModel) {
        return Entry{Model{}, c.animated, false};  // caller loads its own
    }
    return Entry{c.model, false, true};  // shared — do not unload
}
