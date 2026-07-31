#include "particles/particle_manager.h"

#include <cmath>
#include <cstdlib>

namespace {
float frand01() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }
float frand(float lo, float hi) { return lo + (hi - lo) * frand01(); }
}  // namespace

void ParticleManager::burst(const ParticleBurst& cfg, Vector2 pos) {
    for (int i = 0; i < cfg.count; ++i) {
        // Full 360° radial by default; a small spreadRad emits a directional cone.
        float ang = (cfg.spreadRad >= PI) ? frand01() * 2.0f * PI
                                          : cfg.dirAngle + frand(-cfg.spreadRad, cfg.spreadRad);
        float speed = frand(cfg.speedMin, cfg.speedMax);
        posX_.push_back(pos.x);
        posY_.push_back(pos.y);
        velX_.push_back(cosf(ang) * speed);
        velY_.push_back(sinf(ang) * speed);
        age_.push_back(0.0f);
        lifetime_.push_back(frand(cfg.lifeMin, cfg.lifeMax));
        rot_.push_back(frand01() * 360.0f);
        angVel_.push_back(frand(-cfg.angularVelMax, cfg.angularVelMax));
        startSize_.push_back(cfg.startSize);
        endSize_.push_back(cfg.endSize);
        startColor_.push_back(cfg.startColor);
        endColor_.push_back(cfg.endColor);
        texture_.push_back(static_cast<int>(cfg.texture));
    }
}

void ParticleManager::update(float dt) {
    const std::size_t n = posX_.size();
    // Hot loops: unit-stride, only the arrays each op needs.
    for (std::size_t i = 0; i < n; ++i) posX_[i] += velX_[i] * dt;
    for (std::size_t i = 0; i < n; ++i) posY_[i] += velY_[i] * dt;
    for (std::size_t i = 0; i < n; ++i) rot_[i]  += angVel_[i] * dt;
    for (std::size_t i = 0; i < n; ++i) age_[i]  += dt;

    // Expire (swap-and-pop; order doesn't matter for unordered dust).
    std::size_t i = 0;
    while (i < posX_.size()) {
        if (age_[i] >= lifetime_[i]) swapPop(i);  // don't advance i: a live element moved in
        else ++i;
    }
}

void ParticleManager::swapPop(std::size_t i) {
    const std::size_t last = posX_.size() - 1;
    auto sp = [&](auto& v) { v[i] = v[last]; v.pop_back(); };
    sp(posX_); sp(posY_); sp(velX_); sp(velY_); sp(age_); sp(lifetime_);
    sp(rot_); sp(angVel_); sp(startSize_); sp(endSize_);
    sp(startColor_); sp(endColor_); sp(texture_);
}

void ParticleManager::clear() {
    posX_.clear(); posY_.clear(); velX_.clear(); velY_.clear();
    age_.clear(); lifetime_.clear(); rot_.clear(); angVel_.clear();
    startSize_.clear(); endSize_.clear();
    startColor_.clear(); endColor_.clear(); texture_.clear();
}

ParticleSpan ParticleManager::renderData() const {
    ParticleSpan s;
    s.n = posX_.size();
    s.posX = posX_.data();
    s.posY = posY_.data();
    s.age = age_.data();
    s.lifetime = lifetime_.data();
    s.rot = rot_.data();
    s.startSize = startSize_.data();
    s.endSize = endSize_.data();
    s.startColor = startColor_.data();
    s.endColor = endColor_.data();
    s.texture = texture_.data();
    return s;
}
