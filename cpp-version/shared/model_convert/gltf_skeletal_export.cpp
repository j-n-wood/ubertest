#include "gltf_skeletal_export.h"

// Configure tinygltf before including
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE        // We'll handle images separately if needed
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include <cstring>
#include <algorithm>

//------------------------------------------------------------------------------
// Default options
//------------------------------------------------------------------------------
SkeletalExportOptions SkeletalExportDefaultOptions() {
    SkeletalExportOptions opts;
    opts.binary = true;           // Default to GLB
    opts.embed_textures = true;
    opts.export_animations = true;
    opts.texture_dir = nullptr;
    return opts;
}

//------------------------------------------------------------------------------
// Helper: Add data to buffer and return byte offset
//------------------------------------------------------------------------------
static size_t AddToBuffer(tinygltf::Buffer& buffer, const void* data, size_t size) {
    size_t offset = buffer.data.size();
    buffer.data.resize(offset + size);
    memcpy(&buffer.data[offset], data, size);
    return offset;
}

//------------------------------------------------------------------------------
// Helper: Pad buffer to 4-byte alignment
//------------------------------------------------------------------------------
static void AlignBuffer(tinygltf::Buffer& buffer) {
    while (buffer.data.size() % 4 != 0) {
        buffer.data.push_back(0);
    }
}

//------------------------------------------------------------------------------
// Helper: Create accessor for float data
// Set useArrayBufferTarget=false for animation data (no target needed)
//------------------------------------------------------------------------------
static int CreateFloatAccessor(tinygltf::Model& gltf, tinygltf::Buffer& buffer,
                                const float* data, int count, int componentCount,
                                const std::string& type,
                                float* minVals = nullptr, float* maxVals = nullptr,
                                bool useArrayBufferTarget = true) {
    size_t offset = AddToBuffer(buffer, data, count * componentCount * sizeof(float));
    AlignBuffer(buffer);

    // Create buffer view
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = offset;
    view.byteLength = count * componentCount * sizeof(float);
    // Animation data should not have a buffer view target
    if (useArrayBufferTarget) {
        view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    }
    int viewIndex = (int)gltf.bufferViews.size();
    gltf.bufferViews.push_back(view);

    // Create accessor
    tinygltf::Accessor accessor;
    accessor.bufferView = viewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.count = count;
    accessor.type = (type == "SCALAR") ? TINYGLTF_TYPE_SCALAR :
                    (type == "VEC2") ? TINYGLTF_TYPE_VEC2 :
                    (type == "VEC3") ? TINYGLTF_TYPE_VEC3 :
                    (type == "VEC4") ? TINYGLTF_TYPE_VEC4 :
                    (type == "MAT4") ? TINYGLTF_TYPE_MAT4 : TINYGLTF_TYPE_SCALAR;

    if (minVals && maxVals) {
        for (int i = 0; i < componentCount; i++) {
            accessor.minValues.push_back(minVals[i]);
            accessor.maxValues.push_back(maxVals[i]);
        }
    }

    int accessorIndex = (int)gltf.accessors.size();
    gltf.accessors.push_back(accessor);
    return accessorIndex;
}

//------------------------------------------------------------------------------
// Helper: Create accessor for unsigned short indices
//------------------------------------------------------------------------------
static int CreateIndexAccessor(tinygltf::Model& gltf, tinygltf::Buffer& buffer,
                                const unsigned short* data, int count) {
    size_t offset = AddToBuffer(buffer, data, count * sizeof(unsigned short));
    AlignBuffer(buffer);

    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = offset;
    view.byteLength = count * sizeof(unsigned short);
    view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    int viewIndex = (int)gltf.bufferViews.size();
    gltf.bufferViews.push_back(view);

    tinygltf::Accessor accessor;
    accessor.bufferView = viewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    accessor.count = count;
    accessor.type = TINYGLTF_TYPE_SCALAR;

    int accessorIndex = (int)gltf.accessors.size();
    gltf.accessors.push_back(accessor);
    return accessorIndex;
}

//------------------------------------------------------------------------------
// Helper: Create accessor for joint indices (VEC4 of unsigned bytes)
//------------------------------------------------------------------------------
static int CreateJointsAccessor(tinygltf::Model& gltf, tinygltf::Buffer& buffer,
                                 const std::vector<uint8_t>& joints) {
    // Pack into VEC4 (joint index + 3 zeros for MDL's single-bone weighting)
    std::vector<uint8_t> packedJoints(joints.size() * 4);
    for (size_t i = 0; i < joints.size(); i++) {
        packedJoints[i * 4 + 0] = joints[i];
        packedJoints[i * 4 + 1] = 0;
        packedJoints[i * 4 + 2] = 0;
        packedJoints[i * 4 + 3] = 0;
    }

    size_t offset = AddToBuffer(buffer, packedJoints.data(), packedJoints.size());
    AlignBuffer(buffer);

    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = offset;
    view.byteLength = packedJoints.size();
    view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    int viewIndex = (int)gltf.bufferViews.size();
    gltf.bufferViews.push_back(view);

    tinygltf::Accessor accessor;
    accessor.bufferView = viewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    accessor.count = (int)joints.size();
    accessor.type = TINYGLTF_TYPE_VEC4;

    int accessorIndex = (int)gltf.accessors.size();
    gltf.accessors.push_back(accessor);
    return accessorIndex;
}

//------------------------------------------------------------------------------
// Helper: Create accessor for weights (VEC4 of floats, all [1,0,0,0])
//------------------------------------------------------------------------------
static int CreateWeightsAccessor(tinygltf::Model& gltf, tinygltf::Buffer& buffer,
                                  int vertexCount) {
    // MDL uses single-bone weighting, so weight is always [1, 0, 0, 0]
    std::vector<float> weights(vertexCount * 4);
    for (int i = 0; i < vertexCount; i++) {
        weights[i * 4 + 0] = 1.0f;
        weights[i * 4 + 1] = 0.0f;
        weights[i * 4 + 2] = 0.0f;
        weights[i * 4 + 3] = 0.0f;
    }

    size_t offset = AddToBuffer(buffer, weights.data(), weights.size() * sizeof(float));
    AlignBuffer(buffer);

    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = offset;
    view.byteLength = weights.size() * sizeof(float);
    view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    int viewIndex = (int)gltf.bufferViews.size();
    gltf.bufferViews.push_back(view);

    tinygltf::Accessor accessor;
    accessor.bufferView = viewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.count = vertexCount;
    accessor.type = TINYGLTF_TYPE_VEC4;

    int accessorIndex = (int)gltf.accessors.size();
    gltf.accessors.push_back(accessor);
    return accessorIndex;
}

//------------------------------------------------------------------------------
// Export implementation
//------------------------------------------------------------------------------
SkeletalExportResult ExportSkeletalGLTF(const SkeletalModel& model,
                                         const char* output_path,
                                         SkeletalExportOptions options) {
    SkeletalExportResult result = {};
    result.success = false;

    if (model.meshes.empty()) {
        snprintf(result.error_msg, sizeof(result.error_msg), "Model has no meshes");
        return result;
    }

    tinygltf::Model gltf;
    tinygltf::TinyGLTF writer;

    // Asset info
    gltf.asset.version = "2.0";
    gltf.asset.generator = "model_tool MDL Converter";

    // Create single buffer for all data
    tinygltf::Buffer buffer;
    gltf.buffers.push_back(buffer);

    // Node layout: mesh nodes first, then bone nodes
    // This avoids complex index manipulation during creation
    int meshNodeStart = 0;
    int boneNodeStart = (int)model.meshes.size();

    // Create scene
    tinygltf::Scene scene;
    scene.name = model.name.empty() ? "Scene" : model.name;

    // First, create placeholder mesh nodes (will be populated later)
    for (size_t m = 0; m < model.meshes.size(); m++) {
        tinygltf::Node meshNode;
        meshNode.name = model.meshes[m].name + "_node";
        gltf.nodes.push_back(meshNode);
        scene.nodes.push_back(meshNodeStart + (int)m);
    }

    // Create bone nodes with correct indices from the start
    std::vector<int> boneNodeIndices(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); b++) {
        const SkeletalBone& bone = model.bones[b];

        tinygltf::Node node;
        node.name = bone.name;

        // Set TRS
        node.translation = { bone.position.x, bone.position.y, bone.position.z };
        node.rotation = { bone.rotation.x, bone.rotation.y, bone.rotation.z, bone.rotation.w };
        node.scale = { bone.scale.x, bone.scale.y, bone.scale.z };

        boneNodeIndices[b] = boneNodeStart + (int)b;
        gltf.nodes.push_back(node);
    }

    // Set up bone hierarchy (children) and add root bones to scene
    for (size_t b = 0; b < model.bones.size(); b++) {
        int parent = model.bones[b].parent;
        if (parent >= 0 && parent < (int)model.bones.size()) {
            // Add as child of parent bone
            gltf.nodes[boneNodeIndices[parent]].children.push_back(boneNodeIndices[b]);
        } else {
            // Root bone - add to scene so skeleton is in scene hierarchy
            scene.nodes.push_back(boneNodeIndices[b]);
        }
    }

    // Create skin if we have bones
    int skinIndex = -1;
    if (!model.bones.empty()) {
        tinygltf::Skin skin;
        skin.name = "Armature";

        // Find root bone (first bone with no parent)
        for (size_t b = 0; b < model.bones.size(); b++) {
            if (model.bones[b].parent < 0) {
                skin.skeleton = boneNodeIndices[b];
                break;
            }
        }

        // Add all bones as joints
        for (size_t b = 0; b < model.bones.size(); b++) {
            skin.joints.push_back(boneNodeIndices[b]);
        }

        // Create inverse bind matrices accessor (no ARRAY_BUFFER target needed)
        std::vector<float> ibmData(model.bones.size() * 16);
        for (size_t b = 0; b < model.bones.size(); b++) {
            const Matrix& m = model.bones[b].inverseBindMatrix;
            // GLTF uses column-major order
            float* dst = &ibmData[b * 16];
            dst[0] = m.m0;  dst[1] = m.m1;  dst[2] = m.m2;  dst[3] = m.m3;
            dst[4] = m.m4;  dst[5] = m.m5;  dst[6] = m.m6;  dst[7] = m.m7;
            dst[8] = m.m8;  dst[9] = m.m9;  dst[10] = m.m10; dst[11] = m.m11;
            dst[12] = m.m12; dst[13] = m.m13; dst[14] = m.m14; dst[15] = m.m15;
        }

        skin.inverseBindMatrices = CreateFloatAccessor(gltf, gltf.buffers[0],
            ibmData.data(), (int)model.bones.size(), 16, "MAT4", nullptr, nullptr, false);

        skinIndex = (int)gltf.skins.size();
        gltf.skins.push_back(skin);
    }

    // Create meshes and populate mesh nodes
    for (size_t m = 0; m < model.meshes.size(); m++) {
        const SkinnedMesh& srcMesh = model.meshes[m];
        if (srcMesh.vertices.empty()) continue;

        // Prepare vertex data
        std::vector<float> positions(srcMesh.vertices.size() * 3);
        std::vector<float> normals(srcMesh.vertices.size() * 3);
        std::vector<float> texcoords(srcMesh.vertices.size() * 2);
        std::vector<uint8_t> joints(srcMesh.vertices.size());

        float minPos[3] = { 1e10f, 1e10f, 1e10f };
        float maxPos[3] = { -1e10f, -1e10f, -1e10f };

        for (size_t v = 0; v < srcMesh.vertices.size(); v++) {
            const SkinnedVertex& sv = srcMesh.vertices[v];

            positions[v * 3 + 0] = sv.position.x;
            positions[v * 3 + 1] = sv.position.y;
            positions[v * 3 + 2] = sv.position.z;

            minPos[0] = std::min(minPos[0], sv.position.x);
            minPos[1] = std::min(minPos[1], sv.position.y);
            minPos[2] = std::min(minPos[2], sv.position.z);
            maxPos[0] = std::max(maxPos[0], sv.position.x);
            maxPos[1] = std::max(maxPos[1], sv.position.y);
            maxPos[2] = std::max(maxPos[2], sv.position.z);

            normals[v * 3 + 0] = sv.normal.x;
            normals[v * 3 + 1] = sv.normal.y;
            normals[v * 3 + 2] = sv.normal.z;

            texcoords[v * 2 + 0] = sv.texcoord.x;
            texcoords[v * 2 + 1] = sv.texcoord.y;

            joints[v] = (uint8_t)sv.boneIndex;
        }

        // Create GLTF mesh
        tinygltf::Mesh gltfMesh;
        gltfMesh.name = srcMesh.name;

        tinygltf::Primitive prim;
        prim.mode = TINYGLTF_MODE_TRIANGLES;

        // Add attributes
        prim.attributes["POSITION"] = CreateFloatAccessor(gltf, gltf.buffers[0],
            positions.data(), (int)srcMesh.vertices.size(), 3, "VEC3", minPos, maxPos);
        prim.attributes["NORMAL"] = CreateFloatAccessor(gltf, gltf.buffers[0],
            normals.data(), (int)srcMesh.vertices.size(), 3, "VEC3");
        prim.attributes["TEXCOORD_0"] = CreateFloatAccessor(gltf, gltf.buffers[0],
            texcoords.data(), (int)srcMesh.vertices.size(), 2, "VEC2");

        // Add skinning attributes
        if (!model.bones.empty()) {
            prim.attributes["JOINTS_0"] = CreateJointsAccessor(gltf, gltf.buffers[0], joints);
            prim.attributes["WEIGHTS_0"] = CreateWeightsAccessor(gltf, gltf.buffers[0],
                (int)srcMesh.vertices.size());
        }

        // Add indices
        prim.indices = CreateIndexAccessor(gltf, gltf.buffers[0],
            srcMesh.indices.data(), (int)srcMesh.indices.size());

        // Material
        if (srcMesh.materialIndex >= 0 && srcMesh.materialIndex < (int)model.materials.size()) {
            prim.material = srcMesh.materialIndex;
        }

        gltfMesh.primitives.push_back(prim);

        int meshIndex = (int)gltf.meshes.size();
        gltf.meshes.push_back(gltfMesh);

        // Update the pre-created mesh node
        gltf.nodes[meshNodeStart + m].mesh = meshIndex;
        if (skinIndex >= 0) {
            gltf.nodes[meshNodeStart + m].skin = skinIndex;
        }
    }

    // Create materials
    for (size_t m = 0; m < model.materials.size(); m++) {
        const SkeletalMaterial& srcMat = model.materials[m];

        tinygltf::Material mat;
        mat.name = srcMat.name;

        // PBR metallic roughness
        mat.pbrMetallicRoughness.baseColorFactor = {
            srcMat.diffuseColor.r / 255.0,
            srcMat.diffuseColor.g / 255.0,
            srcMat.diffuseColor.b / 255.0,
            srcMat.diffuseColor.a / 255.0
        };
        mat.pbrMetallicRoughness.metallicFactor = 0.0;
        mat.pbrMetallicRoughness.roughnessFactor = 0.8;

        gltf.materials.push_back(mat);
    }

    // Create animations
    if (options.export_animations && !model.animations.empty()) {
        for (const SkeletalAnimation& srcAnim : model.animations) {
            if (srcAnim.tracks.empty()) continue;

            tinygltf::Animation anim;
            anim.name = srcAnim.name;

            for (const BoneAnimationTrack& track : srcAnim.tracks) {
                if (track.keyframes.empty()) continue;
                if (track.boneIndex < 0 || track.boneIndex >= (int)model.bones.size()) continue;

                int targetNode = boneNodeIndices[track.boneIndex];

                // Prepare keyframe data
                std::vector<float> times(track.keyframes.size());
                std::vector<float> translations(track.keyframes.size() * 3);
                std::vector<float> rotations(track.keyframes.size() * 4);

                float minTime = 0, maxTime = 0;
                for (size_t k = 0; k < track.keyframes.size(); k++) {
                    const BoneKeyframe& kf = track.keyframes[k];
                    times[k] = kf.time;
                    if (k == 0) minTime = kf.time;
                    maxTime = kf.time;

                    translations[k * 3 + 0] = kf.translation.x;
                    translations[k * 3 + 1] = kf.translation.y;
                    translations[k * 3 + 2] = kf.translation.z;

                    rotations[k * 4 + 0] = kf.rotation.x;
                    rotations[k * 4 + 1] = kf.rotation.y;
                    rotations[k * 4 + 2] = kf.rotation.z;
                    rotations[k * 4 + 3] = kf.rotation.w;
                }

                // Create time accessor (animation data should not have buffer target)
                int timeAccessor = CreateFloatAccessor(gltf, gltf.buffers[0],
                    times.data(), (int)times.size(), 1, "SCALAR", &minTime, &maxTime, false);

                // Translation sampler and channel
                {
                    tinygltf::AnimationSampler sampler;
                    sampler.input = timeAccessor;
                    sampler.output = CreateFloatAccessor(gltf, gltf.buffers[0],
                        translations.data(), (int)track.keyframes.size(), 3, "VEC3",
                        nullptr, nullptr, false);
                    sampler.interpolation = "LINEAR";

                    int samplerIndex = (int)anim.samplers.size();
                    anim.samplers.push_back(sampler);

                    tinygltf::AnimationChannel channel;
                    channel.sampler = samplerIndex;
                    channel.target_node = targetNode;
                    channel.target_path = "translation";
                    anim.channels.push_back(channel);
                }

                // Rotation sampler and channel
                {
                    tinygltf::AnimationSampler sampler;
                    sampler.input = timeAccessor;
                    sampler.output = CreateFloatAccessor(gltf, gltf.buffers[0],
                        rotations.data(), (int)track.keyframes.size(), 4, "VEC4",
                        nullptr, nullptr, false);
                    sampler.interpolation = "LINEAR";

                    int samplerIndex = (int)anim.samplers.size();
                    anim.samplers.push_back(sampler);

                    tinygltf::AnimationChannel channel;
                    channel.sampler = samplerIndex;
                    channel.target_node = targetNode;
                    channel.target_path = "rotation";
                    anim.channels.push_back(channel);
                }
            }

            if (!anim.channels.empty()) {
                gltf.animations.push_back(anim);
            }
        }
    }

    // Add scene
    gltf.scenes.push_back(scene);
    gltf.defaultScene = 0;

    // Write file
    bool writeSuccess;
    std::string err;
    std::string warn;

    if (options.binary) {
        writeSuccess = writer.WriteGltfSceneToFile(&gltf, output_path,
            true,   // embedImages
            true,   // embedBuffers
            true,   // prettyPrint
            true);  // writeBinary (.glb)
    } else {
        writeSuccess = writer.WriteGltfSceneToFile(&gltf, output_path,
            true,   // embedImages
            true,   // embedBuffers
            true,   // prettyPrint
            false); // writeBinary (.gltf)
    }

    if (!writeSuccess) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Failed to write GLTF file: %s", output_path);
        return result;
    }

    result.success = true;
    return result;
}

//------------------------------------------------------------------------------
// Convenience function
//------------------------------------------------------------------------------
bool ExportSkeletalGLB(const SkeletalModel& model, const char* output_path) {
    SkeletalExportOptions opts = SkeletalExportDefaultOptions();
    SkeletalExportResult result = ExportSkeletalGLTF(model, output_path, opts);
    return result.success;
}
