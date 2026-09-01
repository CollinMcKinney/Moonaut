#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;
layout(location = 3) in float aModelIndex;
layout(location = 4) in vec3 aLocalFaceNormal;
layout(location = 5) in vec3 aLocalCentroid;

uniform mat4 uViewProj;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

layout(std140) uniform MaterialUniforms {
    vec3  uMatColor;
    vec3  uMatTint;
    float uMatAlpha;
    vec3  uMatEmissiveColor;
    float uMatEmissivePulseAmplitude;
    float uMatEmissivePulseFrequency;
    float uMatEmissivePulsePhase;
    float uMatTransmissionStrength;
    vec3  uMatSpecularTint;
    float uMatSurfaceRoughness;
    vec3  uMatRimColor;
    float uMatRimExponent;
    float uMatMetallic;
    float uMatIor;
    float uMatSubsurfaceStrength;
    float uMatFresnelExponent;
    vec3  uMatGoochCool;
    vec3  uMatGoochWarm;
    float uMatAmbientLightFactor;
    float uMatOrenNayarSigma;
    float uMatMinnaertK;
    float uMatSaturation;
    float uMatIridescenceStrength;
    vec3  uMatBackGlowColor;
    float uMatBumpAmplitude;
    float uMatBumpFrequency;
    float uMatBumpSpeed;
    float uMatRoughness;
    float uMatFringeIntensity;
    int   uMatCelBands;
    float uMatGlitchIntensity;
    int   uMatPosterizeLevels;
    vec3  uMatStrobeColor;
    float uMatStrobeFrequency;
    float uMatStrobePhase;
    vec3  uClearcoatColor;
    float uClearcoatRoughness;
    float uClearcoatStrength;
    vec3  uSheenColor;
    float uSheenExponent;
    float uSheenStrength;
    float uMatAnisotropic;
    vec3  uMatTransmissionTint;
};

layout(std140, row_major) uniform ModelMatrices {
    mat4 uModels[1024];
};

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vVertexColor;
flat out vec3 vFlatColor;
flat out vec3 vWorldCentroid;
flat out vec3 vLocalCentroid;
flat out vec3 vWorldFaceNormal;
flat out vec3 vLocalFaceNormal;

// ---- NEW: tangent and bitangent for anisotropy ----
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    mat4 model = uModels[int(aModelIndex)];
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Transform normal to world space
    vec3 normal = normalize(mat3(model) * aNormal);
    vNormal = normal;

    // ---- Compute tangent and bitangent from vertex normal ----
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    // Transform tangent and bitangent to world space (using model matrix)
    vTangent = normalize(mat3(model) * tangent);
    vBitangent = normalize(mat3(model) * bitangent);

    vLocalPos = aLocalPos;
    vLocalFaceNormal = normalize(aLocalFaceNormal);
    vLocalCentroid   = aLocalCentroid;
    vWorldFaceNormal = normalize(mat3(model) * vLocalFaceNormal);
    vWorldCentroid   = (model * vec4(vLocalCentroid, 1.0)).xyz;

    gl_Position = uViewProj * worldPos;
    vVertexColor = vec3(0.0);
    vFlatColor = vec3(0.0);
}