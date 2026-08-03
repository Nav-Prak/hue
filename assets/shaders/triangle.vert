#version 450
// Week 4 bring-up triangle: positions and colors live in the shader, no
// vertex buffers yet (those arrive with real meshes in Week 5).

layout(location = 0) out vec3 v_color;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.62),
    vec2( 0.6,  0.55),
    vec2(-0.6,  0.55)
);

vec3 colors[3] = vec3[](
    vec3(0.96, 0.26, 0.41), // hue red-pink
    vec3(0.30, 0.85, 0.55), // hue green
    vec3(0.35, 0.52, 0.98)  // hue blue
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_color = colors[gl_VertexIndex];
}
