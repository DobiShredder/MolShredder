#version 440

layout(location = 0) in vec3 unitPosition;
layout(location = 1) in vec3 unitNormal;
layout(location = 2) in vec3 instanceStart;
layout(location = 3) in float instanceRadius;
layout(location = 4) in vec3 instanceEnd;
layout(location = 5) in vec4 instanceColor;
layout(location = 6) in vec4 instancePickColor;

layout(location = 0) out vec4 pickColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    vec3 axis = instanceEnd - instanceStart;
    float axisLength = length(axis);
    vec3 tangent = axisLength > 1.0e-7 ? axis / axisLength
                                       : vec3(0.0, 0.0, 1.0);
    vec3 helper = abs(tangent.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
                                       : vec3(0.0, 1.0, 0.0);
    vec3 side = normalize(cross(helper, tangent));
    vec3 up = cross(tangent, side);
    vec3 radial = side * unitPosition.x + up * unitPosition.y;
    vec3 position = instanceStart + radial * instanceRadius +
                    tangent * (unitPosition.z * axisLength);
    pickColor = instancePickColor;
    gl_Position = uniforms.mvp * vec4(position, 1.0);
}
