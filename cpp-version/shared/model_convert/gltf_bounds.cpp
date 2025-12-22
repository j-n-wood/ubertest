#include "gltf_bounds.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

GLTFBounds readGLTFBounds(const char* gltfPath) {
    GLTFBounds bounds;

    if (!gltfPath || gltfPath[0] == '\0') {
        return bounds;
    }

    std::ifstream file(gltfPath);
    if (!file.is_open()) {
        std::cerr << "gltf_bounds: Failed to open " << gltfPath << std::endl;
        return bounds;
    }

    try {
        json gltf = json::parse(file);

        // Find the POSITION accessor
        // GLTF stores position data in accessors, and the first VEC3 accessor
        // is typically the position data
        if (!gltf.contains("accessors") || !gltf["accessors"].is_array()) {
            return bounds;
        }

        for (const auto& accessor : gltf["accessors"]) {
            if (accessor.contains("type") && accessor["type"] == "VEC3") {
                // Check if this accessor has min/max bounds
                if (accessor.contains("min") && accessor.contains("max")) {
                    const auto& minArr = accessor["min"];
                    const auto& maxArr = accessor["max"];

                    if (minArr.is_array() && minArr.size() >= 3 &&
                        maxArr.is_array() && maxArr.size() >= 3) {

                        bounds.min[0] = minArr[0].get<float>();
                        bounds.min[1] = minArr[1].get<float>();
                        bounds.min[2] = minArr[2].get<float>();
                        bounds.max[0] = maxArr[0].get<float>();
                        bounds.max[1] = maxArr[1].get<float>();
                        bounds.max[2] = maxArr[2].get<float>();
                        bounds.valid = true;

                        // Found position accessor with bounds, stop searching
                        break;
                    }
                }
            }
        }
    } catch (const json::exception& e) {
        std::cerr << "gltf_bounds: JSON parse error in " << gltfPath << ": " << e.what() << std::endl;
    }

    return bounds;
}

PhysicsShapeInfo determinePhysicsShape(const GLTFBounds& bounds, float defaultRadius) {
    PhysicsShapeInfo shape;

    if (!bounds.valid) {
        // Fallback to default circle
        shape.type = "circle";
        shape.radius = defaultRadius;
        return shape;
    }

    // Use XZ plane dimensions for 2D top-down physics
    // In Y-up GLTF: X is left/right, Z is forward/back
    float sizeX = bounds.sizeX();
    float sizeZ = bounds.sizeZ();

    // Ensure non-zero sizes
    if (sizeX < 0.001f) sizeX = 0.001f;
    if (sizeZ < 0.001f) sizeZ = 0.001f;

    // Calculate aspect ratio (always >= 1.0)
    float aspectRatio = (sizeX > sizeZ) ? sizeX / sizeZ : sizeZ / sizeX;

    if (aspectRatio < 1.5f) {
        // Nearly square: use circle with radius = max dimension / 2
        shape.type = "circle";
        shape.radius = std::max(sizeX, sizeZ) / 2.0f;
    } else {
        // Elongated: use box with actual dimensions
        shape.type = "box";
        shape.width = sizeX;
        shape.height = sizeZ;
    }

    return shape;
}
