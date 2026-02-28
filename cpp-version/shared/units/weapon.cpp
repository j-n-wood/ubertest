#include "weapon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

//------------------------------------------------------------------------------
// Module-level weapon table (populated by loadWeapons*)
//------------------------------------------------------------------------------

static std::vector<WeaponDefinition> s_weapons;
static WeaponDefinition s_noWeapon; // returned for invalid lookups (id=-1)

//------------------------------------------------------------------------------
// JSON parsing helpers
//------------------------------------------------------------------------------

static WeaponType parseWeaponType(const std::string& s) {
    if (s == "beam")    return WeaponType::Beam;
    if (s == "instant") return WeaponType::Instant;
    if (s == "area")    return WeaponType::Area;
    return WeaponType::Projectile;
}

static DamageType parseDamageType(const std::string& s) {
    if (s == "flame")      return DamageType::Flame;
    if (s == "cutter")     return DamageType::Cutter;
    if (s == "laser")      return DamageType::Laser;
    if (s == "projectile") return DamageType::Projectile;
    if (s == "disruptor")  return DamageType::Disruptor;
    if (s == "impact")     return DamageType::Impact;
    return DamageType::Plasma;
}

static WeaponDefinition parseWeaponFromJson(const json& j) {
    WeaponDefinition w;
    w.id           = j.value("id", -1);
    w.name         = j.value("name", "");
    w.damage       = j.value("damage", 0.0f);
    w.speed        = j.value("speed", 0.0f);
    w.fireRate     = j.value("fireRate", 0.0f);
    w.maxRange     = j.value("maxRange", 0.0f);
    w.optimumRange = j.value("optimumRange", 0.0f);
    w.type         = parseWeaponType(j.value("type", "projectile"));
    w.damageType   = parseDamageType(j.value("damageType", "plasma"));
    w.twin         = j.value("twin", false);
    return w;
}

static bool loadFromParsedJson(const json& j) {
    s_weapons.clear();
    if (!j.is_array()) return false;

    for (const auto& entry : j) {
        s_weapons.push_back(parseWeaponFromJson(entry));
    }

    // Sort by ID for indexed lookup
    std::sort(s_weapons.begin(), s_weapons.end(),
              [](const WeaponDefinition& a, const WeaponDefinition& b) {
                  return a.id < b.id;
              });

    return !s_weapons.empty();
}

//------------------------------------------------------------------------------
// Weapon table loading
//------------------------------------------------------------------------------

bool loadWeaponsFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded()) return false;

    return loadFromParsedJson(j);
}

bool loadWeaponsFromJson(const std::string& jsonString) {
    json j = json::parse(jsonString, nullptr, false);
    if (j.is_discarded()) return false;

    return loadFromParsedJson(j);
}

int weaponCount() {
    return static_cast<int>(s_weapons.size());
}

//------------------------------------------------------------------------------
// Weapon definition lookup
//------------------------------------------------------------------------------

WeaponDefinition getWeaponDefinition(int weaponId) {
    if (weaponId < 0) return s_noWeapon;

    for (const auto& w : s_weapons) {
        if (w.id == weaponId) return w;
    }

    return s_noWeapon; // Unknown weapon ID
}

//------------------------------------------------------------------------------
// Weapon state initialisation
//------------------------------------------------------------------------------

WeaponState initWeaponState(const DroidProperties& properties) {
    WeaponState state;
    state.definition = getWeaponDefinition(properties.weapon);
    state.cooldownRemaining = 0.0f;
    return state;
}

//------------------------------------------------------------------------------
// Weapon firing
//------------------------------------------------------------------------------

bool tryFire(WeaponState& state) {
    if (state.definition.id < 0) return false;         // No weapon
    if (state.cooldownRemaining > 0.0f) return false;  // On cooldown

    state.cooldownRemaining = state.definition.fireRate;
    return true;
}

void updateWeaponCooldown(WeaponState& state, float dt) {
    if (state.cooldownRemaining > 0.0f) {
        state.cooldownRemaining = std::max(0.0f, state.cooldownRemaining - dt);
    }
}

bool hasWeapon(const WeaponState& state) {
    return state.definition.id >= 0;
}
