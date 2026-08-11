#version 330

// Fragment Shader for Blinn-Phong Lighting with Bump/Normal Mapping
//
// Inputs from vertex shader:
//   - fragPosition:  World-space position for lighting calculations
//   - fragTexCoord:  UV coordinates for diffuse texture sampling
//   - fragTexCoord2: UV coordinates for normal/bump texture (separate for custom tiles)
//   - fragColor:     Per-vertex color (typically unused, passed through)
//   - fragNormal:    World-space normal vector (normalized)
//   - fragTangent:   World-space tangent vector (required for normal mapping)
//   - fragBitangent: World-space bitangent vector (computed from cross(N,T) in vertex shader)
//
// NOTE: All geometry is expected to have precomputed tangent data. For procedural
// geometry (e.g., flat tiles), tangents are defined by convention based on known
// geometry orientation (Normal=+Y, Tangent=+X for floor tiles).
//
// Material uniforms (provided by Raylib):
//   - texture0:   Diffuse texture map (MATERIAL_MAP_DIFFUSE)
//                 NOTE: If model has no texture, this samples as black (0,0,0)
//   - texture2:   Normal/bump texture map (MATERIAL_MAP_NORMAL)
//                 RGB encodes tangent-space normal (0-1 range, needs remapping to -1..1)
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
// Bump/Normal mapping uniforms:
//   - useNormalMap:  1 = sample normal from texture2, 0 = use vertex normal only
//   - bumpIntensity: Strength of the normal perturbation (typical: 0.5-2.0)
//
// Lighting uniforms:
//   - viewPos: Camera/eye position in world space (used for rendering)
//   - effectiveEyeHeight: Height above ground plane for specular calculations
//                         Allows top-down camera to simulate lighting as seen from
//                         a viewpoint within the scene (e.g., a character's eye level)
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
in vec2 fragTexCoord2;  // Bump atlas UVs (separate from diffuse UVs for custom tiles)
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragTangent;
in vec3 fragBitangent;

uniform sampler2D texture0;  // Diffuse map
uniform sampler2D texture1;  // Environment map (Raylib: texture1 = MATERIAL_MAP_METALNESS slot) for old-school spherical env mapping
uniform sampler2D texture2;  // Normal/bump map (Raylib convention: texture2 = MATERIAL_MAP_NORMAL)
uniform vec4 colDiffuse;
uniform vec4 colSpecular;  // RGB = specular color, A = normalized shininess (0-1). Also modulates the env map (legacy SPECULARITY).

// Environment mapping (legacy DRAWTYPE ENVMAP / EFFECTTEXTURE). Not a standard GLTF feature:
// the env texture is passed as material `extras` (see docs/env_mapping.md) and bound to texture1.
// The eye-space normal's XY is used as the UV (normals toward camera -> (0.5,0.5), scaled by 0.5),
// giving a cheap spherical reflection. colSpecular (from the legacy SPECULARITY) modulates it,
// then it is added on top of the lit surface, scaled by envIntensity.
uniform mat4 matView;        // World->eye matrix (auto-provided by Raylib as SHADER_LOC_MATRIX_VIEW)
uniform int useEnvMap;       // 1 = this material has an env map bound to texture1, 0 = none
uniform float envIntensity;  // Additive strength of the env contribution (per-material)

uniform vec3 viewPos;
uniform float effectiveEyeHeight;  // Height above ground for specular calculations (-1 = use viewPos.y)
uniform vec4 ambient;

// "Lights out": scene dimming applied to the final lit colour. 0 = full brightness (the GLSL
// default, so any pass that never sets it is unaffected), up to 1 = black. The 3D game ramps this
// up when a level is cleared — the literal-dimming counterpart of the 2D tile "lights out" row.
uniform float darkness;

// Emissive / glow (DRAWTYPE glow — alert lights, lift tops). 0 = normal lit surface (the GLSL
// default, so any pass that never sets it is unaffected). >0 adds a glow colour back as
// self-illumination AFTER shadow + lights-out, so a glowing object stays bright in shadow and when
// the level's lights go out. Set per-draw by Object3DRenderer for glow definitions.
//   emissiveTint = 0 -> glow uses the material's own diffuse colour (steady, e.g. lift tops).
//   emissiveTint = 1 -> glow uses emissiveColor (the ship's pulsing alert-band colour). A separate
//     flag (not "is emissiveColor black?") so the alert pulse can legitimately reach black at its
//     trough without falling back to the material colour.
uniform float emissive;
uniform vec3 emissiveColor;
uniform int  emissiveTint;

// Shadow mapping: a depth map rendered from the light's POV. `useShadows` gates it (0 = off, the GLSL
// default, so any pass that never sets it is unaffected). `lightVP` transforms world space into the
// light's clip space for the occlusion lookup. See shared/rendering/shadow_map.*.
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform int useShadows;
uniform float shadowBias;

// 1.0 (lit) .. 0.0 (fully shadowed) for a world-space position, 3x3 PCF filtered.
float shadowFactor(vec3 worldPos) {
    if (useShadows == 0) return 1.0;
    vec4 lp = lightVP * vec4(worldPos, 1.0);
    vec3 p = lp.xyz / lp.w;
    p = p * 0.5 + 0.5;                        // NDC -> [0,1]
    if (p.z > 1.0) return 1.0;                // past the light's far plane -> lit
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;   // outside the map -> lit
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(shadowMap, p.xy + vec2(x, y) * texel).r;
            lit += (p.z - shadowBias > closest) ? 0.0 : 1.0;
        }
    return lit / 9.0;
}

// Light uniforms
uniform int light0_enabled;
uniform int light0_type;
uniform vec3 light0_position;
uniform vec3 light0_target;
uniform vec4 light0_color;

// Material properties (fallbacks if colSpecular not set)
uniform float specularPower;
uniform float specularIntensity;

// Bump/normal map properties
uniform int useNormalMap;      // 1 = use normal map, 0 = use vertex normal
uniform float bumpIntensity;   // Strength of bump effect (default: 1.0)

// Debug mode: 0=normal rendering, 1=show normals, 2=show lightDir, 3=show specular only
//             4=show viewDir, 5=show halfDir, 6=show tangent-space normal from map
uniform int debugMode;

out vec4 finalColor;

vec3 getNormal() {
    vec3 N = normalize(fragNormal);

    if (useNormalMap == 0) {
        return N;
    }

    // Sample normal map using fragTexCoord2 (bump atlas UVs)
    // For custom tiles mode, this allows different UVs for diffuse and normal textures
    vec3 normalMapSample = texture(texture2, fragTexCoord2).rgb;

    // Check if normal map is valid (not black/empty)
    if (normalMapSample == vec3(0.0)) {
        return N;
    }

    // Convert from 0-1 range to -1..1 range (tangent space)
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;

    // Apply bump intensity (scale X and Y components, keep Z pointing outward)
    tangentNormal.xy *= bumpIntensity;
    tangentNormal = normalize(tangentNormal);

    // Build TBN matrix from vertex tangent/bitangent
    // All geometry is expected to have precomputed tangents
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);

    // Transform tangent-space normal to world space
    return normalize(TBN * tangentNormal);
}

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

    // Get the final normal (with bump mapping applied if enabled)
    vec3 normal = getNormal();

    // Calculate effective view position for specular calculations
    // If effectiveEyeHeight >= 0, use it instead of actual camera Y
    // This allows top-down cameras to show lighting as seen from within the scene
    vec3 effectiveViewPos = viewPos;
    if (effectiveEyeHeight >= 0.0) {
        effectiveViewPos.y = effectiveEyeHeight;
    }
    vec3 viewDir = normalize(effectiveViewPos - fragPosition);

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
            // For directional: light shines from position toward target
            // lightDir = direction TO light = opposite of light ray direction
            lightDir = normalize(light0_position - light0_target);
        } else {
            // Point light: direction from fragment toward light position
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
        // Show final normals as colors (remap -1..1 to 0..1)
        finalColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 2) {
        // Show lightDir as color (direction toward light source)
        vec3 lightDir = normalize(light0_position - light0_target);
        finalColor = vec4(lightDir * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 3) {
        // Show specular component only (white = specular)
        finalColor = vec4(specularLight, 1.0);
        return;
    } else if (debugMode == 4) {
        // Show viewDir as color (using effective eye position)
        finalColor = vec4(viewDir * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 5) {
        // Show halfDir as color (using effective eye position)
        vec3 lightDir = normalize(light0_position - light0_target);
        vec3 halfDir = normalize(lightDir + viewDir);
        finalColor = vec4(halfDir * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == 6) {
        // Show raw normal map sample (tangent-space normal, using bump atlas UVs)
        vec3 normalMapSample = texture(texture2, fragTexCoord2).rgb;
        finalColor = vec4(normalMapSample, 1.0);
        return;
    }

    // Combine lighting with material. Shadow darkens the direct (diffuse + specular) contribution;
    // ambient stays so shadowed surfaces aren't pitch black.
    float sh = shadowFactor(fragPosition);
    vec3 diffuseColor = colDiffuse.rgb * texelColor.rgb;
    vec3 result = (ambientLight + diffuseLight * sh) * diffuseColor;
    result += specularLight * sh;

    // Old-school spherical environment mapping (additive reflection/glow layer).
    // UV = eye-space normal XY remapped to [0,1] (0.5,0.5 = facing camera). Modulated by the
    // legacy SPECULARITY (colSpecular) in all four channels, then scaled by envIntensity.
    if (useEnvMap == 1) {
        vec3 nEye = normalize((matView * vec4(normal, 0.0)).xyz);
        vec2 envUV = nEye.xy * 0.5 + 0.5;
        vec4 envSample = texture(texture1, envUV) * colSpecular;
        result += envIntensity * envSample.a * envSample.rgb;
    }

    // Lights-out dimming (ambient + diffuse + specular + env all fade together).
    result *= (1.0 - clamp(darkness, 0.0, 1.0));

    // Emissive glow: add a glow colour back as self-illumination, unaffected by shadow or lights-out
    // (a glowing light stays lit). emissive is the strength (0 = off); emissiveTint selects the tint.
    if (emissive > 0.0) {
        vec3 glowTint = (emissiveTint == 1) ? emissiveColor : diffuseColor;
        result += emissive * glowTint;
    }

    finalColor = vec4(result, colDiffuse.a * texelColor.a);
}
