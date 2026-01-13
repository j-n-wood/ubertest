#include "unit_generator.h"

#include "asc_loader.h"
#include "gltf_bounds.h"
#include "gltf_export.h"
#include "mdl_loader.h"
#include "gltf_skeletal_export.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <set>

namespace {

struct SectionNode {
    const DroidSection* section = nullptr;
    int sectionIndex = 0;
    std::vector<SectionNode> children;
};

SectionNode buildSectionTree(const std::vector<DroidSection>& sections, int parentIndex) {
    SectionNode node;
    for (size_t i = 0; i < sections.size(); i++) {
        if (sections[i].parentIndex == parentIndex) {
            SectionNode child;
            child.section = &sections[i];
            child.sectionIndex = static_cast<int>(i);
            child.children = buildSectionTree(sections, static_cast<int>(i)).children;

            // Find children of this node
            for (size_t j = 0; j < sections.size(); j++) {
                if (sections[j].parentIndex == static_cast<int>(i)) {
                    SectionNode grandchild;
                    grandchild.section = &sections[j];
                    grandchild.sectionIndex = static_cast<int>(j);
                    // Recursively build subtree
                    for (size_t k = 0; k < sections.size(); k++) {
                        if (sections[k].parentIndex == static_cast<int>(j)) {
                            SectionNode ggchild;
                            ggchild.section = &sections[k];
                            ggchild.sectionIndex = static_cast<int>(k);
                            grandchild.children.push_back(ggchild);
                        }
                    }
                    child.children.push_back(grandchild);
                }
            }
            node.children.push_back(child);
        }
    }
    return node;
}

SectionNode findRoot(const std::vector<DroidSection>& sections) {
    SectionNode root;
    for (size_t i = 0; i < sections.size(); i++) {
        if (sections[i].parentIndex == -1) {
            root.section = &sections[i];
            root.sectionIndex = static_cast<int>(i);
            break;
        }
    }

    if (!root.section && !sections.empty()) {
        root.section = &sections[0];
        root.sectionIndex = 0;
    }

    // Build children
    if (root.section) {
        for (size_t i = 0; i < sections.size(); i++) {
            if (sections[i].parentIndex == root.sectionIndex) {
                SectionNode child;
                child.section = &sections[i];
                child.sectionIndex = static_cast<int>(i);

                // Find children of child
                for (size_t j = 0; j < sections.size(); j++) {
                    if (sections[j].parentIndex == static_cast<int>(i)) {
                        SectionNode grandchild;
                        grandchild.section = &sections[j];
                        grandchild.sectionIndex = static_cast<int>(j);
                        child.children.push_back(grandchild);
                    }
                }
                root.children.push_back(child);
            }
        }
    }

    return root;
}

void writeIndent(FILE* f, int depth) {
    for (int i = 0; i < depth; i++) {
        fprintf(f, "  ");
    }
}

void writeSectionJson(
    FILE* f,
    const SectionNode& node,
    const std::vector<RenderObject>& renderObjects,
    const std::string& modelPathPrefix,
    std::set<int>& processedModels,
    float scale,
    int depth,
    const fs::path& modelsOutputDir
) {
    if (!node.section) return;

    const RenderObject* ro = findRenderObject(renderObjects, node.section->renderIndex);
    std::string modelPath;
    std::string modelExt;
    if (ro && !ro->modelPath.empty() &&
        (ro->type == RenderObjectType::ModelASC || ro->type == RenderObjectType::ModelMDL)) {
        // Extract filename from path and change extension to .gltf/.glb
        fs::path p(ro->modelPath);
        modelExt = (ro->type == RenderObjectType::ModelMDL) ? ".glb" : ".gltf";
        modelPath = modelPathPrefix + "/" + p.stem().string() + modelExt;
    }

    writeIndent(f, depth);
    fprintf(f, "{\n");

    writeIndent(f, depth + 1);
    fprintf(f, "\"name\": \"section_%d\",\n", node.sectionIndex);

    if (!modelPath.empty()) {
        writeIndent(f, depth + 1);
        fprintf(f, "\"model\": \"%s\",\n", modelPath.c_str());
    }

    // offset: [x, y, z] where x/y are physics plane coords, z is vertical height
    // Source (x, y, z) -> swapped to (y, x, z) for new coordinate convention
    // where X is perpendicular to forward and Y is forward
    // X offset is negated to correct arm facing direction
    writeIndent(f, depth + 1);
    fprintf(f, "\"offset\": [%.6f, %.6f, %.6f],\n",
            -node.section->offset[1] * scale,
            node.section->offset[0] * scale,
            node.section->offset[2] * scale);

    // localRotation: yaw only (rz)
    writeIndent(f, depth + 1);
    fprintf(f, "\"localRotation\": %.6f,\n", node.section->rotation[2]);

    // scale
    writeIndent(f, depth + 1);
    fprintf(f, "\"scale\": [1, 1, 1],\n");

    // Physics - derive shape from model bounds (for child sections)
    if (!modelPath.empty() && ro) {
        fs::path gltfPath = modelsOutputDir / (fs::path(ro->modelPath).stem().string() + modelExt);
        GLTFBounds bounds = readGLTFBounds(gltfPath.string().c_str());
        PhysicsShapeInfo physShape = determinePhysicsShape(bounds, 0.15f);

        writeIndent(f, depth + 1);
        fprintf(f, "\"physics\": {\n");
        writeIndent(f, depth + 2);
        fprintf(f, "\"shape\": {\n");
        if (std::string(physShape.type) == "circle") {
            writeIndent(f, depth + 3);
            fprintf(f, "\"type\": \"circle\",\n");
            writeIndent(f, depth + 3);
            fprintf(f, "\"radius\": %.6f\n", physShape.radius);
        } else {
            writeIndent(f, depth + 3);
            fprintf(f, "\"type\": \"box\",\n");
            writeIndent(f, depth + 3);
            fprintf(f, "\"width\": %.6f,\n", physShape.width);
            writeIndent(f, depth + 3);
            fprintf(f, "\"height\": %.6f\n", physShape.height);
        }
        writeIndent(f, depth + 2);
        fprintf(f, "},\n");
        // Child sections use lower density and damping
        writeIndent(f, depth + 2);
        fprintf(f, "\"density\": 0.5,\n");
        writeIndent(f, depth + 2);
        fprintf(f, "\"friction\": 0.3,\n");
        writeIndent(f, depth + 2);
        fprintf(f, "\"restitution\": 0.1,\n");
        writeIndent(f, depth + 2);
        fprintf(f, "\"linearDamping\": 2.0,\n");
        writeIndent(f, depth + 2);
        fprintf(f, "\"angularDamping\": 4.0\n");
        writeIndent(f, depth + 1);
        fprintf(f, "},\n");
    }

    // children
    writeIndent(f, depth + 1);
    fprintf(f, "\"children\": [");

    if (!node.children.empty()) {
        fprintf(f, "\n");
        for (size_t i = 0; i < node.children.size(); i++) {
            writeSectionJson(f, node.children[i], renderObjects, modelPathPrefix, processedModels, scale, depth + 2, modelsOutputDir);
            if (i < node.children.size() - 1) {
                fprintf(f, ",");
            }
            fprintf(f, "\n");
        }
        writeIndent(f, depth + 1);
        fprintf(f, "]\n");
    } else {
        fprintf(f, "]\n");
    }

    writeIndent(f, depth);
    fprintf(f, "}");
}

} // namespace

UnitGeneratorResult generateUnits(
    const std::vector<DroidClass>& classes,
    const std::vector<RenderObject>& renderObjects,
    const UnitGeneratorOptions& options
) {
    UnitGeneratorResult result;
    result.success = true;

    // Create output directories
    std::error_code ec;
    fs::create_directories(options.outputDir, ec);
    fs::create_directories(options.modelsOutputDir, ec);

    // Track converted models to avoid duplicates
    std::set<std::string> convertedModels;
    std::set<std::string> unsupportedModelsSet;

    // Process each droid class
    for (const auto& droidClass : classes) {
        std::cout << "Processing Class " << droidClass.classId << "...\n";

        // Collect all render indices used by this class
        std::set<int> renderIndices;
        for (const auto& section : droidClass.sections) {
            renderIndices.insert(section.renderIndex);
        }

        // Convert models
        if (options.convertModels) {
            for (int renderIdx : renderIndices) {
                const RenderObject* ro = findRenderObject(renderObjects, renderIdx);
                if (!ro) continue;

                if (ro->type == RenderObjectType::ModelASC && !ro->modelPath.empty()) {
                    fs::path srcPath = options.sourceModelsDir / ro->modelPath;
                    fs::path outPath = options.modelsOutputDir / (fs::path(ro->modelPath).stem().string() + ".gltf");

                    std::string srcKey = srcPath.string();
                    if (convertedModels.count(srcKey)) {
                        result.modelsSkipped++;
                        continue;
                    }

                    if (options.skipExisting && fs::exists(outPath)) {
                        std::cout << "  Skipping " << outPath.filename().string() << " (already exists)\n";
                        convertedModels.insert(srcKey);
                        result.modelsSkipped++;
                        continue;
                    }

                    if (!fs::exists(srcPath)) {
                        std::cout << "  Warning: Model not found: " << srcPath.string() << "\n";
                        continue;
                    }

                    std::cout << "  Converting " << srcPath.filename().string() << " -> " << outPath.filename().string() << "\n";

                    // Load ASC
                    ASCLoadOptions loadOpts = ASCDefaultOptions();
                    loadOpts.scale = options.scale;
                    loadOpts.swap_yz = options.swapYZ;
                    loadOpts.skip_gpu_upload = true;

                    Model model = {};
                    ASCLoadResult loadResult = LoadASCEx(srcPath.string().c_str(), &model, loadOpts);

                    if (!loadResult.success) {
                        std::cout << "  Error loading " << srcPath.filename().string() << ": " << loadResult.error_msg << "\n";
                        continue;
                    }

                    // Export GLTF
                    // Note: texture paths in ASC files are relative like "../../textures/materials/file.jpg"
                    // After stripping "../" they become "textures/materials/file.jpg"
                    // So texture_fallback_dir should be parent of textures dir (e.g., uberdroid/)
                    GLTFExportOptions exportOpts = GLTFDefaultOptions();
                    std::string srcDirStr = srcPath.parent_path().string();
                    std::string texFallbackStr = options.textureSourceDir.parent_path().string();
                    std::string srcFilename = srcPath.filename().string();
                    exportOpts.source_dir = srcDirStr.c_str();
                    exportOpts.texture_fallback_dir = texFallbackStr.c_str();
                    exportOpts.model_hint = srcFilename.c_str();
                    exportOpts.texture_count = loadResult.material_count;
                    for (int i = 0; i < loadResult.material_count && i < GLTF_MAX_TEXTURES; i++) {
                        exportOpts.texture_paths[i] = loadResult.texture_paths[i];
                    }

                    fs::create_directories(outPath.parent_path(), ec);
                    GLTFExportResult exportResult = ExportGLTFEx(model, outPath.string().c_str(), exportOpts);

                    if (model.meshCount > 0) {
                        UnloadModel(model);
                    }

                    if (!exportResult.success) {
                        std::cout << "  Error exporting " << outPath.filename().string() << ": " << exportResult.error_msg << "\n";
                        continue;
                    }

                    convertedModels.insert(srcKey);
                    result.modelsConverted++;
                }
                else if (ro->type == RenderObjectType::ModelMDL && !ro->modelPath.empty()) {
                    // MDL file conversion using skeletal exporter
                    fs::path srcPath = options.sourceModelsDir / ro->modelPath;
                    fs::path outPath = options.modelsOutputDir / (fs::path(ro->modelPath).stem().string() + ".glb");

                    std::string srcKey = srcPath.string();
                    if (convertedModels.count(srcKey)) {
                        result.modelsSkipped++;
                        continue;
                    }

                    if (options.skipExisting && fs::exists(outPath)) {
                        std::cout << "  Skipping " << outPath.filename().string() << " (already exists)\n";
                        convertedModels.insert(srcKey);
                        result.modelsSkipped++;
                        continue;
                    }

                    if (!fs::exists(srcPath)) {
                        std::cout << "  Warning: Model not found: " << srcPath.string() << "\n";
                        continue;
                    }

                    std::cout << "  Converting " << srcPath.filename().string() << " -> " << outPath.filename().string() << "\n";

                    // Load MDL with default options (includes 180° Y rotation for correct orientation)
                    MDLLoadOptions loadOpts = MDLDefaultOptions();
                    // MDLDefaultOptions already sets scale=0.0254 and swap_yz=true which handles
                    // coordinate system conversion and the 180° Y rotation for forward orientation

                    MDLLoadResult loadResult = LoadMDL(srcPath.string().c_str(), loadOpts);

                    if (!loadResult.success) {
                        std::cout << "  Error loading " << srcPath.filename().string() << ": " << loadResult.error_msg << "\n";
                        continue;
                    }

                    std::cout << "    Loaded: " << loadResult.model.bones.size() << " bones, "
                              << loadResult.model.meshes.size() << " meshes, "
                              << loadResult.model.animations.size() << " animations\n";

                    // Export skeletal GLTF (GLB format)
                    fs::create_directories(outPath.parent_path(), ec);
                    SkeletalExportOptions exportOpts = SkeletalExportDefaultOptions();
                    exportOpts.binary = true;  // GLB format
                    exportOpts.export_animations = true;

                    SkeletalExportResult exportResult = ExportSkeletalGLTF(loadResult.model, outPath.string().c_str(), exportOpts);

                    if (!exportResult.success) {
                        std::cout << "  Error exporting " << outPath.filename().string() << ": " << exportResult.error_msg << "\n";
                        continue;
                    }

                    convertedModels.insert(srcKey);
                    result.modelsConverted++;
                }
                else if (ro->type == RenderObjectType::ModelMD2) {
                    // MD2 not yet supported
                    if (!ro->modelPath.empty() && !unsupportedModelsSet.count(ro->modelPath)) {
                        unsupportedModelsSet.insert(ro->modelPath);
                        result.unsupportedModels.push_back(ro->modelPath);
                    }
                }
            }
        }

        // Generate JSON
        std::string jsonFilename = "droid_class_" + std::to_string(droidClass.classId) + ".json";
        fs::path jsonPath = options.outputDir / jsonFilename;
        FILE* jsonFile = fopen(jsonPath.string().c_str(), "w");
        if (!jsonFile) {
            std::cout << "  Error: Failed to create " << jsonPath.string() << "\n";
            continue;
        }

        fprintf(jsonFile, "{\n");
        fprintf(jsonFile, "  \"name\": \"Class %d\",\n", droidClass.classId);
        fprintf(jsonFile, "  \"id\": \"droid_class_%d\",\n", droidClass.classId);
        fprintf(jsonFile, "  \"collisionRadius\": %.6f,\n", droidClass.collideRadius * options.radiusScale);
        fprintf(jsonFile, "  \"proximityRadius\": %.6f,\n", droidClass.proximityRadius * options.radiusScale);

        // Properties
        fprintf(jsonFile, "  \"properties\": {\n");
        fprintf(jsonFile, "    \"classId\": %d,\n", droidClass.classId);
        fprintf(jsonFile, "    \"typeCode\": %d,\n", droidClass.typeCode);
        fprintf(jsonFile, "    \"energy\": %d,\n", droidClass.energyCost);
        fprintf(jsonFile, "    \"armour\": %.1f,\n", droidClass.armour);
        fprintf(jsonFile, "    \"weapon\": %.1f,\n", droidClass.weapon);
        fprintf(jsonFile, "    \"droidType\": %d,\n", droidClass.droidType);
        fprintf(jsonFile, "    \"driveType\": %d,\n", droidClass.driveType);
        fprintf(jsonFile, "    \"brainType\": %d,\n", droidClass.brainType);
        fprintf(jsonFile, "    \"hasTurret\": %s,\n", droidClass.hasTurret ? "true" : "false");

        // Escape description for JSON
        std::string escapedDesc;
        for (char c : droidClass.description) {
            if (c == '"') escapedDesc += "\\\"";
            else if (c == '\\') escapedDesc += "\\\\";
            else if (c == '\n') escapedDesc += "\\n";
            else escapedDesc += c;
        }
        fprintf(jsonFile, "    \"description\": \"%s\"\n", escapedDesc.c_str());
        fprintf(jsonFile, "  },\n");

        // Root section
        fprintf(jsonFile, "  \"rootSection\": ");

        std::set<int> processedModels;
        SectionNode root = findRoot(droidClass.sections);

        if (root.section) {
            // Add physics to root
            const RenderObject* rootRo = findRenderObject(renderObjects, root.section->renderIndex);
            std::string modelPath;
            std::string modelExt;
            if (rootRo && !rootRo->modelPath.empty()) {
                if (rootRo->type == RenderObjectType::ModelASC) {
                    fs::path p(rootRo->modelPath);
                    modelExt = ".gltf";
                    modelPath = "models/" + p.stem().string() + modelExt;
                } else if (rootRo->type == RenderObjectType::ModelMDL) {
                    fs::path p(rootRo->modelPath);
                    modelExt = ".glb";
                    modelPath = "models/" + p.stem().string() + modelExt;
                }
            }

            fprintf(jsonFile, "{\n");
            fprintf(jsonFile, "    \"name\": \"section_%d\",\n", root.sectionIndex);
            if (!modelPath.empty()) {
                fprintf(jsonFile, "    \"model\": \"%s\",\n", modelPath.c_str());
            }
            // offset: [x, y, z] swapped (y, x, z) for new coordinate convention, X negated
            fprintf(jsonFile, "    \"offset\": [%.6f, %.6f, %.6f],\n",
                    -root.section->offset[1] * options.scale,
                    root.section->offset[0] * options.scale,
                    root.section->offset[2] * options.scale);
            fprintf(jsonFile, "    \"localRotation\": %.6f,\n", root.section->rotation[2]);
            fprintf(jsonFile, "    \"scale\": [1, 1, 1],\n");

            // Physics - derive shape from model bounds
            PhysicsShapeInfo physShape;
            if (!modelPath.empty()) {
                // Construct full path to the GLTF/GLB file
                fs::path gltfPath = options.modelsOutputDir / (fs::path(rootRo->modelPath).stem().string() + modelExt);
                GLTFBounds bounds = readGLTFBounds(gltfPath.string().c_str());
                physShape = determinePhysicsShape(bounds, 0.25f);

                if (bounds.valid) {
                    std::cout << "  Model bounds: [" << bounds.sizeX() << " x " << bounds.sizeZ() << "] -> ";
                    if (std::string(physShape.type) == "circle") {
                        std::cout << "circle r=" << physShape.radius << "\n";
                    } else {
                        std::cout << "box " << physShape.width << " x " << physShape.height << "\n";
                    }
                }
            } else {
                // No model, use default circle
                physShape.type = "circle";
                physShape.radius = 0.25f;
            }

            fprintf(jsonFile, "    \"physics\": {\n");
            fprintf(jsonFile, "      \"shape\": {\n");
            if (std::string(physShape.type) == "circle") {
                fprintf(jsonFile, "        \"type\": \"circle\",\n");
                fprintf(jsonFile, "        \"radius\": %.6f\n", physShape.radius);
            } else {
                fprintf(jsonFile, "        \"type\": \"box\",\n");
                fprintf(jsonFile, "        \"width\": %.6f,\n", physShape.width);
                fprintf(jsonFile, "        \"height\": %.6f\n", physShape.height);
            }
            fprintf(jsonFile, "      },\n");
            fprintf(jsonFile, "      \"density\": 1.0,\n");
            fprintf(jsonFile, "      \"friction\": 0.3,\n");
            fprintf(jsonFile, "      \"restitution\": 0.0,\n");
            fprintf(jsonFile, "      \"linearDamping\": 4.0,\n");
            fprintf(jsonFile, "      \"angularDamping\": 8.0\n");
            fprintf(jsonFile, "    },\n");

            // Children
            fprintf(jsonFile, "    \"children\": [");
            if (!root.children.empty()) {
                fprintf(jsonFile, "\n");
                for (size_t i = 0; i < root.children.size(); i++) {
                    writeSectionJson(jsonFile, root.children[i], renderObjects, "models", processedModels, options.scale, 3, options.modelsOutputDir);
                    if (i < root.children.size() - 1) {
                        fprintf(jsonFile, ",");
                    }
                    fprintf(jsonFile, "\n");
                }
                fprintf(jsonFile, "    ]\n");
            } else {
                fprintf(jsonFile, "]\n");
            }
            fprintf(jsonFile, "  }\n");
        } else {
            fprintf(jsonFile, "null\n");
        }

        fprintf(jsonFile, "}\n");
        fclose(jsonFile);

        result.unitsGenerated++;
        std::cout << "  Generated " << jsonPath.filename().string() << "\n";
    }

    // Write unsupported models list
    if (!result.unsupportedModels.empty()) {
        fs::path unsupportedPath = options.outputDir / "unsupported_models.txt";
        FILE* unsupportedFile = fopen(unsupportedPath.string().c_str(), "w");
        if (unsupportedFile) {
            fprintf(unsupportedFile, "# Models requiring future conversion support\n");
            fprintf(unsupportedFile, "# These are MD2 or MDL format models\n\n");
            for (const auto& path : result.unsupportedModels) {
                fprintf(unsupportedFile, "%s\n", path.c_str());
            }
            fclose(unsupportedFile);
            std::cout << "\nWrote unsupported models list to " << unsupportedPath.string() << "\n";
        }
    }

    return result;
}
