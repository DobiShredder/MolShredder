#version 440

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec4 vertexColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec3 light = normalize(vec3(0.35, 0.55, 0.85));
    float diffuse = max(dot(normalize(worldNormal), light), 0.0);
    float intensity = 0.22 + 0.78 * diffuse;
    fragmentColor = vec4(vertexColor.rgb * intensity, vertexColor.a);
}
