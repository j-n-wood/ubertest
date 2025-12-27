#pragma once

#include <string>
#include <vector>

struct Waypoint {
    int number;
    int x, y;
    std::vector<int> connections;
};

struct ParadroidLevel {
    int levelNumber = 0;
    int xlen = 0;
    int ylen = 0;
    int color = 0;
    std::string name;
    std::string comment;
    std::string song;
    std::vector<std::vector<int>> tiles;  // [row][col]
    std::vector<Waypoint> waypoints;
};

struct ParadroidMapFile {
    std::string areaName;
    std::vector<ParadroidLevel> levels;
};

struct ParseResult {
    bool success = false;
    std::string errorMsg;
    int errorLine = 0;
    ParadroidMapFile mapFile;
};

// Parse a Paradroid.maps file and return the result
ParseResult parseParadroidMaps(const std::string& filePath);
