#ifndef WEAPON_H
#define WEAPON_H

#include "unit_types.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Weapon type — determines how the weapon deals damage
//------------------------------------------------------------------------------

enum class WeaponType {
    Projectile,  // Standard moving projectile
    Beam,        // Continuous beam (e.g. Gas Axe, Exterminator)
    Instant,     // Instant hit (not currently used)
    Area         // Area effect (e.g. Disruptor)
};

//------------------------------------------------------------------------------
// Damage type — determines armour interaction
//------------------------------------------------------------------------------

enum class DamageType {
    Plasma,
    Flame,
    Cutter,
    Laser,
    Projectile,
    Disruptor,
    Impact
};

//------------------------------------------------------------------------------
// Weapon Definition (immutable stats loaded from weapons.json)
//------------------------------------------------------------------------------

struct WeaponDefinition {
    int id = -1;                    // Weapon ID (-1 = no weapon)
    std::string name;
    float damage = 0.0f;           // Damage per hit
    float speed = 0.0f;            // Projectile velocity (world units/second)
    float fireRate = 0.0f;         // Cooldown between shots (seconds)
    float maxRange = 0.0f;         // Max travel distance (world units)
    float optimumRange = 0.0f;     // AI preferred engagement range (world units)
    WeaponType type = WeaponType::Projectile;
    DamageType damageType = DamageType::Plasma;
    bool twin = false;             // Fires two projectiles
};

//------------------------------------------------------------------------------
// Weapon State (runtime mutable state per unit)
//------------------------------------------------------------------------------

struct WeaponState {
    WeaponDefinition definition;
    float cooldownRemaining = 0.0f;
};

//------------------------------------------------------------------------------
// Weapon table loading
//------------------------------------------------------------------------------

// Load weapon definitions from a JSON file.
// Returns true if at least one weapon was loaded.
bool loadWeaponsFromFile(const std::string& path);

// Load weapon definitions from a JSON string (for testing).
bool loadWeaponsFromJson(const std::string& jsonString);

// Number of loaded weapons.
int weaponCount();

//------------------------------------------------------------------------------
// Free functions
//------------------------------------------------------------------------------

// Look up weapon definition by ID. Returns a no-weapon definition for
// invalid IDs (including -1).
WeaponDefinition getWeaponDefinition(int weaponId);

// Initialise weapon state from droid properties.
// Reads the weapon field and looks up the weapon table.
// A weapon value of -1 means unarmed.
WeaponState initWeaponState(const DroidProperties& properties);

// Try to fire the weapon. Returns true if the weapon fired (cooldown was ready).
// Resets cooldown on successful fire.
bool tryFire(WeaponState& state);

// Advance cooldown timer by dt seconds.
void updateWeaponCooldown(WeaponState& state, float dt);

// Check if this weapon state has a usable weapon (id >= 0).
bool hasWeapon(const WeaponState& state);

#endif // WEAPON_H
