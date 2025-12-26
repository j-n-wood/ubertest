#ifndef DOMAIN_PARSER_H
#define DOMAIN_PARSER_H

#include "scene_types.h"
#include <string_view>
#include <filesystem>

//------------------------------------------------------------------------------
// Domain/Level Parser (xmapfile{n}.txt)
//------------------------------------------------------------------------------

// Parse a domain file (xmapfile0.txt, etc.)
// basePath is used for resolving geometry XML paths
// tilesPath is the path to tiles.txt for archetile expansion
// Returns true on success, false on failure
[[nodiscard]] bool parseDomainFile(
    std::string_view path,
    Domain& outDomain,
    const std::filesystem::path& basePath = "",
    const std::filesystem::path& tilesPath = ""
);

// Load tiles.txt into the archetile cache if not already loaded
// Call this before parsing domains that use archetiles
bool ensureArchetilesLoaded(const std::filesystem::path& tilesPath);

#endif // DOMAIN_PARSER_H
