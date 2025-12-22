#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// Section definition from droidclasses.txt
struct DroidSection {
    int renderIndex = -1;
    int parentIndex = -1;
    int tag = -1;
    int rotateMode = 1;
    std::array<float, 3> offset{};
    std::array<float, 3> rotation{};
};

// Complete droid class definition
struct DroidClass {
    int classId = 0;

    // Line 1: render_index type_code energy armour weapon
    int renderIndex = 0;
    int typeCode = 0;
    int energyCost = 0;
    float armour = 0.0f;
    float weapon = 0.0f;

    // Line 2: weapon_type pulses
    int weaponType = -1;
    int pulses = 0;

    // Line 3: 4 speed values
    std::array<float, 4> speeds{};

    // Line 4: 8 sensor flags
    bool visualSensor = false;
    bool auralSensor = false;
    bool ultrasonicSensor = false;
    bool infraredSensor = false;
    bool motionSensor = false;
    bool radioSensor = false;
    bool radarSensor = false;
    bool magneticSensor = false;

    // Line 5: collision
    float collideRadius = 0.0f;
    float proximityRadius = 0.0f;
    float aggression = 0.0f;

    // Keywords
    float vrad = 0.0f;
    int headRenderIndex = -1;
    std::optional<float> headRotationRate;
    std::optional<float> rotationRate;
    std::array<float, 3> headOffset{};
    std::array<float, 3> fireOffset{};
    bool hasTurret = false;
    bool omnidirectional = false;
    bool targetReticule = false;
    bool performClean = false;

    // Description
    std::string description;

    // Classification
    int droidType = 0;
    int driveType = 0;
    int brainType = 0;

    // Sound
    int soundIndex = -1;

    // Sections
    std::vector<DroidSection> sections;
    int headSectionIndex = -1;
};

struct DroidClassParseResult {
    bool success = false;
    std::string errorMsg;
    int errorLine = 0;
    std::vector<DroidClass> classes;
};

[[nodiscard]] DroidClassParseResult parseDroidClasses(std::string_view filepath);
