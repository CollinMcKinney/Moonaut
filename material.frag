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
//   2. Save the geometric normal, then perturb the shading normal (wave
//      and/or noise bump).
//   3. Compute specular-AA-filtered roughness, F0, F_avg, and coat F0.
//      Apply coat roughening to the base specular roughness.
//   4. Loop over the lights in the fragment's cluster.
//        For each light:
//          a. Evaluate diffuse, specular, transmission, clearcoat,
//             sheen, back glow, and rim.
//          b. Attenuate the base by the layers above it (energy conservation).
//          c. Accumulate.
//   5. Add ambient lighting via sample_env_map(), a placeholder environment
//      probe.
//   6. If EFFECT_TRANSMISSION is defined, sample the pre-transmissive color
//      buffer along the refracted ray.
//   7. Apply VBAO to every ambient lobe except transmission.
//   8. Add emissive, strobe, tint, fog.
//   9. Apply per-material HDR stylizations (iridescence, fringe, glitch,
//      saturation).
//  10. Apply per-material posterize, then return linear HDR radiance.
//      Write it to the appropriate target (single color, or WBOIT
//      accumulation pair). The alpha channel of the normal output is
//      zero for all materials; nothing reads.
//
// All outputs are linear HDR radiance.
//
// =============================================================================
// PHYSICAL ACCURACY NOTES
// =============================================================================
//
// The following invariants are maintained to keep the shading model
// energy-conserving:
//
//   * F_Schlick_F82 output is clamped to [0, 1]. The F82-tint parameter is a
//     ratio applied to the Schlick edge value at mu = 1/7, not a reflectance.
//     For some materials the ratio exceeds 1, which makes the evaluated
//     Fresnel exceed unity at certain angles. A reflectance cannot exceed 1,
//     so the clamp is required. The same clamp appears in the OpenPBR
//     specification, equation 66.
//
//   * compute_fresnel_avg is clamped to [0, 1] for the same reason.
//
//   * Fresnel shadowing is applied to the Fresnel term itself, before the
//     microfacet BRDF is evaluated. On rough surfaces at grazing angles,
//     microfacets self-occlude more, reducing the effective reflectance
//     below the ideal Schlick estimate. Formula from Lagarde & de Rousiers
//     2014, "Moving Frostbite to PBR", section 4.3.
//
//   * The subsurface lobe uses per-channel albedo modulation rather than
//     mixing two BRDFs. A weighted sum of two BRDFs with different albedos
//     does not preserve the material's total energy; per-channel modulation
//     does. This is documented at subsurface_chromatic_modulation.
//
//   * Transmission attenuation of the diffuse lobe is derived from F_avg,
//     the hemispherical average Fresnel. The diffuse lobe integrates over
//     the hemisphere, so the fraction of energy reaching it is the
//     hemispherical average transmittance, not the view-angle transmittance.
//
//   * Sheen attenuation of the base layer is a function of view angle only.
//     Sheen is a top layer; its transmittance to the layers below depends
//     on the view direction, not on any particular light direction.
//
//   * The split-sum environment BRDF bias term (envBRDF.y) is tinted by the
//     base color for metallic materials. See the ambientSpec computation in
//     shade_surface for the derivation.
//
// =============================================================================

#ifdef DEPTH_ONLY
void main() { }
#else

// =============================================================================
// Constants
// =============================================================================
#define PI 3.141592653589793

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

#define MIN_PERCEPTUAL_ROUGHNESS 0.01

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

in float vEyeDepth;

uniform vec3  uAmbientCol;
uniform vec3  uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec2  uScreenSize;

uniform mat4  uView;

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
    vec3  uMatF82Tint;
    vec3  uMatSubsurfaceColor;
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
// Environment map sampling
// TODO: Add an IBL environment map uniform and sample from it instead of generating checker pattern.
// =============================================================================
vec3 sample_env_map(vec3 dir, float roughness) {
    float horizon_blur = saturate(roughness * 1.5);
    float horizon_lo   = mix(-0.25, -0.95, horizon_blur);
    float horizon_hi   = mix( 0.25,  0.95, horizon_blur);
    float sky_t        = smoothstep(horizon_lo, horizon_hi, dir.y);
    vec3  sky          = uAmbientCol;
    vec3  ground       = uAmbientCol * 0.22;
    vec3  base         = mix(ground, sky, sky_t);

    const float CHECKER_CELLS = 4.0;
    const mat3 CHECKER_ROT = mat3(
        0.8165, -0.4082,  0.4082,
        0.0000,  0.8944,  0.4472,
       -0.5774, -0.3651,  0.7303
    );
    vec3 gdir = CHECKER_ROT * dir;
    vec3 grid = floor(gdir * CHECKER_CELLS);
    float check = mod(grid.x + grid.y + grid.z, 2.0);

    float check_mul = mix(0.75, 1.15, check);
    vec3  env       = base * check_mul;

    env = mix(env, base, saturate(roughness * 2.0));

    vec3  dir_fw = fwidth(dir);
    float cell_footprint = CHECKER_CELLS
                         * (abs(dir_fw.x) + abs(dir_fw.y) + abs(dir_fw.z));
    float aa_fade = clamp(1.0 - cell_footprint, 0.0, 1.0);
    env = mix(base, env, aa_fade);

    return env;
}

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
    strength *= (0.5 + 0.5 * grazing);
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

// -----------------------------------------------------------------------------
// F82-tinted Schlick, output clamped to [0, 1].
//
// The F82-tint parameter is a ratio applied to the Schlick edge value at
// mu = 1/7, not a reflectance. For some materials the ratio exceeds 1,
// which makes the evaluated Fresnel exceed unity at certain angles. A
// reflectance cannot exceed 1 (that would reflect more energy than is
// incident), so the clamp is required. The same clamp appears in the
// OpenPBR specification, equation 66.
// -----------------------------------------------------------------------------
vec3 F_Schlick_F82(vec3 F0, vec3 F82, float cosTheta) {
    float mu = saturate(cosTheta);
    const float MU_HAT    = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 F_schlick_edge = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 b = F_schlick_edge * (1.0 - F82) / denom;
    vec3 F_schlick = F_Schlick(F0, mu);
    vec3 F = F_schlick - b * mu * pow(1.0 - mu, 6.0);
    return clamp(F, vec3(0.0), vec3(1.0));
}

// -----------------------------------------------------------------------------
// Hemispherical average of the F82-tinted Fresnel, clamped to [0, 1].
//
// The correction term is gated by `metallic` because non-metals use plain
// Schlick in the direct lobe, so the tint has no effect on their energy
// budget. With F82 = 1 the correction vanishes and this reduces exactly to
// the Schlick average. The final clamp guarantees energy conservation for
// authorable tints whose F82 exceeds the conversion's no-op value.
// -----------------------------------------------------------------------------
vec3 compute_fresnel_avg(vec3 F0, vec3 F82, float metallic) {
    vec3 F_avg_schlick = F0 + (1.0 - F0) / 21.0;
    const float MU_HAT    = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 F_schlick_edge = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 b_metal = F_schlick_edge * (1.0 - F82) / denom;
    vec3 F_avg = F_avg_schlick - metallic * b_metal / 126.0;
    return clamp(F_avg, vec3(0.0), vec3(1.0));
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

vec2 env_brdf_approx(float NdotV, float perceptualRoughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 r = perceptualRoughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 specular_multiscatter_comp(vec3 fss, vec3 F0, vec3 F_avg,
                                float roughness, float NdotV) {
    float E_ss = E_ss_GGX(NdotV, roughness);
    float inv  = 1.0 / max(E_ss, 1e-4) - 1.0;
    return fss * (1.0 + F0 * inv) / (1.0 + F_avg * inv);
}

vec3 specular_microfacet_iso(float NdotL, float NdotV, float NdotH,
                             vec3 fresnel, vec3 F0, vec3 F_avg,
                             float roughness) {
    float D   = D_GGX(NdotH, roughness);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, roughness);
    vec3 fss  = D * vis * fresnel;
    return specular_multiscatter_comp(fss, F0, F_avg, roughness, NdotV);
}

vec3 specular_microfacet_aniso(vec3 V, vec3 L, vec3 H,
                               float NdotL, float NdotV, float NdotH,
                               vec3 fresnel, vec3 F0, vec3 F_avg,
                               float roughness,
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
    return specular_multiscatter_comp(fss, F0, F_avg, roughness, NdotV);
}

// =============================================================================
// Diffuse lobe — EON Oren-Nayar
// =============================================================================
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

// =============================================================================
// Subsurface scattering — chromatic albedo modulation
// =============================================================================
//
// Chromatic SSS via per-channel albedo modulation.
//
// The physically-important effect of subsurface scattering is the
// wavelength-dependent transport distance: channels the material transmits
// strongly (high uMatSubsurfaceColor) travel farther through the material
// and re-emerge at grazing view angles, so they read brighter there;
// channels the material absorbs stay dark. This produces the warm-red
// silhouette on red-dominant materials and the cool-blue cast on
// blue-dominant materials.
//
// We reproduce that effect by modulating the diffuse albedo per channel with
// a view-dependent factor whose per-channel sum averages to 1 over the view
// hemisphere, so the total energy of the diffuse lobe is unchanged — only
// its distribution across channels and view angles.
vec3 subsurface_chromatic_modulation(vec3 subsurfaceColor, float NdotV, float strength) {
    float maxC = max(subsurfaceColor.r,
                     max(subsurfaceColor.g, subsurfaceColor.b));
    maxC = max(maxC, 1e-4);
    vec3 albedo = subsurfaceColor / maxC;
    float mean_albedo = (albedo.r + albedo.g + albedo.b) * (1.0 / 3.0);
    vec3 delta = albedo - mean_albedo;
    float grazing = pow(1.0 - NdotV, 2.0);
    vec3 mod = vec3(1.0) + delta * grazing * strength;
    return max(mod, vec3(0.0));
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

    return specular_multiscatter_comp(fss, clearcoatF0, clearcoatF_avg,
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
// Sheen energy compensation
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

// -----------------------------------------------------------------------------
// View-only sheen base-layer transmittance.
//
// Sheen is a top layer; its transmittance to the base layers below is a
// property of the layer and the view direction, not of any particular light
// direction. The full Kulla-Conty compensation uses E_o (at the view angle)
// and E_i (at the light angle), which makes the base-layer attenuation
// L-dependent. Using E_o for both ends removes the L-dependence while
// preserving the view-angle variation of the energy budget.
// -----------------------------------------------------------------------------
float sheen_base_transmittance(float NdotV) {
    float E_o = sheen_directional_albedo(NdotV, uSheenRoughness);
    vec3 F_avg = clamp(uSheenColor, 0.0, 0.99);
    float F_luma = dot(F_avg, LUMA_REC709);

    float num = (1.0 - E_o) * (1.0 - E_o) * F_luma * F_luma * E_o;
    float den = PI * (1.0 - E_o) * (1.0 - F_luma * (1.0 - E_o));
    den = max(den, 1e-4);

    float opacity = saturate(num / den)
                  * dot(uSheenColor, LUMA_REC709)
                  * uSheenStrength;

    return saturate(1.0 - opacity);
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

#ifdef EFFECT_SUBSURFACE
    // Chromatic SSS via per-channel albedo modulation.
    {
        float sssStrength = clamp(uMatSubsurfaceStrength, 0.0, 2.0);
        float NdotV_local = max(dot(N, V), 1e-4);
        vec3 mod = subsurface_chromatic_modulation(uMatSubsurfaceColor,
                                                   NdotV_local, sssStrength);
        diffuseColor *= mod;
    }
#endif

    vec3 brdf = diffuse_eon_oren_nayar(N, V, L, diffuseColor, uMatDiffuseRoughness);

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

    vec3 result = brdf * NdotL;

#ifdef EFFECT_SUBSURFACE
    // Back-transmission. Light entering the back of the surface, scattering
    // through the material, and exiting toward the viewer. This is the
    // "backlit ears glow red" effect. Only fires for light that arrives on
    // the opposite side of the surface from the viewer, and only for
    // translucent (non-metallic) materials.
    if (NdotL_raw < 0.0 && uMatMetallic < 0.5) {
        float sssStrength  = clamp(uMatSubsurfaceStrength, 0.0, 2.0);
        float NdotLneg     = -NdotL_raw;
        float VdotLneg     = saturate(dot(V, -L));
        float metallicMask = 1.0 - clamp(uMatMetallic, 0.0, 1.0);

        float phase = pow(VdotLneg, 4.0);

        float pathLength = 1.0 / max(NdotLneg, 0.1);

        float maxC = max(uMatSubsurfaceColor.r,
                         max(uMatSubsurfaceColor.g, uMatSubsurfaceColor.b));
        maxC = max(maxC, 1e-4);
        vec3 albedo = uMatSubsurfaceColor / maxC;
        vec3 absorption = (1.0 - albedo) / max(sssStrength, 0.1) + 0.05;

        vec3 transmittance = exp(-absorption * pathLength);

        result += transmittance * uMatSubsurfaceColor * phase * metallicMask;
    }
#endif

    return result * lightCol * (1.0 - F_avg);
}

vec3 lobe_specular(vec3 N, vec3 V, vec3 L, vec3 H, vec3 F0, vec3 F_avg, vec3 lightCol,
                   vec3 Tangent, vec3 Bitangent,
                   float NdotL, float NdotV, float NdotH, float VdotH,
                   float baseRoughness) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    float specularRoughness =
        specular_aa_roughness_halfvec(N, V, L, baseRoughness);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3 F_schlick = F_Schlick(F0, VdotH);
    vec3 F_f82     = F_Schlick_F82(F0, uMatF82Tint, VdotH);
    vec3 fresnel   = mix(F_schlick, F_f82, metallic);

    // -------------------------------------------------------------------------
    // Fresnel shadowing (grazing-visibility reduction).
    //
    // On rough surfaces at grazing angles, microfacets are more likely to
    // be self-occluded by their neighbours, which reduces the effective
    // reflectance below the ideal Schlick estimate. The correction is
    // applied to the Fresnel term itself, before the microfacet BRDF is
    // evaluated, and is a function of the view angle NdotV rather than the
    // half-vector angle VdotH.
    //
    // Formula from Lagarde & de Rousiers 2014, "Moving Frostbite to PBR",
    // section 4.3. The same idea appears in Karis 2013, section 4.2.
    // -------------------------------------------------------------------------
    fresnel *= saturate(1.0 - specularRoughness * pow(1.0 - NdotV, 5.0));

#ifdef EFFECT_ANISOTROPIC
    vec3 spec = specular_microfacet_aniso(V, L, H,
                                          NdotL, NdotV, NdotH,
                                          fresnel, F0, F_avg,
                                          specularRoughness,
                                          Tangent, Bitangent);
#else
    vec3 spec = specular_microfacet_iso(NdotL, NdotV, NdotH,
                                        fresnel, F0, F_avg,
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
//
// Clearcoat and transmission attenuate the base layers per-light. Sheen is
// L-independent (see sheen_base_transmittance) but is computed here so the
// same view-only factor multiplies every light's contribution — which is
// equivalent to applying it once to the accumulated total.
void apply_layer_attenuation(inout vec3 diffuseContrib,
                             inout vec3 specContrib,
                             float NdotL, float NdotV,
                             float clearcoatStrength,
                             vec3 clearcoatF0,
                             vec3 F_avg) {
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
    // -------------------------------------------------------------------------
    // Fresnel-derived transmission attenuation of the diffuse lobe.
    //
    // The diffuse lobe integrates incident light over the hemisphere above
    // the surface, so the correct fraction of energy reaching it is the
    // hemispherical average transmittance: (1 - F_avg * strength).
    //
    // The specular lobe is NOT attenuated here: it represents the surface
    // reflection, which coexists with transmission. The transmission lobe
    // itself carries (1 - F) at its own half-vector, so the three lobes
    // together conserve energy by construction.
    // -------------------------------------------------------------------------
    vec3 transmittance = vec3(1.0)
                       - F_avg * clamp(uMatTransmissionStrength, 0.0, 1.0);
    diffuseContrib *= transmittance;
#endif

#ifdef EFFECT_SHEEN
    // -------------------------------------------------------------------------
    // Sheen base-layer attenuation, L-independent.
    //
    // sheen_base_transmittance() depends on NdotV and sheen roughness only,
    // so the same scalar multiplies every light's contribution. This makes
    // the base layer's attenuation a well-defined, view-dependent quantity,
    // which is what energy conservation requires for a top layer.
    // -------------------------------------------------------------------------
    float sheen_trans = sheen_base_transmittance(NdotV);
    diffuseContrib *= sheen_trans;
    specContrib    *= sheen_trans;
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
                      float NdotV, float baseRoughness,
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
    vec3 specContrib      = lobe_specular(N, V, L, H, F0, F_avg, lightCol,
                                          Tangent, Bitangent,
                                          NdotL, NdotV, NdotH, VdotH,
                                          baseRoughness);
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
                            clearcoatF0, F_avg);

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
                                float NdotV, float baseRoughness,
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
                         NdotV, baseRoughness,
                         clearcoatF0);

#ifdef EFFECT_GOOCH
        avgDir    += lightDir * intensity;
        avgWeight += intensity;
#endif
    }

    // The sheen attenuation has already been applied per-light inside
    // apply_layer_attenuation, using the view-only sheen_base_transmittance().
    // Because that factor does not depend on the light direction, applying it
    // per-light is mathematically identical to applying it once to the
    // accumulated total after the loop.
}

// =============================================================================
// Surface shading
// =============================================================================
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    if (!gl_FrontFacing) N = -N;

    vec3 N_geom = N;

    vec3 V = normalize(uCamEye - worldPos);

    N = perturb_normal(N, worldPos, localPos);

    float baseRoughness = clamp(uMatSpecularRoughness,
                                MIN_PERCEPTUAL_ROUGHNESS, 1.0);

#ifdef EFFECT_CLEARCOAT
    {
        float coatStrength = clamp(uClearcoatStrength, 0.0, 1.0);
        float p_b = baseRoughness;
        float p_c = clamp(uClearcoatRoughness, 0.0, 1.0);
        float p_b2 = p_b * p_b;
        float p_c2 = p_c * p_c;
        float p_b4 = p_b2 * p_b2;
        float p_c4 = p_c2 * p_c2;
        float p_eff = pow(p_b4 + p_c4, 0.25);
        baseRoughness = clamp(mix(p_b, p_eff, coatStrength),
                              MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    }
#endif

    float ambientRoughness = clamp(
        specular_aa_roughness(N, baseRoughness),
        MIN_PERCEPTUAL_ROUGHNESS, 1.0);

    float NdotV = max(dot(N, V), 0.0);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3  F0 = compute_fresnel_f0(uMatColor, metallic, uMatIOR);
    vec3 F_avg = compute_fresnel_avg(F0, uMatF82Tint, metallic);
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
                               NdotV, baseRoughness,
                               coatF0,
                               totalDiffuse, totalSpec, totalClearcoat,
                               totalSheen, totalRim, totalBackGlow,
                               avgDir, avgWeight);

    vec3 diffuseColor  = uMatColor * (1.0 - metallic);
    vec3 directDiffuse = totalDiffuse;

    float vbao_ao = 1.0;
#ifndef WBOIT_PASS
    vbao_ao = texture(uAOTex, gl_FragCoord.xy / uScreenSize).r;
#endif

    float specOcc = specular_occlusion(NdotV, vbao_ao, ambientRoughness);

    vec2 envBRDF = env_brdf_approx(NdotV, ambientRoughness);

    vec3 kD_env = vec3(1.0) - F_avg;

    vec3 R = reflect(-V, N);

#ifdef EFFECT_ANISOTROPIC
    {
        vec3 bendAxis = (uMatAnisotropic >= 0.0) ? Bitangent : Tangent;
        vec3 perpV = V - bendAxis * dot(V, bendAxis);
        float perpLenSq = dot(perpV, perpV);
        if (perpLenSq > 1e-6) {
            vec3 bentN = perpV * inversesqrt(perpLenSq);
            vec3 bentR = reflect(-V, bentN);
            R = normalize(mix(R, bentR, abs(uMatAnisotropic)));
        }
    }
#endif

    vec3 irradiance = sample_env_map(N_geom, 1.0);
    vec3 ambientDiffuse = irradiance * diffuseColor * kD_env
                        * uMatAmbientLightFactor;

    // -------------------------------------------------------------------------
    // Split-sum environment BRDF bias tinting.
    //
    // envBRDF.y is an achromatic constant from the Karis split-sum fit.
    // That is correct for dielectrics but wrong for metals: a metal's
    // reflectance is tinted by its base color at every angle except
    // grazing. Without tinting, the bias term injects white into the
    // reflection, and because it grows as NdotV drops (envBRDF.y ≈ 0.36
    // at NdotV = 0.1, roughness 0.3), a saturated gold sphere reads as
    // pale off-white across most of its visible surface.
    //
    // The mix is continuous in metallic; pure dielectrics are unchanged.
    // F0 * envBRDF.x is left untouched because F0 already carries the
    // correct tint at every metallic value.
    // -------------------------------------------------------------------------
    vec3 envSpec = sample_env_map(R, ambientRoughness);
    vec3 biasTint = mix(vec3(1.0), uMatColor, metallic);
    vec3 F_env = F0 * envBRDF.x + envBRDF.y * biasTint;

    vec3 ambientSpec = envSpec * F_env
                     * uMatSpecularTint * specOcc * uMatAmbientLightFactor;

    vec3 ambientClearcoat = vec3(0.0);
#ifdef EFFECT_CLEARCOAT
    {
        float ccStrength = clamp(uClearcoatStrength, 0.0, 1.0);
        if (ccStrength > 0.0) {
            vec3  coatF_avg = coatF0 + (1.0 - coatF0) / 21.0;
            float ccRough   = clamp(uClearcoatRoughness, 0.0, 1.0);
            vec3  F_cc      = mix(F_Schlick(coatF0, NdotV),
                                  coatF_avg, ccRough);
            vec3  envCoat   = sample_env_map(R, ccRough);
            ambientClearcoat = envCoat * uClearcoatColor * F_cc * ccStrength
                             * specOcc * uMatAmbientLightFactor;
        }
    }
#endif

    vec3 ambientSheen = vec3(0.0);
#ifdef EFFECT_SHEEN
    {
        const float SHEEN_TINT = 0.3;
        float luma_base = dot(uMatColor, LUMA_REC709);
        vec3 sheenColor = mix(vec3(1.0), uMatColor / max(luma_base, 1e-4),
                              SHEEN_TINT);
        sheenColor *= uSheenColor;
        sheenColor = clamp(sheenColor, 0.0, 1.0);

        float E_sheen = sheen_directional_albedo(NdotV, uSheenRoughness);
        ambientSheen = irradiance * sheenColor
                     * E_sheen * uSheenStrength
                     * uMatAmbientLightFactor;
    }
#endif

    vec3 ambientTrans = vec3(0.0);
#ifdef EFFECT_TRANSMISSION
    {
        float transStrength = clamp(uMatTransmissionStrength, 0.0, 1.0);
        if (transStrength > 0.0) {
            vec3 R_trans = refract(-V, N, 1.0 / max(uMatIOR, 1.001));
            if (dot(R_trans, R_trans) < 1e-4) R_trans = -N_geom;

            vec3 envTrans = sample_env_map(R_trans, uMatTransmissionRoughness);
            vec3 kT       = vec3(1.0) - (F0 * envBRDF.x + envBRDF.y);
            ambientTrans = envTrans * uMatTransmissionTint
                         * kT * transStrength * uMatAmbientLightFactor;
        }
    }
#endif

#ifdef EFFECT_GOOCH
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

    ambientDiffuse   *= vbao_ao;
    ambientClearcoat *= vbao_ao;
    ambientSheen     *= vbao_ao;

    vec3 colorHDR = ambientDiffuse + ambientSpec + ambientClearcoat + ambientSheen
                  + ambientTrans + directDiffuse;
    colorHDR += totalSpec + totalClearcoat + totalSheen + totalRim + totalBackGlow;

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
            colorHDR = mix(colorHDR, transmitted, transFraction);
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
    colorHDR += emissive;
#endif

#ifdef EFFECT_STROBE
    float s = sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5;
    colorHDR += uMatStrobeColor * s;
#endif

    colorHDR *= uMatTint;

#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float dist    = length(worldPos - uCamEye);
        float fogDist = max(dist - uFogStart, 0.0);
        float range   = max(uFogEnd - uFogStart, 1e-4);
        float density = 3.0 / range;
        float t       = 1.0 - exp(-density * fogDist);
        colorHDR = mix(colorHDR, uFogColor, t);
    }
#endif

    colorHDR = max(colorHDR, vec3(0.0));

#ifdef EFFECT_IRIDESCENCE
    {
        float angle = NdotV * 2.0 * PI;
        float c     = cos(angle);
        float s_ir  = sin(angle);
        float rot0 = 0.299 + 0.701 * c + 0.168 * s_ir;
        float rot1 = 0.587 - 0.587 * c + 0.330 * s_ir;
        float rot2 = 0.114 - 0.114 * c - 0.497 * s_ir;
        float rot3 = 0.299 - 0.299 * c - 0.328 * s_ir;
        float rot4 = 0.587 + 0.413 * c + 0.035 * s_ir;
        float rot5 = 0.114 - 0.114 * c + 0.292 * s_ir;
        float rot6 = 0.299 - 0.300 * c + 1.250 * s_ir;
        float rot7 = 0.587 - 0.588 * c - 1.050 * s_ir;
        float rot8 = 0.114 + 0.886 * c - 0.203 * s_ir;
        float r = colorHDR.r * rot0 + colorHDR.g * rot1 + colorHDR.b * rot2;
        float g = colorHDR.r * rot3 + colorHDR.g * rot4 + colorHDR.b * rot5;
        float b = colorHDR.r * rot6 + colorHDR.g * rot7 + colorHDR.b * rot8;
        float strength = uMatIridescenceStrength;
        colorHDR.r = r * strength + colorHDR.r * (1.0 - strength);
        colorHDR.g = g * strength + colorHDR.g * (1.0 - strength);
        colorHDR.b = b * strength + colorHDR.b * (1.0 - strength);
    }
#endif

#ifdef EFFECT_FRINGE
    {
        float fringe = pow(saturate(1.0 - NdotV), 3.0) * uMatFringeIntensity;
        colorHDR.r += fringe;
        colorHDR.b -= fringe;
    }
#endif

#ifdef EFFECT_GLITCH
    {
        vec3 q = floor(worldPos * 4096.0 + uTime * 60.0);
        float offset = (hash_float(q) - 0.5) * uMatGlitchIntensity;
        colorHDR.r += offset;
        colorHDR.g += offset * 0.7;
        colorHDR.b -= offset;
    }
#endif

#ifdef EFFECT_SATURATION
    {
        float luma = dot(colorHDR, LUMA_REC709);
        colorHDR = mix(vec3(luma), colorHDR, uMatSaturation);
    }
#endif

#ifdef EFFECT_POSTERIZE
    {
        float levels = max(float(uMatPosterizeLevels), 2.0);
        colorHDR = floor(colorHDR * levels + 0.5) / levels;
    }
#endif

    return colorHDR;
}

// =============================================================================
// WBOIT weight
// =============================================================================
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
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 outNormal;
#endif

void main() {
#ifdef ALPHA_PASS_BEHIND
    float transmissive_z = texelFetch(uTransmissiveDepthTex,
                                      ivec2(gl_FragCoord.xy), 0).r;
    if (transmissive_z >= 1.0) discard;
    if (gl_FragCoord.z < transmissive_z) discard;
#endif

#ifdef ALPHA_PASS_FRONT
#endif

    vec3 colorHDR = shade_surface(vNormal, vWorldPos, vLocalPos);

    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = clamp(uMatAlpha, 0.0, 1.0);
#endif

#ifdef WBOIT_PASS
    // Posterize has already been applied inside shade_surface. The value
    // being weighted here is the quantized radiance.
    float w = wboit_weight(vEyeDepth, alpha);
    outAccumulation = vec4(colorHDR * w, w);
    outRevealage    = vec4(alpha);
#else
    vec3 N_geom = normalize(vNormal);
    if (!gl_FrontFacing) N_geom = -N_geom;
    vec3 viewNormal = normalize((uView * vec4(N_geom, 0.0)).xyz);

    // View-space normal for VBAO. The .a channel is unused; VBAO reads
    // only .rgb.
    outNormal = vec4(viewNormal * 0.5 + 0.5, 0.0);
    FragColor = vec4(colorHDR, alpha);
#endif
}

#endif /* DEPTH_ONLY */