#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;
layout(location = 3) in float aModelIndex;
layout(location = 4) in vec3 aLocalFaceNormal;
layout(location = 5) in vec3 aLocalCentroid;
layout(location = 6) in vec4 aBoneIndex;   // 4 joint indices
layout(location = 7) in vec4 aBoneWeight;  // 4 weights, already normalised to sum 1

uniform mat4 uViewProj;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

// First joint matrix for this draw in the shared palette. -1 disables skinning.
uniform int uJointOffset;

// The mesh node's own transform. It is not baked into the vertices of a
// skinned primitive, so it is applied per draw instead.
uniform mat4 uNodeTransform;

layout(std430, binding = 3) readonly buffer JointPalette {
    mat4 uJoints[];
};

layout(std140) uniform MaterialUniforms {
    vec3  uMatAlbedo;               float uMatAlpha;                    // 16 bytes
    vec3  uMatTint;                 float uMatSpecularRoughness;        // 16 bytes
    vec3  uMatSpecularTint;         float uMatMetallic;                 // 16 bytes
    vec3  uMatF82Tint;              float uMatIOR;                      // 16 bytes

    vec3  uMatClearcoatColor;       float uMatClearcoat;                // 16 bytes
    vec3  uMatTransmissionTint;     float uMatTransmission;             // 16 bytes
    vec3  uMatSubsurfaceColor;      float uMatSubsurface;               // 16 bytes
    vec3  uMatSheenColor;           float uMatSheen;                    // 16 bytes

    float uMatSheenRoughness;
    float uMatDiffuseRoughness;
    float uMatTransmissionRoughness;
    float uMatClearcoatRoughness;                                       // 16 bytes

    float uMatAmbient;
    float uMatClearcoatIOR;
    float uMatThinFilm;
    float uMatThinFilmIOR;                                              // 16 bytes

    float uMatAnisotropic;
    float uMatDiffraction;
    float uMatEmissivePulseFrequency;
    float uMatEmissivePulsePhase;                                       // 16 bytes

    vec3  uMatEmissiveColor;        float uMatEmissivePulseAmplitude;   // 16 bytes
    vec3  uMatRimColor;             float uMatRimExponent;              // 16 bytes
    vec3  uMatBackGlowColor;        float uMatStrobeFrequency;          // 16 bytes
    vec3  uMatStrobeColor;          float uMatStrobePhase;              // 16 bytes
    vec3  uMatGoochCool;            float uMatSaturation;               // 16 bytes
    vec3  uMatGoochWarm;            float uMatBumpWaveAmplitude;        // 16 bytes

    float uMatBumpWaveFrequency;
    float uMatBumpWaveSpeed;
    float uMatBumpNoise;
    float uMatGlitch;                                                   // 16 bytes

    int   uMatCelBands;
    int   uMatPosterizeLevels;
    float _pad296;
    float _pad300;                                                      // 16 bytes
};

layout(std140, row_major) uniform ModelMatrices {
    mat4 uModels[1024];
};

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vVertexColor;
out float vEyeDepth;
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

    // Linear blend skinning: weighted sum of the four joint palettes.
    // An unskinned primitive carries all-zero weights, so the sum collapses
    // to zero and skin falls back to identity.
    mat4 skin = mat4(1.0);
    if (uJointOffset >= 0) {
        skin = mat4(0.0);
        for (int i = 0; i < 4; ++i) {
            float w = aBoneWeight[i];
            if (w > 0.0) {
                skin += w * uJoints[uJointOffset + int(aBoneIndex[i])];
            }
        }
        // A vertex whose weights all cancelled must still land somewhere sane.
        if (dot(skin[3], skin[3]) == 0.0) skin = mat4(1.0);
    }

    // The mesh node's own transform was not baked into the vertices for a
    // skinned primitive, so it is applied here ahead of the entity transform.
    mat4 objectToWorld = model * uNodeTransform * skin;

    vec4 worldPos = objectToWorld * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Joint matrices are rigid (rotation + translation) for skeletal animation,
    // so the 3x3 block transforms directions correctly and only needs
    // renormalising. This is not the inverse-transpose, which would be
    // required for non-uniform scale, but that case does not occur in a
    // skeletal pose.
    vec3 normal = normalize(mat3(objectToWorld) * aNormal);
    vNormal = normal;

    // ---- Compute tangent and bitangent from vertex normal ----
    // Continuous orthonormal basis (Duff et al., "Building an Orthonormal
    // Basis, Revisited"). The obvious formulation -- pick (0,1,0) unless the
    // normal is near vertical, else pick (1,0,0) -- has a hard threshold: a
    // normal crossing it swaps the reference axis and the frame snaps by ~90
    // degrees. On a static mesh each vertex's frame is frozen so the error is
    // merely stable, but an animated mesh sweeps normals through the threshold
    // every frame and the snap shows up as the anisotropic highlight jumping
    // around, which reads as the lighting flickering. This basis picks its
    // branch on n.z, where both branches agree, so the frame is continuous.
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    {
        float s = normal.z >= 0.0 ? 1.0 : -1.0;
        float a = -1.0 / (s + normal.z);
        float b = normal.x * normal.y * a;
        vec3 t = vec3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
        // Guard the degenerate poles, where the construction is undefined.
        tangent = (dot(t, t) > 1e-8) ? normalize(t) : vec3(1.0, 0.0, 0.0);
    }
    vec3 bitangent = cross(normal, tangent);

    // Transform tangent and bitangent to world space (using the full transform)
    vTangent = normalize(mat3(objectToWorld) * tangent);
    vBitangent = normalize(mat3(objectToWorld) * bitangent);

    // Sub-surface terms are evaluated in object space, so they follow the
    // skinned pose rather than the unskinned bind pose.
    vLocalPos = (skin * vec4(aLocalPos, 1.0)).xyz;
    vLocalFaceNormal = normalize(mat3(skin) * aLocalFaceNormal);
    vLocalCentroid   = (skin * vec4(aLocalCentroid, 1.0)).xyz;
    vWorldFaceNormal = normalize(mat3(objectToWorld) * normalize(aLocalFaceNormal));
    vWorldCentroid   = worldPos.xyz;

    gl_Position = uViewProj * worldPos;

    // Clip-space w = -view_z for a standard perspective projection, and
    // view_z is linear in view space, so perspective-correct interpolation
    // of this varying yields the exact linear eye depth at every fragment.
    // Used by the WBOIT weight function in the fragment shader.
    vEyeDepth = gl_Position.w;

    vVertexColor = vec3(0.0);
    vFlatColor = vec3(0.0);
}