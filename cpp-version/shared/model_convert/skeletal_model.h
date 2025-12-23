#ifndef SKELETAL_MODEL_H
#define SKELETAL_MODEL_H

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>

//------------------------------------------------------------------------------
// Bone in rest pose
//------------------------------------------------------------------------------
struct SkeletalBone {
    std::string name;
    int parent;                    // -1 for root bone
    Vector3 position;              // Local position relative to parent
    Quaternion rotation;           // Local rotation as quaternion
    Vector3 scale;                 // Local scale (usually 1,1,1)
    Matrix inverseBindMatrix;      // For skinning: transforms from model space to bone space
};

//------------------------------------------------------------------------------
// Single keyframe for one bone's transform
//------------------------------------------------------------------------------
struct BoneKeyframe {
    float time;                    // Time in seconds
    Vector3 translation;           // Position
    Quaternion rotation;           // Rotation as quaternion
    Vector3 scale;                 // Scale (usually 1,1,1)
};

//------------------------------------------------------------------------------
// Animation track for one bone
//------------------------------------------------------------------------------
struct BoneAnimationTrack {
    int boneIndex;
    std::vector<BoneKeyframe> keyframes;
};

//------------------------------------------------------------------------------
// Complete animation sequence
//------------------------------------------------------------------------------
struct SkeletalAnimation {
    std::string name;
    float duration;                // Total duration in seconds
    float fps;                     // Original frames per second
    bool looping;
    std::vector<BoneAnimationTrack> tracks;  // One per animated bone
};

//------------------------------------------------------------------------------
// Skinned vertex (MDL uses single-bone weighting)
//------------------------------------------------------------------------------
struct SkinnedVertex {
    Vector3 position;
    Vector3 normal;
    Vector2 texcoord;
    int boneIndex;                 // Single bone index (MDL = 100% weight to one bone)
};

//------------------------------------------------------------------------------
// Skinned mesh with material assignment
//------------------------------------------------------------------------------
struct SkinnedMesh {
    std::string name;
    int materialIndex;
    std::vector<SkinnedVertex> vertices;
    std::vector<unsigned short> indices;
};

//------------------------------------------------------------------------------
// Material information
//------------------------------------------------------------------------------
struct SkeletalMaterial {
    std::string name;
    std::string texturePath;       // Path to diffuse texture
    Color diffuseColor;
    int width;                     // Texture dimensions (for UV calculation)
    int height;
};

//------------------------------------------------------------------------------
// Complete skeletal model
//------------------------------------------------------------------------------
struct SkeletalModel {
    // Skeleton hierarchy
    std::vector<SkeletalBone> bones;

    // Meshes with skinning data
    std::vector<SkinnedMesh> meshes;

    // Materials
    std::vector<SkeletalMaterial> materials;

    // Animations
    std::vector<SkeletalAnimation> animations;

    // Bounding box
    Vector3 boundsMin;
    Vector3 boundsMax;

    // Model name
    std::string name;
};

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------

// Helper: Clean up floating-point precision issues in matrix
// Values very close to 0 or 1 are snapped to exact values
inline void CleanupMatrix(Matrix& m) {
    const float epsilon = 1e-5f;  // Threshold for snapping
    float* vals = &m.m0;
    for (int i = 0; i < 16; i++) {
        // Snap near-zero values to exactly 0
        if (fabsf(vals[i]) < epsilon) {
            vals[i] = 0.0f;
        }
        // Snap near-one values to exactly 1
        else if (fabsf(vals[i] - 1.0f) < epsilon) {
            vals[i] = 1.0f;
        }
        // Snap near-negative-one values to exactly -1
        else if (fabsf(vals[i] + 1.0f) < epsilon) {
            vals[i] = -1.0f;
        }
    }
}

// Calculate inverse bind matrices for all bones
// Call after bones are loaded with their rest pose transforms
inline void CalculateInverseBindMatrices(SkeletalModel& model) {
    size_t numBones = model.bones.size();
    std::vector<Matrix> worldMatrices(numBones);

    for (size_t i = 0; i < numBones; i++) {
        SkeletalBone& bone = model.bones[i];

        // Build local transform matrix from TRS
        Matrix translation = MatrixTranslate(bone.position.x, bone.position.y, bone.position.z);
        Matrix rotation = QuaternionToMatrix(bone.rotation);
        Matrix scale = MatrixScale(bone.scale.x, bone.scale.y, bone.scale.z);

        // Local = Scale * Rotation * Translation (column-major order)
        Matrix local = MatrixMultiply(MatrixMultiply(scale, rotation), translation);

        // World = Local * Parent (or just Local for root bones)
        if (bone.parent >= 0 && bone.parent < (int)i) {
            worldMatrices[i] = MatrixMultiply(local, worldMatrices[bone.parent]);
        } else {
            worldMatrices[i] = local;
        }

        // Inverse bind matrix transforms vertices from model space to bone-local space
        bone.inverseBindMatrix = MatrixInvert(worldMatrices[i]);

        // Clean up floating-point precision issues
        CleanupMatrix(bone.inverseBindMatrix);
    }
}

// Get world-space transform for a bone at a specific animation frame
inline Matrix GetBoneWorldTransform(const SkeletalModel& model, int boneIndex,
                                     const SkeletalAnimation* anim, float time) {
    if (boneIndex < 0 || boneIndex >= (int)model.bones.size()) {
        return MatrixIdentity();
    }

    const SkeletalBone& bone = model.bones[boneIndex];

    // Start with rest pose
    Vector3 pos = bone.position;
    Quaternion rot = bone.rotation;
    Vector3 scl = bone.scale;

    // Override with animation data if available
    if (anim) {
        for (const auto& track : anim->tracks) {
            if (track.boneIndex == boneIndex && !track.keyframes.empty()) {
                // Find keyframes for interpolation
                const BoneKeyframe* prev = &track.keyframes[0];
                const BoneKeyframe* next = prev;

                for (size_t k = 0; k < track.keyframes.size(); k++) {
                    if (track.keyframes[k].time <= time) {
                        prev = &track.keyframes[k];
                        next = (k + 1 < track.keyframes.size()) ?
                               &track.keyframes[k + 1] : prev;
                    }
                }

                // Interpolate between keyframes
                float t = 0.0f;
                if (prev != next && next->time > prev->time) {
                    t = (time - prev->time) / (next->time - prev->time);
                }

                pos = Vector3Lerp(prev->translation, next->translation, t);
                rot = QuaternionSlerp(prev->rotation, next->rotation, t);
                scl = Vector3Lerp(prev->scale, next->scale, t);
                break;
            }
        }
    }

    // Build local matrix
    Matrix translation = MatrixTranslate(pos.x, pos.y, pos.z);
    Matrix rotation = QuaternionToMatrix(rot);
    Matrix scale = MatrixScale(scl.x, scl.y, scl.z);
    Matrix local = MatrixMultiply(MatrixMultiply(scale, rotation), translation);

    // Multiply by parent world transform
    if (bone.parent >= 0) {
        Matrix parentWorld = GetBoneWorldTransform(model, bone.parent, anim, time);
        return MatrixMultiply(local, parentWorld);
    }

    return local;
}

#endif // SKELETAL_MODEL_H
