#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 instanceStart;
layout(location = 2) in float instanceWidthPixels;
layout(location = 3) in vec3 instanceEnd;
layout(location = 4) in vec4 instanceStartColor;
layout(location = 5) in vec4 instanceEndColor;

layout(location = 0) out vec4 vertexColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
} uniforms;

void main()
{
    vec4 clipStart = uniforms.mvp * vec4(instanceStart, 1.0);
    vec4 clipEnd = uniforms.mvp * vec4(instanceEnd, 1.0);
    vec2 ndcStart = clipStart.xy / clipStart.w;
    vec2 ndcEnd = clipEnd.xy / clipEnd.w;
    vec2 directionPixels = (ndcEnd - ndcStart) * uniforms.viewport.xy;
    float directionLength = length(directionPixels);
    vec2 sidePixels = directionLength > 1.0e-6
                          ? vec2(-directionPixels.y, directionPixels.x) /
                                directionLength
                          : vec2(0.0, 1.0);
    vec4 clip = mix(clipStart, clipEnd, corner.x);
    vec2 offsetNdc = sidePixels * corner.y * instanceWidthPixels * 2.0 /
                     max(uniforms.viewport.xy, vec2(1.0));
    clip.xy += offsetNdc * clip.w;
    gl_Position = clip;
    vertexColor = mix(instanceStartColor, instanceEndColor, corner.x);
}
