#version 330

// Planar shadow projection. Each caster mesh is drawn with this shader: its world-space vertices are
// projected straight onto the floor plane (Y = shadowFloorY) along the shadow light direction, then
// to clip space. Combined with a flat dark fragment shader + alpha blend, this lays the object's
// silhouette on the ground. See docs/scenery_entities.md (shadow pass).

in vec3 vertexPosition;

uniform mat4 matModel;        // Raylib: world transform of the mesh being drawn
uniform mat4 matView;         // Raylib: camera view
uniform mat4 matProjection;   // Raylib: projection

uniform vec3 shadowLightDir;  // light travel direction (points downward; .y < 0)
uniform float shadowFloorY;   // ground plane height

void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    // Slide the world position along the light direction until it hits the floor plane.
    float t = (shadowFloorY - wp.y) / shadowLightDir.y;
    wp.xyz += t * shadowLightDir;
    wp.y = shadowFloorY;   // clamp exactly (avoid float drift / z-fighting)
    gl_Position = matProjection * matView * wp;
}
