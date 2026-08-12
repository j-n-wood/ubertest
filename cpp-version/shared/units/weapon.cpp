#include "weapon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>

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

// Reverse of parseWeaponType/parseDamageType — used when serialising back to JSON.
static const char* weaponTypeToString(WeaponType t) {
    switch (t) {
        case WeaponType::Beam:    return "beam";
        case WeaponType::Instant: return "instant";
        case WeaponType::Area:    return "area";
        case WeaponType::Projectile:
        default:                  return "projectile";
    }
}

static const char* damageTypeToString(DamageType t) {
    switch (t) {
        case DamageType::Flame:      return "flame";
        case DamageType::Cutter:     return "cutter";
        case DamageType::Laser:      return "laser";
        case DamageType::Projectile: return "projectile";
        case DamageType::Disruptor:  return "disruptor";
        case DamageType::Impact:     return "impact";
        case DamageType::Plasma:
        default:                     return "plasma";
    }
}

// Parse a "[r, g, b]" or "[r, g, b, a]" array under `key` into a Color; returns `dflt` if absent.
static Color parseColorField(const json& j, const char* key, Color dflt) {
    if (!j.contains(key) || !j[key].is_array()) return dflt;
    const auto& c = j[key];
    auto ch = [&](size_t i, unsigned char d) -> unsigned char {
        return i < c.size() ? static_cast<unsigned char>(c[i].get<int>()) : d;
    };
    return {ch(0, dflt.r), ch(1, dflt.g), ch(2, dflt.b), ch(3, 255)};
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
    w.lifetime     = j.value("lifetime", 0.0f);   // 0 = derive from maxRange/speed
    w.radius       = j.value("radius", 0.1f);
    w.type         = parseWeaponType(j.value("type", "projectile"));
    w.damageType   = parseDamageType(j.value("damageType", "plasma"));
    w.twin         = j.value("twin", false);
    w.impactSparks = j.value("impactSparks", DEFAULT_IMPACT_SPARKS);
    w.sparkColor   = parseColorField(j, "sparkColor", DEFAULT_SPARK_COLOR);
    w.spriteColor  = parseColorField(j, "spriteColor", DEFAULT_SPRITE_COLOR);
    w.travelSparkRate = j.value("travelSparkRate", 0.0f);
    w.travelSparkLife = j.value("travelSparkLife", DEFAULT_TRAVEL_SPARK_LIFE);
    w.travelSparkSize = j.value("travelSparkSize", DEFAULT_TRAVEL_SPARK_SIZE);
    w.travelSparkJitter = j.value("travelSparkJitter", 0.0f);
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

WeaponDefinition* getWeaponByIndex(int index) {
    if (index < 0 || index >= static_cast<int>(s_weapons.size())) return nullptr;
    return &s_weapons[index];
}

bool saveWeaponsToFile(const std::string& path) {
    // Round to 3 decimals and store as double so nlohmann emits the clean shortest form
    // (e.g. 0.8, 33.0) rather than the float→double artefact (0.800000011920929).
    auto r3 = [](float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; };

    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
    for (const auto& w : s_weapons) {
        nlohmann::ordered_json o;
        o["id"]           = w.id;
        o["name"]         = w.name;
        o["damage"]       = r3(w.damage);
        o["speed"]        = r3(w.speed);
        o["fireRate"]     = r3(w.fireRate);
        o["maxRange"]     = r3(w.maxRange);
        o["optimumRange"] = r3(w.optimumRange);
        if (w.lifetime > 0.0f) o["lifetime"] = r3(w.lifetime);   // omitted when derived from range
        // radius defaults to 0.1 and is omitted unless overridden (matches the shipped file).
        if (std::fabs(w.radius - 0.1f) > 1e-6f) o["radius"] = r3(w.radius);
        o["type"]         = weaponTypeToString(w.type);
        o["damageType"]   = damageTypeToString(w.damageType);
        o["twin"]         = w.twin;
        // Impact-spark overrides — omitted at their defaults to keep the file lean.
        if (w.impactSparks != DEFAULT_IMPACT_SPARKS) o["impactSparks"] = w.impactSparks;
        auto writeColor = [&](const char* key, const Color& c, const Color& dflt) {
            if (c.r == dflt.r && c.g == dflt.g && c.b == dflt.b && c.a == dflt.a) return;  // omit at default
            if (c.a == 255) o[key] = {c.r, c.g, c.b};
            else            o[key] = {c.r, c.g, c.b, c.a};
        };
        writeColor("sparkColor", w.sparkColor, DEFAULT_SPARK_COLOR);
        writeColor("spriteColor", w.spriteColor, DEFAULT_SPRITE_COLOR);
        if (w.travelSparkRate > 0.0f) o["travelSparkRate"] = r3(w.travelSparkRate);
        if (std::fabs(w.travelSparkLife - DEFAULT_TRAVEL_SPARK_LIFE) > 1e-6f) o["travelSparkLife"] = r3(w.travelSparkLife);
        if (std::fabs(w.travelSparkSize - DEFAULT_TRAVEL_SPARK_SIZE) > 1e-6f) o["travelSparkSize"] = r3(w.travelSparkSize);
        if (w.travelSparkJitter > 0.0f) o["travelSparkJitter"] = r3(w.travelSparkJitter);
        arr.push_back(std::move(o));
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << arr.dump(2) << "\n";
    return file.good();
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
