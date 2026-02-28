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
    // Unit with 50% armour — takes half damage
    UnitCombatState state;
    state.maxHealth = 100.0f;
    state.currentHealth = 100.0f;
    state.armour = 50.0f;
    state.alive = true;

    bool alive = applyDamage(state, 60.0f);

    EXPECT_TRUE(alive);
    EXPECT_TRUE(isAlive(state));
    // 60 raw * (1 - 0.5) = 30 effective damage, 100 - 30 = 70 HP
    EXPECT_FLOAT_EQ(state.currentHealth, 70.0f);
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
    props.energy = 3;
    props.armour = 40.0f;

    UnitCombatState state = initCombatState(props);

    // energy 3 * HEALTH_PER_ENERGY(100) = 300 HP
    EXPECT_FLOAT_EQ(state.maxHealth, 300.0f);
    EXPECT_FLOAT_EQ(state.currentHealth, 300.0f);
    EXPECT_FLOAT_EQ(state.armour, 40.0f);
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

TEST(CombatState, ArmourClampedTo100) {
    // Armour above 100 is clamped — unit takes no damage
    DroidProperties props;
    props.energy = 1;
    props.armour = 150.0f;  // Over 100

    UnitCombatState state = initCombatState(props);

    EXPECT_FLOAT_EQ(state.armour, 100.0f);

    bool alive = applyDamage(state, 500.0f);
    EXPECT_TRUE(alive);
    EXPECT_FLOAT_EQ(state.currentHealth, 100.0f);
}
