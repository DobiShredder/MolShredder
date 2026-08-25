#version 440

layout(std140, binding = 0) uniform StereoCompositeParameters {
    // presentation: 0 anaglyph, 1 row, 2 column, 3 checkerboard
    int presentation;
    int anaglyph_mode;
    int global_origin_x;
    int global_origin_y;
    int target_height;
    int padding_0;
    int padding_1;
    int padding_2;
};
layout(binding = 1) uniform sampler2D left_eye;
layout(binding = 2) uniform sampler2D right_eye;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float luminance(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec3 left = texture(left_eye, v_uv).rgb;
    vec3 right = texture(right_eye, v_uv).rgb;
    if (presentation != 0) {
        int local_x = int(floor(gl_FragCoord.x));
        int local_y_from_top = target_height - 1 - int(floor(gl_FragCoord.y));
        int physical_x = global_origin_x + local_x;
        int physical_y = global_origin_y + local_y_from_top;
        bool use_left = presentation == 1
            ? (physical_y & 1) != 0
            : presentation == 2
                ? (physical_x & 1) != 0
                : ((physical_x + physical_y) & 1) != 0;
        frag_color = vec4(use_left ? left : right, 1.0);
        return;
    }
    vec3 combined;
    if (anaglyph_mode == 0) {
        combined = vec3(luminance(left), 0.0, luminance(right));
    } else if (anaglyph_mode == 1) {
        combined = vec3(luminance(left), vec2(luminance(right)));
    } else if (anaglyph_mode == 2) {
        combined = vec3(left.r, right.g, right.b);
    } else if (anaglyph_mode == 3) {
        combined = vec3(luminance(left), right.g, right.b);
    } else {
        combined = vec3(0.7 * left.g + 0.3 * left.b, right.g, right.b);
    }
    frag_color = vec4(combined, 1.0);
}
