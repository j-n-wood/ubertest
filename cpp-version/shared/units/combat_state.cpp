#include "combat_state.h"
#include <algorithm>

//------------------------------------------------------------------------------
// Combat state initialisation
//------------------------------------------------------------------------------

UnitCombatState initCombatState(const DroidProperties& properties) {
    UnitCombatState state;

    state.maxHealth = std::max(MIN_HEALTH, static_cast<float>(properties.energy) * HEALTH_PER_ENERGY);
    state.currentHealth = state.maxHealth;

    state.armour = std::clamp(properties.armour, 0.0f, 100.0f);
    state.alive = true;

    return state;
}

//------------------------------------------------------------------------------
// Damage model
//------------------------------------------------------------------------------

bool applyDamage(UnitCombatState& state, float rawDamage) {
    if (!state.alive || rawDamage <= 0.0f) return state.alive;

    float reduction = state.armour / 100.0f;
    float effectiveDamage = rawDamage * (1.0f - reduction);

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
