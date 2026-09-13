#version 430 core

#ifdef DEPTH_ONLY
void main() { }
#else

// =============================================================================
// Constants
// =============================================================================
#define PI 3.14159265

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

#define MIN_PERCEPTUAL_ROUGHNESS 0.045

const float EON_CONST1 = 0.5 - 2.0 / (3.0 * PI);
const float EON_CONST2 = 2.0 / 3.0 - 28.0 / (15.0 * PI);

const float INV_FC82 = 1.0 / 0.4733;

const float SHEEN_ROUGHNESS = 0.3;

const float CLUSTER_NEAR_Z = 0.05;
const float CLUSTER_FAR_Z  = 1000.0;
const float CLUSTER_INV_LOG_RANGE = 1.0 / log2(CLUSTER_FAR_Z / CLUSTER_NEAR_Z);

// =============================================================================
// Inputs
// =============================================================================
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocalPos;
in vec3 vVertexColor;
flat in vec3 vFlatColor;
in vec3 vTangent;
in vec3 vBitangent;

flat in vec3 vWorldCentroid;
flat in vec3 vLocalCentroid;
flat in vec3 vWorldFaceNormal;
flat in vec3 vLocalFaceNormal;

uniform vec3  uAmbientCol;
uniform vec3  uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uGouraudBlend;   // UNUSED

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
    float uSheenExponent;   // UNUSED
    float uSheenStrength;
    float uMatAnisotropic;
    vec3  uMatTransmissionTint;
};

// =============================================================================
// Clustered light data
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
uniform int uNumTilesY;   // UNUSED

layout(std430, binding = 0) buffer LightBuffer         { Light lights[]; };
layout(std430, binding = 1) buffer ClusterBuffer       { uint clusterLights[]; };
layout(std430, binding = 2) buffer ClusterOffsetBuffer { uint clusterOffsets[]; };

// =============================================================================
// Utility
// =============================================================================
float saturate(float x) { return clamp(x, 0.0, 1.0); }
float pow5(float x)      { return x * x * x * x * x; }

// =============================================================================
// Hash + value noise
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

vec3 perturb_normal_bump(vec3 N, vec3 localPos) {
    float freq  = uMatBumpFrequency;
    float speed = uMatBumpSpeed;
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
    gradient *= uMatBumpAmplitude;
    return normalize(N - gradient);
}

vec3 perturb_normal_roughness(vec3 N, vec3 worldPos, vec3 localPos) {
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

    float strength = uMatRoughness * 0.5;
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

// =============================================================================
// Geometric specular anti-aliasing
// =============================================================================
float specular_aa_roughness(vec3 N, float perceptualRoughness) {
    vec3 dndx = dFdx(N);
    vec3 dndy = dFdy(N);
    float variance = (dot(dndx, dndx) + dot(dndy, dndy)) * 0.5;

    float curvatureProxy = sqrt(variance);
    float cap = clamp(0.18 + 0.5 * curvatureProxy, 0.18, 0.5);
    float kernelRoughness2 = min(variance, cap);

    float perceptual2 = perceptualRoughness * perceptualRoughness;
    float filtered2 = min(perceptual2 + kernelRoughness2, 1.0);
    return sqrt(filtered2);
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
// Material-derived scalars
// =============================================================================
vec3 compute_fresnel_f0(vec3 baseColor, float metallic, float ior) {
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    vec3 dielectricF0 = vec3(ratio * ratio);
    return mix(dielectricF0, baseColor, metallic);
}

// =============================================================================
// Microfacet distribution D
// =============================================================================
float D_GGX(float NdotH, float perceptualRoughness) {
    float alpha  = perceptualRoughness * perceptualRoughness;
    float alpha2 = alpha * alpha;
    float denom  = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

// GTR1 distribution. Retained for reference; no longer called since the
// clearcoat lobe moved to GGX.
float D_GTR1(float NdotH, float alpha) {
    if (alpha >= 1.0) return 1.0 / PI;
    float a2 = alpha * alpha;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
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
// Smith geometric / visibility terms
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

float V_Sheen(float NdotL, float NdotV) {
    return 1.0 / (4.0 * max(NdotL + NdotV - NdotL * NdotV, 1e-4));
}

// =============================================================================
// Fresnel
// =============================================================================
vec3 F_Schlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

vec3 F_Schlick_exp(vec3 F0, float cosTheta, float exponent) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), exponent);
}

vec3 F82_tint(vec3 baseColor) {
    return mix(vec3(1.0), baseColor, 0.5);
}

vec3 F82_to_F90(vec3 F0, vec3 F82) {
    return F0 + (F82 - F0) * INV_FC82;
}

vec3 F_Schlick_F82(vec3 F0, vec3 baseColor, float cosTheta) {
    float Fc = pow(saturate(1.0 - cosTheta), 5.0) * INV_FC82;
    vec3 F82 = F82_tint(baseColor);
    return F0 + (F82 - F0) * Fc;
}

// =============================================================================
// Specular BRDF (Cook-Torrance microfacet)
// =============================================================================
float E_ss_GGX(float NdotV, float perceptualRoughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 r = perceptualRoughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB.x + AB.y;
}

vec3 specular_multiscatter_comp(vec3 fss, vec3 F0, vec3 F90,
                                float roughness, float NdotV)
{
    vec3 Favg = F0 + (F90 - F0) / 21.0;
    float E_ss = E_ss_GGX(NdotV, roughness);
    float E_ms = 1.0 - E_ss;
    vec3 ms = fss * Favg * E_ms / max(vec3(1.0) - Favg * E_ms, vec3(1e-4));
    return fss + ms;
}

vec3 specular_microfacet_iso(float NdotL, float NdotV, float NdotH,
                             vec3 fresnel, vec3 F0, vec3 F90, float roughness)
{
    float D = D_GGX(NdotH, roughness);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, roughness);
    vec3 fss = D * vis * fresnel;
    return specular_multiscatter_comp(fss, F0, F90, roughness, NdotV);
}

vec3 specular_microfacet_aniso(vec3 V, vec3 L, vec3 H,
                               float NdotL, float NdotV, float NdotH,
                               vec3 fresnel, vec3 F0, vec3 F90, float roughness,
                               vec3 Tangent, vec3 Bitangent)
{
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
    return specular_multiscatter_comp(fss, F0, F90, roughness, NdotV);
}

// =============================================================================
// Diffuse lobes
// =============================================================================
vec3 diffuse_burley(vec3 N, vec3 V, vec3 L, vec3 H, vec3 baseColor, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float LdotH = max(dot(L, H), 0.0);

    float energyBias   = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float FD90         = energyBias + 2.0 * LdotH * LdotH * roughness;

    float lightScatter = 1.0 + (FD90 - 1.0) * pow5(saturate(1.0 - NdotL));
    float viewScatter  = 1.0 + (FD90 - 1.0) * pow5(saturate(1.0 - NdotV));

    return lightScatter * viewScatter * energyFactor * (baseColor / PI);
}

vec3 diffuse_chan(vec3 diffuseColor, float a2, float NdotV, float NdotL,
                  float VdotH, float NdotH, float retroReflectivityWeight)
{
    NdotV = saturate(NdotV);
    NdotL = saturate(NdotL);
    VdotH = saturate(VdotH);
    NdotH = saturate(NdotH);

    float g = saturate((1.0 / 18.0) * log2(2.0 / max(a2, 1e-4) - 1.0));

    float F0  = VdotH + pow5(1.0 - VdotH);
    float FdV = 1.0 - 0.75 * pow5(1.0 - NdotV);
    float FdL = 1.0 - 0.75 * pow5(1.0 - NdotL);

    float Fd = mix(F0, FdV * FdL, saturate(2.2 * g - 0.5));

    float Fb = ((34.5 * g - 59.0) * g + 24.5)
             * VdotH
             * exp2(-max(73.2 * g - 21.2, 8.9) * sqrt(NdotH));
    Fb *= retroReflectivityWeight;

    float lobe = (1.0 / PI) * (Fd + Fb);
    lobe = min(1.0, lobe);

    return diffuseColor * lobe;
}

float E_FON_approx(float mu, float r) {
    float mucomp  = 1.0 - mu;
    float mucomp2 = mucomp * mucomp;
    const mat2 Gcoeffs = mat2(0.0571085289, -0.332181442,
                              0.491881867,  0.0714429953);
    float GoverPi = dot(Gcoeffs * vec2(mucomp, mucomp2), vec2(1.0, mucomp2));
    return (1.0 + r * GoverPi) / (1.0 + EON_CONST1 * r);
}

vec3 diffuse_eon(vec3 N, vec3 V, vec3 L, vec3 baseColor, float roughness) {
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

    vec3 rho_ms = (baseColor * baseColor) * avgEF
                / (vec3(1.0) - baseColor * (1.0 - avgEF));

    const float eps = 1e-7;
    vec3 f_ms = (rho_ms / PI)
              * max(eps, 1.0 - EFo)
              * max(eps, 1.0 - EFi)
              / max(eps, 1.0 - avgEF);

    return f_ss + f_ms;
}

float minnaert_fd(float NdotL, float NdotV, float k) {
    return pow(NdotL, k) * pow(NdotV, 1.0 - k);
}

// =============================================================================
// Subsurface approximation
// =============================================================================
float subsurface_weight(float NdotL_raw, float strength) {
    const float wrap = 0.5;
    float NdotL_sss = (NdotL_raw + wrap) / (1.0 + wrap);
    float sss = pow(clamp(NdotL_sss, 0.0, 1.0), 2.0);
    return clamp(strength * sss, 0.0, 1.0);
}

// =============================================================================
// Layered lobes
// =============================================================================
vec3 clearcoat_disney(float NdotL, float NdotV, float NdotH,
                      float clearcoatGloss, vec3 clearcoatFresnel,
                      vec3 clearcoatF0)
{
    float alpha = mix(0.1, 0.001, clamp(clearcoatGloss, 0.0, 1.0));
    float perceptualRough = sqrt(alpha);

    float D   = D_GGX(NdotH, perceptualRough);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, perceptualRough);
    vec3  fss = D * vis * clearcoatFresnel;

    return specular_multiscatter_comp(fss, clearcoatF0, vec3(1.0),
                                      perceptualRough, NdotV);
}

vec3 sheen_charlie(vec3 baseColor, vec3 sheenColorTint,
                   float NdotL, float NdotV, float NdotH,
                   float sheenStrength)
{
    float sheenTint = SHEEN_ROUGHNESS;

    float luma = dot(baseColor, LUMA_REC709);
    vec3 sheenColor = mix(vec3(1.0), baseColor / max(luma, 1e-4), sheenTint);
    sheenColor *= sheenColorTint;
    sheenColor = clamp(sheenColor, 0.0, 1.0);

    float D = D_Charlie(NdotH, SHEEN_ROUGHNESS);
    float V = V_Sheen(NdotL, NdotV);

    return sheenColor * D * V * sheenStrength;
}

// -----------------------------------------------------------------------------
// Transmission
// -----------------------------------------------------------------------------
// Stylized transmission lobe. Uses the reflection half-vector normalize(L + V)
// shared with the specular lobe, the GGX microfacet distribution, the
// height-correlated Smith visibility, and a (1 - F) energy split so that
// reflected and transmitted energy sum to one.
//
// This is not a physical refraction BTDF. A physical model would bend the
// half-vector using Snell's law (h_t = -normalize(eta_i * L + eta_t * V)) and
// handle total internal reflection; that requires tracking which side of the
// interface the viewer is on and is out of scope for a single-lobe stylized
// transmission.
//
// The previous version of this function used
//   Ht = normalize(V - 2 * (N . L) * N + L)
// which degenerates to normalize(0) at normal incidence (L and V both aligned
// with N), producing a NaN that shows as flickering transmission hotspots on
// any transmissive material under a light roughly aligned with the view. The
// shared half-vector avoids the degeneracy: L + V is only zero when L = -V,
// which the caller's NdotL > 0 && NdotV > 0 guard excludes.
//
// `F0` is passed in so the Fresnel at the half-vector can be computed here.
// `NdotL` and `NdotV` are pre-clamped by the caller.
vec3 transmission_ggx(vec3 N, vec3 V, vec3 L,
                      float NdotL, float NdotV,
                      float roughness, float strength, vec3 tint,
                      vec3 F0)
{
    float transRoughness = clamp(roughness * 0.8, 0.01, 1.0);

    vec3 Ht = normalize(L + V);
    float NdotHt = max(dot(N, Ht), 0.0);

    float Dt = D_GGX(NdotHt, transRoughness);
    float vis = V_SmithGGXCorrelated(NdotL, NdotV, transRoughness);

    // Fresnel at the shared half-vector. (1 - F) is the fraction of energy
    // that is transmitted rather than reflected, so it enters the lobe as an
    // energy-splitting factor.
    float VdotHt = max(dot(V, Ht), 0.0);
    vec3 F_trans = F_Schlick(F0, VdotHt);
    vec3 transmittance = vec3(1.0) - F_trans;

    return vec3(Dt * vis) * tint * strength * transmittance;
}

vec3 back_glow_lobe(vec3 N, vec3 L) {
    return uMatBackGlowColor * max(dot(N, -L), 0.0);
}

vec3 rim_lobe(float NdotV) {
    return uMatRimColor * pow(saturate(1.0 - NdotV), uMatRimExponent);
}

// =============================================================================
// Tone mapping (HDR -> LDR)
// =============================================================================
vec3 tone_map(vec3 color) {
    const float exposure = 2.93;

    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2;

    float lum        = dot(color, LUMA_REC709);
    float exposedLum = lum * exposure;

    float hableLum   = ((exposedLum * (A * exposedLum + C * B) + D * E)
                      / (exposedLum * (A * exposedLum + B) + D * F)) - E / F;
    float hableWhite = ((W * (A * W + C * B) + D * E)
                      / (W * (A * W + B) + D * F)) - E / F;
    float mappedLum  = hableLum / hableWhite;

    float lumScale = mappedLum / max(lum, 1e-5);
    vec3  mappedColor = color * lumScale;

    float maxC = max(mappedColor.r, max(mappedColor.g, mappedColor.b));
    if (maxC > 1.0) {
        float over = maxC - 1.0;
        float compressedMax = 1.0 + over / (1.0 + over * 4.0);
        mappedColor *= compressedMax / maxC;
    }

    return mappedColor;
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
// Light accumulation
// =============================================================================
void accumulate_light(vec3 N, vec3 V, vec3 L, vec3 lightCol,
                      vec3 F0,
                      vec3 Tangent, vec3 Bitangent,
                      inout vec3 diffuse, inout vec3 specular,
                      inout vec3 clearcoat, inout vec3 sheen,
                      inout vec3 rim, inout vec3 backGlow,
                      float NdotV,
                      float diffuseRoughness, float specularRoughness)
{
    float NdotL_raw = dot(N, L);
    float NdotL     = max(NdotL_raw, 0.0);

    vec3  diffuseColor = uMatColor * (1.0 - uMatMetallic);
    float clearcoatStrength = clamp(uClearcoatStrength, 0.0, 1.0);

    vec3  H     = normalize(L + V);
    float VdotH = min(max(dot(V, H), 0.0), 1.0);
    float NdotH = max(dot(N, H), 0.0);

    // ---- Step 1: base diffuse lobe -----------------------------------------
    vec3 diffuseBRDF;
#ifdef EFFECT_OREN_NAYAR
    diffuseBRDF = diffuse_eon(N, V, L, diffuseColor, uMatOrenNayarSigma);
#elif defined(EFFECT_MINNAERT)
    diffuseBRDF = minnaert_fd(NdotL, NdotV, uMatMinnaertK) * (diffuseColor / PI);
#elif defined(EFFECT_BURLEY_DIFFUSE)
    diffuseBRDF = diffuse_burley(N, V, L, H, diffuseColor, diffuseRoughness);
#else
    {
        float a2 = diffuseRoughness * diffuseRoughness;
        a2 *= a2;   // roughness^4
        diffuseBRDF = diffuse_chan(diffuseColor, a2, NdotV, NdotL,
                                   VdotH, NdotH, 1.0);
    }
#endif

    // ---- Step 2: wrap modifier ---------------------------------------------
#ifdef EFFECT_DIFFUSE_WRAP
    {
        float wrapFactor = NdotL * NdotL * (3.0 - 2.0 * NdotL);
        float safeNdotL  = max(NdotL, 1e-4);
        diffuseBRDF *= (wrapFactor / safeNdotL);
    }
#endif

    // ---- Step 3: cel modifier ----------------------------------------------
#ifdef EFFECT_CEL_SHADING
    {
        float inv = 1.0 / float(uMatCelBands);
        float celFactor = min(1.0, floor(NdotL * float(uMatCelBands)) * inv);
        float safeNdotL = max(NdotL, 1e-4);
        diffuseBRDF *= (celFactor / safeNdotL);
    }
#endif

    // ---- Subsurface blend --------------------------------------------------
#ifdef EFFECT_SUBSURFACE
    {
        float sssStrength = clamp(uMatSubsurfaceStrength, 0.0, 2.0);
        float blend = subsurface_weight(NdotL_raw, sssStrength);
        vec3 sssBRDF = pow(clamp((NdotL_raw + 0.5) / 1.5, 0.0, 1.0), 2.0)
                     * (uMatColor / PI);
        diffuseBRDF = mix(diffuseBRDF, sssBRDF, blend);
    }
#endif

    vec3  F_diffuse = F_Schlick(F0, NdotV);
    vec3  diffuseContrib = diffuseBRDF * NdotL * lightCol * (1.0 - F_diffuse);

    // ---- Main specular -----------------------------------------------------
    vec3 specContrib = vec3(0.0);
    if (NdotL > 0.0 && NdotV > 0.0) {
        vec3 fresnel;
        vec3 F90;
#ifdef EFFECT_FRESNEL
        float fresnelExp = max(uMatFresnelExponent, 0.1);
        fresnel = F_Schlick_exp(F0, VdotH, fresnelExp);
        F90 = vec3(1.0);
#else
        float metallic = clamp(uMatMetallic, 0.0, 1.0);
        vec3 F_schlick = F_Schlick(F0, VdotH);
        vec3 F_f82     = F_Schlick_F82(F0, uMatColor, VdotH);
        fresnel = mix(F_schlick, F_f82, metallic);

        vec3 F90_f82 = F82_to_F90(F0, F82_tint(uMatColor));
        F90 = mix(vec3(1.0), F90_f82, metallic);
#endif

#ifdef EFFECT_ANISOTROPIC
        specContrib = specular_microfacet_aniso(V, L, H,
                                                NdotL, NdotV, NdotH,
                                                fresnel, F0, F90,
                                                specularRoughness,
                                                Tangent, Bitangent);
#else
        specContrib = specular_microfacet_iso(NdotL, NdotV, NdotH,
                                              fresnel, F0, F90,
                                              specularRoughness);
#endif

        specContrib *= uMatSpecularTint;
    }

    // ---- Transmission ------------------------------------------------------
    vec3 transContrib = vec3(0.0);
#ifdef EFFECT_TRANSMISSION
    {
        float transStrength = clamp(uMatTransmissionStrength, 0.0, 1.0);
        if (transStrength > 0.001 && NdotL > 0.0 && NdotV > 0.0) {
            transContrib = transmission_ggx(N, V, L, NdotL, NdotV,
                                            specularRoughness, transStrength,
                                            uMatTransmissionTint, F0);
        }
    }
#endif

    // ---- Clearcoat ---------------------------------------------------------
    vec3 clearcoatContrib = vec3(0.0);
    vec3 clearcoatFresnel = vec3(0.0);
    vec3 clearcoatF0      = vec3(0.04);
#ifdef EFFECT_CLEARCOAT
    if (clearcoatStrength > 0.0 && NdotL > 0.0 && NdotV > 0.0) {
        clearcoatFresnel = F_Schlick(clearcoatF0, VdotH);
        float clearcoatGloss = 1.0 - clamp(uClearcoatRoughness, 0.0, 1.0);
        clearcoatContrib = clearcoat_disney(NdotL, NdotV, NdotH,
                                            clearcoatGloss, clearcoatFresnel,
                                            clearcoatF0);
    }
#endif

    // ---- Sheen (Charlie) ---------------------------------------------------
    vec3 sheenContrib = vec3(0.0);
#ifdef EFFECT_SHEEN
    if (NdotL > 0.0 && NdotV > 0.0) {
        sheenContrib = sheen_charlie(uMatColor, uSheenColor,
                                     NdotL, NdotV, NdotH,
                                     uSheenStrength);
    }
#endif

    // ---- Back glow ---------------------------------------------------------
    vec3 backGlowContrib = vec3(0.0);
#ifdef EFFECT_BACK_GLOW
    backGlowContrib = back_glow_lobe(N, L);
#endif

    // ---- Rim ---------------------------------------------------------------
    vec3 rimContrib = vec3(0.0);
#ifdef EFFECT_RIM
    rimContrib = rim_lobe(NdotV);
#endif

    // ---- Energy conservation: layered lobes attenuate the base -------------
#ifdef EFFECT_CLEARCOAT
    float coatPath = 1.0 / max(NdotV, 0.1);
    float coatAbsorption = exp(-clearcoatStrength * 0.35 * coatPath);
    vec3  coatTransmission = coatAbsorption
                           * (vec3(1.0) - clearcoatFresnel * clearcoatStrength);
    diffuseContrib *= coatTransmission;
    specContrib    *= coatTransmission;
#endif
#ifdef EFFECT_TRANSMISSION
    float transScale = 1.0 - clamp(uMatTransmissionStrength, 0.0, 1.0);
    diffuseContrib *= transScale;
    specContrib    *= transScale;
#endif

    // ---- Sum into the output accumulators ----------------------------------
    diffuse   += diffuseContrib;
    specular  += specContrib * lightCol * NdotL;
    clearcoat += clearcoatContrib * lightCol * uClearcoatColor
               * clearcoatStrength * NdotL;
    sheen     += sheenContrib * lightCol * NdotL;
    backGlow  += backGlowContrib * lightCol;
    rim       += rimContrib * lightCol;

#ifdef EFFECT_TRANSMISSION
    specular += transContrib * lightCol * NdotL;
#endif
}

// =============================================================================
// Surface shading
// =============================================================================
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    vec3 V = normalize(uCamEye - worldPos);

    vec3 N_bumped = N;
#ifdef EFFECT_BUMP
    N_bumped = perturb_normal_bump(N_bumped, localPos);
#endif
#ifdef EFFECT_ROUGHNESS
    N_bumped = perturb_normal_roughness(N_bumped, worldPos, localPos);
#endif
    N_bumped = normalize(N_bumped);

    // Only the specular chain receives the widened roughness.
    float diffuseRoughness = uMatSurfaceRoughness;
    float specularRoughness = clamp(
        specular_aa_roughness(N_bumped, uMatSurfaceRoughness),
        MIN_PERCEPTUAL_ROUGHNESS, 1.0);

    float NdotV = max(dot(N_bumped, V), 0.0);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3  F0 = compute_fresnel_f0(uMatColor, metallic, uMatIor);

    vec3 Tangent   = vec3(0.0);
    vec3 Bitangent = vec3(0.0);
#ifdef EFFECT_ANISOTROPIC
    Tangent   = normalize(vTangent - N_bumped * dot(vTangent, N_bumped));
    Bitangent = normalize(cross(N_bumped, Tangent));
#endif

    vec3 totalDiffuse   = vec3(0.0);
    vec3 totalSpec      = vec3(0.0);
    vec3 totalClearcoat = vec3(0.0);
    vec3 totalSheen     = vec3(0.0);
    vec3 totalRim       = vec3(0.0);
    vec3 totalBackGlow  = vec3(0.0);

#ifdef EFFECT_GOOCH
    vec3  weightedDir = vec3(0.0);
    float totalWeight = 0.0;
#endif

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

        accumulate_light(N_bumped, V, lightDir, lightCol, F0,
                         Tangent, Bitangent,
                         totalDiffuse, totalSpec, totalClearcoat, totalSheen,
                         totalRim, totalBackGlow,
                         NdotV,
                         diffuseRoughness, specularRoughness);

#ifdef EFFECT_GOOCH
        weightedDir += lightDir * intensity;
        totalWeight += intensity;
#endif
    }

    // =========================================================================
    // HDR composition (before tone mapping)
    // =========================================================================
    vec3 diffuseColor = uMatColor * (1.0 - metallic);
    vec3 baseColor = uAmbientCol * diffuseColor * uMatAmbientLightFactor + totalDiffuse;

    float ao = clamp(uMatAmbientLightFactor, 0.0, 1.0);
    float specOcc = specular_occlusion(NdotV, ao, specularRoughness);
    vec3 ambientSpec = uAmbientCol * F0 * specOcc * uMatAmbientLightFactor;

#ifdef EFFECT_GOOCH
    if (totalWeight > 0.001) {
        vec3 avgDir = weightedDir / totalWeight;
        float len = length(avgDir);
        if (len > 0.001) {
            avgDir /= len;
            float ndotl_avg = max(dot(N_bumped, avgDir), 0.0);
            float t_gooch = (ndotl_avg + 1.0) * 0.5;
            vec3 goochFactor = mix(uMatGoochCool, uMatGoochWarm, t_gooch);
            baseColor *= goochFactor;
        }
    }
#endif

    vec3 color = baseColor + ambientSpec;
    color += totalSpec + totalClearcoat + totalSheen + totalRim + totalBackGlow;

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

    // ---- LDR post effects --------------------------------------------------
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
        float r = color.r * rot0 + color.g * rot1 + color.b * rot2;
        float g = color.r * rot3 + color.g * rot4 + color.b * rot5;
        float b = color.r * rot6 + color.g * rot7 + color.b * rot8;
        float strength = uMatIridescenceStrength;
        color.r = r * strength + color.r * (1.0 - strength);
        color.g = g * strength + color.g * (1.0 - strength);
        color.b = b * strength + color.b * (1.0 - strength);
    }
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
// Entry point
// =============================================================================
out vec4 FragColor;

void main() {
    vec3 color;

#if defined(MODE_WIREFRAME) || defined(MODE_FLAT)
    color = vFlatColor;
#elif defined(MODE_GOURAUD)
    color = vVertexColor;
#else
    color = shade_surface(vNormal, vWorldPos, vLocalPos);
#endif

    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = uMatAlpha;
#endif
    float dither = (hash_float(vec3(gl_FragCoord.xy, uTime)) - 0.5) / 255.0;
    color += dither;
    FragColor = vec4(color, alpha);
}

#endif /* DEPTH_ONLY */