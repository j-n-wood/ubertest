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

// Default per-hit impact spark count when a weapon doesn't specify one. Larger/heavier
// weapons override with a higher `impactSparks` in weapons.json.
inline constexpr int DEFAULT_IMPACT_SPARKS = 16;
// Default impact spark colour (plasma green-white) — matches the pre-per-weapon look.
inline constexpr Color DEFAULT_SPARK_COLOR = {205, 255, 190, 255};
// Default projectile sprite tint (white = the texture's own colour, unchanged).
inline constexpr Color DEFAULT_SPRITE_COLOR = {255, 255, 255, 255};
// Travel-spark defaults (used when a weapon sheds sparks but doesn't override these).
inline constexpr float DEFAULT_TRAVEL_SPARK_LIFE = 0.5f;   // seconds a shed spark lives
inline constexpr float DEFAULT_TRAVEL_SPARK_SIZE = 0.14f;  // shed-spark start diameter (world units)

struct WeaponDefinition {
    int id = -1;                    // Weapon ID (-1 = no weapon)
    std::string name;
    float damage = 0.0f;           // Damage per hit
    float speed = 0.0f;            // Projectile velocity (world units/second)
    float fireRate = 0.0f;         // Cooldown between shots (seconds)
    float maxRange = 0.0f;         // AI-only firing gate: the AI won't fire beyond this (world units).
                                   // Also the beam's hitscan reach. Does NOT bound projectile travel.
    float optimumRange = 0.0f;     // AI preferred engagement range (world units)
    float lifetime = 0.0f;         // Projectile lifetime override (seconds); 0 = derive from maxRange/speed
                                   // (back-compat). This — not maxRange — controls how far a bolt flies
                                   // and drives its end-of-life alpha fade. See weaponProjectileLifetime.
    float radius = 0.1f;           // Projectile physics (collision) radius (world units)
    float windup = 0.0f;           // Area weapons (disruptor): delay (seconds) between the fire
                                   // command and the area damage landing. 0 = instant. See docs/weapons.md.
    WeaponType type = WeaponType::Projectile;
    DamageType damageType = DamageType::Plasma;
    bool twin = false;             // Fires two projectiles
    int impactSparks = DEFAULT_IMPACT_SPARKS;   // per-hit impact spark count (projectiles)
    Color sparkColor = DEFAULT_SPARK_COLOR;     // impact spark colour (beam + projectile)
    Color spriteColor = DEFAULT_SPRITE_COLOR;   // projectile sprite diffuse tint (white = unchanged)
    // Travel sparks: a plasma bolt sheds `travelSparkRate` sparks/second as it flies (0 = none),
    // radiating in random directions at a small speed and coloured with spriteColor. Their lifetime
    // and start size are tunable per weapon. See docs/weapons.md.
    float travelSparkRate = 0.0f;
    float travelSparkLife = DEFAULT_TRAVEL_SPARK_LIFE;   // seconds each shed spark lives
    float travelSparkSize = DEFAULT_TRAVEL_SPARK_SIZE;   // shed-spark start diameter (world units)
    // Random position jitter (world-unit radius) applied to each shed spark's spawn point. Breaks up
    // the visible banding a fast bolt gets from one spawn per fixed sim tick. 0 = spawn exactly on
    // the bolt.
    float travelSparkJitter = 0.0f;
};

// Resolved projectile lifetime (seconds): the weapon's explicit `lifetime` if set, else derived
// from maxRange/speed (back-compat). This is what the spawn passes to the projectile — maxRange is
// otherwise AI-only. Used by both player and AI fire so the rule lives in one place.
inline float weaponProjectileLifetime(const WeaponDefinition& w) {
    if (w.lifetime > 0.0f) return w.lifetime;
    return (w.speed > 0.0f) ? (w.maxRange / w.speed) : 1.0f;
}

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

// Access a mutable weapon definition by table index [0, weaponCount()). Returns nullptr
// if out of range. Used by the runtime weapon editor to tune fields in place; edits are
// live for future lookups (getWeaponDefinition) but callers that cached a definition copy
// must re-fetch to see them.
WeaponDefinition* getWeaponByIndex(int index);

// Serialise the current in-memory weapon table back to a JSON file (pretty-printed,
// matching the shipped weapons.json field order). Returns true on success.
bool saveWeaponsToFile(const std::string& path);

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
