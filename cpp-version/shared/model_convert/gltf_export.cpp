#include "gltf_export.h"
#include "gltf_bounds.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <cfloat>
#include <sys/stat.h>
#include <errno.h>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Base64 encoding for embedded binary data
//------------------------------------------------------------------------------
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char* data, size_t length) {
    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < length) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < length) n |= data[i + 2];

        result += base64_chars[(n >> 18) & 0x3F];
        result += base64_chars[(n >> 12) & 0x3F];
        result += (i + 1 < length) ? base64_chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < length) ? base64_chars[n & 0x3F] : '=';
    }

    return result;
}

//------------------------------------------------------------------------------
// Helper to write JSON with proper formatting
//------------------------------------------------------------------------------
class JsonWriter {
public:
    std::string buffer;
    int indent_level = 0;
    bool first_element = true;

    // Begin an object as a standalone value (not in array context)
    void begin_object() {
        buffer += "{\n";
        indent_level++;
        first_element = true;
    }

    // Begin an object as an array element
    void begin_array_object() {
        if (!first_element) buffer += ",\n";
        write_indent();
        buffer += "{\n";
        indent_level++;
        first_element = true;
    }

    void end_object() {
        buffer += "\n";
        indent_level--;
        write_indent();
        buffer += "}";
        first_element = false;
    }

    void begin_array() {
        if (!first_element) buffer += ",";
        buffer += "[\n";
        indent_level++;
        first_element = true;
    }

    void end_array() {
        buffer += "\n";
        indent_level--;
        write_indent();
        buffer += "]";
        first_element = false;
    }

    void key(const char* name) {
        if (!first_element) buffer += ",\n";
        write_indent();
        buffer += "\"";
        buffer += name;
        buffer += "\": ";
        first_element = false;
    }

    void key_object(const char* name) {
        key(name);
        first_element = true;
        buffer += "{\n";
        indent_level++;
    }

    void key_array(const char* name) {
        key(name);
        first_element = true;
        buffer += "[\n";
        indent_level++;
    }

    void value_string(const char* val) {
        buffer += "\"";
        // Escape special characters
        for (const char* p = val; *p; p++) {
            switch (*p) {
                case '"': buffer += "\\\""; break;
                case '\\': buffer += "\\\\"; break;
                case '\n': buffer += "\\n"; break;
                case '\r': buffer += "\\r"; break;
                case '\t': buffer += "\\t"; break;
                default: buffer += *p; break;
            }
        }
        buffer += "\"";
    }

    void value_int(int val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", val);
        buffer += buf;
    }

    void value_float(float val) {
        char buf[48];
        // Use enough precision for IEEE 754 single-precision float round-trip (9 digits)
        // This ensures min/max bounds match actual binary data exactly
        snprintf(buf, sizeof(buf), "%.9g", val);
        buffer += buf;
    }

    void value_bool(bool val) {
        buffer += val ? "true" : "false";
    }

    void array_element() {
        if (!first_element) buffer += ",\n";
        write_indent();
        first_element = false;
    }

    void write_indent() {
        for (int i = 0; i < indent_level; i++) {
            buffer += "  ";
        }
    }
};

//------------------------------------------------------------------------------
// Binary buffer builder
//------------------------------------------------------------------------------
struct BinaryBuffer {
    std::vector<unsigned char> data;

    size_t write_floats(const float* values, int count) {
        size_t offset = data.size();
        size_t size = count * sizeof(float);
        data.resize(offset + size);
        memcpy(&data[offset], values, size);
        return offset;
    }

    size_t write_ushorts(const unsigned short* values, int count) {
        size_t offset = data.size();
        size_t size = count * sizeof(unsigned short);
        data.resize(offset + size);
        memcpy(&data[offset], values, size);
        return offset;
    }

    // Pad to 4-byte alignment
    void align4() {
        while (data.size() % 4 != 0) {
            data.push_back(0);
        }
    }
};

//------------------------------------------------------------------------------
// Compute bounding box for a mesh
//------------------------------------------------------------------------------
static void compute_bounds(Mesh* mesh, float* min_out, float* max_out) {
    min_out[0] = min_out[1] = min_out[2] = FLT_MAX;
    max_out[0] = max_out[1] = max_out[2] = -FLT_MAX;

    for (int i = 0; i < mesh->vertexCount; i++) {
        float x = mesh->vertices[i * 3 + 0];
        float y = mesh->vertices[i * 3 + 1];
        float z = mesh->vertices[i * 3 + 2];

        if (x < min_out[0]) min_out[0] = x;
        if (y < min_out[1]) min_out[1] = y;
        if (z < min_out[2]) min_out[2] = z;
        if (x > max_out[0]) max_out[0] = x;
        if (y > max_out[1]) max_out[1] = y;
        if (z > max_out[2]) max_out[2] = z;
    }
}

//------------------------------------------------------------------------------
// Specular to PBR conversion
//------------------------------------------------------------------------------
static void specular_to_pbr(Color specular_color, float shininess,
                            float* out_metallic, float* out_roughness) {
    // Normalize shininess (typically 0-128)
    float shininess_norm = shininess / 128.0f;
    if (shininess_norm > 1.0f) shininess_norm = 1.0f;

    // Roughness: high shininess = low roughness
    *out_roughness = 1.0f - sqrtf(shininess_norm);

    // Metallic: based on specular intensity
    float spec_intensity = (specular_color.r + specular_color.g + specular_color.b) / (3.0f * 255.0f);

    // If specular is very bright, might be metallic
    if (spec_intensity > 0.5f) {
        *out_metallic = spec_intensity;
    } else {
        *out_metallic = 0.0f;
    }

    // Check for colored specular (suggests metallic surface)
    float r = specular_color.r / 255.0f;
    float g = specular_color.g / 255.0f;
    float b = specular_color.b / 255.0f;
    bool colored_specular = (fabsf(r - g) > 0.1f || fabsf(g - b) > 0.1f);
    if (colored_specular && spec_intensity > 0.2f) {
        *out_metallic = 0.8f;
    }
}

//------------------------------------------------------------------------------
// Export implementation
//------------------------------------------------------------------------------
GLTFExportOptions GLTFDefaultOptions(void) {
    GLTFExportOptions opts;
    opts.texture_dir = "textures";  // Subfolder under output directory
    opts.source_dir = NULL;
    opts.texture_fallback_dir = NULL;
    opts.include_extras = true;
    opts.copy_textures = true;
    opts.include_physics_shape = true;
    opts.texture_count = 0;
    opts.normal_texture_count = 0;
    for (int i = 0; i < GLTF_MAX_TEXTURES; i++) {
        opts.texture_paths[i] = NULL;
        opts.normal_texture_paths[i] = NULL;
    }
    return opts;
}

//------------------------------------------------------------------------------
// Extract filename from path (handles both / and \ separators)
//------------------------------------------------------------------------------
static const char* get_filename(const char* path) {
    if (!path || !*path) return NULL;
    const char* last_slash = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_slash = p + 1;
        }
    }
    return last_slash;
}

//------------------------------------------------------------------------------
// Get directory part of a path (returns empty string if no directory)
//------------------------------------------------------------------------------
static std::string get_directory(const char* path) {
    if (!path || !*path) return "";
    std::string str(path);
    size_t last_slash = str.find_last_of("/\\");
    if (last_slash == std::string::npos) return "";
    return str.substr(0, last_slash);
}

//------------------------------------------------------------------------------
// Create directory (and parent directories) if it doesn't exist
//------------------------------------------------------------------------------
static bool ensure_directory(const char* path) {
    if (!path || !*path) return true;

    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Try to create parent directory first
    std::string parent = get_directory(path);
    if (!parent.empty() && !ensure_directory(parent.c_str())) {
        return false;
    }

    // Create this directory
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        TraceLog(LOG_WARNING, "GLTF Export: Failed to create directory: %s", path);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
// Copy a file from src to dst
//------------------------------------------------------------------------------
static bool copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) {
        TraceLog(LOG_WARNING, "GLTF Export: Cannot open source file: %s", src);
        return false;
    }

    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        TraceLog(LOG_WARNING, "GLTF Export: Cannot create destination file: %s", dst);
        return false;
    }

    char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, bytes, out) != bytes) {
            fclose(in);
            fclose(out);
            TraceLog(LOG_WARNING, "GLTF Export: Write error copying to: %s", dst);
            return false;
        }
    }

    fclose(in);
    fclose(out);
    return true;
}

//------------------------------------------------------------------------------
// Check if a path has a specific extension (case-insensitive)
//------------------------------------------------------------------------------
static bool has_extension(const std::string& path, const char* ext) {
    size_t ext_len = strlen(ext);
    if (path.size() < ext_len + 1) return false;

    std::string path_ext = path.substr(path.size() - ext_len);
    for (char& c : path_ext) c = tolower(c);

    std::string check_ext(ext);
    for (char& c : check_ext) c = tolower(c);

    return path_ext == check_ext;
}

//------------------------------------------------------------------------------
// Copy or convert texture file
// BMP files are converted to JPG (GLTF doesn't support BMP)
// Returns the actual destination path (with corrected extension), or empty on failure
//------------------------------------------------------------------------------
static std::string copy_or_convert_texture(const char* src_path, const char* dst_path) {
    std::string src(src_path);
    std::string dst(dst_path);

    // Check if source is BMP
    if (has_extension(src, ".bmp")) {
        // Load BMP using Raylib
        Image img = LoadImage(src_path);
        if (img.data == NULL) {
            TraceLog(LOG_WARNING, "GLTF Export: Failed to load BMP: %s", src_path);
            return "";
        }

        // Change destination extension to .jpg
        if (has_extension(dst, ".bmp")) {
            dst = dst.substr(0, dst.size() - 4) + ".jpg";
        }

        // Export as JPG
        bool success = ExportImage(img, dst.c_str());
        UnloadImage(img);

        if (success) {
            TraceLog(LOG_INFO, "GLTF Export: Converted BMP to JPG: %s", dst.c_str());
            return dst;
        } else {
            TraceLog(LOG_WARNING, "GLTF Export: Failed to export JPG: %s", dst.c_str());
            return "";
        }
    } else {
        // Not BMP, just copy the file
        if (copy_file(src_path, dst_path)) {
            return dst;
        }
        return "";
    }
}

//------------------------------------------------------------------------------
// Resolve a texture path relative to source directory using std::filesystem
// Tries multiple strategies:
// 1. Full relative path from source_dir (ASC file location)
// 2. Progressively stripped paths from fallback_dir
// 3. Just filename from fallback_dir
// Returns empty string if file doesn't exist at any location
//------------------------------------------------------------------------------
static std::string resolve_texture_path(const char* texture_path, const char* source_dir, const char* fallback_dir, const char* model_hint = nullptr) {
    if (!texture_path || !*texture_path) return "";

    // Convert backslashes to forward slashes
    std::string path(texture_path);
    for (char& c : path) {
        if (c == '\\') c = '/';
    }

    // If the given path already resolves (e.g. an absolute path from a texture lookup, as the level
    // exporter passes), use it directly — no source_dir/fallback needed.
    { std::error_code ec; if (fs::exists(fs::path(path), ec)) return path; }

    // Extract just the filename for final fallback
    fs::path texture_filename = fs::path(path).filename();

    try {
        // First, try resolving relative to source_dir (original ASC file location)
        if (source_dir && *source_dir) {
            fs::path base(source_dir);
            fs::path rel(path);
            fs::path resolved = fs::weakly_canonical(base / rel);

            if (fs::exists(resolved)) {
                TraceLog(LOG_INFO, "GLTF Export: Found texture at: %s", resolved.string().c_str());
                return resolved.string();
            }
            TraceLog(LOG_DEBUG, "GLTF Export: Texture not at source path: %s", resolved.string().c_str());
        }

        // Second, try the fallback directory with progressively stripped paths
        // ASC path like "../../textures/materials/file.jpg" with fallback ".../textures"
        // Try: textures/materials/file.jpg, materials/file.jpg, file.jpg
        if (fallback_dir && *fallback_dir) {
            fs::path fallback(fallback_dir);

            // Strip leading ../ and ./ segments
            std::string rel_path = path;
            while (rel_path.size() >= 3 && rel_path.substr(0, 3) == "../") {
                rel_path = rel_path.substr(3);
            }
            while (rel_path.size() >= 2 && rel_path.substr(0, 2) == "./") {
                rel_path = rel_path.substr(2);
            }

            // Try progressively shorter paths
            std::string try_path = rel_path;
            while (!try_path.empty()) {
                fs::path resolved = fs::weakly_canonical(fallback / try_path);
                if (fs::exists(resolved)) {
                    TraceLog(LOG_INFO, "GLTF Export: Found texture at fallback: %s", resolved.string().c_str());
                    return resolved.string();
                }
                TraceLog(LOG_DEBUG, "GLTF Export: Texture not at: %s", resolved.string().c_str());

                // Strip leading directory component for next attempt
                size_t slash_pos = try_path.find('/');
                if (slash_pos == std::string::npos) {
                    break;  // Only filename left, already tried
                }
                try_path = try_path.substr(slash_pos + 1);
            }

            // Last resort: recursively search subdirectories for the filename
            // This handles cases where ASC has just "body.bmp" but file is in "textures/droids/body.bmp"
            std::string filename_lower = texture_filename.string();
            std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);

            // Also try prefix-based matching using model_hint
            // e.g., model "head_j5.asc" referencing "head.bmp" might need "j5_head.bmp"
            std::vector<std::string> prefixes_to_try;
            if (model_hint && *model_hint) {
                fs::path model_path(model_hint);
                std::string model_stem = model_path.stem().string();
                // Look for patterns like "name_suffix" and extract "suffix_"
                size_t underscore_pos = model_stem.rfind('_');
                if (underscore_pos != std::string::npos) {
                    std::string suffix = model_stem.substr(underscore_pos + 1);
                    std::string prefix = suffix + "_";
                    std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                    prefixes_to_try.push_back(prefix);
                }
            }

            for (auto& entry : fs::recursive_directory_iterator(fallback, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    std::string entry_filename = entry.path().filename().string();
                    std::string entry_lower = entry_filename;
                    std::transform(entry_lower.begin(), entry_lower.end(), entry_lower.begin(), ::tolower);

                    // Exact match
                    if (entry_lower == filename_lower) {
                        TraceLog(LOG_INFO, "GLTF Export: Found texture via recursive search: %s", entry.path().string().c_str());
                        return entry.path().string();
                    }

                    // Prefix-based match (e.g., "head.bmp" -> "j5_head.bmp")
                    for (const auto& prefix : prefixes_to_try) {
                        std::string prefixed = prefix + filename_lower;
                        if (entry_lower == prefixed) {
                            TraceLog(LOG_INFO, "GLTF Export: Found texture via prefix search (%s): %s", prefix.c_str(), entry.path().string().c_str());
                            return entry.path().string();
                        }
                    }
                }
            }
        }

        // Not found at any location
        TraceLog(LOG_WARNING, "GLTF Export: Texture file not found: %s", texture_filename.string().c_str());
        return "";
    } catch (const fs::filesystem_error& e) {
        TraceLog(LOG_WARNING, "GLTF Export: Error resolving texture path '%s': %s", texture_path, e.what());
        return "";
    }
}

bool ExportGLTF(Model model, const char* output_path) {
    return ExportGLTFWithOptions(model, output_path, GLTFDefaultOptions());
}

bool ExportGLTFWithOptions(Model model, const char* output_path, GLTFExportOptions options) {
    GLTFExportResult result = ExportGLTFEx(model, output_path, options);
    if (!result.success) {
        TraceLog(LOG_ERROR, "GLTF Export: %s", result.error_msg);
    }
    return result.success;
}

GLTFExportResult ExportGLTFEx(Model model, const char* output_path, GLTFExportOptions options) {
    GLTFExportResult result = {0};
    result.success = false;

    if (model.meshCount == 0) {
        snprintf(result.error_msg, sizeof(result.error_msg), "Model has no meshes");
        return result;
    }

    // Ensure every mesh has tangents so the glTF TANGENT attribute (needed for tangent-space normal
    // maps) is uniform across meshes. CPU-only when the mesh isn't GPU-uploaded (headless convert).
    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];
        if (!mesh->tangents && mesh->vertices && mesh->normals && mesh->texcoords) {
            GenMeshTangents(mesh);
        }
    }

    // Build binary buffer with all mesh data
    BinaryBuffer bin;
    std::vector<size_t> mesh_position_offsets(model.meshCount);
    std::vector<size_t> mesh_normal_offsets(model.meshCount);
    std::vector<size_t> mesh_texcoord_offsets(model.meshCount);
    std::vector<size_t> mesh_tangent_offsets(model.meshCount);
    std::vector<size_t> mesh_index_offsets(model.meshCount);
    std::vector<int> mesh_position_sizes(model.meshCount);
    std::vector<int> mesh_normal_sizes(model.meshCount);
    std::vector<int> mesh_texcoord_sizes(model.meshCount);
    std::vector<int> mesh_tangent_sizes(model.meshCount);
    std::vector<int> mesh_index_sizes(model.meshCount);

    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];

        // Positions (required)
        if (mesh->vertices) {
            mesh_position_offsets[m] = bin.write_floats(mesh->vertices, mesh->vertexCount * 3);
            mesh_position_sizes[m] = mesh->vertexCount * 3 * sizeof(float);
        }
        bin.align4();

        // Normals (optional)
        if (mesh->normals) {
            mesh_normal_offsets[m] = bin.write_floats(mesh->normals, mesh->vertexCount * 3);
            mesh_normal_sizes[m] = mesh->vertexCount * 3 * sizeof(float);
        }
        bin.align4();

        // Texcoords (optional)
        if (mesh->texcoords) {
            mesh_texcoord_offsets[m] = bin.write_floats(mesh->texcoords, mesh->vertexCount * 2);
            mesh_texcoord_sizes[m] = mesh->vertexCount * 2 * sizeof(float);
        }
        bin.align4();

        // Tangents (VEC4: xyz + handedness). Always written so the per-mesh accessor layout is
        // uniform; falls back to a default tangent (unused unless the material has a normal map).
        if (mesh->tangents) {
            mesh_tangent_offsets[m] = bin.write_floats(mesh->tangents, mesh->vertexCount * 4);
        } else {
            std::vector<float> def((size_t)mesh->vertexCount * 4);
            for (int v = 0; v < mesh->vertexCount; v++) {
                def[v*4+0] = 1.0f; def[v*4+1] = 0.0f; def[v*4+2] = 0.0f; def[v*4+3] = 1.0f;
            }
            mesh_tangent_offsets[m] = bin.write_floats(def.data(), mesh->vertexCount * 4);
        }
        mesh_tangent_sizes[m] = mesh->vertexCount * 4 * sizeof(float);
        bin.align4();

        // Indices (optional but common)
        if (mesh->indices && mesh->triangleCount > 0) {
            int index_count = mesh->triangleCount * 3;
            mesh_index_offsets[m] = bin.write_ushorts(mesh->indices, index_count);
            mesh_index_sizes[m] = index_count * sizeof(unsigned short);
        }
        bin.align4();
    }

    // Build JSON
    JsonWriter json;
    json.begin_object();

    // Asset info
    json.key_object("asset");
    json.key("version"); json.value_string("2.0");
    json.key("generator"); json.value_string("model_tool (Raylib ASC Converter)");
    json.end_object();

    // Compute combined bounds across all meshes for physics shape
    if (options.include_physics_shape) {
        float combined_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float combined_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

        for (int m = 0; m < model.meshCount; m++) {
            float mesh_min[3], mesh_max[3];
            compute_bounds(&model.meshes[m], mesh_min, mesh_max);
            for (int i = 0; i < 3; i++) {
                combined_min[i] = std::min(combined_min[i], mesh_min[i]);
                combined_max[i] = std::max(combined_max[i], mesh_max[i]);
            }
        }

        // Create GLTFBounds from combined bounds
        GLTFBounds bounds;
        bounds.valid = true;
        for (int i = 0; i < 3; i++) {
            bounds.min[i] = combined_min[i];
            bounds.max[i] = combined_max[i];
        }

        // Determine physics shape (uses XZ plane for top-down 2D physics)
        PhysicsShapeInfo shape = determinePhysicsShape(bounds);

        // Write extras with physics shape
        json.key_object("extras");
        json.key_object("physics");
        json.key_object("shape");

        if (strcmp(shape.type, "circle") == 0) {
            json.key("type"); json.value_string("circle");
            json.key("radius"); json.value_float(shape.radius);
        } else {
            json.key("type"); json.value_string("box");
            json.key("width"); json.value_float(shape.width);
            json.key("height"); json.value_float(shape.height);
        }

        json.end_object();  // shape
        json.end_object();  // physics
        json.end_object();  // extras
    }

    // Scene
    json.key("scene"); json.value_int(0);

    // Scenes array
    json.key_array("scenes");
    json.begin_array_object();
    json.key("name"); json.value_string("Scene");
    json.key_array("nodes");
    for (int m = 0; m < model.meshCount; m++) {
        json.array_element();
        json.value_int(m);
    }
    json.end_array();
    json.end_object();
    json.end_array();

    // Nodes array (one per mesh)
    json.key_array("nodes");
    for (int m = 0; m < model.meshCount; m++) {
        json.begin_array_object();
        char name[64];
        snprintf(name, sizeof(name), "Mesh_%d", m);
        json.key("name"); json.value_string(name);
        json.key("mesh"); json.value_int(m);
        json.end_object();
    }
    json.end_array();

    // Meshes array
    json.key_array("meshes");
    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];
        int base_accessor = m * 5;  // position, normal, texcoord, tangent, indices

        json.begin_array_object();

        char name[64];
        snprintf(name, sizeof(name), "Mesh_%d", m);
        json.key("name"); json.value_string(name);

        json.key_array("primitives");
        json.begin_array_object();

        json.key_object("attributes");
        json.key("POSITION"); json.value_int(base_accessor);
        if (mesh->normals) {
            json.key("NORMAL"); json.value_int(base_accessor + 1);
        }
        if (mesh->texcoords) {
            json.key("TEXCOORD_0"); json.value_int(base_accessor + 2);
        }
        json.key("TANGENT"); json.value_int(base_accessor + 3);
        json.end_object();

        if (mesh->indices && mesh->triangleCount > 0) {
            json.key("indices"); json.value_int(base_accessor + 4);
        }

        // Material assignment
        int mat_id = (m < model.meshCount) ? model.meshMaterial[m] : 0;
        if (mat_id >= 0 && mat_id < model.materialCount) {
            json.key("material"); json.value_int(mat_id);
        }

        json.end_object();
        json.end_array();

        json.end_object();
    }
    json.end_array();

    // Accessors array
    json.key_array("accessors");
    int buffer_view_idx = 0;
    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];

        // Position accessor
        json.begin_array_object();
        json.key("bufferView"); json.value_int(buffer_view_idx++);
        json.key("componentType"); json.value_int(5126);  // FLOAT
        json.key("count"); json.value_int(mesh->vertexCount);
        json.key("type"); json.value_string("VEC3");

        // Bounds for positions
        float min_bounds[3], max_bounds[3];
        compute_bounds(mesh, min_bounds, max_bounds);
        json.key_array("min");
        for (int i = 0; i < 3; i++) { json.array_element(); json.value_float(min_bounds[i]); }
        json.end_array();
        json.key_array("max");
        for (int i = 0; i < 3; i++) { json.array_element(); json.value_float(max_bounds[i]); }
        json.end_array();
        json.end_object();

        // Normal accessor
        json.begin_array_object();
        json.key("bufferView"); json.value_int(buffer_view_idx++);
        json.key("componentType"); json.value_int(5126);  // FLOAT
        json.key("count"); json.value_int(mesh->vertexCount);
        json.key("type"); json.value_string("VEC3");
        json.end_object();

        // Texcoord accessor
        json.begin_array_object();
        json.key("bufferView"); json.value_int(buffer_view_idx++);
        json.key("componentType"); json.value_int(5126);  // FLOAT
        json.key("count"); json.value_int(mesh->vertexCount);
        json.key("type"); json.value_string("VEC2");
        json.end_object();

        // Tangent accessor (VEC4)
        json.begin_array_object();
        json.key("bufferView"); json.value_int(buffer_view_idx++);
        json.key("componentType"); json.value_int(5126);  // FLOAT
        json.key("count"); json.value_int(mesh->vertexCount);
        json.key("type"); json.value_string("VEC4");
        json.end_object();

        // Index accessor
        json.begin_array_object();
        json.key("bufferView"); json.value_int(buffer_view_idx++);
        json.key("componentType"); json.value_int(5123);  // UNSIGNED_SHORT
        json.key("count"); json.value_int(mesh->triangleCount * 3);
        json.key("type"); json.value_string("SCALAR");
        json.end_object();
    }
    json.end_array();

    // Buffer views array
    json.key_array("bufferViews");
    for (int m = 0; m < model.meshCount; m++) {
        Mesh* mesh = &model.meshes[m];

        // Position buffer view
        json.begin_array_object();
        json.key("buffer"); json.value_int(0);
        json.key("byteOffset"); json.value_int((int)mesh_position_offsets[m]);
        json.key("byteLength"); json.value_int(mesh_position_sizes[m]);
        json.key("target"); json.value_int(34962);  // ARRAY_BUFFER
        json.end_object();

        // Normal buffer view
        json.begin_array_object();
        json.key("buffer"); json.value_int(0);
        json.key("byteOffset"); json.value_int((int)mesh_normal_offsets[m]);
        json.key("byteLength"); json.value_int(mesh_normal_sizes[m]);
        json.key("target"); json.value_int(34962);
        json.end_object();

        // Texcoord buffer view
        json.begin_array_object();
        json.key("buffer"); json.value_int(0);
        json.key("byteOffset"); json.value_int((int)mesh_texcoord_offsets[m]);
        json.key("byteLength"); json.value_int(mesh_texcoord_sizes[m]);
        json.key("target"); json.value_int(34962);
        json.end_object();

        // Tangent buffer view
        json.begin_array_object();
        json.key("buffer"); json.value_int(0);
        json.key("byteOffset"); json.value_int((int)mesh_tangent_offsets[m]);
        json.key("byteLength"); json.value_int(mesh_tangent_sizes[m]);
        json.key("target"); json.value_int(34962);  // ARRAY_BUFFER
        json.end_object();

        // Index buffer view
        json.begin_array_object();
        json.key("buffer"); json.value_int(0);
        json.key("byteOffset"); json.value_int((int)mesh_index_offsets[m]);
        json.key("byteLength"); json.value_int(mesh_index_sizes[m]);
        json.key("target"); json.value_int(34963);  // ELEMENT_ARRAY_BUFFER
        json.end_object();
    }
    json.end_array();

    // Compute output directory from output_path for texture copying
    std::string output_dir = get_directory(output_path);
    if (output_dir.empty()) output_dir = ".";

    // Build list of unique textures (by filename) and map material index to texture index
    std::vector<std::string> unique_textures;      // Relative URIs for GLTF
    std::vector<std::string> unique_src_paths;     // Resolved source paths for copying
    std::vector<int> material_to_texture(model.materialCount, -1);  // -1 = no texture

    for (int m = 0; m < model.materialCount && m < options.texture_count; m++) {
        const char* tex_path = options.texture_paths[m];
        if (tex_path && *tex_path) {
            const char* filename = get_filename(tex_path);
            if (filename && *filename) {
                // Build relative URI: texture_dir/filename
                std::string uri = options.texture_dir;
                uri += "/";
                uri += filename;

                // Convert .bmp extension to .jpg in URI (GLTF doesn't support BMP)
                if (has_extension(uri, ".bmp")) {
                    uri = uri.substr(0, uri.size() - 4) + ".jpg";
                }

                // Check if we already have this texture
                int tex_idx = -1;
                for (int i = 0; i < (int)unique_textures.size(); i++) {
                    if (unique_textures[i] == uri) {
                        tex_idx = i;
                        break;
                    }
                }
                if (tex_idx < 0) {
                    tex_idx = (int)unique_textures.size();
                    unique_textures.push_back(uri);

                    // Resolve source path for copying (try source_dir first, then fallback_dir)
                    std::string src_path = resolve_texture_path(tex_path, options.source_dir, options.texture_fallback_dir, options.model_hint);
                    unique_src_paths.push_back(src_path);
                }
                material_to_texture[m] = tex_idx;
            }
        }
    }

    // Build normal (bump) textures the same way, into the shared texture list. Normal maps must
    // stay lossless, so BMP is converted to PNG (not JPG like diffuse).
    std::vector<int> material_to_normal(model.materialCount, -1);
    for (int m = 0; m < model.materialCount && m < options.normal_texture_count; m++) {
        const char* np = options.normal_texture_paths[m];
        if (np && *np) {
            const char* filename = get_filename(np);
            if (filename && *filename) {
                std::string uri = options.texture_dir;
                uri += "/";
                uri += filename;
                if (has_extension(uri, ".bmp")) {
                    uri = uri.substr(0, uri.size() - 4) + ".png";
                }
                int tex_idx = -1;
                for (int i = 0; i < (int)unique_textures.size(); i++) {
                    if (unique_textures[i] == uri) { tex_idx = i; break; }
                }
                if (tex_idx < 0) {
                    tex_idx = (int)unique_textures.size();
                    unique_textures.push_back(uri);
                    std::string src_path = resolve_texture_path(np, options.source_dir, options.texture_fallback_dir, options.model_hint);
                    unique_src_paths.push_back(src_path);
                }
                material_to_normal[m] = tex_idx;
            }
        }
    }

    // Materials array
    json.key_array("materials");
    for (int m = 0; m < model.materialCount; m++) {
        Material* mat = &model.materials[m];

        json.begin_array_object();

        char name[64];
        snprintf(name, sizeof(name), "Material_%d", m);
        json.key("name"); json.value_string(name);

        // Get diffuse color
        Color diffuse = mat->maps[MATERIAL_MAP_ALBEDO].color;
        float dr = diffuse.r / 255.0f;
        float dg = diffuse.g / 255.0f;
        float db = diffuse.b / 255.0f;
        float da = diffuse.a / 255.0f;

        // Get specular info for PBR conversion
        Color specular = mat->maps[MATERIAL_MAP_SPECULAR].color;
        float shininess = mat->params[0];  // Original shininess stored in params
        if (shininess == 0.0f) {
            // Fallback to SPECULAR value if params not set
            shininess = mat->maps[MATERIAL_MAP_SPECULAR].value * 128.0f;
        }

        float metallic, roughness;
        specular_to_pbr(specular, shininess, &metallic, &roughness);

        // PBR Metallic-Roughness
        json.key_object("pbrMetallicRoughness");
        json.key_array("baseColorFactor");
        json.array_element(); json.value_float(dr);
        json.array_element(); json.value_float(dg);
        json.array_element(); json.value_float(db);
        json.array_element(); json.value_float(da);
        json.end_array();

        // Add baseColorTexture if material has a texture
        if (material_to_texture[m] >= 0) {
            json.key_object("baseColorTexture");
            json.key("index"); json.value_int(material_to_texture[m]);
            json.end_object();
        }

        json.key("metallicFactor"); json.value_float(metallic);
        json.key("roughnessFactor"); json.value_float(roughness);
        json.end_object();

        // Normal (bump) map — standard glTF, so the engine needs no special handling.
        if (material_to_normal[m] >= 0) {
            json.key_object("normalTexture");
            json.key("index"); json.value_int(material_to_normal[m]);
            json.end_object();
        }

        // Emissive
        Color emissive = mat->maps[MATERIAL_MAP_EMISSION].color;
        if (emissive.r > 0 || emissive.g > 0 || emissive.b > 0) {
            json.key_array("emissiveFactor");
            json.array_element(); json.value_float(emissive.r / 255.0f);
            json.array_element(); json.value_float(emissive.g / 255.0f);
            json.array_element(); json.value_float(emissive.b / 255.0f);
            json.end_array();
        }

        // Extras with original Blinn-Phong data
        if (options.include_extras) {
            json.key_object("extras");
            json.key("shaderHint"); json.value_string("blinn-phong");
            json.key("originalFormat"); json.value_string("asc");

            json.key_object("blinnPhong");

            // Diffuse
            json.key_array("diffuse");
            json.array_element(); json.value_float(dr);
            json.array_element(); json.value_float(dg);
            json.array_element(); json.value_float(db);
            json.array_element(); json.value_float(da);
            json.end_array();

            // Specular
            json.key_array("specular");
            json.array_element(); json.value_float(specular.r / 255.0f);
            json.array_element(); json.value_float(specular.g / 255.0f);
            json.array_element(); json.value_float(specular.b / 255.0f);
            json.array_element(); json.value_float(specular.a / 255.0f);
            json.end_array();

            // Emissive
            json.key_array("emissive");
            json.array_element(); json.value_float(emissive.r / 255.0f);
            json.array_element(); json.value_float(emissive.g / 255.0f);
            json.array_element(); json.value_float(emissive.b / 255.0f);
            json.array_element(); json.value_float(emissive.a / 255.0f);
            json.end_array();

            json.key("shininess"); json.value_float(shininess);
            json.key("transparency"); json.value_float(mat->params[1]);

            json.end_object();  // blinnPhong
            json.end_object();  // extras
        }

        json.end_object();  // material
    }
    json.end_array();  // materials

    // Images array (if we have textures)
    if (!unique_textures.empty()) {
        json.key_array("images");
        for (const auto& uri : unique_textures) {
            json.begin_array_object();
            json.key("uri"); json.value_string(uri.c_str());
            json.end_object();
        }
        json.end_array();

        // Textures array (1:1 mapping with images for now)
        json.key_array("textures");
        for (int i = 0; i < (int)unique_textures.size(); i++) {
            json.begin_array_object();
            json.key("source"); json.value_int(i);
            json.end_object();
        }
        json.end_array();

        // Copy/convert texture files if enabled
        if (options.copy_textures) {
            // Build destination directory path: output_dir + texture_dir
            std::string tex_dest_dir = output_dir + "/" + options.texture_dir;

            // Ensure texture destination directory exists
            if (!ensure_directory(tex_dest_dir.c_str())) {
                TraceLog(LOG_WARNING, "GLTF Export: Could not create texture directory: %s", tex_dest_dir.c_str());
            } else {
                // Copy/convert each unique texture
                for (size_t i = 0; i < unique_textures.size(); i++) {
                    const std::string& src_path = unique_src_paths[i];

                    // Skip if source path is empty (file not found during resolution)
                    if (src_path.empty()) {
                        continue;
                    }

                    // Destination uses the URI filename (already has .jpg extension for BMPs)
                    const char* filename = get_filename(unique_textures[i].c_str());
                    std::string dst_path = tex_dest_dir + "/" + filename;

                    // Check if destination file already exists (shared textures)
                    if (fs::exists(dst_path)) {
                        TraceLog(LOG_INFO, "GLTF Export: Texture already exists, skipping: %s", dst_path.c_str());
                        continue;
                    }

                    // copy_or_convert_texture handles BMP->JPG conversion
                    std::string result = copy_or_convert_texture(src_path.c_str(), dst_path.c_str());
                    if (result.empty()) {
                        TraceLog(LOG_WARNING, "GLTF Export: Failed to process texture: %s", src_path.c_str());
                    }
                }
            }
        }
    }

    // Buffer (embedded as base64 data URI)
    json.key_array("buffers");
    json.begin_array_object();
    json.key("byteLength"); json.value_int((int)bin.data.size());

    // Encode binary data as base64 data URI
    std::string base64_data = base64_encode(bin.data.data(), bin.data.size());
    std::string data_uri = "data:application/octet-stream;base64," + base64_data;
    json.key("uri"); json.value_string(data_uri.c_str());

    json.end_object();
    json.end_array();

    json.end_object();  // root

    // Write to file
    FILE* f = fopen(output_path, "w");
    if (!f) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Failed to open output file: %s", output_path);
        return result;
    }

    fwrite(json.buffer.c_str(), 1, json.buffer.size(), f);
    fclose(f);

    TraceLog(LOG_INFO, "GLTF Export: Wrote %s (%d meshes, %d materials, %zu bytes binary)",
             output_path, model.meshCount, model.materialCount, bin.data.size());

    result.success = true;
    return result;
}
