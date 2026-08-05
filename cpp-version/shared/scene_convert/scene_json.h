#ifndef SCENE_JSON_H
#define SCENE_JSON_H

#include "scene_types.h"
#include <string_view>
#include <string>

//------------------------------------------------------------------------------
// JSON Serialization
//------------------------------------------------------------------------------

// Serialize ship to JSON string
[[nodiscard]] std::string shipToJson(const Ship& ship, bool pretty = true);

// Serialize domain to JSON string
[[nodiscard]] std::string domainToJson(const Domain& domain, bool pretty = true);

// Parse ship from JSON string
[[nodiscard]] bool jsonToShip(std::string_view json, Ship& outShip);

// Parse domain from JSON string
[[nodiscard]] bool jsonToDomain(std::string_view json, Domain& outDomain);

//------------------------------------------------------------------------------
// File I/O
//------------------------------------------------------------------------------

// Save ship to JSON file
[[nodiscard]] bool saveShipToFile(std::string_view path, const Ship& ship, bool pretty = true);

// Save domain to JSON file. `transformToRender` applies the game->render coordinate transform to
// positions (default true, for the convert path whose input is game space). Pass false when the
// domain is ALREADY in render space (e.g. re-saving a domain that was loaded from JSON) — otherwise
// the transform is applied twice and the reloaded file is corrupt.
[[nodiscard]] bool saveDomainToFile(std::string_view path, const Domain& domain, bool pretty = true,
                                    bool transformToRender = true);

// Load ship from JSON file
[[nodiscard]] bool loadShipFromFile(std::string_view path, Ship& outShip);

// Load domain from JSON file
[[nodiscard]] bool loadDomainFromFile(std::string_view path, Domain& outDomain);

#endif // SCENE_JSON_H
