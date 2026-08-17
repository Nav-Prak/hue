#version 450
// Week 6 static mesh: frame UBO carries view-projection + camera + light,
// push constants carry the model matrix and material factors.

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 light_direction; // w = point light count
    vec4 light_color;
    vec4 ambient_color;
    vec4 point_position_radius[4];
    vec4 point_color_intensity[4];
} u_frame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 base_color;
    vec4 metallic_roughness;
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) out vec3 v_world_position;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;

void main() {
    vec4 world = pc.model * vec4(a_position, 1.0);
    gl_Position = u_frame.view_projection * world;
    v_world_position = world.xyz;
    // Uniform-scale assumption holds for current content; a proper normal
    // matrix ships with skinning in Week 7.
    v_world_normal = mat3(pc.model) * a_normal;
    v_uv = a_uv;
}
