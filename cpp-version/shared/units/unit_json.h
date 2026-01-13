#ifndef UNIT_JSON_H
#define UNIT_JSON_H

#include "unit_types.h"
#include <string_view>

//------------------------------------------------------------------------------
// JSON Loading and Saving
//------------------------------------------------------------------------------

// Load a unit definition from a JSON file
// Returns true on success, false on failure
[[nodiscard]] bool loadUnitDefinitionFromFile(
    std::string_view path,
    UnitDefinition& outDefinition
);

// Save a unit definition to a JSON file
// Returns true on success, false on failure
[[nodiscard]] bool saveUnitDefinitionToFile(
    std::string_view path,
    const UnitDefinition& definition
);

// Parse a unit definition from a JSON string
[[nodiscard]] bool parseUnitDefinitionFromString(
    std::string_view jsonString,
    UnitDefinition& outDefinition
);

// Serialize a unit definition to a JSON string
[[nodiscard]] std::string serializeUnitDefinitionToString(
    const UnitDefinition& definition,
    bool pretty = true
);

// Scale all offset values in a section definition tree by the given factor
// Modifies the section in place, recursively including all children
void scaleDefinitionOffsets(SectionDefinition& section, float scale);

#endif // UNIT_JSON_H
