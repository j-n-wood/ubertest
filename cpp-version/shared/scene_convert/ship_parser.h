#ifndef SHIP_PARSER_H
#define SHIP_PARSER_H

#include "scene_types.h"
#include <string_view>
#include <filesystem>

//------------------------------------------------------------------------------
// Ship File Parser
//------------------------------------------------------------------------------

// Parse a ship file (ship1.txt, etc.)
// basePath is the directory containing the ship file (for resolving relative paths)
// Returns true on success, false on failure
[[nodiscard]] bool parseShipFile(
    std::string_view path,
    Ship& outShip
);

// Parse transporters from transport.txt
[[nodiscard]] bool parseTransportFile(
    std::string_view path,
    std::vector<Transporter>& outTransporters
);

// Parse deck layout from lifts.txt
[[nodiscard]] bool parseLiftsFile(
    std::string_view path,
    Decks& outDecks
);

// Resolve relative paths in ship file to absolute paths
// Converts Windows-style backslashes to forward slashes
std::filesystem::path resolveShipPath(
    const std::filesystem::path& basePath,
    const std::string& relativePath
);

#endif // SHIP_PARSER_H
