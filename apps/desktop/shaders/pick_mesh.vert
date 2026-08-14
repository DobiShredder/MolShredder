#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 instancePickColor;

layout(location = 0) out vec4 pickColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    pickColor = instancePickColor;
    gl_Position = uniforms.mvp * vec4(position, 1.0);
}
