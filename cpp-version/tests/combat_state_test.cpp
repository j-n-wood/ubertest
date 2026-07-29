#include <gtest/gtest.h>
#include "units/combat_state.h"

//------------------------------------------------------------------------------
// DamageModel tests
//------------------------------------------------------------------------------

TEST(DamageModel, BasicDamage) {
    // Unit with no armour, 100 HP — damage exceeding health kills it
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 100.0f;
    state.armour = 0.0f;
    state.alive = true;

    bool alive = applyDamage(state, 150.0f);

    EXPECT_FALSE(alive);
    EXPECT_FALSE(isAlive(state));
    EXPECT_FLOAT_EQ(state.currentHealth, 0.0f);
}

TEST(DamageModel, ArmourReduction) {
    // Armour is a flat reduction: 60 raw - 5 armour = 55 effective, 100 - 55 = 45 HP.
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 100.0f;
    state.armour = 5.0f;
    state.alive = true;

    bool alive = applyDamage(state, 60.0f);

    EXPECT_TRUE(alive);
    EXPECT_TRUE(isAlive(state));
    EXPECT_FLOAT_EQ(state.currentHealth, 45.0f);
}

TEST(DamageModel, ArmourFullyAbsorbsWeakHits) {
    // A hit that doesn't beat the armour deals nothing (damage -= armour < 0).
    UnitCombatState state;
    state.maxHealth = 50.0f;
    state.currentHealth = 50.0f;
    state.armour = 8.0f;
    state.alive = true;

    bool alive = applyDamage(state, 6.0f);

    EXPECT_TRUE(alive);
    EXPECT_FLOAT_EQ(state.currentHealth, 50.0f);
}

TEST(DamageModel, ZeroDamage) {
    // Zero damage is a no-op
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 100.0f;
    state.armour = 0.0f;
    state.alive = true;

    bool alive = applyDamage(state, 0.0f);

    EXPECT_TRUE(alive);
    EXPECT_FLOAT_EQ(state.currentHealth, 100.0f);
}

TEST(DamageModel, OverkillClamps) {
    // Health must not go below zero
    UnitCombatState state;
    state.maxHealth = 50.0f;
    state.currentHealth = 50.0f;
    state.armour = 0.0f;
    state.alive = true;

    applyDamage(state, 9999.0f);

    EXPECT_FALSE(isAlive(state));
    EXPECT_FLOAT_EQ(state.currentHealth, 0.0f);
}

//------------------------------------------------------------------------------
// CombatState tests
//------------------------------------------------------------------------------

TEST(CombatState, InitFromProperties) {
    DroidProperties props;
    props.energy = 40;
    props.armour = 6.0f;

    UnitCombatState state = initCombatState(props);

    // energy IS health (HEALTH_PER_ENERGY == 1)
    EXPECT_FLOAT_EQ(state.maxHealth, 40.0f);
    EXPECT_FLOAT_EQ(state.currentHealth, 40.0f);
    EXPECT_FLOAT_EQ(state.armour, 6.0f);
    EXPECT_TRUE(state.alive);
}

TEST(CombatState, InitFromPropertiesZeroEnergy) {
    // Class 0 has energy=0 — should get MIN_HEALTH
    DroidProperties props;
    props.energy = 0;
    props.armour = 40.0f;

    UnitCombatState state = initCombatState(props);

    EXPECT_FLOAT_EQ(state.maxHealth, MIN_HEALTH);
    EXPECT_FLOAT_EQ(state.currentHealth, MIN_HEALTH);
    EXPECT_FLOAT_EQ(state.armour, 40.0f);
    EXPECT_TRUE(state.alive);
}

TEST(CombatState, InitFromPropertiesMissingKeys) {
    // Default properties should use defaults (energy=0, armour=0)
    DroidProperties props;

    UnitCombatState state = initCombatState(props);

    EXPECT_FLOAT_EQ(state.maxHealth, MIN_HEALTH);
    EXPECT_FLOAT_EQ(state.currentHealth, MIN_HEALTH);
    EXPECT_FLOAT_EQ(state.armour, 0.0f);
    EXPECT_TRUE(state.alive);
}

TEST(CombatState, DestroyKillsUnit) {
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 100.0f;
    state.armour = 0.0f;
    state.alive = true;

    destroy(state);

    EXPECT_FALSE(isAlive(state));
    EXPECT_FLOAT_EQ(state.currentHealth, 0.0f);
}

TEST(CombatState, DamageOnDeadUnitIsNoop) {
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 0.0f;
    state.armour = 0.0f;
    state.alive = false;

    bool alive = applyDamage(state, 50.0f);

    EXPECT_FALSE(alive);
    EXPECT_FLOAT_EQ(state.currentHealth, 0.0f);
}

//------------------------------------------------------------------------------
// Realtime (continuous) damage: accumulate, flush once per REALTIME_DAMAGE_INTERVAL.
//------------------------------------------------------------------------------
TEST(RealtimeDamage, AccumulatesAndFlushesOnTick) {
    UnitCombatState s;
    s.maxHealth = 100.0f; s.currentHealth = 100.0f; s.armour = 0.0f; s.alive = true;

    accumulateRealtimeDamage(s, 3.0f);
    updateRealtimeDamage(s, 0.05f);               // < interval → not flushed
    EXPECT_FLOAT_EQ(s.currentHealth, 100.0f);
    EXPECT_FLOAT_EQ(s.pendingDamage, 3.0f);

    accumulateRealtimeDamage(s, 4.0f);            // pending now 7
    updateRealtimeDamage(s, 0.05f);               // crosses 0.1s → flush 7
    EXPECT_FLOAT_EQ(s.currentHealth, 93.0f);
    EXPECT_FLOAT_EQ(s.pendingDamage, 0.0f);
}

TEST(RealtimeDamage, ArmourAppliedOncePerFlushNotPerFrame) {
    UnitCombatState s;
    s.maxHealth = 100.0f; s.currentHealth = 100.0f; s.armour = 2.0f; s.alive = true;

    // 10 raw accumulated across the interval, flushed once → armour subtracted once (10-2=8).
    accumulateRealtimeDamage(s, 10.0f);
    updateRealtimeDamage(s, 0.1f);
    EXPECT_FLOAT_EQ(s.currentHealth, 92.0f);
}

TEST(RealtimeDamage, ZeroDtIsNoop) {
    UnitCombatState s;
    s.maxHealth = 100.0f; s.currentHealth = 100.0f; s.alive = true;
    accumulateRealtimeDamage(s, 5.0f);
    updateRealtimeDamage(s, 0.0f);
    EXPECT_FLOAT_EQ(s.currentHealth, 100.0f);     // timer didn't advance
    EXPECT_FLOAT_EQ(s.pendingDamage, 5.0f);
}

TEST(CombatState, ArmourFromPropertiesIsFlat) {
    // Armour comes straight from properties (non-negative) and is a flat reduction.
    DroidProperties props;
    props.energy = 50;
    props.armour = 7.0f;

    UnitCombatState state = initCombatState(props);

    EXPECT_FLOAT_EQ(state.armour, 7.0f);
    EXPECT_FLOAT_EQ(state.maxHealth, 50.0f);

    applyDamage(state, 11.0f);                     // 11 - 7 = 4 through
    EXPECT_FLOAT_EQ(state.currentHealth, 46.0f);

    applyDamage(state, 5.0f);                       // under armour → absorbed
    EXPECT_FLOAT_EQ(state.currentHealth, 46.0f);
}
