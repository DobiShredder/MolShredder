#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec4 vertexColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
} uniforms;

void main()
{
    worldNormal = normalize(mat3(uniforms.model) * normal);
    vertexColor = color;
    gl_Position = uniforms.mvp * vec4(position, 1.0);
}
