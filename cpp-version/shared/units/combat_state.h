#ifndef COMBAT_STATE_H
#define COMBAT_STATE_H

#include "unit_types.h"

//------------------------------------------------------------------------------
// Unit Combat State (runtime mutable state for damage and health)
//------------------------------------------------------------------------------

struct UnitCombatState {
    float currentHealth = 0.0f;
    float maxHealth = 0.0f;
    float armour = 0.0f;        // Damage reduction percentage (0-100)
    bool alive = true;
};

// Health scaling: energy property value * this factor = max health
inline constexpr float HEALTH_PER_ENERGY = 100.0f;
inline constexpr float MIN_HEALTH = 10.0f;

//------------------------------------------------------------------------------
// Free functions
//------------------------------------------------------------------------------

// Initialise combat state from droid properties.
// Maps: energy -> health (scaled), armour -> damage reduction percentage.
UnitCombatState initCombatState(const DroidProperties& properties);

// Apply raw damage to combat state. Armour reduces effective damage.
// Returns true if the unit is still alive after damage.
bool applyDamage(UnitCombatState& state, float rawDamage);

// Check if the unit is alive.
bool isAlive(const UnitCombatState& state);

// Immediately kill the unit (set health to 0, alive to false).
void destroy(UnitCombatState& state);

#endif // COMBAT_STATE_H
