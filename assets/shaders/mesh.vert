#version 450
// Week 5 static mesh: push-constant MVP, world normal for lighting.

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) out vec3 v_world_normal;
layout(location = 1) out vec2 v_uv;

void main() {
    gl_Position = pc.mvp * vec4(a_position, 1.0);
    // Uniform-scale assumption holds for Week 5 content; a proper normal
    // matrix ships with skinning in Week 7.
    v_world_normal = mat3(pc.model) * a_normal;
    v_uv = a_uv;
}
