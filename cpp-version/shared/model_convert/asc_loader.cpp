#include "asc_loader.h"
#include "rlgl.h"
#include "raymath.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cfloat>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
#define INCHES_TO_METERS 0.0254f

//------------------------------------------------------------------------------
// Internal structures for parsing (raw file data, no transforms applied)
//------------------------------------------------------------------------------
struct ASCVertex {
    float x, y, z;      // Position as read from file
    float s, t;         // Texture coordinates
};

struct ASCNormal {
    float x, y, z;      // Normal vector as read from file
};

struct ASCTriangle {
    int v1, v2, v3;     // Vertex indices
    int n1, n2, n3;     // Normal indices
};

struct ASCMesh {
    char name[64];
    int material_id;
    std::vector<ASCVertex> vertices;
    std::vector<ASCNormal> normals;
    std::vector<ASCTriangle> triangles;
};

struct ASCMaterial {
    char name[64];
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float emissive[4];
    float shininess;
    float transparency;
    char texture_path[256];
    char alphamap_path[256];
};

//------------------------------------------------------------------------------
// Skip whitespace and empty lines
//------------------------------------------------------------------------------
static bool read_line(FILE* f, char* buf, int buf_size, int* line_num) {
    while (fgets(buf, buf_size, f)) {
        (*line_num)++;
        // Skip empty lines and comment lines
        char* p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\n' && *p != '\r' && *p != '\0') {
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
// Parse ASC file into internal structures (no transforms, raw data)
//------------------------------------------------------------------------------
static bool parse_asc_file(const char* filepath,
                           std::vector<ASCMesh>& meshes,
                           std::vector<ASCMaterial>& materials,
                           ASCLoadResult* result) {
    FILE* f = fopen(filepath, "r");
    if (!f) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Failed to open file: %s", filepath);
        result->error_line = 0;
        return false;
    }

    char buf[512];
    int line_num = 0;
    int mesh_count = 0;
    int material_count = 0;

    // Read header - look for "Meshes: N"
    while (read_line(f, buf, sizeof(buf), &line_num)) {
        if (strncmp(buf, "Meshes:", 7) == 0) {
            sscanf(buf, "Meshes: %d", &mesh_count);
            break;
        }
    }

    if (mesh_count == 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No meshes found in file");
        result->error_line = line_num;
        fclose(f);
        return false;
    }

    TraceLog(LOG_INFO, "ASC: Found %d meshes", mesh_count);

    // Parse each mesh
    for (int m = 0; m < mesh_count; m++) {
        ASCMesh mesh = {0};
        int flags, vert_count;

        // Read mesh header: "name" flags material_id
        if (!read_line(f, buf, sizeof(buf), &line_num)) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Unexpected end of file reading mesh %d header", m);
            result->error_line = line_num;
            fclose(f);
            return false;
        }

        // Parse: "name" flags material_id
        if (sscanf(buf, "\"%63[^\"]\" %d %d", mesh.name, &flags, &mesh.material_id) != 3) {
            // Try without quotes
            if (sscanf(buf, "%63s %d %d", mesh.name, &flags, &mesh.material_id) != 3) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to parse mesh header: %s", buf);
                result->error_line = line_num;
                fclose(f);
                return false;
            }
        }

        // Read vertex count
        if (!read_line(f, buf, sizeof(buf), &line_num)) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Unexpected end of file reading vertex count for mesh %d", m);
            result->error_line = line_num;
            fclose(f);
            return false;
        }
        sscanf(buf, "%d", &vert_count);

        TraceLog(LOG_DEBUG, "ASC: Mesh '%s' has %d vertices", mesh.name, vert_count);

        // Read vertices - store raw, no transforms
        // Format: flag x y z s t bone_id
        for (int v = 0; v < vert_count; v++) {
            if (!read_line(f, buf, sizeof(buf), &line_num)) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Unexpected end of file reading vertex %d of mesh %d", v, m);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            ASCVertex vert = {0};
            int flag, bone_id;

            if (sscanf(buf, "%d %f %f %f %f %f %d",
                       &flag, &vert.x, &vert.y, &vert.z, &vert.s, &vert.t, &bone_id) != 7) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to parse vertex: %s", buf);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            mesh.vertices.push_back(vert);
        }

        // Read normal count
        int norm_count;
        if (!read_line(f, buf, sizeof(buf), &line_num)) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Unexpected end of file reading normal count for mesh %d", m);
            result->error_line = line_num;
            fclose(f);
            return false;
        }
        sscanf(buf, "%d", &norm_count);

        TraceLog(LOG_DEBUG, "ASC: Mesh '%s' has %d normals", mesh.name, norm_count);

        // Read normals - store raw, no transforms
        // Format: nx ny nz
        for (int n = 0; n < norm_count; n++) {
            if (!read_line(f, buf, sizeof(buf), &line_num)) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Unexpected end of file reading normal %d of mesh %d", n, m);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            ASCNormal norm = {0};

            if (sscanf(buf, "%f %f %f", &norm.x, &norm.y, &norm.z) != 3) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to parse normal: %s", buf);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            mesh.normals.push_back(norm);
        }

        // Read triangle count
        int tri_count;
        if (!read_line(f, buf, sizeof(buf), &line_num)) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Unexpected end of file reading triangle count for mesh %d", m);
            result->error_line = line_num;
            fclose(f);
            return false;
        }
        sscanf(buf, "%d", &tri_count);

        TraceLog(LOG_DEBUG, "ASC: Mesh '%s' has %d triangles", mesh.name, tri_count);

        // Read triangles
        // Format: flag v1 v2 v3 n1 n2 n3 smoothing_group
        for (int t = 0; t < tri_count; t++) {
            if (!read_line(f, buf, sizeof(buf), &line_num)) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Unexpected end of file reading triangle %d of mesh %d", t, m);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            ASCTriangle tri = {0};
            int flag, smoothing;

            if (sscanf(buf, "%d %d %d %d %d %d %d %d",
                       &flag, &tri.v1, &tri.v2, &tri.v3,
                       &tri.n1, &tri.n2, &tri.n3, &smoothing) != 8) {
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to parse triangle: %s", buf);
                result->error_line = line_num;
                fclose(f);
                return false;
            }

            mesh.triangles.push_back(tri);
        }

        meshes.push_back(mesh);
    }

    // Parse materials section (optional)
    while (read_line(f, buf, sizeof(buf), &line_num)) {
        if (strncmp(buf, "Materials:", 10) == 0) {
            sscanf(buf, "Materials: %d", &material_count);
            break;
        }
        if (strncmp(buf, "Bones:", 6) == 0) {
            // No materials section, we're at bones
            break;
        }
    }

    TraceLog(LOG_INFO, "ASC: Found %d materials", material_count);

    for (int m = 0; m < material_count; m++) {
        ASCMaterial mat = {0};

        // Material name
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "\"%63[^\"]\"", mat.name);

        // Ambient (4 floats)
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f %f %f %f", &mat.ambient[0], &mat.ambient[1], &mat.ambient[2], &mat.ambient[3]);

        // Diffuse (4 floats)
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f %f %f %f", &mat.diffuse[0], &mat.diffuse[1], &mat.diffuse[2], &mat.diffuse[3]);

        // Specular (4 floats)
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f %f %f %f", &mat.specular[0], &mat.specular[1], &mat.specular[2], &mat.specular[3]);

        // Emissive (4 floats)
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f %f %f %f", &mat.emissive[0], &mat.emissive[1], &mat.emissive[2], &mat.emissive[3]);

        // Shininess
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f", &mat.shininess);

        // Transparency
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "%f", &mat.transparency);

        // Texture path
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "\"%255[^\"]\"", mat.texture_path);

        // Alphamap path
        if (!read_line(f, buf, sizeof(buf), &line_num)) break;
        sscanf(buf, "\"%255[^\"]\"", mat.alphamap_path);

        materials.push_back(mat);
    }

    fclose(f);
    result->success = true;
    return true;
}

//------------------------------------------------------------------------------
// Build Raylib Model from parsed ASC data with transforms
//------------------------------------------------------------------------------
static Model build_raylib_model(const std::vector<ASCMesh>& asc_meshes,
                                const std::vector<ASCMaterial>& asc_materials,
                                ASCLoadOptions options,
                                ASCLoadResult* result) {
    Model model = {0};

    if (asc_meshes.empty()) {
        return model;
    }

    // Log transform options
    TraceLog(LOG_INFO, "ASC: Transform options - scale: %.4f, swap_yz: %s, flip_winding: %s",
             options.scale,
             options.swap_yz ? "yes" : "no",
             options.flip_winding ? "yes" : "no");

    // Initialize bounds tracking
    result->bounds_min = (Vector3){ FLT_MAX, FLT_MAX, FLT_MAX };
    result->bounds_max = (Vector3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };

    model.meshCount = (int)asc_meshes.size();
    model.meshes = (Mesh*)RL_CALLOC(model.meshCount, sizeof(Mesh));

    model.materialCount = asc_materials.empty() ? 1 : (int)asc_materials.size();
    model.materials = (Material*)RL_CALLOC(model.materialCount, sizeof(Material));
    model.meshMaterial = (int*)RL_CALLOC(model.meshCount, sizeof(int));

    // Initialize materials with full ASC properties
    for (int i = 0; i < model.materialCount; i++) {
        // Initialize material (avoid LoadMaterialDefault for headless mode)
        // MAX_MATERIAL_MAPS is 12 in Raylib's rmodels.c
        const int MATERIAL_MAP_COUNT = 12;
        if (options.skip_gpu_upload) {
            // Manual material initialization without GL context
            model.materials[i] = {0};
            model.materials[i].maps = (MaterialMap*)RL_CALLOC(MATERIAL_MAP_COUNT, sizeof(MaterialMap));
            for (int j = 0; j < MATERIAL_MAP_COUNT; j++) {
                model.materials[i].maps[j].color = WHITE;
                model.materials[i].maps[j].value = 0.0f;
            }
        } else {
            model.materials[i] = LoadMaterialDefault();
        }

        if (i < (int)asc_materials.size()) {
            const ASCMaterial& asc_mat = asc_materials[i];

            // Diffuse color -> ALBEDO map
            model.materials[i].maps[MATERIAL_MAP_ALBEDO].color = (Color){
                (unsigned char)(asc_mat.diffuse[0] * 255),
                (unsigned char)(asc_mat.diffuse[1] * 255),
                (unsigned char)(asc_mat.diffuse[2] * 255),
                (unsigned char)(asc_mat.diffuse[3] * 255)
            };

            // Specular color -> SPECULAR map (for colSpecular uniform)
            // RGB = specular color, A = normalized shininess for shader
            model.materials[i].maps[MATERIAL_MAP_SPECULAR].color = (Color){
                (unsigned char)(asc_mat.specular[0] * 255),
                (unsigned char)(asc_mat.specular[1] * 255),
                (unsigned char)(asc_mat.specular[2] * 255),
                (unsigned char)(asc_mat.shininess / 128.0f * 255)  // Store shininess in alpha
            };
            model.materials[i].maps[MATERIAL_MAP_SPECULAR].value = asc_mat.shininess / 128.0f;

            // Also store in METALNESS for PBR conversion during GLTF export
            model.materials[i].maps[MATERIAL_MAP_METALNESS].color = (Color){
                (unsigned char)(asc_mat.specular[0] * 255),
                (unsigned char)(asc_mat.specular[1] * 255),
                (unsigned char)(asc_mat.specular[2] * 255),
                (unsigned char)(asc_mat.specular[3] * 255)
            };
            model.materials[i].maps[MATERIAL_MAP_METALNESS].value = asc_mat.shininess / 128.0f;

            // Roughness derived from shininess (high shininess = low roughness)
            model.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 1.0f - (asc_mat.shininess / 128.0f);

            // Emissive color -> EMISSION map
            model.materials[i].maps[MATERIAL_MAP_EMISSION].color = (Color){
                (unsigned char)(asc_mat.emissive[0] * 255),
                (unsigned char)(asc_mat.emissive[1] * 255),
                (unsigned char)(asc_mat.emissive[2] * 255),
                (unsigned char)(asc_mat.emissive[3] * 255)
            };

            // Store original ASC properties in params[] for shader access
            // params[0] = shininess (original 0-128 value)
            // params[1] = transparency (1.0 = opaque)
            // params[2] = reserved (env map flag in future)
            // params[3] = reserved
            model.materials[i].params[0] = asc_mat.shininess;
            model.materials[i].params[1] = asc_mat.transparency;
            model.materials[i].params[2] = 0.0f;
            model.materials[i].params[3] = 0.0f;

            TraceLog(LOG_DEBUG, "ASC: Material '%s' - diffuse(%.2f,%.2f,%.2f) specular(%.2f,%.2f,%.2f) shininess:%.1f",
                     asc_mat.name,
                     asc_mat.diffuse[0], asc_mat.diffuse[1], asc_mat.diffuse[2],
                     asc_mat.specular[0], asc_mat.specular[1], asc_mat.specular[2],
                     asc_mat.shininess);
        } else {
            // Default gray material
            model.materials[i].maps[MATERIAL_MAP_ALBEDO].color = (Color){180, 180, 180, 255};
            model.materials[i].maps[MATERIAL_MAP_SPECULAR].color = (Color){0, 0, 0, 0};  // No specular, alpha=0 for no shininess
            model.materials[i].maps[MATERIAL_MAP_SPECULAR].value = 0.0f;
            model.materials[i].maps[MATERIAL_MAP_METALNESS].color = (Color){0, 0, 0, 255};
            model.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
            model.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 1.0f;
            model.materials[i].maps[MATERIAL_MAP_EMISSION].color = (Color){0, 0, 0, 255};
            model.materials[i].params[0] = 0.0f;
            model.materials[i].params[1] = 1.0f;
            model.materials[i].params[2] = 0.0f;
            model.materials[i].params[3] = 0.0f;
        }
    }

    // Build each mesh
    for (int m = 0; m < model.meshCount; m++) {
        const ASCMesh& asc_mesh = asc_meshes[m];
        Mesh& mesh = model.meshes[m];

        // Assign material
        model.meshMaterial[m] = (asc_mesh.material_id >= 0 &&
                                  asc_mesh.material_id < model.materialCount)
                                 ? asc_mesh.material_id : 0;

        // Count output vertices - we expand indexed normals to per-vertex
        int tri_count = (int)asc_mesh.triangles.size();
        int out_vertex_count = tri_count * 3;

        mesh.vertexCount = out_vertex_count;
        mesh.triangleCount = tri_count;

        // Allocate vertex data
        mesh.vertices = (float*)RL_CALLOC(out_vertex_count * 3, sizeof(float));
        mesh.normals = (float*)RL_CALLOC(out_vertex_count * 3, sizeof(float));
        mesh.texcoords = (float*)RL_CALLOC(out_vertex_count * 2, sizeof(float));
        mesh.indices = (unsigned short*)RL_CALLOC(out_vertex_count, sizeof(unsigned short));

        // Fill vertex data from triangles
        int out_idx = 0;
        for (int t = 0; t < tri_count; t++) {
            const ASCTriangle& tri = asc_mesh.triangles[t];

            // Winding order: optionally swap v2 and v3
            int vert_indices[3];
            int norm_indices[3];

            if (options.flip_winding) {
                // v1, v2, v3 -> v1, v3, v2
                vert_indices[0] = tri.v1; vert_indices[1] = tri.v3; vert_indices[2] = tri.v2;
                norm_indices[0] = tri.n1; norm_indices[1] = tri.n3; norm_indices[2] = tri.n2;
            } else {
                // Keep original order
                vert_indices[0] = tri.v1; vert_indices[1] = tri.v2; vert_indices[2] = tri.v3;
                norm_indices[0] = tri.n1; norm_indices[1] = tri.n2; norm_indices[2] = tri.n3;
            }

            for (int i = 0; i < 3; i++) {
                int vi = vert_indices[i];
                int ni = norm_indices[i];

                // Bounds check
                if (vi < 0 || vi >= (int)asc_mesh.vertices.size()) {
                    TraceLog(LOG_WARNING, "ASC: Vertex index %d out of bounds in mesh '%s'",
                             vi, asc_mesh.name);
                    vi = 0;
                }
                if (ni < 0 || ni >= (int)asc_mesh.normals.size()) {
                    TraceLog(LOG_WARNING, "ASC: Normal index %d out of bounds in mesh '%s'",
                             ni, asc_mesh.name);
                    ni = 0;
                }

                const ASCVertex& v = asc_mesh.vertices[vi];
                const ASCNormal& n = asc_mesh.normals[ni];

                // Apply transforms to position
                float px, py, pz;
                if (options.swap_yz) {
                    // File (x, y, z) -> Output (x, z, y) - swap Y and Z
                    px = v.x * options.scale;
                    py = v.z * options.scale;
                    pz = v.y * options.scale;
                } else {
                    px = v.x * options.scale;
                    py = v.y * options.scale;
                    pz = v.z * options.scale;
                }

                mesh.vertices[out_idx * 3 + 0] = px;
                mesh.vertices[out_idx * 3 + 1] = py;
                mesh.vertices[out_idx * 3 + 2] = pz;

                // Track bounds
                if (px < result->bounds_min.x) result->bounds_min.x = px;
                if (py < result->bounds_min.y) result->bounds_min.y = py;
                if (pz < result->bounds_min.z) result->bounds_min.z = pz;
                if (px > result->bounds_max.x) result->bounds_max.x = px;
                if (py > result->bounds_max.y) result->bounds_max.y = py;
                if (pz > result->bounds_max.z) result->bounds_max.z = pz;

                // Apply transforms to normal (no scaling, just axis swap)
                float nx, ny, nz;
                if (options.swap_yz) {
                    nx = n.x;
                    ny = n.z;
                    nz = n.y;
                } else {
                    nx = n.x;
                    ny = n.y;
                    nz = n.z;
                }

                mesh.normals[out_idx * 3 + 0] = nx;
                mesh.normals[out_idx * 3 + 1] = ny;
                mesh.normals[out_idx * 3 + 2] = nz;

                // Texcoord (unchanged)
                mesh.texcoords[out_idx * 2 + 0] = v.s;
                mesh.texcoords[out_idx * 2 + 1] = v.t;

                // Index (sequential since we expanded)
                mesh.indices[out_idx] = (unsigned short)out_idx;

                out_idx++;
            }
        }

        // Upload mesh to GPU (skip for headless conversion)
        if (!options.skip_gpu_upload) {
            UploadMesh(&mesh, false);
        }

        TraceLog(LOG_INFO, "ASC: Built mesh '%s' with %d vertices, %d triangles",
                 asc_mesh.name, mesh.vertexCount, mesh.triangleCount);
    }

    // Log bounding box
    TraceLog(LOG_INFO, "ASC: Bounds min: (%.3f, %.3f, %.3f)",
             result->bounds_min.x, result->bounds_min.y, result->bounds_min.z);
    TraceLog(LOG_INFO, "ASC: Bounds max: (%.3f, %.3f, %.3f)",
             result->bounds_max.x, result->bounds_max.y, result->bounds_max.z);

    Vector3 size = {
        result->bounds_max.x - result->bounds_min.x,
        result->bounds_max.y - result->bounds_min.y,
        result->bounds_max.z - result->bounds_min.z
    };
    TraceLog(LOG_INFO, "ASC: Size: (%.3f, %.3f, %.3f)", size.x, size.y, size.z);

    // Set up transform (identity)
    model.transform = MatrixIdentity();

    return model;
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------
ASCLoadOptions ASCDefaultOptions(void) {
    ASCLoadOptions options;
    options.scale = INCHES_TO_METERS;  // Default: inches to meters
    options.swap_yz = false;           // Default: no axis swap
    options.flip_winding = false;      // Default: no winding flip
    options.skip_gpu_upload = false;   // Default: upload to GPU
    return options;
}

Model LoadASC(const char* filepath) {
    ASCLoadOptions options = ASCDefaultOptions();
    return LoadASCWithOptions(filepath, options);
}

Model LoadASCWithOptions(const char* filepath, ASCLoadOptions options) {
    Model model = {0};
    LoadASCEx(filepath, &model, options);
    return model;
}

ASCLoadResult LoadASCEx(const char* filepath, Model* out_model, ASCLoadOptions options) {
    ASCLoadResult result = {0};
    result.success = false;
    result.error_line = 0;
    result.error_msg[0] = '\0';
    result.bounds_min = (Vector3){0, 0, 0};
    result.bounds_max = (Vector3){0, 0, 0};
    result.material_count = 0;
    for (int i = 0; i < ASC_MAX_MATERIALS; i++) {
        result.texture_paths[i][0] = '\0';
    }

    std::vector<ASCMesh> meshes;
    std::vector<ASCMaterial> materials;

    if (!parse_asc_file(filepath, meshes, materials, &result)) {
        TraceLog(LOG_ERROR, "ASC: Failed to parse '%s': %s (line %d)",
                 filepath, result.error_msg, result.error_line);
        *out_model = (Model){0};
        return result;
    }

    *out_model = build_raylib_model(meshes, materials, options, &result);

    // Copy texture paths to result for use in export
    result.material_count = (int)materials.size();
    for (int i = 0; i < (int)materials.size() && i < ASC_MAX_MATERIALS; i++) {
        strncpy(result.texture_paths[i], materials[i].texture_path, sizeof(result.texture_paths[i]) - 1);
        result.texture_paths[i][sizeof(result.texture_paths[i]) - 1] = '\0';
        if (result.texture_paths[i][0] != '\0') {
            TraceLog(LOG_DEBUG, "ASC: Material %d texture: %s", i, result.texture_paths[i]);
        }
    }

    if (out_model->meshCount > 0) {
        result.success = true;
        TraceLog(LOG_INFO, "ASC: Loaded '%s' with %d meshes, %d materials",
                 filepath, out_model->meshCount, result.material_count);
    } else {
        snprintf(result.error_msg, sizeof(result.error_msg), "No valid meshes built");
        result.success = false;
    }

    return result;
}
