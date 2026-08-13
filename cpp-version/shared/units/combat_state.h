#ifndef COMBAT_STATE_H
#define COMBAT_STATE_H

#include "unit_types.h"

//------------------------------------------------------------------------------
// Unit Combat State (runtime mutable state for damage and health)
//------------------------------------------------------------------------------

struct UnitCombatState {
    float currentHealth = 0.0f;
    float maxHealth = 0.0f;
    float armour = 0.0f;        // Flat damage reduction (subtracted per applyDamage)
    bool alive = true;

    // Realtime (continuous) damage accumulator, e.g. standing in an explosion. Raw damage is
    // summed here and flushed through applyDamage once every REALTIME_DAMAGE_INTERVAL, so
    // armour is applied per tick (not per frame) and on-damage reactions fire at a bounded
    // rate. Single-hit damage (projectiles) still goes straight through applyDamage.
    float pendingDamage = 0.0f;
    float damageAccumTimer = 0.0f;
};

// How often accumulated realtime damage is flushed (seconds).
inline constexpr float REALTIME_DAMAGE_INTERVAL = 0.1f;

// Health scaling: the droid's `energy` stat IS its health (droidclasses.txt lists 20, 40,
// 100, ...), so the factor is 1. (It was 100 while energy was being mis-parsed as the small
// 0-9 tier column — see docs/weapons.md.)
inline constexpr float HEALTH_PER_ENERGY = 1.0f;
inline constexpr float MIN_HEALTH = 10.0f;

//------------------------------------------------------------------------------
// Free functions
//------------------------------------------------------------------------------

// Initialise combat state from droid properties.
// Maps: energy -> health (scaled), armour -> damage reduction percentage.
UnitCombatState initCombatState(const DroidProperties& properties);

// Apply raw damage to combat state. Armour reduces effective damage, unless `ignoreArmour` is set
// (e.g. the disruptor, whose damage type bypasses armour entirely — matches uber's mArmour=0).
// Returns true if the unit is still alive after damage.
bool applyDamage(UnitCombatState& state, float rawDamage, bool ignoreArmour = false);

// Accumulate continuous/realtime raw damage (applied later by updateRealtimeDamage).
void accumulateRealtimeDamage(UnitCombatState& state, float rawDamage);

// Advance the realtime-damage timer by dt; every REALTIME_DAMAGE_INTERVAL, flush the
// accumulated raw damage through applyDamage in one hit and reset the accumulator.
void updateRealtimeDamage(UnitCombatState& state, float dt);

// Check if the unit is alive.
bool isAlive(const UnitCombatState& state);

// Immediately kill the unit (set health to 0, alive to false).
void destroy(UnitCombatState& state);

#endif // COMBAT_STATE_H
