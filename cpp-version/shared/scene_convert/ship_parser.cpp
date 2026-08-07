#include "ship_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

static std::string getCurrentDateTimeISO() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string normalizePathSeparators(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

//------------------------------------------------------------------------------
// Path Resolution
//------------------------------------------------------------------------------

fs::path resolveShipPath(const fs::path& basePath, const std::string& relativePath) {
    std::string normalized = normalizePathSeparators(relativePath);
    return basePath / normalized;
}

//------------------------------------------------------------------------------
// Ship Parser
//------------------------------------------------------------------------------

bool parseShipFile(std::string_view path, Ship& outShip) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open ship file: " << path << std::endl;
        return false;
    }

    fs::path filePath(path);
    fs::path basePath = filePath.parent_path();

    outShip = Ship{};
    outShip.metadata.sourceFile = filePath.filename().string();
    outShip.metadata.conversionDate = getCurrentDateTimeISO();

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "Name") {
            std::getline(iss, outShip.name);
            outShip.name = trim(outShip.name);
        }
        else if (keyword == "Crew") {
            iss >> outShip.crew;
        }
        else if (keyword == "Capacity") {
            iss >> outShip.capacity;
        }
        else if (keyword == "Desc") {
            int index;
            iss >> index;
            std::string desc;
            std::getline(iss, desc);
            desc = trim(desc);

            // Ensure vector is large enough
            if (index >= static_cast<int>(outShip.description.size())) {
                outShip.description.resize(index + 1);
            }
            outShip.description[index] = desc;
        }
        else if (keyword == "Domain") {
            std::string domainPath;
            iss >> domainPath;
            domainPath = normalizePathSeparators(domainPath);
            outShip.domainPaths.push_back(domainPath);
        }
        else if (keyword == "Transporters") {
            std::string transportPath;
            iss >> transportPath;
            // Paths are relative to uberdroid root (parent of data directory)
            fs::path uberdroidRoot = basePath.parent_path();
            fs::path fullPath = resolveShipPath(uberdroidRoot, transportPath);
            if (!parseTransportFile(fullPath.string(), outShip.transporters)) {
                std::cerr << "Warning: Failed to parse transporters file: " << fullPath << std::endl;
            }
        }
        else if (keyword == "Decks") {
            std::string decksPath;
            iss >> decksPath;
            // Paths are relative to uberdroid root (parent of data directory)
            fs::path uberdroidRoot = basePath.parent_path();
            fs::path fullPath = resolveShipPath(uberdroidRoot, decksPath);
            if (!parseLiftsFile(fullPath.string(), outShip.decks)) {
                std::cerr << "Warning: Failed to parse decks file: " << fullPath << std::endl;
            }
        }
        else if (keyword == "End") {
            break;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Transport Parser
//------------------------------------------------------------------------------

bool parseTransportFile(std::string_view path, std::vector<Transporter>& outTransporters) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open transport file: " << path << std::endl;
        return false;
    }

    outTransporters.clear();

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Format: Label <id> Deck <domain> PosX <x> PosY <y> LevelUp <up> LevelDown <down> LiftRow <row>
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword != "Label") continue;

        Transporter t;
        int gridX, gridY;

        iss >> t.id;

        std::string token;
        while (iss >> token) {
            if (token == "Deck") {
                iss >> t.domainIndex;
            }
            else if (token == "PosX") {
                iss >> gridX;
            }
            else if (token == "PosY") {
                iss >> gridY;
            }
            else if (token == "LevelUp") {
                iss >> t.levelUp;
            }
            else if (token == "LevelDown") {
                iss >> t.levelDown;
            }
            else if (token == "LiftRow") {
                iss >> t.liftRow;
            }
        }

        // Convert grid position to world coordinates (tile centre). The Y half-tile offset is
        // SUBTRACTED, not added: game Y is negated by gameToRenderCoords (Y-axis inversion), so a
        // +32 centre offset would land the stop one tile off in render Z. X is not inverted (+32).
        t.position.x = static_cast<float>(gridX * 64 + 32);
        t.position.y = static_cast<float>(gridY * 64 - 32);
        t.position.z = 20.0f;

        outTransporters.push_back(t);
    }

    return true;
}

//------------------------------------------------------------------------------
// Lifts/Decks Parser
//------------------------------------------------------------------------------

bool parseLiftsFile(std::string_view path, Decks& outDecks) {
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        std::cerr << "Failed to open lifts file: " << path << std::endl;
        return false;
    }

    outDecks = Decks{};

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "Elevator") {
            Elevator e;
            iss >> e.id;

            std::string token;
            while (iss >> token) {
                if (token == "ElRowX") {
                    iss >> e.rect.x;
                }
                else if (token == "ElRowY") {
                    iss >> e.rect.y;
                }
                else if (token == "ElRowW") {
                    iss >> e.rect.w;
                }
                else if (token == "ElRowH") {
                    iss >> e.rect.h;
                }
            }
            outDecks.elevators.push_back(e);
        }
        else if (keyword == "Domain") {
            DomainRect dr;
            iss >> dr.domainIndex;

            std::string token;
            while (iss >> token) {
                // Handle both "RectNumber 0" and "RectNumber=0" formats
                if (token == "RectNumber") {
                    iss >> dr.rectNumber;
                }
                else if (token.find("RectNumber") == 0) {
                    // Handle "RectNumber=0" format
                    size_t eqPos = token.find('=');
                    if (eqPos != std::string::npos) {
                        dr.rectNumber = std::stoi(token.substr(eqPos + 1));
                    }
                }
                else if (token == "DeckX" || token.find("DeckX") == 0) {
                    // Handle both "DeckX 0" and "DeckX=0" formats
                    size_t eqPos = token.find('=');
                    if (eqPos != std::string::npos) {
                        dr.rect.x = std::stoi(token.substr(eqPos + 1));
                    } else {
                        iss >> dr.rect.x;
                    }
                }
                else if (token == "DeckY") {
                    iss >> dr.rect.y;
                }
                else if (token == "DeckW") {
                    iss >> dr.rect.w;
                }
                else if (token == "DeckH") {
                    iss >> dr.rect.h;
                }
            }
            outDecks.domainRects.push_back(dr);
        }
    }

    return true;
}
