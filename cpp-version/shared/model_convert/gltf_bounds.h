#pragma once

// GLTF bounds extracted from accessor min/max values
struct GLTFBounds {
    bool valid = false;
    float min[3] = {0, 0, 0};  // X, Y, Z minimum
    float max[3] = {0, 0, 0};  // X, Y, Z maximum

    // Computed sizes
    float sizeX() const { return max[0] - min[0]; }
    float sizeY() const { return max[1] - min[1]; }
    float sizeZ() const { return max[2] - min[2]; }
};

// Physics shape info for unit generation
struct PhysicsShapeInfo {
    const char* type = "circle";  // "circle" or "box"
    float radius = 0.0f;          // For circle
    float width = 0.0f;           // For box (X dimension)
    float height = 0.0f;          // For box (Z dimension in physics plane)
};

// Read bounds from GLTF file without loading full model
// Works with .gltf files (JSON format)
[[nodiscard]] GLTFBounds readGLTFBounds(const char* gltfPath);

// Determine best physics shape based on model bounds
// Uses XZ plane dimensions (Y is height in Y-up GLTF)
// Returns circle for aspect ratio < 1.5, box otherwise
[[nodiscard]] PhysicsShapeInfo determinePhysicsShape(const GLTFBounds& bounds, float defaultRadius = 0.25f);
