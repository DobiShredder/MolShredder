#version 440

layout(std140, binding = 0) uniform VolumeParameters {
    mat4 inverse_view_projection;
    mat4 world_to_texture;
    vec4 camera_position;
    vec4 sampling;
    vec4 pick_color;
};
layout(binding = 1) uniform sampler3D volume_texture;
layout(binding = 2) uniform sampler2D transfer_texture;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

bool intersect_unit_box(vec3 origin, vec3 direction, out float near_t,
                        out float far_t)
{
    vec3 safe_direction = vec3(
        abs(direction.x) < 1.0e-8 ? 1.0e-8 : direction.x,
        abs(direction.y) < 1.0e-8 ? 1.0e-8 : direction.y,
        abs(direction.z) < 1.0e-8 ? 1.0e-8 : direction.z);
    vec3 inverse_direction = 1.0 / safe_direction;
    vec3 first = (vec3(0.0) - origin) * inverse_direction;
    vec3 second = (vec3(1.0) - origin) * inverse_direction;
    vec3 lower = min(first, second);
    vec3 upper = max(first, second);
    near_t = max(max(lower.x, lower.y), max(lower.z, 0.0));
    far_t = min(min(upper.x, upper.y), upper.z);
    return far_t >= near_t;
}

void main()
{
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 near_world = inverse_view_projection * vec4(ndc, 0.0, 1.0);
    vec4 far_world = inverse_view_projection * vec4(ndc, 1.0, 1.0);
    near_world /= near_world.w;
    far_world /= far_world.w;
    vec3 world_origin = sampling.z > 0.5 ? camera_position.xyz : near_world.xyz;
    vec3 world_direction = normalize(far_world.xyz - world_origin);
    vec3 texture_origin = (world_to_texture * vec4(world_origin, 1.0)).xyz;
    vec3 texture_direction =
        (world_to_texture * vec4(world_direction, 0.0)).xyz;
    float near_t;
    float far_t;
    if (!intersect_unit_box(texture_origin, texture_direction, near_t, far_t))
        discard;

    float direction_length = length(texture_direction);
    float world_step = sampling.x / direction_length;
    vec4 accumulated = vec4(0.0);
    float distance = near_t;
    int maximum_steps = int(sampling.y + 0.5);
    for (int step = 0; step < 4096; ++step) {
        if (step >= maximum_steps || distance > far_t || accumulated.a >= 0.995)
            break;
        vec3 position = texture_origin + texture_direction * distance;
        float scalar = texture(volume_texture, position).r;
        vec4 color = texture(transfer_texture, vec2(scalar, 0.5));
        float corrected_alpha = 1.0 - pow(1.0 - color.a, sampling.w);
        float weight = (1.0 - accumulated.a) * corrected_alpha;
        accumulated.rgb += weight * color.rgb;
        accumulated.a += weight;
        distance += world_step;
    }
    if (accumulated.a <= 0.0001)
        discard;
    frag_color = accumulated;
}
