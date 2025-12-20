#include "renderer.h"
#include "entities/entity.h"
#include "lighting/light.h"

void renderer_init(Renderer* renderer) {
    // Load lighting shader
    renderer->lighting_shader = LoadShader("assets/shaders/lighting.vs", "assets/shaders/lighting.fs");

    // Get shader locations
    renderer->lighting_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(renderer->lighting_shader, "viewPos");
    renderer->ambient_loc = GetShaderLocation(renderer->lighting_shader, "ambient");
    renderer->debug_mode_loc = GetShaderLocation(renderer->lighting_shader, "debugMode");
    renderer->debug_mode = 0;

    // Set ambient light
    float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(renderer->lighting_shader, renderer->ambient_loc, ambient, SHADER_UNIFORM_VEC4);

    // Set default specular values
    int specPowerLoc = GetShaderLocation(renderer->lighting_shader, "specularPower");
    int specIntensityLoc = GetShaderLocation(renderer->lighting_shader, "specularIntensity");
    float defaultSpecPower = 32.0f;
    float defaultSpecIntensity = 0.5f;
    SetShaderValue(renderer->lighting_shader, specPowerLoc, &defaultSpecPower, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->lighting_shader, specIntensityLoc, &defaultSpecIntensity, SHADER_UNIFORM_FLOAT);

    // Initialize debug mode to 0 (normal rendering)
    SetShaderValue(renderer->lighting_shader, renderer->debug_mode_loc, &renderer->debug_mode, SHADER_UNIFORM_INT);

    // Create directional light from above
    // Position is where light comes FROM, target is where it shines TO
    // Shader computes: lightDir = normalize(target - position) = direction toward light
    renderer->light_count = 0;
    renderer->lights[renderer->light_count++] = create_light(
        LIGHT_DIRECTIONAL,
        (Vector3){0, 0, 0},    // Light shines toward origin
        (Vector3){0, 50, 0},   // Light comes from above → lightDir = (0, 1, 0)
        WHITE,
        renderer->lighting_shader,
        0
    );

    // Create shared cube model with lighting shader
    Mesh cubeMesh = GenMeshCube(2.0f, 2.0f, 2.0f);
    renderer->cube_model = LoadModelFromMesh(cubeMesh);
    renderer->cube_model.materials[0].shader = renderer->lighting_shader;
    renderer->cube_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){60, 60, 60, 255};
}

void renderer_destroy(Renderer* renderer) {
    UnloadModel(renderer->cube_model);
    UnloadShader(renderer->lighting_shader);
}

void renderer_set_debug_mode(Renderer* renderer, int mode) {
    renderer->debug_mode = mode;
    SetShaderValue(renderer->lighting_shader, renderer->debug_mode_loc, &renderer->debug_mode, SHADER_UNIFORM_INT);
}

Model renderer_load_gltf(const char* path) {
    return LoadModel(path);
}

Model renderer_load_gltf_specular(Renderer* renderer, const char* path, float specular_power, float specular_intensity) {
    Model model = LoadModel(path);

    // Apply lighting shader to all materials
    for (int i = 0; i < model.materialCount; i++) {
        model.materials[i].shader = renderer->lighting_shader;

        // Set low diffuse color (dark blue-gray)
        model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = (Color){40, 45, 60, 255};
    }

    // Set specular uniforms
    int specPowerLoc = GetShaderLocation(renderer->lighting_shader, "specularPower");
    int specIntensityLoc = GetShaderLocation(renderer->lighting_shader, "specularIntensity");
    SetShaderValue(renderer->lighting_shader, specPowerLoc, &specular_power, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->lighting_shader, specIntensityLoc, &specular_intensity, SHADER_UNIFORM_FLOAT);

    return model;
}

void renderer_begin_lighting(Renderer* renderer, Camera3D* camera) {
    // Update camera position for specular calculations
    float cameraPos[3] = {camera->position.x, camera->position.y, camera->position.z};
    SetShaderValue(renderer->lighting_shader, renderer->lighting_shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void renderer_draw_entity(Renderer* renderer, Entity* entity) {
    if (!entity->active) return;

    if (entity->has_model) {
        Vector3 axis = {0, 1, 0};
        float angle_deg = entity->rotation * RAD2DEG;
        DrawModelEx(entity->model, entity->position, axis, angle_deg,
                    (Vector3){1, 1, 1}, WHITE);
    } else {
        // Draw shared cube model with lighting shader
        Vector3 axis = {0, 1, 0};
        float angle_deg = entity->rotation * RAD2DEG;
        DrawModelEx(renderer->cube_model, entity->position, axis, angle_deg,
                    (Vector3){1, 1, 1}, WHITE);
    }
}
