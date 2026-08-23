#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 light_direction;
    vec4 light_color;
    vec4 ambient_color;
    vec4 point_position_radius[4];
    vec4 point_color_intensity[4];
} u_frame;

layout(set = 2, binding = 0, std430) readonly buffer SkinningMatrices {
    mat4 matrices[];
} u_skin;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 base_color;
    vec4 metallic_roughness;
    uvec4 skin; // x = first matrix for this draw
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uvec4 a_joints;
layout(location = 4) in vec4 a_weights;

layout(location = 0) out vec3 v_world_position;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;

void main() {
    mat4 skin = a_weights.x * u_skin.matrices[pc.skin.x + a_joints.x] +
                a_weights.y * u_skin.matrices[pc.skin.x + a_joints.y] +
                a_weights.z * u_skin.matrices[pc.skin.x + a_joints.z] +
                a_weights.w * u_skin.matrices[pc.skin.x + a_joints.w];
    vec4 skinned_position = skin * vec4(a_position, 1.0);
    vec3 skinned_normal = mat3(skin) * a_normal;
    vec4 world = pc.model * skinned_position;
    gl_Position = u_frame.view_projection * world;
    v_world_position = world.xyz;
    v_world_normal = normalize(mat3(pc.model) * skinned_normal);
    v_uv = a_uv;
}
