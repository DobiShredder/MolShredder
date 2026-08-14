#version 440

layout(location = 0) in vec3 unitPosition;
layout(location = 1) in vec3 unitNormal;
layout(location = 2) in vec3 instanceStart;
layout(location = 3) in float instanceRadius;
layout(location = 4) in vec3 instanceEnd;
layout(location = 5) in vec4 instanceColor;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec4 vertexColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    vec3 axis = instanceEnd - instanceStart;
    float axisLength = max(length(axis), 1.0e-8);
    vec3 w = axis / axisLength;
    vec3 referenceAxis = abs(w.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                           : vec3(0.0, 1.0, 0.0);
    vec3 u = normalize(cross(referenceAxis, w));
    vec3 v = cross(w, u);
    vec3 position = instanceStart +
                    u * (unitPosition.x * instanceRadius) +
                    v * (unitPosition.y * instanceRadius) +
                    w * (unitPosition.z * axisLength);
    vec3 normal = u * unitNormal.x + v * unitNormal.y + w * unitNormal.z;
    worldNormal = normalize(mat3(uniforms.model) * normal);
    vertexColor = instanceColor;
    gl_Position = uniforms.mvp * vec4(position, 1.0);
}
