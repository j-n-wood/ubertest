#include "combat_state.h"
#include <algorithm>

//------------------------------------------------------------------------------
// Combat state initialisation
//------------------------------------------------------------------------------

UnitCombatState initCombatState(const DroidProperties& properties) {
    UnitCombatState state;

    state.maxHealth = std::max(MIN_HEALTH, static_cast<float>(properties.energy) * HEALTH_PER_ENERGY);
    state.currentHealth = state.maxHealth;

    state.armour = std::max(0.0f, properties.armour);  // flat reduction; never negative
    state.alive = true;

    return state;
}

//------------------------------------------------------------------------------
// Damage model
//------------------------------------------------------------------------------

bool applyDamage(UnitCombatState& state, float rawDamage, bool ignoreArmour) {
    if (!state.alive || rawDamage <= 0.0f) return state.alive;

    // Armour is a flat damage reduction (uber/source/uberdroid/destructible.cpp:
    // `damage -= armour`). A hit that doesn't beat the armour is fully absorbed. `ignoreArmour`
    // bypasses it entirely (disruptor).
    float effectiveDamage = ignoreArmour ? rawDamage : (rawDamage - state.armour);
    if (effectiveDamage <= 0.0f) return state.alive;

    state.currentHealth = std::max(0.0f, state.currentHealth - effectiveDamage);
    if (state.currentHealth <= 0.0f) {
        state.alive = false;
    }

    return state.alive;
}

//------------------------------------------------------------------------------
// Realtime (continuous) damage accumulation
//------------------------------------------------------------------------------

void accumulateRealtimeDamage(UnitCombatState& state, float rawDamage) {
    if (rawDamage > 0.0f) state.pendingDamage += rawDamage;
}

void updateRealtimeDamage(UnitCombatState& state, float dt) {
    if (dt <= 0.0f) return;
    state.damageAccumTimer += dt;
    // Flush once per interval; a while-loop handles a dt that spans several intervals.
    while (state.damageAccumTimer >= REALTIME_DAMAGE_INTERVAL) {
        state.damageAccumTimer -= REALTIME_DAMAGE_INTERVAL;
        if (state.pendingDamage > 0.0f) {
            applyDamage(state, state.pendingDamage);  // armour applied once per flush
            state.pendingDamage = 0.0f;
        }
    }
}

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

bool isAlive(const UnitCombatState& state) {
    return state.alive;
}

void destroy(UnitCombatState& state) {
    state.currentHealth = 0.0f;
    state.alive = false;
}
