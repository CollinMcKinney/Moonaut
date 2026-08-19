#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;
layout(location = 3) in vec3 aColor;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;

void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    gl_Position = uViewProj * worldPos;
    vWorldPos = aPos;
    vNormal = aNormal;
    vLocalPos = aLocalPos;
}