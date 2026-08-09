#version 450
// Flat lambert + ambient with a UV checker so orientation and texture
// coordinates are visually verifiable before real textures land in Week 6.

layout(location = 0) in vec3 v_world_normal;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

const vec3 kLightDirection = normalize(vec3(0.4, -1.0, 0.3));
const vec3 kBaseColor = vec3(0.62, 0.66, 0.74);

void main() {
    vec3 normal = normalize(v_world_normal);
    float diffuse = max(dot(normal, -kLightDirection), 0.0);
    float checker = mod(floor(v_uv.x * 8.0) + floor(v_uv.y * 8.0), 2.0);
    vec3 albedo = kBaseColor * mix(0.82, 1.0, checker);
    vec3 color = albedo * (0.18 + 0.82 * diffuse);
    o_color = vec4(color, 1.0);
}
