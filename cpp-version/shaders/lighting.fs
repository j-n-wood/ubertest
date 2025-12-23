#version 330

// Fragment Shader for Blinn-Phong Lighting
//
// Inputs from vertex shader:
//   - fragPosition: World-space position for lighting calculations
//   - fragTexCoord: UV coordinates for texture sampling
//   - fragColor:    Per-vertex color (typically unused, passed through)
//   - fragNormal:   World-space normal vector (normalized)
//
// Material uniforms (provided by Raylib):
//   - texture0:   Diffuse texture map (MATERIAL_MAP_DIFFUSE)
//                 NOTE: If model has no texture, this samples as black (0,0,0)
//   - colDiffuse: Material diffuse color (material.maps[MATERIAL_MAP_DIFFUSE].color)
//                 This is the base color of the material, normalized to 0-1 range
//
// Material specular uniforms:
//   - colSpecular:       Specular color (from material.maps[MATERIAL_MAP_METALNESS].color)
//                        RGB = specular color, A = shininess intensity (0-1)
//   - specularPower:     Shininess exponent (higher = tighter highlight, typical: 16-128)
//                        Can be set globally or derived from material
//   - specularIntensity: Multiplier for specular contribution (typical: 0.5-2.0)
//
// Lighting uniforms:
//   - viewPos: Camera/eye position in world space for specular calculations
//   - ambient: Global ambient light color (RGBA, typical: 0.1-0.2 for RGB)
//
// Light uniforms (per-light, indexed):
//   - light0_enabled:  1 if light is active, 0 if disabled
//   - light0_type:     0 = directional, 1 = point light
//   - light0_position: Light position (directional: used with target for direction)
//   - light0_target:   Target point (directional light aims from position toward target)
//   - light0_color:    Light color (RGBA, normalized 0-1)

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 colSpecular;  // RGB = specular color, A = normalized shininess (0-1)

uniform vec3 viewPos;
uniform vec4 ambient;

// Light uniforms
uniform int light0_enabled;
uniform int light0_type;
uniform vec3 light0_position;
uniform vec3 light0_target;
uniform vec4 light0_color;

// Material properties (fallbacks if colSpecular not set)
uniform float specularPower;
uniform float specularIntensity;

// Debug mode: 0=normal rendering, 1=show normals, 2=show lightDir, 3=show specular only
uniform int debugMode;

out vec4 finalColor;

void main() {
    vec4 texelColor = texture(texture0, fragTexCoord);

    // FALLBACK: Models without textures (e.g., procedural GLTF meshes) have no
    // texture data, causing texture0 to sample as black (0,0,0). Without this
    // fallback, the entire diffuse contribution would be zero, making the model
    // appear completely black except for specular highlights.
    // Using white (1,1,1) allows colDiffuse to be the sole diffuse color source.
    if (texelColor.rgb == vec3(0.0)) {
        texelColor = vec4(1.0);
    }

    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    // Ambient
    vec3 ambientLight = ambient.rgb;

    // Initialize lighting
    vec3 diffuseLight = vec3(0.0);
    vec3 specularLight = vec3(0.0);

    if (light0_enabled == 1) {
        vec3 lightDir;

        // Directional light (type 0)
        // lightDir points TOWARD the light source (standard Blinn-Phong convention)
        if (light0_type == 0) {
            lightDir = normalize(light0_target - light0_position);
        } else {
            lightDir = normalize(light0_position - fragPosition);
        }

        // Diffuse
        float diff = max(dot(normal, lightDir), 0.0);
        diffuseLight += diff * light0_color.rgb;

        // Specular (Blinn-Phong)
        // Use colSpecular.a (normalized shininess 0-1) to derive power, or fallback to uniform
        float shininess = colSpecular.a > 0.0 ? colSpecular.a * 128.0 : specularPower;
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), max(shininess, 1.0));

        // Use colSpecular.rgb if set (non-black), otherwise use light color with intensity
        vec3 specColor = (colSpecular.rgb != vec3(0.0)) ? colSpecular.rgb : vec3(specularIntensity);
        specularLight += spec * light0_color.rgb * specColor;
    }

    // Debug visualization
    if (debugMode == 1) {
        // Show normals as colors (remap -1..1 to 0..1)
        finalColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 2) {
        // Show lightDir as color
        vec3 lightDir = normalize(light0_target - light0_position);
        finalColor = vec4(lightDir * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 3) {
        // Show specular component only (white = specular)
        finalColor = vec4(specularLight, 1.0);
        return;
    } else if (debugMode == 4) {
        // Show viewDir as color
        finalColor = vec4(viewDir * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 5) {
        // Show halfDir as color
        vec3 lightDir = normalize(light0_target - light0_position);
        vec3 halfDir = normalize(lightDir + viewDir);
        finalColor = vec4(halfDir * 0.5 + 0.5, 1.0);
        return;
    }

    // Combine lighting with material
    vec3 diffuseColor = colDiffuse.rgb * texelColor.rgb;
    vec3 result = (ambientLight + diffuseLight) * diffuseColor;
    result += specularLight;

    finalColor = vec4(result, colDiffuse.a * texelColor.a);
}
