#version 330

// Flat shadow colour (dark, semi-transparent). The whole projected silhouette is one colour; the
// alpha comes from shadowColor and is alpha-blended onto the floor.

out vec4 finalColor;

uniform vec4 shadowColor;   // e.g. (0, 0, 0, 0.4)

void main() {
    finalColor = shadowColor;
}
