#version 430 core

// =============================================================================
// material.frag — Forward-lit surface shader
// =============================================================================
//
// Program flow:
//
//   1. If ALPHA_PASS_BEHIND is defined, discard fragments that are not
//      strictly behind the frontmost transmissive surface. The front pass
//      has no such block — depth testing against the combined opaque +
//      transmissive depth buffer does the culling there.
//   2. Perturb the normal (wave and/or noise bump).
//   3. Compute specular-AA-filtered roughness, F0, F_avg, and coat F0.
//      Apply OpenPBR coat roughening to the base specular roughness.
//      Specular AA uses half-vector slope-space NDF filtering.
//   4. Loop over the lights in the fragment's cluster.
//        For each light:
//          a. Evaluate diffuse (EON or VMF, selected by EFFECT_VMF_DIFFUSE),
//             specular, transmission, clearcoat, sheen, back glow, and rim.
//          b. Attenuate the base by the layers above it (energy conservation).
//             Clearcoat uses OpenPBR darkening; sheen uses Kulla-Conty
//             multiscatter compensation with an analytic Charlie albedo fit.
//          c. Accumulate.
//   5. Add ambient diffuse and specular with energy conservation.
//      Specular uses corrected Turquin compensation (F0, not F_avg).
//   6. If EFFECT_TRANSMISSION is defined, sample the pre-transmissive colour
//      buffer along the refracted ray, apply the material's transmission
//      tint, and blend by Fresnel. This gives the glass/water/ice/etc.
//      surface its refracted background.
//   7. Apply VBAO to the ambient diffuse and ambient specular terms only.
//      Direct lighting is NOT multiplied by AO — AO is a visibility
//      approximation for the ambient term, not a shadow. Darkening direct
//      light by AO turns matte materials with zero specular tint pure black.
//   8. Add emissive, strobe, tint, fog.
//   9. Tone map (Halo 3 style luminance-only filmic curve, hue-preserving).
//  10. Apply LDR post effects.
//
// Output paths:
//   - Opaque and transmissive variants write a single FragColor plus the
//     view-space geometric normal for VBAO (target 1).
//   - Alpha variants compile with WBOIT_PASS and emit to a pair of
//     (accumulation, revealage) targets instead. The C side resolves them
//     with a full-screen composite between the two alpha passes.
//
// Subsurface scattering uses a Burley-inspired local diffusion
// approximation (exponential sum) instead of the previous wrap-light
// heuristic.
//
// =============================================================================

#ifdef DEPTH_ONLY
void main() { }
#else

// =============================================================================
// Constants
// =============================================================================
#define PI 3.14159265

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

#define MIN_PERCEPTUAL_ROUGHNESS 0.045

// EON Oren-Nayar model constants.
const float EON_CONST1 = 0.5 - 2.0 / (3.0 * PI);
const float EON_CONST2 = 2.0 / 3.0 - 28.0 / (15.0 * PI);

const float CLUSTER_NEAR_Z = 0.05;
const float CLUSTER_FAR_Z  = 1000.0;
const float CLUSTER_INV_LOG_RANGE = 1.0 / log2(CLUSTER_FAR_Z / CLUSTER_NEAR_Z);

// =============================================================================
// Inputs
// =============================================================================
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocalPos;
in vec3 vTangent;
in vec3 vBitangent;

// Linear eye-space depth, positive in front of the camera. Equals -view_z,
// interpolated exactly by the rasterizer from gl_Position.w. Used only by
// the WBOIT weight function.
in float vEyeDepth;

uniform vec3  uAmbientCol;
uniform vec3  uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec2  uScreenSize;

// View matrix. Used to transform the geometric world-space normal into
// view space for the VBAO normal buffer output.
uniform mat4  uView;

// --- Refraction inputs -------------------------------------------------------
// uRefractionSrc          : copy of the opaque + behind-alpha colour buffer,
//                           made just before the transmissive colour pass.
// uTransmissiveDepthTex   : frontmost transmissive gl_FragCoord.z, or 1.0
//                           where no transmissive surface is present.
// uAOTex                  : VBAO output, single channel, sampled on the
//                           ambient terms of the non-WBOIT variants.
// uAlphaPass              : 0 for ALPHA_PASS_BEHIND, 1 for ALPHA_PASS_FRONT.
//                           Informational; the compile-time defines select
//                           the behaviour. Kept for future runtime branches.
// uRefractionScale        : scalar applied to R.xy before the UV offset.
//
// These samplers are declared with layout(binding = N), so they have no
// addressable uniform location. The C side binds them to their fixed units
// (2, 3, 4) unconditionally in set_uniforms_for_variant.
layout(binding = 2) uniform sampler2D uRefractionSrc;

#ifdef ALPHA_PASS_BEHIND
layout(binding = 3) uniform sampler2D uTransmissiveDepthTex;
#endif

layout(binding = 4) uniform sampler2D uAOTex;

uniform float     uRefractionScale;
uniform int       uAlphaPass;

// -----------------------------------------------------------------------------
// MaterialUniforms
// -----------------------------------------------------------------------------
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
    float uMatSpecularRoughness;
    vec3  uMatRimColor;
    float uMatRimExponent;
    float uMatMetallic;
    float uMatIOR;
    float uMatSubsurfaceStrength;
    float uMatClearcoatIOR;
    vec3  uMatGoochCool;
    vec3  uMatGoochWarm;
    float uMatAmbientLightFactor;
    float uMatDiffuseRoughness;
    float uMatTransmissionRoughness;
    float uMatSaturation;
    float uMatIridescenceStrength;
    vec3  uMatBackGlowColor;
    float uMatBumpWaveAmplitude;
    float uMatBumpWaveFrequency;
    float uMatBumpWaveSpeed;
    float uMatBumpNoise;
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
    float uSheenRoughness;
    float uSheenStrength;
    float uMatAnisotropic;
    vec3  uMatTransmissionTint;
};

// =============================================================================
// Cluster data
// =============================================================================
#define CLUSTER_TILE_SIZE     16
#define CLUSTER_DEPTH_SLICES  24
#define CLUSTER_MAX_LIGHTS_PER 64

struct Light {
    vec4  pos;
    vec4  dir;
    vec4  color;
    float range;
    float inner_cos;
    float outer_cos;
    float falloff;
};

uniform int uNumTilesX;
uniform int uNumTilesY;

layout(std430, binding = 0) buffer LightBuffer         { Light lights[]; };
layout(std430, binding = 1) buffer ClusterBuffer       { uint clusterLights[]; };
layout(std430, binding = 2) buffer ClusterOffsetBuffer { uint clusterOffsets[]; };

// =============================================================================
// Utility
// =============================================================================
float saturate(float x) { return clamp(x, 0.0, 1.0); }

// =============================================================================
// Hash and value noise
// =============================================================================
uint hash(uint x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

float hash_float(vec3 p) {
    uint h = hash(floatBitsToUint(p.x));
    h = hash(h ^ floatBitsToUint(p.y));
    h = hash(h ^ floatBitsToUint(p.z));
    return float(h) / 4294967296.0;
}

float value_noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash_float(i);
    float b = hash_float(i + vec3(1.0, 0.0, 0.0));
    float c = hash_float(i + vec3(0.0, 1.0, 0.0));
    float d = hash_float(i + vec3(1.0, 1.0, 0.0));
    float e = hash_float(i + vec3(0.0, 0.0, 1.0));
    float f1 = hash_float(i + vec3(1.0, 0.0, 1.0));
    float g = hash_float(i + vec3(0.0, 1.0, 1.0));
    float h = hash_float(i + vec3(1.0, 1.0, 1.0));
    float mix1 = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    float mix2 = mix(mix(e, f1, f.x), mix(g, h, f.x), f.y);
    return mix(mix1, mix2, f.z);
}

// =============================================================================
// Normal perturbation
// =============================================================================
float bump_height(vec3 p, float time, float speed, float noise) {
    float phase1 = p.x * 1.0 + p.z * 0.5 + time * speed + noise * 2.0;
    float phase2 = p.y * 0.7 + p.x * 0.3 + time * speed * 0.7 + 1.2 + noise * 1.5;
    return sin(phase1) * 0.6 + sin(phase2) * 0.4;
}

vec3 perturb_normal_wave(vec3 N, vec3 localPos) {
    float freq  = uMatBumpWaveFrequency;
    float speed = uMatBumpWaveSpeed;
    float time  = uTime;

    vec3 p = localPos * freq;
    float eps = 0.01;

    vec3 px = p + vec3(eps, 0.0, 0.0);
    vec3 py = p + vec3(0.0, eps, 0.0);
    vec3 pz = p + vec3(0.0, 0.0, eps);

    float h0 = bump_height(p,  time, speed, value_noise(p  * 0.1));
    float hx = bump_height(px, time, speed, value_noise(px * 0.1));
    float hy = bump_height(py, time, speed, value_noise(py * 0.1));
    float hz = bump_height(pz, time, speed, value_noise(pz * 0.1));

    vec3 gradient = vec3(hx - h0, hy - h0, hz - h0) / eps;
    gradient *= uMatBumpWaveAmplitude;
    return normalize(N - gradient);
}

vec3 perturb_normal_noise(vec3 N, vec3 worldPos, vec3 localPos) {
    vec3 dpx = dFdx(localPos);
    vec3 dpy = dFdy(localPos);
    float footprint = sqrt(max(dot(dpx, dpx), dot(dpy, dpy)));

    const float NOISE_FREQ = 16.0;
    float fade = saturate(1.0 - footprint * NOISE_FREQ);

    float fadeSmooth = smoothstep(0.0, 0.05, fade);
    if (fadeSmooth < 1e-4) return N;

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 p_rough = localPos * NOISE_FREQ;
    float eps_rough = 0.01;
    float h0       = value_noise(p_rough);
    float hx_rough = value_noise(p_rough + vec3(eps_rough, 0.0, 0.0));
    float hy_rough = value_noise(p_rough + vec3(0.0, eps_rough, 0.0));
    float grad_u = (hx_rough - h0) / eps_rough;
    float grad_v = (hy_rough - h0) / eps_rough;

    float strength = uMatBumpNoise * 0.5;
    vec3 V = normalize(uCamEye - worldPos);
    float NdotV = max(dot(N, V), 0.0);
    float grazing = 1.0 - NdotV;
    strength *= (0.5 + 0.5 * (1.0 - grazing));
    strength *= fadeSmooth;

    vec3 perturb = tangent * grad_u * strength + bitangent * grad_v * strength;

    const float MAX_PERTURB = 0.5;
    float pl = length(perturb);
    if (pl > MAX_PERTURB) perturb *= MAX_PERTURB / pl;

    return normalize(N - perturb);
}

vec3 perturb_normal(vec3 N, vec3 worldPos, vec3 localPos) {
#ifdef EFFECT_BUMP_WAVE
    N = perturb_normal_wave(N, localPos);
#endif
#ifdef EFFECT_BUMP_NOISE
    N = perturb_normal_noise(N, worldPos, localPos);
#endif
    return normalize(N);
}

// =============================================================================
// Specular anti-aliasing — half-vector slope-space NDF filtering
// =============================================================================
float specular_aa_roughness_halfvec(vec3 N, vec3 V, vec3 L,
                                    float perceptualRoughness) {
    vec3 Hraw = L + V;
    if (dot(Hraw, Hraw) < 1e-8) return perceptualRoughness;
    vec3 H = normalize(Hraw);

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    float NdotH = max(dot(N, H), 1e-4);
    vec3 dHdx = dFdx(H);
    vec3 dHdy = dFdy(H);

    float dHdx_slope = dot(dHdx, T) / NdotH;
    float dHdy_slope = dot(dHdy, B) / NdotH;

    float slopeVariance = 0.25 * (dHdx_slope * dHdx_slope
                                 + dHdy_slope * dHdy_slope);
    slopeVariance = min(slopeVariance, 0.18);

    float perceptual2 = perceptualRoughness * perceptualRoughness;
    float kernel = slopeVariance / (perceptual2 + slopeVariance + 1e-6);
    kernel = clamp(kernel, 0.0, 1.0);

    float filtered2 = perceptual2 + kernel * slopeVariance;
    return sqrt(min(filtered2, 1.0));
}

// Legacy normal-variance filter, retained as a fallback for call sites that
// don't have L available (e.g. ambient).
float specular_aa_roughness(vec3 N, float perceptualRoughness) {
    vec3 dndx = dFdx(N);
    vec3 dndy = dFdy(N);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    variance = min(variance, 0.18);

    float perceptual2 = perceptualRoughness * perceptualRoughness;
    float kernel = variance / (perceptual2 + variance + 1e-6);
    kernel = clamp(kernel, 0.0, 1.0);

    float filtered2 = perceptual2 + kernel * variance;
    return sqrt(min(filtered2, 1.0));
}

// =============================================================================
// Specular occlusion
// =============================================================================
float specular_occlusion(float NdotV, float ao, float roughness) {
    float occ = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    float roughnessFade = smoothstep(0.1, 0.3, roughness);
    return mix(1.0, occ, roughnessFade);
}

// =============================================================================
// Material scalars
// =============================================================================
vec3 compute_fresnel_f0(vec3 baseColor, float metallic, float ior) {
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    vec3 dielectricF0 = vec3(ratio * ratio);
    return mix(dielectricF0, baseColor, metallic);
}

vec3 compute_clearcoat_f0() {
    float ior = uMatClearcoatIOR;
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    return vec3(ratio * ratio);
}

vec3 compute_dielectric_f0() {
    float ior = uMatIOR;
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    return vec3(ratio * ratio);
}

// =============================================================================
// Microfacet distributions
// =============================================================================
float D_GGX(float NdotH, float perceptualRoughness) {
    float alpha  = perceptualRoughness * perceptualRoughness;
    float alpha2 = alpha * alpha;
    float denom  = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

float D_GTR2_aniso(float HdotT, float HdotB, float HdotN, float ax, float ay) {
    float denomDist = (HdotT * HdotT) / (ax * ax)
                    + (HdotB * HdotB) / (ay * ay)
                    + HdotN * HdotN;
    return 1.0 / (PI * ax * ay * denomDist * denomDist);
}

float D_Charlie(float NdotH, float sheenRoughness) {
    float invAlpha = 1.0 / max(sheenRoughness, 1e-4);
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0 - cos2h, 1e-4);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

// =============================================================================
// Visibility terms
// =============================================================================
float V_SmithGGXCorrelated(float NdotL, float NdotV, float perceptualRoughness) {
    float alpha = perceptualRoughness * perceptualRoughness;
    float a2    = alpha * alpha;
    float lambdaV = NdotL * sqrt(max(0.0, NdotV * (NdotV - NdotV * a2) + a2));
    float lambdaL = NdotV * sqrt(max(0.0, NdotL * (NdotL - NdotL * a2) + a2));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float V_SmithGGXCorrelated_Aniso(float NdotL, float NdotV,
                                 float LdotT, float LdotB,
                                 float VdotT, float VdotB,
                                 float at, float ab) {
    float lambdaV = NdotL * length(vec3(VdotT * at, VdotB * ab, NdotV));
    float lambdaL = NdotV * length(vec3(LdotT * at, LdotB * ab, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float lambdaSheenNumericHelper(float x, float alphaG) {
    float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);

    float a = mix(25.3245, 21.5473, 1.0 - oneMinusAlphaSq);
    float b = mix( 3.32435, 3.82987, 1.0 - oneMinusAlphaSq);
    float c = mix( 0.16801, 0.19823, 1.0 - oneMinusAlphaSq);
    float d = mix(-1.27393, -1.97760, 1.0 - oneMinusAlphaSq);
    float e = mix(-4.85967, -4.32054, 1.0 - oneMinusAlphaSq);

    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

float lambdaSheen(float cosTheta, float alphaG) {
    if (cosTheta < 0.5) {
        return exp(lambdaSheenNumericHelper(cosTheta, alphaG));
    } else {
        return exp(2.0 * lambdaSheenNumericHelper(0.5, alphaG)
                   - lambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
    }
}

float lambdaSheenLight(float cosTheta, float alphaG) {
    float lambda = lambdaSheen(cosTheta, alphaG);
    float softener = 1.0 + 2.0 * pow(1.0 - cosTheta, 8.0);
    return pow(lambda, softener);
}

// =============================================================================
// Fresnel
// =============================================================================
vec3 F_Schlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

vec3 F82_tint(vec3 baseColor) {
    return mix(vec3(1.0), baseColor, 0.5);
}

vec3 F_Schlick_F82(vec3 F0, vec3 baseColor, float cosTheta) {
    float mu = saturate(cosTheta);
    const float MU_HAT    = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 Cs = F82_tint(baseColor);
    vec3 F_schlick_edge = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 b = F_schlick_edge * (1.0 - Cs) / denom;
    vec3 F_schlick = F_Schlick(F0, mu);
    return F_schlick - b * mu * pow(1.0 - mu, 6.0);
}

vec3 compute_fresnel_avg(vec3 F0, vec3 baseColor, float metallic) {
    vec3 F_avg_schlick = F0 + (1.0 - F0) / 21.0;
    const float MU_HAT    = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 Cs_metal = F82_tint(baseColor);
    vec3 F_schlick_edge = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 b_metal = F_schlick_edge * (1.0 - Cs_metal) / denom;
    return F_avg_schlick - metallic * b_metal / 126.0;
}

// =============================================================================
// Specular BRDF
// =============================================================================
float E_ss_GGX(float NdotV, float perceptualRoughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 r = perceptualRoughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB.x + AB.y;
}

// Corrected Turquin multiscatter compensation. Uses F0, not F_avg, per
// three.js PR #33983. At roughness 1.0, NoV 0.5, white-furnace goes from
// 0.72 (old form) to 0.94 (corrected form).
vec3 specular_multiscatter_comp(vec3 fss, vec3 F0,
                                float roughness, float NdotV) {
    float E_ss = E_ss_GGX(NdotV, roughness);
    vec3 energyCompensation = 1.0 + F0 * (1.0 / max(E_ss, 1e-4) - 1.0);
    return fss * energyCompensation;
}

vec3 specular_microfacet_iso(float NdotL, float NdotV, float NdotH,
                             vec3 fresnel, vec3 F0, float roughness) {
    float D = D_GGX(NdotH, roughness);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, roughness);
    vec3 fss = D * vis * fresnel;
    return specular_multiscatter_comp(fss, F0, roughness, NdotV);
}

vec3 specular_microfacet_aniso(vec3 V, vec3 L, vec3 H,
                               float NdotL, float NdotV, float NdotH,
                               vec3 fresnel, vec3 F0, float roughness,
                               vec3 Tangent, vec3 Bitangent) {
    float anisotropy = clamp(uMatAnisotropic, -1.0, 1.0);
    float aspect = sqrt(1.0 - 0.9 * anisotropy);
    float alpha = roughness * roughness;
    float ax = max(alpha / aspect, 0.001);
    float ay = max(alpha * aspect, 0.001);

    float HdotT = dot(H, Tangent);
    float HdotB = dot(H, Bitangent);
    float D = D_GTR2_aniso(HdotT, HdotB, NdotH, ax, ay);

    float LdotT = dot(L, Tangent);
    float LdotB = dot(L, Bitangent);
    float VdotT = dot(V, Tangent);
    float VdotB = dot(V, Bitangent);

    float vis = V_SmithGGXCorrelated_Aniso(NdotL, NdotV,
                                           LdotT, LdotB,
                                           VdotT, VdotB,
                                           ax, ay);
    vec3 fss = D * vis * fresnel;
    return specular_multiscatter_comp(fss, F0, roughness, NdotV);
}

// =============================================================================
// Diffuse lobes — EON and VMF, selectable at the call site
// =============================================================================

// ---- EON Oren-Nayar --------------------------------------------------------
float E_FON_approx(float mu, float r) {
    float mucomp  = 1.0 - mu;
    float mucomp2 = mucomp * mucomp;
    const mat2 Gcoeffs = mat2(0.0571085289, -0.332181442,
                              0.491881867,  0.0714429953);
    float GoverPi = dot(Gcoeffs * vec2(mucomp, mucomp2), vec2(1.0, mucomp2));
    return (1.0 + r * GoverPi) / (1.0 + EON_CONST1 * r);
}

vec3 diffuse_eon_oren_nayar(vec3 N, vec3 V, vec3 L, vec3 baseColor, float roughness) {
    float mu_i = max(dot(N, L), 0.0);
    float mu_o = max(dot(N, V), 0.0);
    float s    = dot(L, V) - mu_i * mu_o;

    float sovertF;
    if (s > 0.0) {
        float denom = max(mu_i, mu_o);
        sovertF = denom > 1e-4 ? s / denom : 0.0;
    } else {
        sovertF = s;
    }

    float AF = 1.0 / (1.0 + EON_CONST1 * roughness);

    vec3 f_ss = (baseColor / PI) * AF * (1.0 + roughness * sovertF);

    float EFo   = E_FON_approx(mu_o, roughness);
    float EFi   = E_FON_approx(mu_i, roughness);
    float avgEF = AF * (1.0 + EON_CONST2 * roughness);

    vec3 denom = max(vec3(1e-4), vec3(1.0) - baseColor * (1.0 - avgEF));
    vec3 rho_ms = (baseColor * baseColor) * avgEF / denom;

    const float eps = 1e-7;
    vec3 f_ms = (rho_ms / PI)
              * max(eps, 1.0 - EFo)
              * max(eps, 1.0 - EFi)
              / max(eps, 1.0 - avgEF);

    return f_ss + f_ms;
}

// ---- VMF von Mises-Fisher --------------------------------------------------
float Coth(float x) { return (exp(-x) + exp(x)) / (-exp(-x) + exp(x)); }
float Sinh(float x) { return -0.5 * 1.0 / exp(x) + exp(x) / 2.0; }

float erf_approx(float x) {
    float sgn = sign(x);
    float ax  = abs(x);
    float t   = 1.0 / (1.0 + 0.3275911 * ax);
    float y   = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t
                      - 0.284496736) * t + 0.254829592) * t * exp(-ax * ax);
    return sgn * y;
}

float sigmaBeckmannExpanded(float u, float m) {
    if (0.0 == m) return (u + abs(u)) / 2.0;
    float m2 = m * m;
    if (1.0 == u) return 1.0 - 0.5 * m2;
    float expansionTerm = -0.25 * m2 * (u + abs(u));
    float u2 = u * u;
    return ((exp(u2 / (m2 * (-1.0 + u2))) * m * sqrt(1.0 - u2)) / sqrt(PI)
            + u * (1.0 + erf_approx(u / (m * sqrt(1.0 - u2))))) / 2.0
           + expansionTerm;
}

float sigmaVMF(float u, float m) {
    if (m < 0.25) return sigmaBeckmannExpanded(u, m);
    float m2 = m * m;
    float m4 = m2 * m2;
    float m8 = m4 * m4;
    float u2 = u * u;
    float u4 = u2 * u2;
    float u6 = u2 * u4;
    float u8 = u4 * u4;
    float u10 = u6 * u4;
    float u12 = u6 * u6;
    float coth2m2 = Coth(2.0 / m2);
    float sinh2m2 = Sinh(2.0 / m2);
    if (m > 0.9)
        return 0.25 - 0.25 * u * (m2 - 2.0 * coth2m2)
             + 0.0390625 * (-1.0 + 3.0 * u2)
               * (4.0 + 3.0 * m4 - 6.0 * m2 * coth2m2);
    return 0.25 - 0.25 * u * (m2 - 2.0 * coth2m2)
         + 0.0390625 * (-1.0 + 3.0 * u2)
           * (4.0 + 3.0 * m4 - 6.0 * m2 * coth2m2)
         - 0.000732421875 * (3.0 - 30.0 * u2 + 35.0 * u4)
           * (16.0 + 180.0 * m4 + 105.0 * m8
              - 10.0 * m2 * (8.0 + 21.0 * m4) * coth2m2)
         + 0.000049591064453125 * (-5.0 + 105.0 * u2 - 315.0 * u4 + 231.0 * u6)
           * (64.0 + 105.0 * m4 * (32.0 + 180.0 * m4 + 99.0 * m8)
              - 42.0 * m2 * (16.0 + 240.0 * m4 + 495.0 * m8) * coth2m2)
         + (1.0132789611816406e-6 * (35.0 - 1260.0 * u2 + 6930.0 * u4
              - 12012.0 * u6 + 6435.0 * u8) * (1.0 + coth2m2)
            * (-256.0 - 315.0 * m4 * (128.0 + 33.0 * m4
                 * (80.0 + 364.0 * m4 + 195.0 * m8))
               + 18.0 * m2 * (256.0 + 385.0 * m4
                 * (32.0 + 312.0 * m4 + 585.0 * m8)) * coth2m2)
            * sinh2m2) / exp(2.0 / m2)
         - (9.12696123123169e-8 * (-63.0 + 3465.0 * u2 - 30030.0 * u4
              + 90090.0 * u6 - 109395.0 * u8 + 46189.0 * u10)
            * (1.0 + coth2m2)
            * (-1024.0 - 495.0 * m4 * (768.0 + 91.0 * m4
                 * (448.0 + 15.0 * m4 * (448.0 + 1836.0 * m4 + 969.0 * m8)))
               + 110.0 * m2 * (256.0 + 117.0 * m4
                 * (256.0 + 21.0 * m4
                    * (336.0 + 85.0 * m4 * (32.0 + 57.0 * m4)))) * coth2m2)
            * sinh2m2) / exp(2.0 / m2)
         + (4.3655745685100555e-9 * (231.0 - 18018.0 * u2 + 225225.0 * u4
              - 1.02102e6 * u6 + 2.078505e6 * u8
              - 1.939938e6 * u10 + 676039.0 * u12)
            * (1.0 + coth2m2)
            * (-4096.0 - 3003.0 * m4 * (1024.0 + 45.0 * m4
                 * (2560.0 + 51.0 * m4
                    * (1792.0 + 285.0 * m4
                       * (80.0 + 308.0 * m4 + 161.0 * m8))))
               + 78.0 * m2 * (2048.0 + 385.0 * m4
                 * (1280.0 + 153.0 * m4
                    * (512.0 + 57.0 * m4
                       * (192.0 + 35.0 * m4 * (40.0 + 69.0 * m4)))))
               * coth2m2)
            * sinh2m2) / exp(2.0 / m2);
}

vec3 Erf(vec3 c) {
    return vec3(erf_approx(c.x), erf_approx(c.y), erf_approx(c.z));
}

vec3 nonNegative(vec3 c) { return vec3(max(0.0, c.x), max(0.0, c.y), max(0.0, c.z)); }

vec3 fm(float ui, float uo, float r, vec3 c) {
    vec3 C = sqrt(1.0 - c);
    vec3 Ck = (1.0 - 0.5441615108674713 * C
                   - 0.45302863761693374 * (1.0 - c))
            / (1.0 + 1.4293127703064865 * C);
    vec3 Ca = c / pow(1.0075 + 1.16942 * C,
                      atan((0.0225272 + (-0.264641 + r) * r) * Erf(C)));
    return nonNegative(0.384016 * (-0.341969 + Ca) * Ca * Ck
                       * (-0.0578978 / (0.287663 + ui * uo)
                          + abs(-0.0898863 + tanh(r))));
}

vec3 vMFdiffuseBRDF(float ui, float uo, float phi, float r, vec3 c) {
    if (0.0 == r) return c / PI;
    float m = -log(1.0 - sqrt(r));
    float sigmai = sigmaVMF(ui, m);
    float sigmao = sigmaVMF(uo, m);
    float sigmano = sigmaVMF(-uo, m);
    float sigio = sigmai * sigmao;
    float sigdenom = uo * sigmai + ui * sigmano;
    float r2 = r * r;
    float r25 = r2 * sqrt(r);
    float r3 = r * r2;
    float r4 = r2 * r2;
    float r45 = r4 * sqrt(r);
    float r5 = r3 * r2;
    float ui2 = ui * ui;
    float uo2 = uo * uo;
    float sqrtuiuo = sqrt((1.0 - ui2) * (1.0 - uo2));
    float C100 = 1.0 + (-0.1 * r + 0.84 * r4) / (1.0 + 9.0 * r3);
    float C101 = (0.0173 * r + 20.4 * r2 - 9.47 * r3) / (1.0 + 7.46 * r);
    float C102 = (-0.927 * r + 2.37 * r2) / (1.24 + r2);
    float C103 = (-0.11 * r - 1.54 * r2) / (1.0 - 1.05 * r + 7.1 * r2);
    float f10 = ((C100 + C101 * ui * uo + C102 * ui2 * uo2
                  + C103 * (ui2 + uo2)) * sigio) / sigdenom;
    float C110 = (0.54 * r - 0.182 * r3) / (1.0 + 1.32 * r2);
    float C111 = (-0.097 * r + 0.62 * r2 - 0.375 * r3) / (1.0 + 0.4 * r3);
    float C112 = 0.283 + 0.862 * r - 0.681 * r2;
    float f11 = (sqrtuiuo * (C110 + C111 * ui * uo))
              * pow(sigio, C112) / sigdenom;
    float C120 = (2.25 * r + 5.1 * r2) / (1.0 + 9.8 * r + 32.4 * r2);
    float C121 = (-4.32 * r + 6.0 * r3) / (1.0 + 9.7 * r + 287.0 * r3);
    float f12 = ((1.0 - ui2) * (1.0 - uo2) * (C120 + C121 * uo)
                 * (C120 + C121 * ui)) / (ui + uo);
    float C200 = (0.00056 * r + 0.226 * r2) / (1.0 + 7.07 * r2);
    float C201 = (-0.268 * r + 4.57 * r2 - 12.04 * r3) / (1.0 + 36.7 * r3);
    float C202 = (0.418 * r + 2.52 * r2 - 0.97 * r3) / (1.0 + 10.0 * r2);
    float C203 = (0.068 * r - 2.25 * r2 + 2.65 * r3) / (1.0 + 21.4 * r3);
    float C204 = (0.05 * r - 4.22 * r3) / (1.0 + 17.6 * r2 + 43.1 * r3);
    float f20 = (C200 + C201 * ui * uo + C203 * ui2 * uo2
                 + C202 * (ui + uo) + C204 * (ui2 + uo2)) / (ui + uo);
    float C210 = (-0.049 * r - 0.027 * r3) / (1.0 + 3.36 * r2);
    float C211 = (2.77 * r2 - 8.332 * r25 + 6.073 * r3) / (1.0 + 50.0 * r4);
    float C212 = (-0.431 * r2 - 0.295 * r3) / (1.0 + 23.9 * r3);
    float f21 = (sqrtuiuo * (C210 + C211 * ui * uo
                             + C212 * (ui + uo))) / (ui + uo);
    float C300 = (-0.083 * r3 + 0.262 * r4) / (1.0 - 1.9 * r2 + 38.6 * r4);
    float C301 = (-0.627 * r2 + 4.95 * r25 - 2.44 * r3) / (1.0 + 31.5 * r4);
    float C302 = (0.33 * r2 + 0.31 * r25 + 1.4 * r3) / (1.0 + 20.0 * r3);
    float C303 = (-0.74 * r2 + 1.77 * r25 - 4.06 * r3) / (1.0 + 215.0 * r5);
    float C304 = (-1.026 * r3) / (1.0 + 5.81 * r2 + 13.2 * r3);
    float f30 = (C300 + C301 * ui * uo + C303 * ui2 * uo2
                 + C302 * (ui + uo) + C304 * (ui2 + uo2)) / (ui + uo);
    float C310 = (0.028 * r2 - 0.0132 * r3) / (1.0 + 7.46 * r2 - 3.315 * r4);
    float C311 = (-0.134 * r2 + 0.162 * r25 + 0.302 * r3) / (1.0 + 57.5 * r45);
    float C312 = (-0.119 * r2 + 0.5 * r25 - 0.207 * r3) / (1.0 + 18.7 * r3);
    float f31 = (sqrtuiuo * (C310 + C311 * ui * uo
                             + C312 * (ui + uo))) / (ui + uo);
    return (1.0 / PI) * (c * max(0.0, f10 + f11 * cos(phi) * 2.0
                                 + f12 * cos(2.0 * phi) * 2.0)
                         + c * c * max(0.0, f20 + f21 * cos(phi) * 2.0)
                         + c * c * c * max(0.0, f30 + f31 * cos(phi) * 2.0))
           + fm(ui, uo, r, c);
}

// =============================================================================
// Subsurface scattering — Burley-inspired local diffusion
// =============================================================================
float subsurface_burley(float NdotL_raw, float NdotV, float strength) {
    float d = 1.0 / max(strength, 1e-3);
    float r = 1.0 / max(NdotV, 0.1);
    float R = (exp(-r / d) + exp(-r / (3.0 * d))) / (8.0 * PI * d);
    float wrap = (NdotL_raw + 0.5) / 1.5;
    return clamp(R * pow(saturate(wrap), 2.0), 0.0, 1.0);
}

// =============================================================================
// Layered lobes
// =============================================================================
vec3 clearcoat_disney(float NdotL, float NdotV, float NdotH,
                      float clearcoatGloss, vec3 clearcoatFresnel,
                      vec3 clearcoatF0) {
    float alpha = mix(0.1, 0.001, clamp(clearcoatGloss, 0.0, 1.0));
    float perceptualRough = sqrt(alpha);

    float D   = D_GGX(NdotH, perceptualRough);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, perceptualRough);
    vec3  fss = D * vis * clearcoatFresnel;

    vec3 clearcoatF_avg = clearcoatF0 + (1.0 - clearcoatF0) / 21.0;

    return specular_multiscatter_comp(fss, clearcoatF_avg,
                                      perceptualRough, NdotV);
}

vec3 sheen_charlie(vec3 baseColor, vec3 sheenColorTint,
                   float NdotL, float NdotV, float NdotH,
                   float sheenRoughness, float sheenStrength) {
    const float SHEEN_TINT = 0.3;

    float luma = dot(baseColor, LUMA_REC709);
    vec3 sheenColor = mix(vec3(1.0), baseColor / max(luma, 1e-4), SHEEN_TINT);
    sheenColor *= sheenColorTint;
    sheenColor = clamp(sheenColor, 0.0, 1.0);

    float D = D_Charlie(NdotH, sheenRoughness);

    float G = 1.0 / (1.0 + lambdaSheen(NdotV, sheenRoughness)
                         + lambdaSheenLight(NdotL, sheenRoughness));
    float V = G / (4.0 * NdotL * NdotV);

    return sheenColor * D * V * sheenStrength;
}

// =============================================================================
// OpenPBR coat helpers
// =============================================================================
float compute_coat_darkening(vec3 coatF0, vec3 baseColor,
                             float NdotV, float baseRoughness) {
    float Ks = F_Schlick(coatF0, NdotV).r;
    float Kr = coatF0.r + (1.0 - coatF0.r) / 21.0;
    float K0 = mix(Ks, Kr, clamp(baseRoughness, 0.0, 1.0));
    float E_base = dot(baseColor, LUMA_REC709);
    return (1.0 - K0) / (1.0 - E_base * K0);
}

// =============================================================================
// Sheen energy compensation — Kulla-Conty with analytic Charlie albedo
// =============================================================================
float sheen_directional_albedo(float NdotV, float sheenRoughness) {
    float r2 = sheenRoughness * sheenRoughness;

    float a = sheenRoughness < 0.25
        ? -339.2 * r2 + 161.4 * sheenRoughness - 25.9
        : -8.48 * r2 + 14.3 * sheenRoughness - 9.95;

    float b = sheenRoughness < 0.25
        ? 44.0 * r2 - 23.7 * sheenRoughness + 3.26
        : 1.97 * r2 - 3.27 * sheenRoughness + 0.72;

    float DG = exp(a * NdotV + b)
             + (sheenRoughness < 0.25
                ? 0.0 : 0.1 * (sheenRoughness - 0.25));

    return saturate(DG / PI);
}

float sheen_multiscatter_comp(float NdotL, float NdotV, float sheenRoughness,
                              vec3 sheenColorTint) {
    float E_o = sheen_directional_albedo(NdotV, sheenRoughness);
    float E_i = sheen_directional_albedo(NdotL, sheenRoughness);
    float E_avg = 0.5 * (E_o + E_i);

    vec3 F_avg = clamp(sheenColorTint, 0.0, 0.99);

    float num = (1.0 - E_o) * (1.0 - E_i)
              * dot(F_avg, LUMA_REC709) * dot(F_avg, LUMA_REC709) * E_avg;
    float den = PI * (1.0 - E_avg)
              * (1.0 - dot(F_avg, LUMA_REC709) * (1.0 - E_avg));

    return saturate(num / max(den, 1e-4));
}

// =============================================================================
// Transmission
// =============================================================================
vec3 transmission_ggx(vec3 N, vec3 V, vec3 L,
                      float NdotL, float NdotV,
                      float roughness, float strength,
                      vec3 tint, vec3 F0, float ior) {
    float eta_i = gl_FrontFacing ? 1.0  : ior;
    float eta_t = gl_FrontFacing ? ior  : 1.0;

    float etaV = eta_t / eta_i;

    vec3 HtRaw = L + etaV * V;
    float lenSq = dot(HtRaw, HtRaw);
    if (lenSq < 1e-8) return vec3(0.0);
    vec3 Ht = HtRaw * inversesqrt(lenSq);

    float NdotHt = dot(N, Ht);
    if (NdotHt <= 0.0) return vec3(0.0);

    float VdotHt = dot(V, Ht);
    if (VdotHt <= 0.0) return vec3(0.0);

    float etaRel = eta_i / eta_t;
    float sin2t  = etaRel * etaRel * (1.0 - VdotHt * VdotHt);
    if (sin2t >= 1.0) return vec3(0.0);

    float transRoughness = clamp(roughness, 0.01, 1.0);
    float D   = D_GGX(NdotHt, transRoughness);
    float vis = V_SmithGGXCorrelated(max(NdotV, 1e-4), max(NdotL, 1e-4),
                                     transRoughness);

    vec3 F = F_Schlick(F0, VdotHt);
    vec3 T = (vec3(1.0) - F) * strength;

    float LdotHt = dot(L, Ht);
    float denom  = eta_i * LdotHt + eta_t * VdotHt;
    float jacobian = 0.0;
    if (denom > 1e-3) {
        jacobian = (eta_t * eta_t * LdotHt) / (denom * denom);
        jacobian = min(jacobian, 1e3);
    }

    const float thicknessScale = 3.0;
    float cosT  = sqrt(max(1.0 - sin2t, 1e-4));
    float path  = clamp(1.0 / cosT, 1.0, 8.0) * thicknessScale;
    vec3  absorb = pow(max(tint, vec3(1e-4)), vec3(path));

    float E_ss = E_ss_GGX(NdotV, transRoughness);
    vec3 T0 = vec3(1.0) - F0;
    vec3 energyCompensation = 1.0 + T0 * (1.0 / max(E_ss, 1e-4) - 1.0);

    return vec3(D * vis) * absorb * T * jacobian * energyCompensation;
}

// =============================================================================
// Support lobes
// =============================================================================
float fresnel_scalar_dielectric(float cosTheta) {
    vec3 F0 = compute_dielectric_f0();
    vec3 F  = F_Schlick(F0, cosTheta);
    return dot(F, LUMA_REC709);
}

vec3 rim_lobe(float NdotV) {
    float f0_luma = dot(compute_dielectric_f0(), LUMA_REC709);
    float grazing = pow(saturate(1.0 - NdotV), max(uMatRimExponent, 0.001));
    float fresnel = f0_luma + (1.0 - f0_luma) * grazing;
    return uMatRimColor * fresnel;
}

vec3 back_glow_lobe(vec3 N, vec3 L, float NdotV) {
    float cosBack = max(dot(N, -L), 0.0);
    float T_back  = 1.0 - fresnel_scalar_dielectric(cosBack);
    float T_front = 1.0 - fresnel_scalar_dielectric(NdotV);
    float metallicMask = 1.0 - clamp(uMatMetallic, 0.0, 1.0);
    return uMatBackGlowColor * cosBack * T_back * T_front * metallicMask;
}

// =============================================================================
// Tone mapping
// =============================================================================
float filmic_base(float x, float A, float B, float C,
                  float D, float E, float F) {
    return ((x * (A * x + C * B) + D * E)
          / (x * (A * x + B) + D * F)) - E / F;
}

vec3 soft_knee_exponential(vec3 c, float knee) {
    float M = max(c.r, max(c.g, c.b));
    if (M <= knee) return c;
    float t  = (M - knee) / (1.0 - knee);
    float Mc = 1.0 - (1.0 - knee) * exp(-t);
    return c * (Mc / M);
}

vec3 soft_knee_rational(vec3 c, float knee) {
    float M = max(c.r, max(c.g, c.b));
    if (M <= knee) return c;
    float K  = 1.0 - knee;
    float t  = (M - knee) / K;
    float Mc = 1.0 - K / (1.0 + t + t * t);
    return c * (Mc / M);
}

vec3 tone_map(vec3 color) {
    const float A = 0.15;
    const float B = 0.55;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.35;
    const float W = 10.0;

    const float exposure = 2.93;

    color = max(color * exposure, vec3(0.0));

    float L  = dot(color, LUMA_REC709);
    float Lm = filmic_base(L, A, B, C, D, E, F)
             / filmic_base(W, A, B, C, D, E, F);

    const float CHROMA_COMPRESS = 0.25;
    float chromaScale = 1.0 - CHROMA_COMPRESS * smoothstep(0.70, 1.0, Lm);

    vec3 grey   = vec3(Lm);
    vec3 scaled = color * (Lm / max(L, 1e-5));
    vec3 mapped = grey + (scaled - grey) * chromaScale;

    const float KNEE = 0.80;
    mapped = soft_knee_exponential(mapped, KNEE);

    const float TOE_AMOUNT = 0.006;
    const float TOE_RADIUS = 0.15;
    float L_final = dot(mapped, LUMA_REC709);
    mapped += TOE_AMOUNT * (1.0 - smoothstep(0.0, TOE_RADIUS, L_final));

    return mapped;
}

// =============================================================================
// Cluster lookup
// =============================================================================
void cluster_lookup(out uint count, out uint base) {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 tile  = pixel / CLUSTER_TILE_SIZE;

    float depth = max(gl_FragCoord.z, 1e-6);
    float logDepth = log2(depth) * CLUSTER_INV_LOG_RANGE;
    int slice = int(floor(logDepth * CLUSTER_DEPTH_SLICES));
    slice = clamp(slice, 0, CLUSTER_DEPTH_SLICES - 1);

    uint clusterIndex = uint(tile.y * uNumTilesX + tile.x);
    uint offsetIdx    = clusterIndex * CLUSTER_DEPTH_SLICES + uint(slice);
    count = clusterOffsets[offsetIdx];
    base  = offsetIdx * CLUSTER_MAX_LIGHTS_PER;
}

// =============================================================================
// Light evaluation
// =============================================================================
bool evaluate_light(Light light, vec3 worldPos, out vec3 lightDir, out float atten) {
    int  lightType = int(light.pos.w);
    vec3 lightPos  = light.pos.xyz;
    atten = 1.0;

    if (lightType == 0) {
        lightDir = normalize(light.dir.xyz);
        return true;
    }
    if (lightType == 1 || lightType == 2) {
        vec3  toLight = lightPos - worldPos;
        float dist    = max(length(toLight), 0.01);
        if (dist > light.range) return false;

        float r = dist / light.range;
        float a = max(0.0, 1.0 - r * r);
        a *= a;
        a /= (dist * dist + 0.01);
        lightDir = normalize(toLight);
        atten = a;

        if (lightType == 2) {
            float cosAngle = dot(-lightDir, normalize(light.dir.xyz));
            if (cosAngle < light.outer_cos) return false;
            float spot = clamp((cosAngle - light.outer_cos)
                             / (light.inner_cos - light.outer_cos), 0.0, 1.0);
            spot = pow(spot, light.falloff);
            atten *= spot;
        }
        return true;
    }
    return false;
}

// =============================================================================
// Lobe contributions
// =============================================================================
vec3 lobe_diffuse(vec3 N, vec3 V, vec3 L, vec3 lightCol,
                  vec3 F_avg,
                  float NdotL, float NdotL_raw) {
    vec3 diffuseColor = uMatColor * (1.0 - uMatMetallic);

#ifdef EFFECT_VMF_DIFFUSE
    float ui = NdotL;
    float uo = max(dot(N, V), 1e-4);

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);
    vec2 Lp = vec2(dot(L, T), dot(L, B));
    vec2 Vp = vec2(dot(V, T), dot(V, B));
    float phi = atan(dot(Lp, Vp), dot(Lp, vec2(-Vp.y, Vp.x)));

    vec3 brdf = vMFdiffuseBRDF(ui, uo, phi, uMatDiffuseRoughness, diffuseColor);
#else
    vec3 brdf = diffuse_eon_oren_nayar(N, V, L, diffuseColor, uMatDiffuseRoughness);
#endif

#ifdef EFFECT_DIFFUSE_WRAP
    float wrapFactor     = NdotL * NdotL * (3.0 - 2.0 * NdotL);
    float safeNdotL_wrap = max(NdotL, 1e-4);
    brdf *= (wrapFactor / safeNdotL_wrap);
#endif

#ifdef EFFECT_CEL_SHADING
    float bands         = max(float(uMatCelBands), 1.0);
    float inv           = 1.0 / bands;
    float celFactor     = min(1.0, floor(NdotL * bands) * inv);
    float safeNdotL_cel = max(NdotL, 1e-4);
    brdf *= (celFactor / safeNdotL_cel);
#endif

#ifdef EFFECT_SUBSURFACE
    float sssStrength = clamp(uMatSubsurfaceStrength, 0.0, 2.0);
    float NdotV_local = max(dot(N, V), 1e-4);
    float blend       = subsurface_burley(NdotL_raw, NdotV_local, sssStrength);
    vec3  sssBRDF     = pow(clamp((NdotL_raw + 0.5) / 1.5, 0.0, 1.0), 2.0)
                      * (uMatColor / PI);
    brdf = mix(brdf, sssBRDF, blend);
#endif

    return brdf * NdotL * lightCol * (1.0 - F_avg);
}

vec3 lobe_specular(vec3 V, vec3 L, vec3 H, vec3 F0, vec3 F_avg, vec3 lightCol,
                   vec3 Tangent, vec3 Bitangent,
                   float NdotL, float NdotV, float NdotH, float VdotH,
                   float specularRoughness) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3 F_schlick = F_Schlick(F0, VdotH);
    vec3 F_f82     = F_Schlick_F82(F0, uMatColor, VdotH);
    vec3 fresnel   = mix(F_schlick, F_f82, metallic);

#ifdef EFFECT_ANISOTROPIC
    vec3 spec = specular_microfacet_aniso(V, L, H,
                                          NdotL, NdotV, NdotH,
                                          fresnel, F0,
                                          specularRoughness,
                                          Tangent, Bitangent);
#else
    vec3 spec = specular_microfacet_iso(NdotL, NdotV, NdotH,
                                        fresnel, F0,
                                        specularRoughness);
#endif

    return spec * uMatSpecularTint * NdotL * lightCol;
}

vec3 lobe_transmission(vec3 N, vec3 V, vec3 L, vec3 F0, vec3 lightCol,
                       float NdotL, float NdotV) {
    float transStrength = clamp(uMatTransmissionStrength, 0.0, 1.0);
    if (transStrength <= 0.001 || NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    const float dispersion = 0.02;
    vec3 tR = transmission_ggx(N, V, L, NdotL, NdotV,
                               uMatTransmissionRoughness, transStrength,
                               uMatTransmissionTint, F0,
                               uMatIOR * (1.0 - dispersion));
    vec3 tG = transmission_ggx(N, V, L, NdotL, NdotV,
                               uMatTransmissionRoughness, transStrength,
                               uMatTransmissionTint, F0,
                               uMatIOR);
    vec3 tB = transmission_ggx(N, V, L, NdotL, NdotV,
                               uMatTransmissionRoughness, transStrength,
                               uMatTransmissionTint, F0,
                               uMatIOR * (1.0 + dispersion));

    return vec3(tR.r, tG.g, tB.b) * NdotL * lightCol;
}

vec3 lobe_clearcoat(vec3 N, vec3 V, vec3 L, vec3 H, vec3 lightCol,
                    float NdotL, float NdotV, float NdotH, float VdotH,
                    float clearcoatStrength,
                    vec3 clearcoatF0) {
    if (clearcoatStrength <= 0.0 || NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 clearcoatFresnel = F_Schlick(clearcoatF0, VdotH);
    float clearcoatGloss  = 1.0 - clamp(uClearcoatRoughness, 0.0, 1.0);

    vec3 contrib = clearcoat_disney(NdotL, NdotV, NdotH,
                                    clearcoatGloss, clearcoatFresnel,
                                    clearcoatF0);

    return contrib * lightCol * uClearcoatColor * clearcoatStrength * NdotL;
}

vec3 lobe_sheen(vec3 N, vec3 V, vec3 L, vec3 H, vec3 lightCol,
                float NdotL, float NdotV, float NdotH) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 contrib = sheen_charlie(uMatColor, uSheenColor,
                                 NdotL, NdotV, NdotH,
                                 uSheenRoughness, uSheenStrength);

    return contrib * lightCol * NdotL;
}

vec3 lobe_back_glow(vec3 N, vec3 L, vec3 lightCol, float NdotV) {
    return back_glow_lobe(N, L, NdotV) * lightCol;
}

vec3 lobe_rim(vec3 lightCol, float NdotV) {
    return rim_lobe(NdotV) * lightCol;
}

// =============================================================================
// Layer attenuation
// =============================================================================
void apply_layer_attenuation(inout vec3 diffuseContrib,
                             inout vec3 specContrib,
                             float NdotL, float NdotV,
                             float clearcoatStrength,
                             vec3 clearcoatF0) {
#ifdef EFFECT_CLEARCOAT
    vec3 coatF_light = F_Schlick(clearcoatF0, NdotL);

    float coatDarkening = compute_coat_darkening(clearcoatF0, uMatColor,
                                                 NdotV, uMatSpecularRoughness);
    float darkening = mix(1.0, coatDarkening, clearcoatStrength);

    vec3 coatTransmission = vec3(darkening)
                          * (vec3(1.0) - coatF_light * clearcoatStrength);

    diffuseContrib *= coatTransmission;
    specContrib    *= coatTransmission;
#endif

#ifdef EFFECT_TRANSMISSION
    float transScale = 1.0 - clamp(uMatTransmissionStrength, 0.0, 1.0);
    diffuseContrib *= transScale;
    //specContrib    *= transScale;
#endif

#ifdef EFFECT_SHEEN
    float sheenComp = sheen_multiscatter_comp(NdotL, NdotV,
                                              uSheenRoughness, uSheenColor);
    float sheenOpacity = saturate(sheenComp
                                  * dot(uSheenColor, LUMA_REC709)
                                  * uSheenStrength);

    diffuseContrib *= (1.0 - sheenOpacity);
    specContrib    *= (1.0 - sheenOpacity);
#endif
}

// =============================================================================
// Light accumulation
// =============================================================================
void accumulate_light(vec3 N, vec3 V, vec3 L, vec3 lightCol,
                      vec3 F0, vec3 F_avg,
                      vec3 Tangent, vec3 Bitangent,
                      inout vec3 diffuse, inout vec3 specular,
                      inout vec3 clearcoat, inout vec3 sheen,
                      inout vec3 rim, inout vec3 backGlow,
                      float NdotV, float specularRoughness,
                      vec3 clearcoatF0) {
    float NdotL_raw = dot(N, L);
    float NdotL     = max(NdotL_raw, 0.0);

    float clearcoatStrength = clamp(uClearcoatStrength, 0.0, 1.0);

    vec3  Hraw  = L + V;
    float lenSq = dot(Hraw, Hraw);
    vec3  H     = (lenSq > 1e-8) ? Hraw * inversesqrt(lenSq) : N;
    float VdotH = min(max(dot(V, H), 0.0), 1.0);
    float NdotH = max(dot(N, H), 0.0);

    vec3 diffuseContrib   = lobe_diffuse(N, V, L, lightCol,
                                         F_avg, NdotL, NdotL_raw);
    vec3 specContrib      = lobe_specular(V, L, H, F0, F_avg, lightCol,
                                          Tangent, Bitangent,
                                          NdotL, NdotV, NdotH, VdotH,
                                          specularRoughness);
    vec3 transContrib     = vec3(0.0);
    vec3 clearcoatContrib = vec3(0.0);
    vec3 sheenContrib     = vec3(0.0);
    vec3 backGlowContrib  = vec3(0.0);
    vec3 rimContrib       = vec3(0.0);

#ifdef EFFECT_TRANSMISSION
    transContrib = lobe_transmission(N, V, L, F0, lightCol, NdotL, NdotV);
#endif
#ifdef EFFECT_CLEARCOAT
    clearcoatContrib = lobe_clearcoat(N, V, L, H, lightCol,
                                      NdotL, NdotV, NdotH, VdotH,
                                      clearcoatStrength,
                                      clearcoatF0);
#endif
#ifdef EFFECT_SHEEN
    sheenContrib = lobe_sheen(N, V, L, H, lightCol, NdotL, NdotV, NdotH);
#endif
#ifdef EFFECT_BACK_GLOW
    backGlowContrib = lobe_back_glow(N, L, lightCol, NdotV);
#endif
#ifdef EFFECT_RIM
    rimContrib = lobe_rim(lightCol, NdotV);
#endif

    apply_layer_attenuation(diffuseContrib, specContrib,
                            NdotL, NdotV, clearcoatStrength,
                            clearcoatF0);

    diffuse   += diffuseContrib;
    specular  += specContrib + transContrib;
    clearcoat += clearcoatContrib;
    sheen     += sheenContrib;
    backGlow  += backGlowContrib;
    rim       += rimContrib;
}

// =============================================================================
// Direct lighting
// =============================================================================
void accumulate_direct_lighting(vec3 N, vec3 V, vec3 worldPos,
                                vec3 F0, vec3 F_avg,
                                vec3 Tangent, vec3 Bitangent,
                                float NdotV, float specularRoughness,
                                vec3 clearcoatF0,
                                out vec3 totalDiffuse,
                                out vec3 totalSpec,
                                out vec3 totalClearcoat,
                                out vec3 totalSheen,
                                out vec3 totalRim,
                                out vec3 totalBackGlow,
                                out vec3 avgDir,
                                out float avgWeight) {
    totalDiffuse   = vec3(0.0);
    totalSpec      = vec3(0.0);
    totalClearcoat = vec3(0.0);
    totalSheen     = vec3(0.0);
    totalRim       = vec3(0.0);
    totalBackGlow  = vec3(0.0);
    avgDir         = vec3(0.0);
    avgWeight      = 0.0;

    uint count;
    uint base;
    cluster_lookup(count, base);

    for (uint i = 0; i < count; i++) {
        uint  lightIdx = clusterLights[base + i];
        Light light    = lights[lightIdx];

        float intensity = length(light.color.xyz);
        if (intensity < 0.001) continue;

        vec3  lightDir;
        float atten;
        if (!evaluate_light(light, worldPos, lightDir, atten)) continue;

        vec3 lightCol = light.color.xyz * atten;
        intensity *= atten;

        accumulate_light(N, V, lightDir, lightCol, F0, F_avg,
                         Tangent, Bitangent,
                         totalDiffuse, totalSpec, totalClearcoat, totalSheen,
                         totalRim, totalBackGlow,
                         NdotV, specularRoughness,
                         clearcoatF0);

#ifdef EFFECT_GOOCH
        avgDir    += lightDir * intensity;
        avgWeight += intensity;
#endif
    }
}

// =============================================================================
// LDR post effects
// =============================================================================
vec3 apply_ldr_post_effects(vec3 color, float NdotV, vec3 worldPos) {
#ifdef EFFECT_IRIDESCENCE
    float angle    = NdotV * 2.0 * PI;
    float c        = cos(angle);
    float s_ir     = sin(angle);
    float rot0 = 0.299 + 0.701 * c + 0.168 * s_ir;
    float rot1 = 0.587 - 0.587 * c + 0.330 * s_ir;
    float rot2 = 0.114 - 0.114 * c - 0.497 * s_ir;
    float rot3 = 0.299 - 0.299 * c - 0.328 * s_ir;
    float rot4 = 0.587 + 0.413 * c + 0.035 * s_ir;
    float rot5 = 0.114 - 0.114 * c + 0.292 * s_ir;
    float rot6 = 0.299 - 0.300 * c + 1.250 * s_ir;
    float rot7 = 0.587 - 0.588 * c - 1.050 * s_ir;
    float rot8 = 0.114 + 0.886 * c - 0.203 * s_ir;
    float r = color.r * rot0 + color.g * rot1 + color.b * rot2;
    float g = color.r * rot3 + color.g * rot4 + color.b * rot5;
    float b = color.r * rot6 + color.g * rot7 + color.b * rot8;
    float strength = uMatIridescenceStrength;
    color.r = r * strength + color.r * (1.0 - strength);
    color.g = g * strength + color.g * (1.0 - strength);
    color.b = b * strength + color.b * (1.0 - strength);
#endif

#ifdef EFFECT_GLITCH
    vec3 q = floor(worldPos * 4096.0 + uTime * 60.0);
    float offset = (hash_float(q) - 0.5) * uMatGlitchIntensity;
    color.r += offset;
    color.g += offset * 0.7;
    color.b -= offset;
#endif

#ifdef EFFECT_SATURATION
    float luma = dot(color, LUMA_REC709);
    color = mix(vec3(luma), color, uMatSaturation);
#endif

#ifdef EFFECT_FRINGE
    float fringe = pow(saturate(1.0 - NdotV), 3.0) * uMatFringeIntensity;
    color.r += fringe;
    color.b -= fringe;
#endif

#ifdef EFFECT_POSTERIZE
    float levels = float(uMatPosterizeLevels);
    color = floor(color * levels + 0.5) / levels;
#endif

    return color;
}

// =============================================================================
// Surface shading
// =============================================================================
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    if (!gl_FrontFacing) N = -N;

    vec3 V = normalize(uCamEye - worldPos);

    N = perturb_normal(N, worldPos, localPos);

    // Specular AA: half-vector slope-space filtering. We need L for the
    // half-vector, so this is computed per-light in accumulate_light.
    // Here we set a base filtered roughness using the normal-variance
    // fallback for the ambient path and the coat roughening.
    float specularRoughness = clamp(
        specular_aa_roughness(N, uMatSpecularRoughness),
        MIN_PERCEPTUAL_ROUGHNESS, 1.0);

#ifdef EFFECT_CLEARCOAT
    {
        float coatStrength = clamp(uClearcoatStrength, 0.0, 1.0);
        float p_b = specularRoughness;
        float p_c = clamp(uClearcoatRoughness, 0.0, 1.0);
        float p_b2 = p_b * p_b;
        float p_c2 = p_c * p_c;
        float p_b4 = p_b2 * p_b2;
        float p_c4 = p_c2 * p_c2;
        float p_eff = pow(p_b4 + p_c4, 0.25);
        specularRoughness = clamp(mix(p_b, p_eff, coatStrength),
                                  MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    }
#endif

    float NdotV = max(dot(N, V), 0.0);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3  F0 = compute_fresnel_f0(uMatColor, metallic, uMatIOR);
    vec3 F_avg = compute_fresnel_avg(F0, uMatColor, metallic);
    vec3 coatF0 = compute_clearcoat_f0();

    vec3 Tangent   = vec3(0.0);
    vec3 Bitangent = vec3(0.0);
#ifdef EFFECT_ANISOTROPIC
    Tangent   = normalize(vTangent - N * dot(vTangent, N));
    Bitangent = normalize(cross(N, Tangent));
#endif

    vec3 totalDiffuse, totalSpec, totalClearcoat;
    vec3 totalSheen, totalRim, totalBackGlow;
    vec3  avgDir;
    float avgWeight;

    accumulate_direct_lighting(N, V, worldPos, F0, F_avg,
                               Tangent, Bitangent,
                               NdotV, specularRoughness,
                               coatF0,
                               totalDiffuse, totalSpec, totalClearcoat,
                               totalSheen, totalRim, totalBackGlow,
                               avgDir, avgWeight);

    // -------------------------------------------------------------------------
    // Split the diffuse contribution into an ambient part and a direct part.
    //
    // VBAO is a *visibility approximation for the ambient term* — it is not a
    // shadow and must not attenuate direct lighting. Multiplying direct light
    // by AO turns any material with zero specular tint (dirt, brick, chalk, …)
    // pure black, because AO ≈ 0 in occluded regions leaves nothing behind.
    //
    // So we compute:
    //   ambientDiffuse  : uAmbientCol * albedo * ambientLightFactor * (1 - F_avg)
    //   ambientSpec     : uAmbientCol * F_avg * specOcc * ambientLightFactor
    //   directDiffuse   : totalDiffuse (unaffected by AO)
    // and apply AO only to the two ambient terms.
    // -------------------------------------------------------------------------
    vec3 diffuseColor = uMatColor * (1.0 - metallic);

    vec3 ambientDiffuse = uAmbientCol * diffuseColor * uMatAmbientLightFactor
                        * (1.0 - F_avg);
    vec3 directDiffuse  = totalDiffuse;

    float mat_ao = clamp(uMatAmbientLightFactor, 0.0, 1.0);
    float specOcc = specular_occlusion(NdotV, mat_ao, specularRoughness);
    vec3 ambientSpec = uAmbientCol * F_avg * specOcc * uMatAmbientLightFactor;

#ifdef EFFECT_GOOCH
    // GOOCH tints both ambient and direct diffuse by the average incoming
    // light direction, before AO is applied to the ambient terms.
    if (avgWeight > 0.001) {
        vec3 dir = avgDir / avgWeight;
        float len = length(dir);
        if (len > 0.001) {
            dir /= len;
            float ndotl_avg = max(dot(N, dir), 0.0);
            float t_gooch = (ndotl_avg + 1.0) * 0.5;
            vec3 goochFactor = mix(uMatGoochCool, uMatGoochWarm, t_gooch);
            ambientDiffuse *= goochFactor;
            directDiffuse  *= goochFactor;
        }
    }
#endif

    // -------------------------------------------------------------------------
    // VBAO — attenuate only the ambient terms. Transparent (WBOIT) variants
    // do not receive AO; they are drawn after the AO dispatch and would need
    // a second lookup to be occluded by opaque geometry behind them.
    // -------------------------------------------------------------------------
#ifndef WBOIT_PASS
    float vbao_ao = texture(uAOTex, gl_FragCoord.xy / uScreenSize).r;
    ambientDiffuse *= vbao_ao;
    ambientSpec    *= vbao_ao;
#endif

    vec3 color = ambientDiffuse + ambientSpec + directDiffuse;
    color += totalSpec + totalClearcoat + totalSheen + totalRim + totalBackGlow;

    // -------------------------------------------------------------------------
    // Screen-space refraction.
    // -------------------------------------------------------------------------
#ifdef EFFECT_TRANSMISSION
    {
        vec3 R = refract(-V, N, 1.0 / max(uMatIOR, 1.001));
        if (uRefractionScale > 0.0 && dot(R, R) > 0.0) {
            vec2 uv  = gl_FragCoord.xy / uScreenSize;
            vec2 duv = R.xy * uRefractionScale * (1.0 - NdotV);
            vec3 bg  = texture(uRefractionSrc, uv + duv).rgb;

            vec3 transmitted = bg * uMatTransmissionTint;
            vec3 F = F_Schlick(F0, NdotV);
            float Favg = dot(F, LUMA_REC709);
            float strength = clamp(uMatTransmissionStrength, 0.0, 1.0);

            float transFraction = strength * (1.0 - Favg);
            color = mix(color, transmitted, transFraction);
        }
    }
#endif

#ifdef EFFECT_EMISSIVE
    vec3 emissive = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
    float pulse = 1.0 + uMatEmissivePulseAmplitude
                * sin(uTime * uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
    emissive *= pulse;
#endif
    color += emissive;
#endif

#ifdef EFFECT_STROBE
    float s = sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5;
    color += uMatStrobeColor * s;
#endif

    color *= uMatTint;

#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float dist    = length(worldPos - uCamEye);
        float fogDist = max(dist - uFogStart, 0.0);
        float range   = max(uFogEnd - uFogStart, 1e-4);
        float density = 3.0 / range;
        float t       = 1.0 - exp(-density * fogDist);
        color = mix(color, uFogColor, t);
    }
#endif

    color = max(color, vec3(0.0));

    color = tone_map(color);
    color = apply_ldr_post_effects(color, NdotV, worldPos);

    return color;
}

// =============================================================================
// WBOIT weight
// =============================================================================
//
// McGuire & Bavoil 2013, "Weighted Blended Order-Independent Transparency".
// The weight biases nearer fragments more heavily so that when many
// transparent layers overlap, closer ones dominate the resolved colour.
//
// Input is linear eye-space depth — the distance along the camera's view
// axis, positive in front. This is NOT gl_FragCoord.z (hyperbolic, saturates
// toward 1 past a few units) and NOT Euclidean distance from the camera
// (nonlinear in view space, so interpolation is approximate on large
// triangles). The vertex shader emits gl_Position.w as a varying; because
// clip-space w equals -view_z for a standard perspective projection and is
// linear in view space, perspective-correct interpolation recovers the
// exact eye depth at every fragment.
//
// The 200.0 tuning constant is the eye distance beyond which layers start
// contributing meaningfully less. Tune it to roughly the scale at which
// your transparent geometry stops being visually important; for a scene
// spanning 100–500 world units, 200 is a reasonable default.
float wboit_weight(float eye_depth, float alpha) {
    float z = eye_depth;
    float w = 10.0 / (1e-5 + pow(z / 200.0, 4.0) + pow(z / 200.0, 2.0));
    return alpha * clamp(w, 1e-2, 3e3);
}

// =============================================================================
// Entry point
// =============================================================================
#ifdef WBOIT_PASS
layout(location = 0) out vec4 outAccumulation;
layout(location = 1) out vec4 outRevealage;
#else
out vec4 FragColor;
layout(location = 1) out vec4 outNormal;
#endif

void main() {
    // -------------------------------------------------------------------------
    // ALPHA_PASS_BEHIND culls fragments that are not strictly behind the
    // frontmost transmissive surface. Fragments in front of it, or with no
    // transmissive surface behind them, are drawn in ALPHA_PASS_FRONT
    // instead. The test is a straight depth comparison against
    // uTransmissiveDepthTex, which was filled by the transmissive depth pass.
    // -------------------------------------------------------------------------
#ifdef ALPHA_PASS_BEHIND
    float transmissive_z = texelFetch(uTransmissiveDepthTex,
                                      ivec2(gl_FragCoord.xy), 0).r;
    if (transmissive_z >= 1.0) discard;            // no transmissive here
    if (gl_FragCoord.z < transmissive_z) discard;  // in front of it
#endif

    // -------------------------------------------------------------------------
    // ALPHA_PASS_FRONT draws alpha geometry in front of a transmissive
    // surface and alpha geometry with no transmissive surface behind it.
    // Depth testing against the combined opaque + transmissive depth buffer
    // culls fragments that ALPHA_PASS_BEHIND already drew. No branch is
    // needed here because the pass identity is carried by the depth state
    // set in the C code, not by the shader.
    // -------------------------------------------------------------------------
#ifdef ALPHA_PASS_FRONT
#endif

    vec3 color = shade_surface(vNormal, vWorldPos, vLocalPos);

    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = clamp(uMatAlpha, 0.0, 1.0);
#endif

#ifdef WBOIT_PASS
    // McGuire & Bavoil 2013 weighted-blended OIT. Weighting is on linear
    // eye-space depth, not gl_FragCoord.z — see wboit_weight() above.
    //
    //   accum.rgb += color * w
    //   accum.a   += w
    //   revealage *= (1 - alpha)      (handled by fixed-function blend)
    float w = wboit_weight(vEyeDepth, alpha);

    outAccumulation = vec4(color * w, w);
    outRevealage    = vec4(alpha);
#else
    // Opaque / transmissive variants write a single colour plus the view-
    // space geometric normal for VBAO. The normal must be the un-perturbed
    // surface normal, consistent with the depth buffer; perturbing it here
    // would break the AO consistency at every bump-mapped pixel.
    vec3 N_geom = normalize(vNormal);
    if (!gl_FrontFacing) N_geom = -N_geom;
    vec3 viewNormal = normalize((uView * vec4(N_geom, 0.0)).xyz);
    outNormal = vec4(viewNormal * 0.5 + 0.5, 1.0);

    // Dither to break up 8-bit banding on smooth gradients.
    float dither = (hash_float(vec3(gl_FragCoord.xy, uTime)) - 0.5) / 255.0;
    color += dither;
    FragColor = vec4(color, alpha);
#endif
}

#endif /* DEPTH_ONLY */