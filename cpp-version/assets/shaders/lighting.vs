#version 330

// Vertex Shader for Blinn-Phong Lighting
//
// Expected vertex attributes (provided by Raylib):
//   - vertexPosition: Model-space position
//   - vertexTexCoord: UV coordinates (may be unused if model has no texture)
//   - vertexNormal:   Model-space normal vector
//   - vertexColor:    Per-vertex color (typically white if not specified)
//
// Expected uniforms (provided by Raylib):
//   - mvp:       Model-View-Projection matrix
//   - matModel:  Model matrix for world-space transformation
//
// NOTE: We compute the normal matrix from matModel in the shader rather than
// using Raylib's matNormal, which is in view space (see raylib issue #1870).

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main() {
    // Transform position to world space for lighting calculations
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    // Pass through texture coordinates and vertex color
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    // Transform normal to world space
    // For uniform scaling, we can use mat3(matModel) directly
    // For non-uniform scaling, this should be transpose(inverse(mat3(matModel)))
    // but that's expensive - assume uniform scaling for now
    fragNormal = normalize(mat3(matModel) * vertexNormal);

    // Final clip-space position
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
