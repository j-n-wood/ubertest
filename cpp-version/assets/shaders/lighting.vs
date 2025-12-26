#version 330

// Vertex Shader for Blinn-Phong Lighting with Bump/Normal Mapping
//
// Expected vertex attributes (provided by Raylib):
//   - vertexPosition: Model-space position
//   - vertexTexCoord: UV coordinates (may be unused if model has no texture)
//   - vertexNormal:   Model-space normal vector
//   - vertexTangent:  Model-space tangent vector (optional, for normal mapping)
//   - vertexColor:    Per-vertex color (typically white if not specified)
//
// Expected uniforms (provided by Raylib):
//   - mvp:       Model-View-Projection matrix
//   - matModel:  Model matrix for world-space transformation
//
// NOTE: We compute the normal matrix from matModel in the shader rather than
// using Raylib's matNormal, which is in view space (see raylib issue #1870).
//
// TBN Matrix:
//   For models with tangent data (GLTF), we compute the TBN matrix here.
//   For models without tangents, the fragment shader can compute tangents
//   from screen-space derivatives.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;  // vec4: xyz = tangent direction, w = handedness (+1 or -1)
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragTangent;
out vec3 fragBitangent;

void main() {
    // Transform position to world space for lighting calculations
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    // Pass through texture coordinates and vertex color
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    // Compute normal matrix (transpose of inverse for non-uniform scaling)
    // For performance, we use mat3(matModel) which works for uniform scaling
    mat3 normalMatrix = mat3(matModel);

    // Transform normal to world space
    fragNormal = normalize(normalMatrix * vertexNormal);

    // Transform tangent to world space (if tangent data exists)
    // Tangent w component stores handedness for bitangent calculation
    vec3 T = normalize(normalMatrix * vertexTangent.xyz);

    // Re-orthogonalize tangent with respect to normal (Gram-Schmidt)
    T = normalize(T - dot(T, fragNormal) * fragNormal);

    // Compute bitangent using cross product and handedness
    vec3 B = cross(fragNormal, T) * vertexTangent.w;

    fragTangent = T;
    fragBitangent = B;

    // Final clip-space position
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
