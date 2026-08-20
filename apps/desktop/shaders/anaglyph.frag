#version 440

layout(std140, binding = 0) uniform AnaglyphParameters {
    int mode;
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
    vec3 combined;
    if (mode == 0) {
        combined = vec3(luminance(left), 0.0, luminance(right));
    } else if (mode == 1) {
        combined = vec3(luminance(left), vec2(luminance(right)));
    } else if (mode == 2) {
        combined = vec3(left.r, right.g, right.b);
    } else if (mode == 3) {
        combined = vec3(luminance(left), right.g, right.b);
    } else {
        combined = vec3(0.7 * left.g + 0.3 * left.b, right.g, right.b);
    }
    frag_color = vec4(combined, 1.0);
}
