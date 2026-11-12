#version 450
// Metallic-roughness PBR (Week 6 spec): Cook-Torrance with GGX normal
// distribution, Smith geometry, and Schlick fresnel. One directional key
// light plus up to 4 point lights with windowed inverse-square falloff,
// plus flat ambient. The swapchain is sRGB, so everything here stays
// linear and the hardware encodes on store. Per glTF, metallic samples
// from the B channel and roughness from G. The key light is attenuated by
// one directional shadow map (3x3 PCF over hardware depth comparison).

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 light_direction; // w = point light count
    vec4 light_color;
    vec4 ambient_color;
    vec4 point_position_radius[4];
    vec4 point_color_intensity[4];
    mat4 light_view_projection; // world -> shadow-map clip space
    vec4 shadow_params;         // x enabled, y texel size, z normal offset (m)
} u_frame;

layout(set = 0, binding = 1) uniform sampler2DShadow u_shadow_map;

layout(set = 1, binding = 0) uniform sampler2D u_base_color;
layout(set = 1, binding = 1) uniform sampler2D u_metallic_roughness;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 base_color;
    vec4 metallic_roughness;
} pc;

layout(location = 0) in vec3 v_world_position;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

const float kPi = 3.14159265359;

float distribution_ggx(float n_dot_h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-6);
}

float geometry_smith(float n_dot_v, float n_dot_l, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float g_v = n_dot_v / (n_dot_v * (1.0 - k) + k);
    float g_l = n_dot_l / (n_dot_l * (1.0 - k) + k);
    return g_v * g_l;
}

vec3 fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// Cook-Torrance contribution of one light arriving from direction `light`
// with the given linear radiance.
vec3 shade(vec3 normal, vec3 view, vec3 light, vec3 radiance, vec3 albedo, float metallic,
           float roughness, vec3 f0) {
    vec3 halfway = normalize(view + light);
    float n_dot_l = max(dot(normal, light), 0.0);
    float n_dot_v = max(dot(normal, view), 1e-4);
    float n_dot_h = max(dot(normal, halfway), 0.0);
    float h_dot_v = max(dot(halfway, view), 0.0);

    float ndf = distribution_ggx(n_dot_h, roughness);
    float geometry = geometry_smith(n_dot_v, n_dot_l, roughness);
    vec3 fresnel = fresnel_schlick(h_dot_v, f0);

    vec3 specular = (ndf * geometry * fresnel) / max(4.0 * n_dot_v * n_dot_l, 1e-4);
    vec3 diffuse_weight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuse_weight * albedo / kPi + specular) * radiance * n_dot_l;
}

// Fraction of the key light reaching this point: 1 lit, 0 fully shadowed.
// Anything outside the shadow map's box is treated as lit.
float key_light_visibility(vec3 world_position, vec3 normal) {
    if (u_frame.shadow_params.x < 0.5) {
        return 1.0;
    }
    // Offset along the normal so a surface does not shadow itself.
    vec3 biased = world_position + normal * u_frame.shadow_params.z;
    vec4 clip = u_frame.light_view_projection * vec4(biased, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    // The shadow pass renders through the same Y-flipped viewport as the
    // main pass, so NDC +Y is the top row of the map.
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || ndc.z <= 0.0 ||
        ndc.z >= 1.0) {
        return 1.0;
    }
    float texel = u_frame.shadow_params.y;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(u_shadow_map, vec3(uv + vec2(float(x), float(y)) * texel, ndc.z));
        }
    }
    return lit / 9.0;
}

void main() {
    vec4 base = texture(u_base_color, v_uv) * pc.base_color;
    vec3 albedo = base.rgb;
    vec3 mr_sample = texture(u_metallic_roughness, v_uv).rgb;
    float metallic = clamp(mr_sample.b * pc.metallic_roughness.x, 0.0, 1.0);
    float roughness = clamp(mr_sample.g * pc.metallic_roughness.y, 0.045, 1.0);

    vec3 normal = normalize(v_world_normal);
    vec3 view = normalize(u_frame.camera_position.xyz - v_world_position);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // Directional key light.
    vec3 key_direction = normalize(-u_frame.light_direction.xyz);
    vec3 key_radiance = u_frame.light_color.rgb * u_frame.light_color.a;
    vec3 color = shade(normal, view, key_direction, key_radiance, albedo, metallic, roughness,
                       f0) *
                 key_light_visibility(v_world_position, normal);

    // Point lights: inverse-square falloff inside a windowed radius so a
    // light's influence reaches exactly zero at its bounding sphere.
    int point_count = int(u_frame.light_direction.w);
    for (int i = 0; i < point_count; ++i) {
        vec3 to_light = u_frame.point_position_radius[i].xyz - v_world_position;
        float radius = u_frame.point_position_radius[i].w;
        float dist = length(to_light);
        if (dist >= radius) {
            continue;
        }
        float window = clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0);
        float attenuation = (window * window) / (dist * dist + 0.01);
        vec3 radiance = u_frame.point_color_intensity[i].rgb *
                        u_frame.point_color_intensity[i].a * attenuation;
        color += shade(normal, view, to_light / max(dist, 1e-4), radiance, albedo, metallic,
                       roughness, f0);
    }

    color += u_frame.ambient_color.rgb * albedo;
    color = color / (color + vec3(1.0)); // Reinhard: keep speculars from clipping
    o_color = vec4(color, base.a);
}
