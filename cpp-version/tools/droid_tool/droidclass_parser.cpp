#include "droidclass_parser.h"

#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

bool startsWith(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

std::string_view trim(std::string_view str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r'))
        str.remove_prefix(1);
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n'))
        str.remove_suffix(1);
    return str;
}

} // namespace

DroidClassParseResult parseDroidClasses(std::string_view filepath) {
    DroidClassParseResult result;

    FILE* file = fopen(std::string(filepath).c_str(), "r");
    if (!file) {
        result.errorMsg = "Failed to open file";
        return result;
    }

    char lineBuf[1024];
    int lineNum = 0;
    DroidClass* currentClass = nullptr;
    bool inSection = false;
    DroidSection currentSection;

    while (fgets(lineBuf, sizeof(lineBuf), file)) {
        lineNum++;
        std::string_view line = trim(lineBuf);

        if (line.empty()) continue;

        // Class header
        if (startsWith(line, "Class ")) {
            if (inSection && currentClass) {
                currentClass->sections.push_back(currentSection);
                inSection = false;
            }

            int classId = 0;
            if (sscanf(lineBuf, "Class %d", &classId) != 1) {
                result.errorMsg = "Failed to parse Class ID";
                result.errorLine = lineNum;
                fclose(file);
                return result;
            }

            result.classes.push_back(DroidClass{});
            currentClass = &result.classes.back();
            currentClass->classId = classId;

            // Read the next 5 numeric lines
            // Line 1: render_index type_code energy armour weapon
            if (!fgets(lineBuf, sizeof(lineBuf), file)) break;
            lineNum++;
            sscanf(lineBuf, "%d %d %d %f %f",
                   &currentClass->renderIndex,
                   &currentClass->typeCode,
                   &currentClass->energyCost,
                   &currentClass->armour,
                   &currentClass->weapon);

            // Line 2: weapon_type pulses
            if (!fgets(lineBuf, sizeof(lineBuf), file)) break;
            lineNum++;
            sscanf(lineBuf, "%d %d",
                   &currentClass->weaponType,
                   &currentClass->pulses);

            // Line 3: 4 speeds
            if (!fgets(lineBuf, sizeof(lineBuf), file)) break;
            lineNum++;
            sscanf(lineBuf, "%f %f %f %f",
                   &currentClass->speeds[0],
                   &currentClass->speeds[1],
                   &currentClass->speeds[2],
                   &currentClass->speeds[3]);

            // Line 4: 8 sensor flags
            if (!fgets(lineBuf, sizeof(lineBuf), file)) break;
            lineNum++;
            int v, a, u, i, m, r, ra, ma;
            sscanf(lineBuf, "%d %d %d %d %d %d %d %d", &v, &a, &u, &i, &m, &r, &ra, &ma);
            currentClass->visualSensor = (v != 0);
            currentClass->auralSensor = (a != 0);
            currentClass->ultrasonicSensor = (u != 0);
            currentClass->infraredSensor = (i != 0);
            currentClass->motionSensor = (m != 0);
            currentClass->radioSensor = (r != 0);
            currentClass->radarSensor = (ra != 0);
            currentClass->magneticSensor = (ma != 0);

            // Line 5: collide_radius proximity_radius aggression unused
            if (!fgets(lineBuf, sizeof(lineBuf), file)) break;
            lineNum++;
            float unused;
            sscanf(lineBuf, "%f %f %f %f",
                   &currentClass->collideRadius,
                   &currentClass->proximityRadius,
                   &currentClass->aggression,
                   &unused);

            continue;
        }

        if (!currentClass) continue;

        // Keywords
        if (startsWith(line, "vrad ")) {
            sscanf(lineBuf, "vrad %f", &currentClass->vrad);
        }
        else if (startsWith(line, "head ")) {
            sscanf(lineBuf, "head %d", &currentClass->headRenderIndex);
        }
        else if (startsWith(line, "SOUND ")) {
            sscanf(lineBuf, "SOUND %d", &currentClass->soundIndex);
        }
        else if (startsWith(line, "ULTRASONIC ")) {
            // ULTRASONIC is also a sound-like thing, parse sound index
            sscanf(lineBuf, "ULTRASONIC %d", &currentClass->soundIndex);
        }
        else if (line == "TURRET") {
            currentClass->hasTurret = true;
        }
        else if (line == "OMNIDIRECTIONAL") {
            currentClass->omnidirectional = true;
        }
        else if (line == "TARGETRETICULE") {
            currentClass->targetReticule = true;
        }
        else if (line == "PERFORMCLEAN") {
            currentClass->performClean = true;
        }
        else if (startsWith(line, "HEADROTATIONRATE ")) {
            float rate;
            sscanf(lineBuf, "HEADROTATIONRATE %f", &rate);
            currentClass->headRotationRate = rate;
        }
        else if (startsWith(line, "ROTATIONRATE ")) {
            float rate;
            sscanf(lineBuf, "ROTATIONRATE %f", &rate);
            currentClass->rotationRate = rate;
        }
        else if (startsWith(line, "HEADOFFSET ")) {
            sscanf(lineBuf, "HEADOFFSET %f %f %f",
                   &currentClass->headOffset[0],
                   &currentClass->headOffset[1],
                   &currentClass->headOffset[2]);
        }
        else if (startsWith(line, "FIREOFFSET ")) {
            sscanf(lineBuf, "FIREOFFSET %f %f %f",
                   &currentClass->fireOffset[0],
                   &currentClass->fireOffset[1],
                   &currentClass->fireOffset[2]);
        }
        else if (startsWith(line, "HEADSECTION ")) {
            sscanf(lineBuf, "HEADSECTION %d", &currentClass->headSectionIndex);
        }
        else if (startsWith(line, "DESCRIPTION ")) {
            // Format: DESCRIPTION N text
            int descNum;
            char text[512];
            if (sscanf(lineBuf, "DESCRIPTION %d %[^\n]", &descNum, text) == 2) {
                // Trim trailing whitespace (CR/LF from Windows line endings)
                std::string_view trimmedText = trim(text);
                if (!currentClass->description.empty()) {
                    currentClass->description += " ";
                }
                currentClass->description += trimmedText;
            }
        }
        else if (startsWith(line, "DROIDTYPE ")) {
            sscanf(lineBuf, "DROIDTYPE %d", &currentClass->droidType);
        }
        else if (startsWith(line, "DRIVETYPE ")) {
            sscanf(lineBuf, "DRIVETYPE %d", &currentClass->driveType);
        }
        else if (startsWith(line, "BRAINTYPE ")) {
            sscanf(lineBuf, "BRAINTYPE %d", &currentClass->brainType);
        }
        else if (line == "SECTION") {
            // Save previous section if any
            if (inSection) {
                currentClass->sections.push_back(currentSection);
            }
            inSection = true;
            currentSection = DroidSection{};
        }
        else if (startsWith(line, "Render ") && inSection) {
            sscanf(lineBuf, "Render %d", &currentSection.renderIndex);
        }
        else if (startsWith(line, "Parent ") && inSection) {
            sscanf(lineBuf, "Parent %d", &currentSection.parentIndex);
        }
        else if (startsWith(line, "Tag ") && inSection) {
            sscanf(lineBuf, "Tag %d", &currentSection.tag);
        }
        else if (startsWith(line, "Rotate ") && inSection) {
            sscanf(lineBuf, "Rotate %d", &currentSection.rotateMode);
        }
        else if (startsWith(line, "Offset ") && inSection) {
            sscanf(lineBuf, "Offset %f %f %f",
                   &currentSection.offset[0],
                   &currentSection.offset[1],
                   &currentSection.offset[2]);
        }
        else if (startsWith(line, "Rotation ") && inSection) {
            sscanf(lineBuf, "Rotation %f %f %f",
                   &currentSection.rotation[0],
                   &currentSection.rotation[1],
                   &currentSection.rotation[2]);
        }
        else if (line == "END") {
            if (inSection) {
                currentClass->sections.push_back(currentSection);
                inSection = false;
            }
            currentClass = nullptr;
        }
        // Ignore unknown keywords (SPECIALSAMPLE, SPECIALSAMPLERANGE, SPECIALRATE, etc.)
    }

    fclose(file);
    result.success = true;
    return result;
}
