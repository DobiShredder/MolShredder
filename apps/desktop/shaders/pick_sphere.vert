#version 440

layout(location = 0) in vec3 unitPosition;
layout(location = 2) in vec3 instanceCenter;
layout(location = 3) in float instanceRadius;
layout(location = 5) in vec4 instancePickColor;

layout(location = 0) out vec4 pickColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    pickColor = instancePickColor;
    gl_Position = uniforms.mvp *
                  vec4(instanceCenter + unitPosition * instanceRadius, 1.0);
}
