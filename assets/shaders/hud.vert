#version 450
// Screen-space HUD quad: six vertices from gl_VertexIndex, NDC corners
// and color from push constants. Depth is off; drawn after the 3D pass.

layout(push_constant) uniform Quad {
    vec2 ndc_min;
    vec2 ndc_max;
    vec4 color;
} q;

layout(location = 0) out vec4 v_color;

void main() {
    const vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    const vec2 uv = corners[gl_VertexIndex];
    const vec2 position = mix(q.ndc_min, q.ndc_max, uv);
    gl_Position = vec4(position, 0.0, 1.0);
    v_color = q.color;
}
