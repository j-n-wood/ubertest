#include "mdl_loader.h"
#include "mdl_types.h"
#include "raymath.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

//------------------------------------------------------------------------------
// Default options
//------------------------------------------------------------------------------
MDLLoadOptions MDLDefaultOptions() {
    MDLLoadOptions opts;
    opts.scale = 0.0254f;   // MDL uses inches, convert to meters
    opts.swap_yz = true;    // MDL is Z-up, glTF is Y-up
    opts.load_animations = true;
    opts.load_textures = true;
    return opts;
}

//------------------------------------------------------------------------------
// Helper: Read entire file into memory
//------------------------------------------------------------------------------
static std::vector<uint8_t> ReadFileData(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> data(size);
    if (fread(data.data(), 1, size, f) != (size_t)size) {
        fclose(f);
        return {};
    }
    fclose(f);
    return data;
}

//------------------------------------------------------------------------------
// Helper: Convert Euler angles (in radians) to quaternion
// MDL uses Euler angles in its own coordinate system (Z-up, X-forward)
// The angles are: [0]=X rotation, [1]=Y rotation, [2]=Z rotation
// This is applied as: Rz * Ry * Rx (standard aerospace/game convention)
//------------------------------------------------------------------------------
static Quaternion EulerToQuat(float rx, float ry, float rz) {
    // Build quaternion from Euler angles using ZYX order (Rz * Ry * Rx)
    // This matches GoldSrc/Source engine convention
    float cx = cosf(rx * 0.5f);
    float sx = sinf(rx * 0.5f);
    float cy = cosf(ry * 0.5f);
    float sy = sinf(ry * 0.5f);
    float cz = cosf(rz * 0.5f);
    float sz = sinf(rz * 0.5f);

    Quaternion q;
    q.w = cx * cy * cz + sx * sy * sz;
    q.x = sx * cy * cz - cx * sy * sz;
    q.y = cx * sy * cz + sx * cy * sz;
    q.z = cx * cy * sz - sx * sy * cz;

    return q;
}

//------------------------------------------------------------------------------
// Helper: Apply coordinate transform
// MDL (GoldSrc): Z-up, X-forward, -Y right (right-handed)
// glTF 2.0:      Y-up, -Z forward, X right (right-handed)
//
// Transformation: (x, y, z) -> (-y, z, -x)
//   MDL X (forward)  -> glTF -Z (forward in glTF is -Z)
//   MDL Y (left)     -> glTF -X (left)
//   MDL Z (up)       -> glTF Y (up)
//------------------------------------------------------------------------------
static Vector3 TransformCoord(Vector3 v, bool swap_yz, float scale) {
    if (swap_yz) {
        // MDL to glTF: (x, y, z) -> (-y, z, -x)
        return { -v.y * scale, v.z * scale, -v.x * scale };
    }
    return { v.x * scale, v.y * scale, v.z * scale };
}

static Quaternion TransformRotation(Quaternion q, bool swap_yz) {
    if (swap_yz) {
        // Transform quaternion for coordinate system change
        // MDL (GoldSrc): Z-up, X-forward, -Y right
        // glTF 2.0:      Y-up, -Z forward, X right
        //
        // Position mapping: (x, y, z) -> (-y, z, -x)
        // For quaternion imaginary parts, apply same mapping:
        // (qx, qy, qz) -> (-qy, qz, -qx)
        return { -q.y, q.z, -q.x, q.w };
    }
    return q;
}

//------------------------------------------------------------------------------
// Helper: Extract animation value from RLE-compressed data
//------------------------------------------------------------------------------
static float ExtractAnimValue(const uint8_t* baseData, uint16_t offset,
                               int frame, float baseValue, float scale) {
    if (offset == 0) {
        return baseValue;
    }

    const MDLAnimValue* animValue = (const MDLAnimValue*)(baseData + offset);

    // Walk through RLE spans to find the frame
    int remaining = frame;
    while (animValue->num.total <= remaining) {
        remaining -= animValue->num.total;
        animValue += 1 + animValue->num.valid;
    }

    // Get value from this span
    int validIndex = std::min(remaining, (int)animValue->num.valid - 1);
    if (validIndex < 0) validIndex = 0;

    int16_t rawValue = (animValue + 1 + validIndex)->value;
    return baseValue + (rawValue * scale);
}

//------------------------------------------------------------------------------
// Parse bones from MDL data
//------------------------------------------------------------------------------
static void ParseBones(const std::vector<uint8_t>& data, const MDLHeader* header,
                       SkeletalModel& model, const MDLLoadOptions& opts) {
    const MDLBone* bones = (const MDLBone*)(data.data() + header->boneindex);

    for (int i = 0; i < header->numbones; i++) {
        SkeletalBone bone;
        bone.name = bones[i].name;
        bone.parent = bones[i].parent;

        // Extract rest pose position from bone table
        Vector3 pos = { bones[i].value[0], bones[i].value[1], bones[i].value[2] };
        bone.position = TransformCoord(pos, opts.swap_yz, opts.scale);

        // Extract rest pose rotation (Euler angles in radians)
        Quaternion rot = EulerToQuat(bones[i].value[3], bones[i].value[4], bones[i].value[5]);
        bone.rotation = TransformRotation(rot, opts.swap_yz);

        bone.scale = { 1.0f, 1.0f, 1.0f };
        bone.inverseBindMatrix = MatrixIdentity();

        model.bones.push_back(bone);
    }

    // NOTE: We delay inverse bind matrix calculation until after animations are loaded.
    // This allows us to use animation frame 0 as the bind pose, which often matches
    // how the mesh geometry was authored.
}

//------------------------------------------------------------------------------
// Parse a single mesh from triangle commands
//------------------------------------------------------------------------------
static void ParseMeshTriangles(const std::vector<uint8_t>& data,
                               const MDLModel* mdlModel,
                               const MDLMesh* mdlMesh,
                               SkinnedMesh& mesh,
                               const MDLLoadOptions& opts,
                               Vector3& boundsMin, Vector3& boundsMax) {
    // Get vertex data
    const float* verts = (const float*)(data.data() + mdlModel->vertindex);
    const uint8_t* vertBones = data.data() + mdlModel->vertinfoindex;
    const float* norms = (const float*)(data.data() + mdlModel->normindex);
    const uint8_t* normBones = data.data() + mdlModel->norminfoindex;

    // Triangle command stream
    const int16_t* triCmds = (const int16_t*)(data.data() + mdlMesh->triindex);

    std::vector<SkinnedVertex> tempVerts;

    while (true) {
        int16_t cmd = *triCmds++;
        if (cmd == 0) break;

        bool isStrip = (cmd > 0);
        int count = std::abs(cmd);

        // Read vertices for this primitive
        std::vector<SkinnedVertex> primVerts;
        for (int i = 0; i < count; i++) {
            int16_t vertIndex = *triCmds++;
            int16_t normIndex = *triCmds++;
            int16_t s = *triCmds++;
            int16_t t = *triCmds++;

            SkinnedVertex v;

            // Position
            Vector3 pos = {
                verts[vertIndex * 3 + 0],
                verts[vertIndex * 3 + 1],
                verts[vertIndex * 3 + 2]
            };
            v.position = TransformCoord(pos, opts.swap_yz, opts.scale);

            // Update bounds
            boundsMin.x = std::min(boundsMin.x, v.position.x);
            boundsMin.y = std::min(boundsMin.y, v.position.y);
            boundsMin.z = std::min(boundsMin.z, v.position.z);
            boundsMax.x = std::max(boundsMax.x, v.position.x);
            boundsMax.y = std::max(boundsMax.y, v.position.y);
            boundsMax.z = std::max(boundsMax.z, v.position.z);

            // Normal
            Vector3 norm = {
                norms[normIndex * 3 + 0],
                norms[normIndex * 3 + 1],
                norms[normIndex * 3 + 2]
            };
            v.normal = TransformCoord(norm, opts.swap_yz, 1.0f);  // Don't scale normals

            // Texture coordinates (convert from fixed-point)
            v.texcoord.x = s / 32768.0f;
            v.texcoord.y = t / 32768.0f;

            // Bone index
            v.boneIndex = vertBones[vertIndex];

            primVerts.push_back(v);
        }

        // Convert strip/fan to triangles
        // Note: When swap_yz is enabled, we flip two axes which changes handedness
        // This requires reversing triangle winding to maintain correct face culling
        bool flipWinding = opts.swap_yz;

        if (count >= 3) {
            if (isStrip) {
                // Triangle strip
                for (int i = 0; i < count - 2; i++) {
                    bool oddTri = (i % 2 != 0);
                    // XOR: flip winding for odd triangles, but also flip if coordinate transform changed handedness
                    bool reverseThis = oddTri != flipWinding;

                    if (!reverseThis) {
                        tempVerts.push_back(primVerts[i]);
                        tempVerts.push_back(primVerts[i + 1]);
                        tempVerts.push_back(primVerts[i + 2]);
                    } else {
                        tempVerts.push_back(primVerts[i]);
                        tempVerts.push_back(primVerts[i + 2]);
                        tempVerts.push_back(primVerts[i + 1]);
                    }
                }
            } else {
                // Triangle fan
                for (int i = 1; i < count - 1; i++) {
                    if (!flipWinding) {
                        tempVerts.push_back(primVerts[0]);
                        tempVerts.push_back(primVerts[i]);
                        tempVerts.push_back(primVerts[i + 1]);
                    } else {
                        tempVerts.push_back(primVerts[0]);
                        tempVerts.push_back(primVerts[i + 1]);
                        tempVerts.push_back(primVerts[i]);
                    }
                }
            }
        }
    }

    // Store vertices and create indices
    mesh.vertices = std::move(tempVerts);
    mesh.indices.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        mesh.indices[i] = (unsigned short)i;
    }
}

//------------------------------------------------------------------------------
// Parse meshes from MDL data
//------------------------------------------------------------------------------
static void ParseMeshes(const std::vector<uint8_t>& data, const MDLHeader* header,
                        SkeletalModel& model, const MDLLoadOptions& opts) {
    model.boundsMin = { 1e10f, 1e10f, 1e10f };
    model.boundsMax = { -1e10f, -1e10f, -1e10f };

    const MDLBodyPart* bodyParts = (const MDLBodyPart*)(data.data() + header->bodypartindex);

    int meshIndex = 0;
    for (int bp = 0; bp < header->numbodyparts; bp++) {
        const MDLBodyPart& bodyPart = bodyParts[bp];
        const MDLModel* models = (const MDLModel*)(data.data() + bodyPart.modelindex);

        // For simplicity, use only the first model variant in each body part
        for (int m = 0; m < std::min(1, bodyPart.nummodels); m++) {
            const MDLModel& mdlModel = models[m];
            const MDLMesh* meshes = (const MDLMesh*)(data.data() + mdlModel.meshindex);

            for (int ms = 0; ms < mdlModel.nummesh; ms++) {
                const MDLMesh& mdlMesh = meshes[ms];

                SkinnedMesh mesh;
                mesh.name = std::string(bodyPart.name) + "_" + std::to_string(meshIndex++);
                mesh.materialIndex = mdlMesh.skinref;

                ParseMeshTriangles(data, &mdlModel, &mdlMesh, mesh, opts,
                                   model.boundsMin, model.boundsMax);

                if (!mesh.vertices.empty()) {
                    model.meshes.push_back(std::move(mesh));
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Parse textures/materials from MDL data
//------------------------------------------------------------------------------
static void ParseTextures(const std::vector<uint8_t>& data, const MDLHeader* header,
                          SkeletalModel& model) {
    if (header->numtextures == 0) return;

    const MDLTexture* textures = (const MDLTexture*)(data.data() + header->textureindex);

    for (int i = 0; i < header->numtextures; i++) {
        SkeletalMaterial mat;
        mat.name = textures[i].name;
        mat.texturePath = textures[i].name;  // Embedded, but store name
        mat.width = textures[i].width;
        mat.height = textures[i].height;
        mat.diffuseColor = WHITE;

        model.materials.push_back(mat);
    }
}

//------------------------------------------------------------------------------
// Parse animations from MDL data
//------------------------------------------------------------------------------
static void ParseAnimations(const std::vector<uint8_t>& data, const MDLHeader* header,
                            SkeletalModel& model, const MDLLoadOptions& opts) {
    if (header->numseq == 0) return;

    const MDLSequenceDesc* seqs = (const MDLSequenceDesc*)(data.data() + header->seqindex);
    const MDLBone* bones = (const MDLBone*)(data.data() + header->boneindex);

    for (int s = 0; s < header->numseq; s++) {
        const MDLSequenceDesc& seq = seqs[s];

        SkeletalAnimation anim;
        anim.name = seq.label;
        anim.fps = seq.fps;
        anim.duration = (seq.numframes > 0 && seq.fps > 0) ?
                        (seq.numframes - 1) / seq.fps : 0.0f;
        anim.looping = (seq.flags & STUDIO_LOOPING) != 0;

        // Get animation data pointer
        // For seqgroup 0, animation data is in main file
        const uint8_t* animBase = data.data();
        if (seq.seqgroup != 0) {
            // External animation file - skip for now
            continue;
        }

        const MDLAnim* anims = (const MDLAnim*)(animBase + seq.animindex);

        // Parse animation for each bone
        for (int b = 0; b < header->numbones; b++) {
            const MDLBone& bone = bones[b];
            const MDLAnim& boneAnim = anims[b];  // Using blend 0

            BoneAnimationTrack track;
            track.boneIndex = b;

            // Sample each frame
            for (int f = 0; f < seq.numframes; f++) {
                BoneKeyframe kf;
                kf.time = (seq.fps > 0) ? f / seq.fps : 0.0f;

                // Extract position
                Vector3 pos;
                pos.x = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                         boneAnim.offset[0], f, bone.value[0], bone.scale[0]);
                pos.y = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                         boneAnim.offset[1], f, bone.value[1], bone.scale[1]);
                pos.z = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                         boneAnim.offset[2], f, bone.value[2], bone.scale[2]);

                kf.translation = TransformCoord(pos, opts.swap_yz, opts.scale);

                // Extract rotation (Euler angles)
                float rx = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                            boneAnim.offset[3], f, bone.value[3], bone.scale[3]);
                float ry = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                            boneAnim.offset[4], f, bone.value[4], bone.scale[4]);
                float rz = ExtractAnimValue(animBase + seq.animindex + sizeof(MDLAnim) * b,
                                            boneAnim.offset[5], f, bone.value[5], bone.scale[5]);

                Quaternion rot = EulerToQuat(rx, ry, rz);
                kf.rotation = TransformRotation(rot, opts.swap_yz);

                kf.scale = { 1.0f, 1.0f, 1.0f };

                track.keyframes.push_back(kf);
            }

            // Only add track if it has keyframes
            if (!track.keyframes.empty()) {
                anim.tracks.push_back(std::move(track));
            }
        }

        model.animations.push_back(std::move(anim));
    }
}

//------------------------------------------------------------------------------
// Calculate bind pose world matrices for all bones
// Used for transforming vertices from bone-local to model space
//------------------------------------------------------------------------------
static void CalculateBoneWorldMatrices(const SkeletalModel& model, std::vector<Matrix>& worldMatrices) {
    size_t numBones = model.bones.size();
    worldMatrices.resize(numBones);

    for (size_t i = 0; i < numBones; i++) {
        const SkeletalBone& bone = model.bones[i];

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
    }
}

//------------------------------------------------------------------------------
// Transform vertices from bone-local space to model space
// MDL stores vertices in bone-local space, but glTF expects model space
//------------------------------------------------------------------------------
static void TransformVerticesToModelSpace(SkeletalModel& model,
                                           const std::vector<Matrix>& boneWorldMatrices) {
    // Reset bounds
    model.boundsMin = { 1e10f, 1e10f, 1e10f };
    model.boundsMax = { -1e10f, -1e10f, -1e10f };

    for (auto& mesh : model.meshes) {
        for (auto& vertex : mesh.vertices) {
            int boneIdx = vertex.boneIndex;
            if (boneIdx < 0 || boneIdx >= (int)boneWorldMatrices.size()) continue;

            const Matrix& boneWorld = boneWorldMatrices[boneIdx];

            // Transform position: pos_model = boneWorld * pos_local
            Vector3 pos = vertex.position;
            vertex.position.x = pos.x * boneWorld.m0 + pos.y * boneWorld.m4 + pos.z * boneWorld.m8 + boneWorld.m12;
            vertex.position.y = pos.x * boneWorld.m1 + pos.y * boneWorld.m5 + pos.z * boneWorld.m9 + boneWorld.m13;
            vertex.position.z = pos.x * boneWorld.m2 + pos.y * boneWorld.m6 + pos.z * boneWorld.m10 + boneWorld.m14;

            // Transform normal (rotation only, no translation)
            Vector3 norm = vertex.normal;
            vertex.normal.x = norm.x * boneWorld.m0 + norm.y * boneWorld.m4 + norm.z * boneWorld.m8;
            vertex.normal.y = norm.x * boneWorld.m1 + norm.y * boneWorld.m5 + norm.z * boneWorld.m9;
            vertex.normal.z = norm.x * boneWorld.m2 + norm.y * boneWorld.m6 + norm.z * boneWorld.m10;

            // Normalize
            float len = sqrtf(vertex.normal.x * vertex.normal.x +
                              vertex.normal.y * vertex.normal.y +
                              vertex.normal.z * vertex.normal.z);
            if (len > 0.0001f) {
                vertex.normal.x /= len;
                vertex.normal.y /= len;
                vertex.normal.z /= len;
            }

            // Update bounds
            model.boundsMin.x = std::min(model.boundsMin.x, vertex.position.x);
            model.boundsMin.y = std::min(model.boundsMin.y, vertex.position.y);
            model.boundsMin.z = std::min(model.boundsMin.z, vertex.position.z);
            model.boundsMax.x = std::max(model.boundsMax.x, vertex.position.x);
            model.boundsMax.y = std::max(model.boundsMax.y, vertex.position.y);
            model.boundsMax.z = std::max(model.boundsMax.z, vertex.position.z);
        }
    }
}

//------------------------------------------------------------------------------
// Main load function
//------------------------------------------------------------------------------
MDLLoadResult LoadMDL(const char* filepath, MDLLoadOptions options) {
    MDLLoadResult result = {};
    result.success = false;

    // Read file
    std::vector<uint8_t> data = ReadFileData(filepath);
    if (data.empty()) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Failed to read file: %s", filepath);
        return result;
    }

    // Validate header
    if (data.size() < sizeof(MDLHeader)) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "File too small to contain MDL header");
        return result;
    }

    const MDLHeader* header = (const MDLHeader*)data.data();

    if (header->id != MDL_ID) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Invalid MDL magic number (expected IDST)");
        return result;
    }

    if (header->version != MDL_VERSION) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Unsupported MDL version %d (expected %d)", header->version, MDL_VERSION);
        return result;
    }

    // Extract model name
    result.model.name = header->name;

    // Parse components
    ParseBones(data, header, result.model, options);
    ParseMeshes(data, header, result.model, options);

    if (options.load_textures) {
        ParseTextures(data, header, result.model);
    }

    if (options.load_animations) {
        ParseAnimations(data, header, result.model, options);
    }

    // MDL animations store absolute transforms, not relative to rest pose.
    // The bone rest pose in MDL header is just the skeleton hierarchy.
    // For glTF, we need the bind pose to match the pose at animation frame 0.
    // This ensures that playing the first animation starts without any jump.
    //
    // IMPORTANT: We must update the bind pose BEFORE transforming vertices,
    // because vertices should be transformed using the same matrices that
    // will be used for inverse bind matrices.
    if (!result.model.animations.empty() && !result.model.animations[0].tracks.empty()) {
        const SkeletalAnimation& firstAnim = result.model.animations[0];

        for (const auto& track : firstAnim.tracks) {
            if (track.boneIndex >= 0 && track.boneIndex < (int)result.model.bones.size() &&
                !track.keyframes.empty()) {
                SkeletalBone& bone = result.model.bones[track.boneIndex];
                const BoneKeyframe& frame0 = track.keyframes[0];

                // Update bone to use animation frame 0 as bind pose
                bone.position = frame0.translation;
                bone.rotation = frame0.rotation;
                bone.scale = frame0.scale;
            }
        }
    }

    // MDL stores vertices in bone-local space, but glTF expects model space.
    // Transform vertices using the (updated) bind pose bone world matrices.
    std::vector<Matrix> boneWorldMatrices;
    CalculateBoneWorldMatrices(result.model, boneWorldMatrices);
    TransformVerticesToModelSpace(result.model, boneWorldMatrices);

    // Now calculate inverse bind matrices
    CalculateInverseBindMatrices(result.model);

    result.success = true;
    return result;
}

//------------------------------------------------------------------------------
// Get sequence names without full load
//------------------------------------------------------------------------------
std::vector<std::string> GetMDLSequenceNames(const char* filepath) {
    std::vector<std::string> names;

    std::vector<uint8_t> data = ReadFileData(filepath);
    if (data.size() < sizeof(MDLHeader)) return names;

    const MDLHeader* header = (const MDLHeader*)data.data();
    if (header->id != MDL_ID || header->version != MDL_VERSION) return names;

    const MDLSequenceDesc* seqs = (const MDLSequenceDesc*)(data.data() + header->seqindex);
    for (int i = 0; i < header->numseq; i++) {
        names.push_back(seqs[i].label);
    }

    return names;
}
