#include "spawn_config.h"
#include "units/unit_types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <random>

using json = nlohmann::json;

//------------------------------------------------------------------------------
// Internal types
//------------------------------------------------------------------------------

struct TypeClassEntry {
    std::vector<int> classIds;
};

struct ShipSpawnData {
    std::string name;
    std::vector<LevelSpawnDef> levels;   // Indexed by level number
};

//------------------------------------------------------------------------------
// Module-level state
//------------------------------------------------------------------------------

static std::vector<ShipSpawnData> s_ships;
static std::vector<TypeClassEntry> s_typeClassMap;

// Thread-local RNG for spawn randomness
static std::mt19937& getRng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

//------------------------------------------------------------------------------
// Loading
//------------------------------------------------------------------------------

void clearSpawnConfig() {
    s_ships.clear();
    s_typeClassMap.clear();
}

void buildTypeClassMap(const DroidProperties* allDroids, int count) {
    s_typeClassMap.clear();
    s_typeClassMap.resize(SPAWN_TYPE_COUNT);

    for (int i = 0; i < count; ++i) {
        // classId 0 is the player's influence device (type 0) — never spawned as AI.
        // See docs/transfer.md.
        if (allDroids[i].classId == 0) continue;
        int type = allDroids[i].typeCode / 100;  // e.g., 302 -> 3
        int typeIndex = type - 1;                 // type 1 at index 0, ... type 9 at index 8
        if (typeIndex >= 0 && typeIndex < SPAWN_TYPE_COUNT) {
            s_typeClassMap[typeIndex].classIds.push_back(allDroids[i].classId);
        }
    }
}

static bool parseShipJson(const json& j, ShipSpawnData& ship) {
    if (!j.is_object()) return false;

    ship.name = j.value("name", "");

    if (!j.contains("levels") || !j["levels"].is_array()) return false;

    for (const auto& levelJson : j["levels"]) {
        LevelSpawnDef def;
        int levelIdx = levelJson.value("level", -1);

        // Parse profile array
        if (levelJson.contains("profile") && levelJson["profile"].is_array()) {
            const auto& profile = levelJson["profile"];
            for (int i = 0; i < SPAWN_TYPE_COUNT && i < static_cast<int>(profile.size()); ++i) {
                def.profile[i] = profile[i].get<int>();
            }
        }

        // Parse placed droids
        if (levelJson.contains("placedDroids") && levelJson["placedDroids"].is_array()) {
            for (const auto& pd : levelJson["placedDroids"]) {
                SpawnEntry placed;
                placed.classId = pd.value("classId", -1);
                placed.waypointIndex = pd.value("waypointIndex", -1);
                placed.angle = pd.value("angle", 0.0f);
                def.placedDroids.push_back(placed);
            }
        }

        // Ensure levels vector is large enough
        if (levelIdx >= 0) {
            if (levelIdx >= static_cast<int>(ship.levels.size())) {
                ship.levels.resize(levelIdx + 1);
            }
            ship.levels[levelIdx] = std::move(def);
        }
    }

    return true;
}

bool loadShipSpawns(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded()) return false;

    ShipSpawnData ship;
    if (!parseShipJson(j, ship)) return false;

    s_ships.push_back(std::move(ship));
    return true;
}

bool loadShipSpawnsFromJson(const std::string& jsonString) {
    json j = json::parse(jsonString, nullptr, false);
    if (j.is_discarded()) return false;

    ShipSpawnData ship;
    if (!parseShipJson(j, ship)) return false;

    s_ships.push_back(std::move(ship));
    return true;
}

int spawnShipCount() {
    return static_cast<int>(s_ships.size());
}

//------------------------------------------------------------------------------
// Access
//------------------------------------------------------------------------------

const LevelSpawnDef* getSpawnDef(int shipIndex, int levelIndex) {
    if (shipIndex < 0 || shipIndex >= static_cast<int>(s_ships.size())) return nullptr;
    const auto& ship = s_ships[shipIndex];
    if (levelIndex < 0 || levelIndex >= static_cast<int>(ship.levels.size())) return nullptr;
    return &ship.levels[levelIndex];
}

//------------------------------------------------------------------------------
// Spawn resolution
//------------------------------------------------------------------------------

// Pick a random class ID from the given type index (0-8).
// Falls back to the type index itself if no mapping is loaded.
static int pickRandomClass(int typeIndex) {
    if (typeIndex < 0 || typeIndex >= static_cast<int>(s_typeClassMap.size())) {
        return typeIndex;
    }

    const auto& entry = s_typeClassMap[typeIndex];
    if (entry.classIds.empty()) return typeIndex;

    auto& rng = getRng();
    std::uniform_int_distribution<int> dist(0, static_cast<int>(entry.classIds.size()) - 1);
    return entry.classIds[dist(rng)];
}

std::vector<SpawnEntry> resolveSpawns(
    const LevelSpawnDef& def,
    int waypointCount,
    int playerWaypointIdx)
{
    std::vector<SpawnEntry> results;

    // Build list of available waypoint indices (excluding player's)
    std::vector<int> availableWaypoints;
    availableWaypoints.reserve(waypointCount);
    for (int i = 0; i < waypointCount; ++i) {
        if (i != playerWaypointIdx) {
            availableWaypoints.push_back(i);
        }
    }

    // Shuffle waypoints for random assignment
    auto& rng = getRng();
    std::shuffle(availableWaypoints.begin(), availableWaypoints.end(), rng);

    int nextWaypoint = 0;  // Index into availableWaypoints

    // Reserve waypoints needed by placed droids with specific positions
    // (so profile-spawned droids don't take their spots)
    std::vector<int> reservedWaypoints;
    for (const auto& pd : def.placedDroids) {
        if (pd.waypointIndex >= 0 && pd.waypointIndex < waypointCount &&
            pd.waypointIndex != playerWaypointIdx) {
            reservedWaypoints.push_back(pd.waypointIndex);
        }
    }

    // Remove reserved waypoints from available pool
    for (int reserved : reservedWaypoints) {
        auto it = std::find(availableWaypoints.begin(), availableWaypoints.end(), reserved);
        if (it != availableWaypoints.end()) {
            availableWaypoints.erase(it);
        }
    }

    // Resolve profile-based spawns (random class within each type group)
    for (int typeIdx = 0; typeIdx < SPAWN_TYPE_COUNT; ++typeIdx) {
        int count = def.profile[typeIdx];
        for (int i = 0; i < count; ++i) {
            SpawnEntry spawn;
            spawn.classId = pickRandomClass(typeIdx);
            spawn.angle = 0.0f;

            if (nextWaypoint < static_cast<int>(availableWaypoints.size())) {
                spawn.waypointIndex = availableWaypoints[nextWaypoint++];
            } else {
                // Insufficient waypoints — reuse from the pool (wrap around)
                if (!availableWaypoints.empty()) {
                    spawn.waypointIndex = availableWaypoints[nextWaypoint % availableWaypoints.size()];
                    ++nextWaypoint;
                } else {
                    spawn.waypointIndex = 0;  // Fallback
                }
            }

            results.push_back(spawn);
        }
    }

    // Add placed droids
    for (const auto& pd : def.placedDroids) {
        if (pd.classId == 0) continue;  // classId 0 is the player device — never an AI enemy
        SpawnEntry spawn;
        spawn.classId = pd.classId;
        spawn.angle = pd.angle;

        if (pd.waypointIndex >= 0 && pd.waypointIndex < waypointCount) {
            spawn.waypointIndex = pd.waypointIndex;
        } else if (nextWaypoint < static_cast<int>(availableWaypoints.size())) {
            spawn.waypointIndex = availableWaypoints[nextWaypoint++];
        } else if (!availableWaypoints.empty()) {
            spawn.waypointIndex = availableWaypoints[nextWaypoint % availableWaypoints.size()];
            ++nextWaypoint;
        } else {
            spawn.waypointIndex = 0;
        }

        results.push_back(spawn);
    }

    return results;
}
