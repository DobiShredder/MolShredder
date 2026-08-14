#version 440

layout(location = 0) in vec3 unitPosition;
layout(location = 1) in vec3 unitNormal;
layout(location = 2) in vec3 instanceCenter;
layout(location = 3) in float instanceRadius;
layout(location = 4) in vec4 instanceColor;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec4 vertexColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    vec3 position = instanceCenter + unitPosition * instanceRadius;
    worldNormal = normalize(mat3(uniforms.model) * unitNormal);
    vertexColor = instanceColor;
    gl_Position = uniforms.mvp * vec4(position, 1.0);
}
