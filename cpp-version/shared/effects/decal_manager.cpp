#include "effects/decal_manager.h"
#include <algorithm>
#include <cmath>

namespace {
float randomYaw() {
    return (float)GetRandomValue(0, 62831) / 10000.0f;   // 0 .. ~2PI
}
}  // namespace

void DecalManager::build(int levelCount) {
    byLevel_.assign(levelCount > 0 ? levelCount : 0, {});
    active_ = 0;
}

void DecalManager::setActiveLevel(int level) {
    active_ = level;
}

std::vector<Decal>* DecalManager::activeVec() {
    if (active_ < 0 || active_ >= (int)byLevel_.size()) return nullptr;
    return &byLevel_[active_];
}

const std::vector<Decal>* DecalManager::activeVec() const {
    if (active_ < 0 || active_ >= (int)byLevel_.size()) return nullptr;
    return &byLevel_[active_];
}

const std::vector<Decal>& DecalManager::active() const {
    static const std::vector<Decal> kEmpty;
    const std::vector<Decal>* v = activeVec();
    return v ? *v : kEmpty;
}

static void pushCapped(std::vector<Decal>& v, const Decal& d) {
    if ((int)v.size() >= DECAL_MAX_PER_LEVEL) v.erase(v.begin());  // drop the oldest mark
    v.push_back(d);
}

void DecalManager::spawnBlastmark(Vector2 pos, float size) {
    std::vector<Decal>* v = activeVec();
    if (!v || size <= 0.0f) return;
    pushCapped(*v, Decal{pos, size, randomYaw(), 1.0f, TEX_DECAL_BLASTMARK, true});
}

void DecalManager::spawnDrip(Vector2 pos, float size) {
    std::vector<Decal>* v = activeVec();
    if (!v || size <= 0.0f) return;
    pushCapped(*v, Decal{pos, size, randomYaw(), 1.0f, TEX_DECAL_DRIP, true});
}

void DecalManager::update(float /*dt*/) {
    std::vector<Decal>* v = activeVec();
    if (!v) return;
    // Cleaning drives alpha toward 0 (cleanAt); reap the fully-faded ones here.
    v->erase(std::remove_if(v->begin(), v->end(),
                            [](const Decal& d) { return d.alpha <= 0.0f; }),
             v->end());
}

int DecalManager::nearestCleanable(Vector2 pos, float maxDist) const {
    const std::vector<Decal>* v = activeVec();
    if (!v) return -1;
    int best = -1;
    float bestD2 = maxDist * maxDist;
    for (int i = 0; i < (int)v->size(); ++i) {
        const Decal& d = (*v)[i];
        if (!d.cleanable || d.alpha <= 0.0f) continue;
        float dx = d.pos.x - pos.x, dy = d.pos.y - pos.y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

bool DecalManager::cleanAt(int idx, float dt) {
    std::vector<Decal>* v = activeVec();
    if (!v || idx < 0 || idx >= (int)v->size()) return true;   // gone → treat as done
    Decal& d = (*v)[idx];
    d.alpha -= DECAL_CLEAN_RATE * dt;
    if (d.alpha <= 0.0f) { d.alpha = 0.0f; return true; }
    return false;
}

void DecalManager::clear() {
    for (auto& v : byLevel_) v.clear();
}
