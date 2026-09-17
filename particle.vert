#version 330 core

layout(location = 0) in vec3 aCenter;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;

out vec4  vColor;
out vec2  vCorner;
out float vEyeDepth;

void main() {
    vec2 corner[6] = vec2[6](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    vec2 c = corner[gl_VertexID % 6];
    vCorner = c;
    float halfSize = aSize * 0.5;
    vec3 offset = uCamRight * c.x * halfSize + uCamUp * c.y * halfSize;
    vec4 worldPos = vec4(aCenter + offset, 1.0);
    gl_Position = uViewProj * worldPos;
    vColor = aColor;

    // Linear eye-space depth for the WBOIT weight function. gl_Position.w
    // equals -view_z for a standard perspective projection and is linear in
    // view space, so perspective-correct interpolation is exact at every
    // fragment regardless of triangle size.
    vEyeDepth = gl_Position.w;
}