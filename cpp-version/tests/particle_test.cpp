#include <gtest/gtest.h>
#include "particles/particle_manager.h"
#include <cmath>

// A deterministic burst config (fixed speed/lifetime so tests don't depend on rand()).
static ParticleBurst fixedCfg(int n) {
    ParticleBurst b;
    b.count = n;
    b.speedMin = b.speedMax = 2.0f;    // constant speed
    b.lifeMin = b.lifeMax = 0.5f;      // constant lifetime
    b.startSize = 0.2f; b.endSize = 0.0f;
    b.startColor = {255, 255, 255, 255}; b.endColor = {255, 255, 255, 0};
    b.angularVelMax = 0.0f;
    b.texture = TEX_FLARE;
    return b;
}

TEST(Particles, BurstCreatesCount) {
    ParticleManager m;
    m.burst(fixedCfg(16), {0, 0});
    EXPECT_EQ(m.count(), 16u);
}

TEST(Particles, MovesByVelocity) {
    ParticleManager m;
    m.burst(fixedCfg(1), {5.0f, 5.0f});
    ParticleSpan a = m.renderData();
    ASSERT_EQ(a.n, 1u);
    float x0 = a.posX[0], y0 = a.posY[0];

    m.update(0.1f);
    ParticleSpan b = m.renderData();
    ASSERT_EQ(b.n, 1u);
    float dx = b.posX[0] - x0, dy = b.posY[0] - y0;
    EXPECT_NEAR(sqrtf(dx * dx + dy * dy), 0.2f, 1e-4f);  // speed 2.0 * dt 0.1
    EXPECT_NEAR(b.age[0], 0.1f, 1e-5f);
}

TEST(Particles, ExpireAfterLifetime) {
    ParticleManager m;
    m.burst(fixedCfg(10), {0, 0});
    m.update(0.4f);              // < 0.5 lifetime
    EXPECT_EQ(m.count(), 10u);
    m.update(0.2f);              // total 0.6 > 0.5 → all expire
    EXPECT_EQ(m.count(), 0u);
}

TEST(Particles, SwapPopKeepsArraysConsistent) {
    // Two bursts with different lifetimes; after a partial update the short-lived ones expire
    // (swap-and-pop) and the survivors' arrays stay in sync (position matches a live particle).
    ParticleManager m;
    ParticleBurst shortLived = fixedCfg(5); shortLived.lifeMin = shortLived.lifeMax = 0.1f;
    ParticleBurst longLived  = fixedCfg(5); longLived.lifeMin  = longLived.lifeMax  = 1.0f;
    m.burst(shortLived, {0, 0});
    m.burst(longLived, {10, 10});
    m.update(0.2f);                        // short-lived gone, long-lived remain
    ParticleSpan s = m.renderData();
    ASSERT_EQ(s.n, 5u);
    for (std::size_t i = 0; i < s.n; ++i) {
        EXPECT_LT(s.age[i], s.lifetime[i]);          // only survivors
        EXPECT_FLOAT_EQ(s.lifetime[i], 1.0f);        // they are the long-lived ones
    }
}

TEST(Particles, ClearEmpties) {
    ParticleManager m;
    m.burst(fixedCfg(5), {0, 0});
    m.clear();
    EXPECT_EQ(m.count(), 0u);
}
