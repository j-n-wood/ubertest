#include "paradroid_parser.h"

#include <cstdio>
#include <cstring>
#include <sstream>

// Trim whitespace from both ends of a string
static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Check if a line starts with a given prefix
static bool startsWith(const std::string& line, const char* prefix) {
    return line.compare(0, strlen(prefix), prefix) == 0;
}

// Extract value after '=' or ':' from a line
static std::string extractValue(const std::string& line, char delim) {
    size_t pos = line.find(delim);
    if (pos == std::string::npos) return "";
    return trim(line.substr(pos + 1));
}

// Parse a row of space-separated integers
static std::vector<int> parseIntRow(const std::string& line) {
    std::vector<int> result;
    std::istringstream iss(line);
    int val;
    while (iss >> val) {
        result.push_back(val);
    }
    return result;
}

ParseResult parseParadroidMaps(const std::string& filePath) {
    ParseResult result;

    FILE* file = fopen(filePath.c_str(), "r");
    if (!file) {
        result.errorMsg = "Failed to open file: " + filePath;
        return result;
    }

    char lineBuffer[4096];
    int lineNum = 0;
    ParadroidLevel currentLevel;
    bool inLevel = false;
    bool inMap = false;
    bool inWaypoints = false;
    int mapRowsRead = 0;

    while (fgets(lineBuffer, sizeof(lineBuffer), file)) {
        lineNum++;
        std::string line = trim(lineBuffer);

        // Skip empty lines
        if (line.empty()) continue;

        // Skip separator lines
        if (line.find("------") != std::string::npos) continue;

        // Skip comment header lines
        if (startsWith(line, "This file was generated") ||
            startsWith(line, "Please feel free") ||
            startsWith(line, "to have an easier")) {
            continue;
        }

        // Parse area name
        if (startsWith(line, "Area name=")) {
            std::string val = extractValue(line, '=');
            // Remove quotes if present
            if (val.front() == '"') val = val.substr(1);
            if (val.back() == '"') val.pop_back();
            result.mapFile.areaName = val;
            continue;
        }

        // Parse level number (starts a new level)
        if (startsWith(line, "Levelnumber:")) {
            // Save previous level if we were in one
            if (inLevel) {
                result.mapFile.levels.push_back(currentLevel);
            }

            currentLevel = ParadroidLevel();
            inLevel = true;
            inMap = false;
            inWaypoints = false;
            mapRowsRead = 0;

            currentLevel.levelNumber = std::stoi(extractValue(line, ':'));
            continue;
        }

        if (!inLevel) continue;

        // Parse level metadata
        if (startsWith(line, "xlen of this level:")) {
            currentLevel.xlen = std::stoi(extractValue(line, ':'));
            continue;
        }

        if (startsWith(line, "ylen of this level:")) {
            currentLevel.ylen = std::stoi(extractValue(line, ':'));
            continue;
        }

        if (startsWith(line, "color of this level:")) {
            currentLevel.color = std::stoi(extractValue(line, ':'));
            continue;
        }

        if (startsWith(line, "Name of this level=")) {
            currentLevel.name = extractValue(line, '=');
            continue;
        }

        if (startsWith(line, "Comment of the Influencer")) {
            std::string val = extractValue(line, '=');
            // Remove leading quote if present
            if (!val.empty() && val.front() == '"') val = val.substr(1);
            currentLevel.comment = val;
            continue;
        }

        if (startsWith(line, "Name of background song")) {
            currentLevel.song = extractValue(line, '=');
            continue;
        }

        // Begin map section
        if (line == "begin_map") {
            inMap = true;
            inWaypoints = false;
            mapRowsRead = 0;
            continue;
        }

        // Begin waypoints section
        if (line == "begin_waypoints") {
            inMap = false;
            inWaypoints = true;
            continue;
        }

        // End level
        if (line == "end_level") {
            inMap = false;
            inWaypoints = false;
            continue;
        }

        // Parse map data
        if (inMap && mapRowsRead < currentLevel.ylen) {
            std::vector<int> row = parseIntRow(line);
            if (!row.empty()) {
                currentLevel.tiles.push_back(row);
                mapRowsRead++;
            }
            continue;
        }

        // Parse waypoints
        if (inWaypoints && startsWith(line, "Nr.=")) {
            Waypoint wp;

            // Parse "Nr.=  0 x=  11 y=   1	 connections:  5  1  2  3"
            int nr, x, y;
            char* connStart = nullptr;

            // Find connections part
            const char* connStr = strstr(line.c_str(), "connections:");
            if (connStr) {
                // Parse the numeric fields before connections
                sscanf(line.c_str(), "Nr.= %d x= %d y= %d", &nr, &x, &y);
                wp.number = nr;
                wp.x = x;
                wp.y = y;

                // Parse connections
                const char* connValues = connStr + strlen("connections:");
                std::istringstream iss(connValues);
                int conn;
                while (iss >> conn) {
                    wp.connections.push_back(conn);
                }

                currentLevel.waypoints.push_back(wp);
            }
            continue;
        }
    }

    // Don't forget the last level
    if (inLevel) {
        result.mapFile.levels.push_back(currentLevel);
    }

    fclose(file);

    result.success = true;
    return result;
}
