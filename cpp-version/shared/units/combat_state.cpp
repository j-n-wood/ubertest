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

bool applyDamage(UnitCombatState& state, float rawDamage) {
    if (!state.alive || rawDamage <= 0.0f) return state.alive;

    // Armour is a flat damage reduction (uber/source/uberdroid/destructible.cpp:
    // `damage -= armour`). A hit that doesn't beat the armour is fully absorbed.
    float effectiveDamage = rawDamage - state.armour;
    if (effectiveDamage <= 0.0f) return state.alive;

    state.currentHealth = std::max(0.0f, state.currentHealth - effectiveDamage);
    if (state.currentHealth <= 0.0f) {
        state.alive = false;
    }

    return state.alive;
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
