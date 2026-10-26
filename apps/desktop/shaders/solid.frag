#version 440

layout(location = 0) in vec3 worldNormal;   // camera-space normal (view * model)
layout(location = 1) in vec4 vertexColor;
layout(location = 0) out vec4 fragmentColor;

layout(std140, binding = 0) uniform Buffer {
    mat4 mvp;
    mat4 model;
    vec4 viewport;
    vec4 lightParams;   // ambient, headlight strength, strength per extra light, shininess
    vec4 specParams;    // headlight specular, extra-light specular, extra light count
    vec4 lights[9];     // xyz: unit vector toward each extra light, camera space
} uniforms;

// PyMOL lighting: ambient + a headlight along the view direction + extra lights
// fixed in camera space, so the side facing the viewer is always lit.
void main()
{
    vec3 normal = normalize(worldNormal);
    vec3 toViewer = vec3(0.0, 0.0, 1.0);
    float diffuse = uniforms.lightParams.x +
                    uniforms.lightParams.y * max(dot(normal, toViewer), 0.0);
    float specular = uniforms.specParams.x *
                     pow(max(dot(normal, toViewer), 0.0), uniforms.lightParams.w);
    int extraLights = int(uniforms.specParams.z + 0.5);
    for (int i = 0; i < 9; ++i) {
        if (i >= extraLights)
            break;
        vec3 toLight = uniforms.lights[i].xyz;
        diffuse += uniforms.lightParams.z * max(dot(normal, toLight), 0.0);
        vec3 halfway = normalize(toLight + toViewer);
        specular += uniforms.specParams.y * pow(max(dot(normal, halfway), 0.0), uniforms.lightParams.w);
    }
    vec3 color = vertexColor.rgb * diffuse + vec3(specular);
    fragmentColor = vec4(min(color, vec3(1.0)), vertexColor.a);
}
