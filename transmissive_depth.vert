#version 430 core

// =============================================================================
// transmissive_depth.vert — Transmissive depth pass
// =============================================================================
//
// Renders frontmost transmissive geometry so its gl_FragCoord.z can be
// captured by the fragment stage. Only position and model index are
// consumed; the model index selects a matrix from the shared
// ModelMatrices UBO.
// =============================================================================

layout(location = 0) in vec3  aPos;
layout(location = 3) in float aModelIndex;

uniform mat4 uViewProj;

layout(std140, row_major) uniform ModelMatrices {
    mat4 uModels[1024];
};

void main() {
    gl_Position = uViewProj * uModels[int(aModelIndex)] * vec4(aPos, 1.0);
}