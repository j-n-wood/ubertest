#ifndef ARCHETILE_PARSER_H
#define ARCHETILE_PARSER_H

#include "scene_types.h"
#include <string_view>
#include <vector>
#include <unordered_map>

//------------------------------------------------------------------------------
// Archetile Parser (tiles.txt)
//------------------------------------------------------------------------------

// Parse tiles.txt containing archetile definitions
// Returns true on success, false on failure
[[nodiscard]] bool parseArchetilesFile(
    std::string_view path,
    std::vector<Archetile>& outArchetiles
);

// Convert an archetile reference to a concrete tile
// Applies the position offset to the template tile
Tile expandArchetile(
    const Archetile& archetype,
    float xOffset,
    float yOffset
);

//------------------------------------------------------------------------------
// Archetile Cache
//------------------------------------------------------------------------------

// Singleton cache for loaded archetiles
class ArchetileCache {
public:
    static ArchetileCache& instance();

    // Load archetiles from file if not already cached
    bool load(std::string_view path);

    // Get archetile by index (returns nullptr if not found)
    const Archetile* get(int index) const;

    // Check if loaded
    bool isLoaded() const { return loaded_; }

    // Clear cache
    void clear();

private:
    ArchetileCache() = default;
    std::vector<Archetile> archetiles_;
    std::unordered_map<int, size_t> indexMap_;  // index -> vector position
    bool loaded_ = false;
};

#endif // ARCHETILE_PARSER_H
