#ifndef SPAWN_CONFIG_H
#define SPAWN_CONFIG_H

#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Spawn data types
//------------------------------------------------------------------------------

// Number of droid type groups (types 1-9, indexed 0-8)
inline constexpr int SPAWN_TYPE_COUNT = 9;

// A droid to spawn: loaded from level data or generated from profile rules
struct SpawnEntry {
    int classId = -1;
    int waypointIndex = -1;   // -1 = assign randomly
    float angle = 0.0f;
};

// Spawn definition for a single level
struct LevelSpawnDef {
    std::string name;
    int profile[SPAWN_TYPE_COUNT] = {};   // Count per type (type 1 at index 0, ... type 9 at index 8)
    std::vector<SpawnEntry> placedDroids;
};

// Type-to-class mapping: which droid classes belong to each type group
struct TypeClassEntry {
    int type = 0;                    // Type number (1-9)
    std::vector<int> classIds;       // Droid class IDs in this type group
};

//------------------------------------------------------------------------------
// Loading
//------------------------------------------------------------------------------

// Load spawn configuration from a JSON file.
bool loadSpawnConfigFromFile(const std::string& path);

// Load spawn configuration from a JSON string (for testing).
bool loadSpawnConfigFromJson(const std::string& jsonString);

// Number of ships loaded.
int spawnShipCount();

//------------------------------------------------------------------------------
// Access
//------------------------------------------------------------------------------

// Get the spawn definition for a specific ship and level.
// Returns nullptr if indices are out of range.
const LevelSpawnDef* getSpawnDef(int shipIndex, int levelIndex);

// Get the level name for a given level index.
const std::string& getLevelName(int levelIndex);

//------------------------------------------------------------------------------
// Spawn resolution
//------------------------------------------------------------------------------

// Resolve a level's spawn definition into concrete spawn entries.
// Picks random classes within each type group based on the profile.
// Assigns distinct waypoints, avoiding playerWaypointIdx.
// waypointCount = total available waypoints (indices 0..waypointCount-1).
std::vector<SpawnEntry> resolveSpawns(
    const LevelSpawnDef& def,
    int waypointCount,
    int playerWaypointIdx);

#endif // SPAWN_CONFIG_H
