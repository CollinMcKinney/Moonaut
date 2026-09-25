#version 430 core

// =============================================================================
// material.frag
// =============================================================================

#ifdef DEPTH_ONLY
void main() { }
#else

#define PI 3.141592653589793

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

#define MIN_PERCEPTUAL_ROUGHNESS 0.01

const float EON_CONST1 = 0.5 - 2.0 / (3.0 * PI);
const float EON_CONST2 = 2.0 / 3.0 - 28.0 / (15.0 * PI);

const float CLUSTER_NEAR_Z = 0.05;
const float CLUSTER_FAR_Z  = 1000.0;
const float CLUSTER_INV_LOG_RANGE = 1.0 / log2(CLUSTER_FAR_Z / CLUSTER_NEAR_Z);

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

#if defined(ALPHA_PASS_BEHIND) || defined(ALPHA_PASS_FRONT)
layout(binding = 3) uniform sampler2D uTransmissiveDepthTex;
#endif

layout(binding = 4) uniform sampler2D uAOTex;

uniform float     uRefractionScale;
uniform int       uAlphaPass;

layout(binding = 5) uniform samplerCube uEnvCube;
uniform float uEnvCubeMaxMip;

uniform float uSkyAmbientScale;

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
// Saturate Overloads (Fixes GLSL vector mismatch)
// =============================================================================
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2  saturate(vec2 x)  { return clamp(x, vec2(0.0), vec2(1.0)); }
vec3  saturate(vec3 x)  { return clamp(x, vec3(0.0), vec3(1.0)); }
vec4  saturate(vec4 x)  { return clamp(x, vec4(0.0), vec4(1.0)); }

// =============================================================================
// Noise primitives
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
// Environment sampling
// =============================================================================
vec3 sample_env_map(vec3 dir, float roughness) {
#ifdef USE_ENV_CUBE
    float mip = clamp(roughness * uEnvCubeMaxMip, 0.0, uEnvCubeMaxMip);
    return textureLod(uEnvCube, dir, mip).rgb;
#else
    return uAmbientCol;
#endif
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
// Specular AA
// =============================================================================
float specular_aa_roughness_halfvec(vec3 N, vec3 V, vec3 L, float r) {
    vec3 Hraw = L + V;
    if (dot(Hraw, Hraw) < 1e-8) return r;
    vec3 H = normalize(Hraw);
    float NdotH = max(dot(N, H), 1e-4);
    vec3 dHdx = dFdx(H);
    vec3 dHdy = dFdy(H);
    vec3 dHdx_proj = dHdx - N * dot(dHdx, N);
    vec3 dHdy_proj = dHdy - N * dot(dHdy, N);
    float varX = dot(dHdx_proj, dHdx_proj) / (NdotH * NdotH);
    float varY = dot(dHdy_proj, dHdy_proj) / (NdotH * NdotH);
    float slopeVariance = min(0.25 * (varX + varY), 0.18);
    float p2 = r * r;
    float kernel = clamp(slopeVariance / (p2 + slopeVariance + 1e-6), 0.0, 1.0);
    return sqrt(min(p2 + kernel * slopeVariance, 1.0));
}

float specular_aa_roughness(vec3 N, float r) {
    vec3 dndx = dFdx(N);
    vec3 dndy = dFdy(N);
    float variance = min(0.25 * (dot(dndx, dndx) + dot(dndy, dndy)), 0.18);
    float p2 = r * r;
    float kernel = clamp(variance / (p2 + variance + 1e-6), 0.0, 1.0);
    return sqrt(min(p2 + kernel * variance, 1.0));
}

float specular_occlusion(float NdotV, float ao, float roughness) {
    float occ = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    return mix(1.0, occ, smoothstep(0.1, 0.3, roughness));
}

// =============================================================================
// Material scalars
// =============================================================================
vec3 compute_fresnel_f0(vec3 baseColor, float metallic, float ior) {
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    return mix(vec3(ratio * ratio), baseColor, metallic);
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
float D_GGX(float NdotH, float r) {
    float a = r * r;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float D_GTR2_aniso(float HdotT, float HdotB, float HdotN, float ax, float ay) {
    float d = (HdotT * HdotT) / (ax * ax) + (HdotB * HdotB) / (ay * ay) + HdotN * HdotN;
    return 1.0 / (PI * ax * ay * d * d);
}

float D_Charlie(float NdotH, float r) {
    float invA = 1.0 / max(r, 1e-4);
    float c2 = NdotH * NdotH;
    float s2 = max(1.0 - c2, 1e-4);
    return (2.0 + invA) * pow(s2, invA * 0.5) / (2.0 * PI);
}

float D_Charlie_Aniso(float HdotT, float HdotB, float NdotH, float ax, float ay) {
    float invAx = 1.0 / max(ax, 1e-4);
    float invAy = 1.0 / max(ay, 1e-4);
    return (2.0 + sqrt(invAx * invAy)) * pow(max(1.0 - NdotH * NdotH, 1e-4), 0.5 * sqrt(invAx * invAy)) / (2.0 * PI);
}

// =============================================================================
// Visibility
// =============================================================================
float V_SmithGGXCorrelated(float NdotL, float NdotV, float r) {
    float a = r * r;
    float a2 = a * a;
    float lv = NdotL * sqrt(max(0.0, NdotV * (NdotV - NdotV * a2) + a2));
    float ll = NdotV * sqrt(max(0.0, NdotL * (NdotL - NdotL * a2) + a2));
    return 0.5 / max(lv + ll, 1e-5);
}

float V_SmithGGXCorrelated_Aniso(float NdotL, float NdotV,
                                 float LdotT, float LdotB,
                                 float VdotT, float VdotB,
                                 float at, float ab) {
    float lv = NdotL * length(vec3(VdotT * at, VdotB * ab, NdotV));
    float ll = NdotV * length(vec3(LdotT * at, LdotB * ab, NdotL));
    return 0.5 / max(lv + ll, 1e-5);
}

// =============================================================================
// Fresnel Formulations
// =============================================================================
vec3 F_Schlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

vec3 F_Schlick_F82(vec3 F0, vec3 F82, float cosTheta) {
    float mu = saturate(cosTheta);
    const float MU_HAT = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 Fe = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 b = Fe * (1.0 - F82) / denom;
    vec3 F = F_Schlick(F0, mu) - b * mu * pow(1.0 - mu, 6.0);
    return clamp(F, vec3(0.0), vec3(1.0));
}

vec3 F_ThinFilm_Airy(float cosTheta, float strength, float filmIOR, vec3 baseF0) {
    if (strength <= 0.0) return baseF0;

    float d = mix(100.0, 800.0, strength); 
    float n2 = filmIOR > 1.0 ? filmIOR : 1.33;

    float eta = 1.0 / n2;
    float sinSq = eta * eta * (1.0 - cosTheta * cosTheta);
    if (sinSq > 1.0) return vec3(1.0);
    float cosThetaT = sqrt(1.0 - sinSq);

    float opd = 2.0 * n2 * d * cosThetaT;

    const vec3 WAVELENGTHS = vec3(650.0, 550.0, 450.0);
    vec3 phase = (2.0 * PI * opd) / WAVELENGTHS;

    float Rs = (cosTheta - n2 * cosThetaT) / (cosTheta + n2 * cosThetaT);
    float Rp = (n2 * cosTheta - cosThetaT) / (n2 * cosTheta + cosThetaT);
    vec3 R12 = vec3((Rs * Rs + Rp * Rp) * 0.5);
    vec3 R23 = baseF0;
    vec3 interference = clamp(R12 + R23 + 2.0 * sqrt(R12 * R23) * cos(phase), 0.0, 1.0);

    return mix(baseF0, interference, strength);
}

vec3 compute_fresnel_avg(vec3 F0, vec3 F82, float metallic) {
    vec3 F_avg_schlick = F0 + (1.0 - F0) / 21.0;
    const float MU_HAT = 1.0 / 7.0;
    const float ONE_MINUS = 6.0 / 7.0;
    vec3 Fe = F_Schlick(F0, MU_HAT);
    float denom = MU_HAT * pow(ONE_MINUS, 6.0);
    vec3 bm = Fe * (1.0 - F82) / denom;
    return clamp(F_avg_schlick - metallic * bm / 126.0, vec3(0.0), vec3(1.0));
}

// =============================================================================
// Specular BRDF
// =============================================================================
float E_ss_GGX(float NdotV, float r) {
    r = r * r;
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 rr = r * c0 + c1;
    float a004 = min(rr.x * rr.x, exp2(-9.28 * NdotV)) * rr.x + rr.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + rr.zw;
    return AB.x + AB.y;
}

vec2 env_brdf_approx(float NdotV, float r) {
    r = r * r;
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 rr = r * c0 + c1;
    float a004 = min(rr.x * rr.x, exp2(-9.28 * NdotV)) * rr.x + rr.y;
    return vec2(-1.04, 1.04) * a004 + rr.zw;
}

vec3 specular_multiscatter_comp(vec3 fss, vec3 F0, vec3 F_avg, float r, float NdotV) {
    float E = E_ss_GGX(NdotV, r);
    vec3 Fms = F_avg * (1.0 / max(E, 1e-4) - 1.0);
    return fss * (vec3(1.0) + Fms);
}

vec3 specular_microfacet_iso(float NdotL, float NdotV, float NdotH,
                             vec3 F, vec3 F0, vec3 F_avg, float r) {
    float D = D_GGX(NdotH, r);
    float V = V_SmithGGXCorrelated(NdotL, NdotV, r);
    return specular_multiscatter_comp(D * V * F, F0, F_avg, r, NdotV);
}

vec3 specular_microfacet_aniso(vec3 V, vec3 L, vec3 H,
                               float NdotL, float NdotV, float NdotH,
                               vec3 F, vec3 F0, vec3 F_avg, float r,
                               vec3 T, vec3 B) {
    float an = clamp(uMatAnisotropic, -0.99, 0.99);
    float aspect = sqrt(1.0 - 0.9 * an);
    float a = r * r;
    float ax = max(a / aspect, 0.001);
    float ay = max(a * aspect, 0.001);
    float D = D_GTR2_aniso(dot(H, T), dot(H, B), NdotH, ax, ay);
    float vis = V_SmithGGXCorrelated_Aniso(NdotL, NdotV,
                                           dot(L, T), dot(L, B),
                                           dot(V, T), dot(V, B),
                                           ax, ay);
    return specular_multiscatter_comp(D * vis * F, F0, F_avg, r, NdotV);
}

// =============================================================================
// Diffuse
// =============================================================================
float E_FON_approx(float mu, float r) {
    float m = 1.0 - mu;
    float m2 = m * m;
    const mat2 G = mat2(0.0571085289, -0.332181442, 0.491881867, 0.0714429953);
    float Gp = dot(G * vec2(m, m2), vec2(1.0, m2));
    return (1.0 + r * Gp) / (1.0 + EON_CONST1 * r);
}

vec3 diffuse_eon_oren_nayar(vec3 N, vec3 V, vec3 L, vec3 baseColor, float r) {
    float mi = max(dot(N, L), 0.0);
    float mo = max(dot(N, V), 0.0);
    float s = dot(L, V) - mi * mo;
    float sovertF;
    if (s > 0.0) {
        float d = max(mi, mo);
        sovertF = d > 1e-4 ? s / d : 0.0;
    } else sovertF = s;
    float AF = 1.0 / (1.0 + EON_CONST1 * r);
    vec3 f_ss = (baseColor / PI) * AF * (1.0 + r * sovertF);
    float EFo = E_FON_approx(mo, r);
    float EFi = E_FON_approx(mi, r);
    float avgEF = AF * (1.0 + EON_CONST2 * r);
    vec3 denom = max(vec3(1e-4), vec3(1.0) - baseColor * (1.0 - avgEF));
    vec3 rho_ms = (baseColor * baseColor) * avgEF / denom;
    const float eps = 1e-7;
    vec3 f_ms = (rho_ms / PI) * max(eps, 1.0 - EFo)
              * max(eps, 1.0 - EFi) / max(eps, 1.0 - avgEF);
    return f_ss + f_ms;
}

vec3 subsurface_chromatic_modulation(vec3 c, float NdotV, float strength) {
    float m = max(c.r, max(c.g, c.b));
    m = max(m, 1e-4);
    vec3 albedo = c / m;
    float mean = (albedo.r + albedo.g + albedo.b) / 3.0;
    vec3 d = albedo - mean;
    float g = pow(1.0 - NdotV, 2.0);
    return max(vec3(1.0) + d * g * strength, vec3(0.0));
}

// =============================================================================
// Layered lobes
// =============================================================================
vec3 clearcoat_disney(float NdotL, float NdotV, float NdotH,
                      float clearcoatRoughness, vec3 Fresnel, vec3 F0) {
    float r = clamp(clearcoatRoughness, 0.01, 1.0);
    float D = D_GGX(NdotH, r);
    float V = V_SmithGGXCorrelated(NdotL, NdotV, r);
    vec3 Fa = F0 + (1.0 - F0) / 21.0;
    return specular_multiscatter_comp(D * V * Fresnel, F0, Fa, r, NdotV);
}

vec3 compute_clearcoat_absorption(float NdotV, vec3 coatColor, float coatStrength, float coatIOR) {
    if (coatStrength <= 0.0) return vec3(1.0);

    float eta = coatIOR > 1.0 ? coatIOR : 1.5;
    float sinSq = (1.0 / (eta * eta)) * (1.0 - NdotV * NdotV);
    float cosThetaT = sqrt(max(1.0 - sinSq, 1e-4));

    float pathLength = coatStrength / cosThetaT;
    vec3 sigma_a = -log(clamp(coatColor, vec3(0.001), vec3(1.0)));
    return exp(-sigma_a * pathLength);
}

vec3 sheen_charlie_aniso(vec3 baseColor, vec3 sheenTint,
                         float NdotL, float NdotV, float NdotH,
                         float HdotT, float HdotB, float VdotH,
                         float r, float strength, float aniso) {
    const float SHEEN_TINT = 0.3;
    float luma = dot(baseColor, LUMA_REC709);
    vec3 c = mix(vec3(1.0), baseColor / max(luma, 1e-4), SHEEN_TINT);
    c = clamp(c * sheenTint, 0.0, 1.0);

    float an = clamp(aniso, -0.99, 0.99);
    float aspect = sqrt(1.0 - 0.9 * an);
    float ax = max(r / aspect, 0.001);
    float ay = max(r * aspect, 0.001);

    float D = D_Charlie_Aniso(HdotT, HdotB, NdotH, ax, ay);
    float V = 1.0 / max(4.0 * (NdotL + NdotV - NdotL * NdotV), 1e-4);
    vec3 F = F_Schlick(vec3(0.04), VdotH);
    return c * D * V * strength * F;
}

vec3 sheen_charlie(vec3 baseColor, vec3 sheenTint,
                   float NdotL, float NdotV, float NdotH,
                   float VdotH, float r, float strength) {
    const float SHEEN_TINT = 0.3;
    float luma = dot(baseColor, LUMA_REC709);
    vec3 c = mix(vec3(1.0), baseColor / max(luma, 1e-4), SHEEN_TINT);
    c = clamp(c * sheenTint, 0.0, 1.0);

    float D = D_Charlie(NdotH, r);
    float V = 1.0 / max(4.0 * (NdotL + NdotV - NdotL * NdotV), 1e-4);
    vec3 F = F_Schlick(vec3(0.04), VdotH);
    return c * D * V * strength * F;
}

float compute_coat_darkening(vec3 coatF0, vec3 baseColor, float NdotV, float baseRough) {
    float Ks = F_Schlick(coatF0, NdotV).r;
    float Kr = coatF0.r + (1.0 - coatF0.r) / 21.0;
    float K0 = mix(Ks, Kr, clamp(baseRough, 0.0, 1.0));
    float E = dot(baseColor, LUMA_REC709);
    return (1.0 - K0) / (1.0 - E * K0);
}

float sheen_directional_albedo(float NdotV, float r) {
    float r2 = r * r;
    float a = r < 0.25 ? -339.2 * r2 + 161.4 * r - 25.9 : -8.48 * r2 + 14.3 * r - 9.95;
    float b = r < 0.25 ?   44.0 * r2 -  23.7 * r +  3.26 :  1.97 * r2 -  3.27 * r +  0.72;
    float DG = exp(a * NdotV + b) + (r < 0.25 ? 0.0 : 0.1 * (r - 0.25));
    return saturate(DG / PI);
}

float sheen_base_transmittance(float NdotV) {
    float E = sheen_directional_albedo(NdotV, uMatSheenRoughness);
    vec3 Fav = clamp(uMatSheenColor, 0.0, 0.99);
    float Fl = dot(Fav, LUMA_REC709);
    float num = (1.0 - E) * (1.0 - E) * Fl * Fl * E;
    float den = max(PI * (1.0 - E) * (1.0 - Fl * (1.0 - E)), 1e-4);
    float op = saturate(num / den) * dot(uMatSheenColor, LUMA_REC709) * uMatSheen;
    return saturate(1.0 - op);
}

// =============================================================================
// Micro-Diffraction Lobe
// =============================================================================
vec3 lobe_micro_diffraction(vec3 N, vec3 V, vec3 L, vec3 tangent, float diffractionIntensity) {
    if (diffractionIntensity <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float projH = dot(H, tangent);

    float d = mix(500.0, 3000.0, diffractionIntensity);

    const vec3 WAVELENGTHS = vec3(650.0, 550.0, 450.0);
    vec3 m = (d * projH) / WAVELENGTHS;

    vec3 diffraction = pow(saturate(cos(PI * m)), vec3(64.0));
    return diffractionIntensity * diffraction * saturate(dot(N, L));
}

// =============================================================================
// Transmission
// =============================================================================
vec3 transmission_ggx(vec3 N, vec3 V, vec3 L_trans, float NdotL_trans, float NdotV,
                      float r, float strength, vec3 tint, vec3 F0, float ior) {
    float etaI = gl_FrontFacing ? 1.0 : ior;
    float etaT = gl_FrontFacing ? ior : 1.0;

    vec3 Ht = -(etaI * L_trans + etaT * V);
    float htLen2 = dot(Ht, Ht);
    if (htLen2 < 1e-8) return vec3(0.0);
    Ht *= inversesqrt(htLen2);

    if (dot(N, Ht) < 0.0) Ht = -Ht;

    float NdotHt = max(dot(N, Ht), 1e-4);
    float VdotHt = max(dot(V, Ht), 1e-4);
    float LdotHt = max(dot(L_trans, Ht), 1e-4);

    float sqrtDenom = etaI * LdotHt + etaT * VdotHt;
    if (sqrtDenom < 1e-4) return vec3(0.0);

    float tr = clamp(r, 0.01, 1.0);
    float D = D_GGX(NdotHt, tr);
    float vis = V_SmithGGXCorrelated(NdotV, NdotL_trans, tr);
    vec3 F = F_Schlick_F82(F0, uMatF82Tint, VdotHt);

    float factor = 4.0 * abs(LdotHt) * abs(VdotHt);
    float jacobian = (etaT * etaT * factor) / (sqrtDenom * sqrtDenom);

    vec3 T = (vec3(1.0) - F) * strength;
    return D * vis * jacobian * T * tint;
}

// =============================================================================
// Support lobes
// =============================================================================
float fresnel_scalar_dielectric(float cosTheta) {
    return dot(F_Schlick(compute_dielectric_f0(), cosTheta), LUMA_REC709);
}

vec3 rim_lobe(float NdotV) {
    float f0 = dot(compute_dielectric_f0(), LUMA_REC709);
    float g = pow(saturate(1.0 - NdotV), max(uMatRimExponent, 0.001));
    return uMatRimColor * (f0 + (1.0 - f0) * g);
}

vec3 back_glow_lobe(vec3 N, vec3 L, float NdotV) {
    float cb = max(dot(N, -L), 0.0);
    float Tb = 1.0 - fresnel_scalar_dielectric(cb);
    float Tf = 1.0 - fresnel_scalar_dielectric(NdotV);
    float mm = 1.0 - clamp(uMatMetallic, 0.0, 1.0);
    return uMatBackGlowColor * cb * Tb * Tf * mm;
}

// =============================================================================
// Cluster
// =============================================================================
void cluster_lookup(out uint count, out uint base) {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 tile = pixel / CLUSTER_TILE_SIZE;
    float depth = max(gl_FragCoord.z, 1e-6);
    float logDepth = log2(depth) * CLUSTER_INV_LOG_RANGE;
    int slice = clamp(int(floor(logDepth * CLUSTER_DEPTH_SLICES)), 0, CLUSTER_DEPTH_SLICES - 1);
    uint ci = uint(tile.y * uNumTilesX + tile.x);
    uint oi = ci * CLUSTER_DEPTH_SLICES + uint(slice);
    count = clusterOffsets[oi];
    base = oi * CLUSTER_MAX_LIGHTS_PER;
}

bool evaluate_light(Light l, vec3 wp, out vec3 ld, out float at) {
    int t = int(l.pos.w);
    vec3 lp = l.pos.xyz;
    at = 1.0;
    if (t == 0) { ld = normalize(l.dir.xyz); return true; }
    if (t == 1 || t == 2) {
        vec3 tl = lp - wp;
        float d = max(length(tl), 0.01);
        if (d > l.range) return false;
        float rr = d / l.range;
        float a = max(0.0, 1.0 - rr * rr);
        a *= a; a /= max(d * d, 0.01);
        ld = normalize(tl);
        at = a;
        if (t == 2) {
            float ca = dot(-ld, normalize(l.dir.xyz));
            if (ca < l.outer_cos) return false;
            float sp = clamp((ca - l.outer_cos) / (l.inner_cos - l.outer_cos), 0.0, 1.0);
            at *= pow(sp, l.falloff);
        }
        return true;
    }
    return false;
}

// =============================================================================
// Lobe contributions
// =============================================================================
vec3 lobe_diffuse(vec3 N, vec3 V, vec3 L, vec3 lc, vec3 F_avg,
                  float NdotL, float NdotL_raw) {
    vec3 dc = uMatAlbedo * (1.0 - uMatMetallic);
    vec3 brdf;

#ifdef EFFECT_SUBSURFACE
    float s = clamp(uMatSubsurface, 0.0, 2.0);
    float nv = max(dot(N, V), 1e-4);
    dc *= min(subsurface_chromatic_modulation(uMatSubsurfaceColor, nv, s), 1.0);
#endif
    brdf = diffuse_eon_oren_nayar(N, V, L, dc, uMatDiffuseRoughness);

#ifdef EFFECT_DIFFUSE_WRAP
    float wf = NdotL * NdotL * (3.0 - 2.0 * NdotL);
    brdf *= wf / max(NdotL, 1e-4);
#endif
#ifdef EFFECT_CEL_SHADING
    float b = max(float(uMatCelBands), 1.0);
    float cf = min(1.0, floor(NdotL * b) / b);
    brdf *= cf / max(NdotL, 1e-4);
#endif

    vec3 result = brdf * NdotL;
#ifdef EFFECT_SUBSURFACE
    if (NdotL_raw < 0.0 && uMatMetallic < 0.5) {
        float nl = -NdotL_raw;
        float vd = saturate(dot(V, -L));
        float mm = 1.0 - clamp(uMatMetallic, 0.0, 1.0);
        float ph = pow(vd, 4.0);
        float pl = 1.0 / max(nl, 0.1);
        float mc = max(uMatSubsurfaceColor.r, max(uMatSubsurfaceColor.g, uMatSubsurfaceColor.b));
        mc = max(mc, 1e-4);
        vec3 ab = uMatSubsurfaceColor / mc;
        vec3 abs_ = (1.0 - ab) / max(s, 0.1) + 0.05;
        result += exp(-abs_ * pl) * uMatSubsurfaceColor * ph * mm;
    }
#endif
    return result * lc * (1.0 - F_avg);
}

vec3 lobe_specular(vec3 N, vec3 V, vec3 L, vec3 H, vec3 F0, vec3 F_avg, vec3 lc,
                   vec3 T, vec3 B,
                   float NdotL, float NdotV, float NdotH, float VdotH,
                   float baseRough) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
    float r = specular_aa_roughness_halfvec(N, V, L, baseRough);
    float m = clamp(uMatMetallic, 0.0, 1.0);

    vec3 F = mix(F_Schlick(F0, VdotH), F_Schlick_F82(F0, uMatF82Tint, VdotH), m);

    #ifdef EFFECT_ANISOTROPIC
    vec3 s = specular_microfacet_aniso(V, L, H, NdotL, NdotV, NdotH, F, F0, F_avg, r, T, B);
#else
    vec3 s = specular_microfacet_iso(NdotL, NdotV, NdotH, F, F0, F_avg, r);
#endif
    return s * uMatSpecularTint * NdotL * lc;
}

vec3 lobe_transmission(vec3 N, vec3 V, vec3 L, vec3 F0, vec3 lc, float NdotL_raw, float NdotV) {
    float ts = clamp(uMatTransmission, 0.0, 1.0);
    float NdotL_trans = max(-NdotL_raw, 0.0);
    if (ts <= 0.001 || NdotL_trans <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 L_trans = -L;
    const float d = 0.02;
    vec3 r = transmission_ggx(N, V, L_trans, NdotL_trans, NdotV, uMatTransmissionRoughness, ts,
                              uMatTransmissionTint, F0, uMatIOR * (1.0 - d));
    vec3 g = transmission_ggx(N, V, L_trans, NdotL_trans, NdotV, uMatTransmissionRoughness, ts,
                              uMatTransmissionTint, F0, uMatIOR);
    vec3 b = transmission_ggx(N, V, L_trans, NdotL_trans, NdotV, uMatTransmissionRoughness, ts,
                              uMatTransmissionTint, F0, uMatIOR * (1.0 + d));
    return vec3(r.r, g.g, b.b) * NdotL_trans * lc;
}

vec3 lobe_clearcoat(vec3 N, vec3 V, vec3 L, vec3 H, vec3 lc,
                    float NdotL, float NdotV, float NdotH, float VdotH,
                    float cs, vec3 cf0) {
    if (cs <= 0.0 || NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
    vec3 cf = F_Schlick(cf0, VdotH);
    float cr = clamp(uMatClearcoatRoughness, 0.0, 1.0);
    vec3 c = clearcoat_disney(NdotL, NdotV, NdotH, cr, cf, cf0);
    return c * lc * uMatClearcoatColor * cs * NdotL;
}

vec3 lobe_sheen(vec3 N, vec3 V, vec3 L, vec3 H, vec3 lc, vec3 T, vec3 B,
                float NdotL, float NdotV, float NdotH, float VdotH) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
    vec3 c;
#ifdef EFFECT_ANISOTROPIC
    c = sheen_charlie_aniso(uMatAlbedo, uMatSheenColor, NdotL, NdotV, NdotH,
                            dot(H, T), dot(H, B), VdotH,
                            uMatSheenRoughness, uMatSheen, uMatAnisotropic);
#else
    c = sheen_charlie(uMatAlbedo, uMatSheenColor, NdotL, NdotV, NdotH,
                      VdotH, uMatSheenRoughness, uMatSheen);
#endif
    return c * lc * NdotL;
}

// =============================================================================
// Layer attenuation
// =============================================================================
void apply_layer_attenuation(inout vec3 dc, inout vec3 sc,
                             float NdotL, float NdotV,
                             float cs, vec3 cf0, vec3 F_avg) {
#ifdef EFFECT_CLEARCOAT
    vec3 cfl = F_Schlick(cf0, NdotL);
    float cd = compute_coat_darkening(cf0, uMatAlbedo, NdotV, uMatSpecularRoughness);
    float dk = mix(1.0, cd, cs);
    vec3 ct = vec3(dk) * (vec3(1.0) - cfl * cs);

    vec3 absorption = compute_clearcoat_absorption(NdotV, uMatClearcoatColor, cs, uMatClearcoatIOR);
    ct *= absorption;

    dc *= ct; sc *= ct;
#endif
#ifdef EFFECT_TRANSMISSION
    dc *= vec3(1.0) - F_avg * clamp(uMatTransmission, 0.0, 1.0);
#endif
#ifdef EFFECT_SHEEN
    float st = sheen_base_transmittance(NdotV);
    dc *= st; sc *= st;
#endif
}

void accumulate_light(vec3 N, vec3 V, vec3 L, vec3 lc,
                      vec3 F0, vec3 F_avg, vec3 T, vec3 B,
                      inout vec3 d, inout vec3 s,
                      inout vec3 tr, inout vec3 cc, inout vec3 sh,
                      inout vec3 ri, inout vec3 bg,
                      float NdotV, float baseRough, vec3 cf0) {
    float NdotL_raw = dot(N, L);
    float NdotL = max(NdotL_raw, 0.0);
    float cs = clamp(uMatClearcoat, 0.0, 1.0);

    vec3 V_sub = V;
    vec3 L_sub = L;
    float NdotL_sub = NdotL;
    float NdotV_sub = NdotV;

#ifdef EFFECT_CLEARCOAT
    if (cs > 0.0) {
        float etaCoat = uMatClearcoatIOR > 0.0 ? uMatClearcoatIOR : 1.5;
        vec3 V_refract = refract(-V, N, 1.0 / etaCoat);
        vec3 L_refract = refract(-L, N, 1.0 / etaCoat);
        if (dot(V_refract, V_refract) > 1e-6) V_sub = -V_refract;
        if (dot(L_refract, L_refract) > 1e-6) L_sub = -L_refract;
        NdotL_sub = max(dot(N, L_sub), 0.0);
        NdotV_sub = max(dot(N, V_sub), 0.0);
    }
#endif

    vec3 Hraw = L_sub + V_sub;
    float ls = dot(Hraw, Hraw);
    vec3 H = (ls > 1e-8) ? Hraw * inversesqrt(ls) : N;
    float VdotH = min(max(dot(V_sub, H), 0.0), 1.0);
    float NdotH = max(dot(N, H), 0.0);

    vec3 Hraw_cc = L + V;
    float ls_cc = dot(Hraw_cc, Hraw_cc);
    vec3 H_cc = (ls_cc > 1e-8) ? Hraw_cc * inversesqrt(ls_cc) : N;
    float VdotH_cc = min(max(dot(V, H_cc), 0.0), 1.0);
    float NdotH_cc = max(dot(N, H_cc), 0.0);

    vec3 dc = lobe_diffuse(N, V_sub, L_sub, lc, F_avg, NdotL_sub, NdotL_raw);
    vec3 sc = lobe_specular(N, V_sub, L_sub, H, F0, F_avg, lc, T, B,
                            NdotL_sub, NdotV_sub, NdotH, VdotH, baseRough);
    vec3 tc = vec3(0.0), ccc = vec3(0.0), shc = vec3(0.0), bgc = vec3(0.0), ric = vec3(0.0);

#ifdef EFFECT_TRANSMISSION
    tc = lobe_transmission(N, V, L, F0, lc, NdotL_raw, NdotV);
#endif
#ifdef EFFECT_CLEARCOAT
    ccc = lobe_clearcoat(N, V, L, H_cc, lc, NdotL, NdotV, NdotH_cc, VdotH_cc, cs, cf0);
#endif
#ifdef EFFECT_SHEEN
    shc = lobe_sheen(N, V, L, H_cc, lc, T, B, NdotL, NdotV, NdotH_cc, VdotH_cc);
#endif
#ifdef EFFECT_DIFFRACTION
    ric += lobe_micro_diffraction(N, V, L, T, uMatDiffraction) * lc;
#endif
#ifdef EFFECT_BACK_GLOW
    bgc = back_glow_lobe(N, L, NdotV) * lc;
#endif
#ifdef EFFECT_RIM
    ric += rim_lobe(NdotV) * lc;
#endif

    apply_layer_attenuation(dc, sc, NdotL, NdotV, cs, cf0, F_avg);
    d += dc; s += sc; tr += tc; cc += ccc; sh += shc; bg += bgc; ri += ric;
}

void accumulate_direct_lighting(vec3 N, vec3 V, vec3 wp,
                                vec3 F0, vec3 F_avg, vec3 T, vec3 B,
                                float NdotV, float baseRough, vec3 cf0,
                                out vec3 td, out vec3 ts, out vec3 tt,
                                out vec3 tcc, out vec3 tsh, out vec3 tri,
                                out vec3 tbg, out vec3 avgD, out float avgW) {
    td = ts = tt = tcc = tsh = tri = tbg = avgD = vec3(0.0);
    avgW = 0.0;
    uint count, base;
    cluster_lookup(count, base);
    for (uint i = 0; i < count; i++) {
        uint li = clusterLights[base + i];
        Light l = lights[li];
        float inten = length(l.color.xyz);
        if (inten < 0.001) continue;
        vec3 ld; float at;
        if (!evaluate_light(l, wp, ld, at)) continue;
        vec3 lc = l.color.xyz * at;
        inten *= at;
        accumulate_light(N, V, ld, lc, F0, F_avg, T, B,
                         td, ts, tt, tcc, tsh, tri, tbg,
                         NdotV, baseRough, cf0);
#ifdef EFFECT_GOOCH
        avgD += ld * inten;
        avgW += inten;
#endif
    }
}

// =============================================================================
// Surface shading
// =============================================================================
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    if (!gl_FrontFacing) N = -N;
    vec3 N_geom = N;
    vec3 V = normalize(uCamEye - worldPos);
    N = perturb_normal(N, worldPos, localPos);

    float baseRough = clamp(uMatSpecularRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    float ambientRough = clamp(specular_aa_roughness(N, baseRough), MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    float NdotV = max(dot(N, V), 0.0);
    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    vec3 F0 = compute_fresnel_f0(uMatAlbedo, metallic, uMatIOR);
    vec3 F_avg = compute_fresnel_avg(F0, uMatF82Tint, metallic);
    vec3 coatF0 = compute_clearcoat_f0();
    vec3 T = vec3(1.0, 0.0, 0.0), B = vec3(0.0, 1.0, 0.0);

    vec3 t_proj = vTangent - N * dot(vTangent, N);
    if (dot(t_proj, t_proj) < 1e-5) {
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
    } else {
        T = normalize(t_proj);
    }
    B = normalize(cross(N, T));

    vec3 td, ts, tt, tcc, tsh, tri, tbg, avgDir;
    float avgW;
    accumulate_direct_lighting(N, V, worldPos, F0, F_avg, T, B,
                               NdotV, baseRough, coatF0,
                               td, ts, tt, tcc, tsh, tri, tbg, avgDir, avgW);
    vec3 diffuseColor = uMatAlbedo * (1.0 - metallic);
    vec3 directDiffuse = td;

    vec3 ambientDiffuse = vec3(0.0);
    vec3 ambientSpec = vec3(0.0);
    vec3 ambientClearcoat = vec3(0.0);
    vec3 ambientSheen = vec3(0.0);

    float vbao_ao = 1.0;
#ifndef WBOIT_PASS
    vbao_ao = texture(uAOTex, gl_FragCoord.xy / uScreenSize).r;
#endif
    float specOcc = specular_occlusion(NdotV, vbao_ao, ambientRough);
    vec2 envBRDF = env_brdf_approx(NdotV, ambientRough);
    vec3 kD_env = vec3(1.0) - F_avg;
    vec3 R = reflect(-V, N);

#ifdef EFFECT_ANISOTROPIC
    vec3 ba = (uMatAnisotropic >= 0.0) ? B : T;
    vec3 pv = V - ba * dot(V, ba);
    float pl = dot(pv, pv);
    if (pl > 1e-6) {
        vec3 bn = pv * inversesqrt(pl);
        R = normalize(mix(R, reflect(-V, bn), abs(uMatAnisotropic)));
    }
#endif

    vec3 irradiance = sample_env_map(N_geom, uEnvCubeMaxMip);
    ambientDiffuse = irradiance * diffuseColor * kD_env
                        * uMatAmbient * uSkyAmbientScale;

    vec3 envSpec = sample_env_map(R, ambientRough);
    vec3 biasTint = mix(vec3(1.0), uMatAlbedo, metallic);
    vec3 F_env = F0 * envBRDF.x + envBRDF.y * biasTint;

    ambientSpec = specular_multiscatter_comp(
        envSpec * F_env * uMatSpecularTint * specOcc * uMatAmbient,
        F0, F_avg, ambientRough, NdotV);

#ifdef EFFECT_CLEARCOAT
    float cc = clamp(uMatClearcoat, 0.0, 1.0);
    if (cc > 0.0) {
        vec3 ca = coatF0 + (1.0 - coatF0) / 21.0;
        float cr = clamp(uMatClearcoatRoughness, 0.0, 1.0);
        vec3 Fc = mix(F_Schlick(coatF0, NdotV), ca, cr);
        vec3 ec = sample_env_map(R, cr);
        vec3 absorption = compute_clearcoat_absorption(NdotV, uMatClearcoatColor, cc, uMatClearcoatIOR);
        float ccSpecOcc = specular_occlusion(NdotV, vbao_ao, cr);
        ambientClearcoat = specular_multiscatter_comp(
            ec * uMatClearcoatColor * Fc * cc * ccSpecOcc * uMatAmbient * absorption,
            coatF0, ca, cr, NdotV);
    }
#endif
#ifdef EFFECT_SHEEN
    const float ST = 0.3;
    float lb = dot(uMatAlbedo, LUMA_REC709);
    vec3 sc = mix(vec3(1.0), uMatAlbedo / max(lb, 1e-4), ST);
    sc *= uMatSheenColor;
    sc = clamp(sc, 0.0, 1.0);
    float Es = sheen_directional_albedo(NdotV, uMatSheenRoughness);
    vec3 sheenF = F_Schlick(vec3(0.04), NdotV);
    vec3 sheenBase = irradiance * sc * Es * uMatSheen * uMatAmbient * sheenF;
    vec3 sheenFms = sc * (1.0 / max(Es, 1e-4) - 1.0);
    ambientSheen = sheenBase * (vec3(1.0) + sheenFms);
#endif
#ifdef EFFECT_GOOCH
    if (avgW > 0.001) {
        vec3 dir = avgDir / avgW;
        float l = length(dir);
        if (l > 0.001) {
            dir /= l;
            float ndl = dot(N, dir);
            float tg = ndl * 0.5 + 0.5;
            vec3 gf = mix(uMatGoochCool, uMatGoochWarm, tg);
            ambientDiffuse *= gf;
        }
    }
#endif
    ambientDiffuse *= vbao_ao;
    ambientClearcoat *= vbao_ao;
    ambientSheen *= vbao_ao;

    vec3 surfaceDiffuse = ambientDiffuse + directDiffuse;
    vec3 surfaceReflection = ambientSpec + ambientClearcoat + ambientSheen
                            + ts + tcc + tsh + tri + tbg;
#ifdef EFFECT_THIN_FILM
    vec3 iridColor = F_ThinFilm_Airy(NdotV, uMatThinFilm, uMatThinFilmIOR, F0);
    surfaceReflection = mix(surfaceReflection, surfaceReflection + iridColor, uMatThinFilm);
#endif
    vec3 surfaceTransmission = tt;

#ifdef EFFECT_TRANSMISSION
    {
        float s = clamp(uMatTransmission, 0.0, 1.0);

        if (s > 0.0) {
            float etaI = gl_FrontFacing ? 1.0 : max(uMatIOR, 1.001);
            float etaT = gl_FrontFacing ? max(uMatIOR, 1.001) : 1.0;
            vec3 Rr = refract(-V, N, etaI / etaT);

            if (dot(Rr, Rr) > 1e-8) {
                vec3 F = mix(F_Schlick(F0, NdotV),
                             F_Schlick_F82(F0, uMatF82Tint, NdotV),
                             metallic);
                float Fa = dot(F, LUMA_REC709);
                float transFraction = s * (1.0 - Fa);

                vec3 transmitted = sample_env_map(Rr, uMatTransmissionRoughness)
                                 * uMatTransmissionTint;

                if (uRefractionScale > 0.0) {
                    vec2 uv  = gl_FragCoord.xy / uScreenSize;
                    vec3 Rr_view = (uView * vec4(Rr, 0.0)).xyz;
                    vec2 duv = Rr_view.xy * uRefractionScale * (1.0 - NdotV);
                    vec3 bg  = texture(uRefractionSrc, uv + duv).rgb;
                    transmitted = bg * uMatTransmissionTint;
                }

                surfaceDiffuse *= (1.0 - transFraction);
                surfaceTransmission += transmitted * transFraction;
            }
        }
    }
#endif

    vec3 colorHDR = surfaceDiffuse + surfaceReflection + surfaceTransmission;

#ifdef EFFECT_EMISSIVE
    vec3 em = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
    em *= 1.0 + uMatEmissivePulseAmplitude
         * sin(uTime * uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
#endif
    colorHDR += em;
#endif

#ifdef EFFECT_STROBE
    colorHDR += uMatStrobeColor * (sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5);
#endif

    colorHDR *= uMatTint;

#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float d = length(worldPos - uCamEye);
        float fd = max(d - uFogStart, 0.0);
        float rg = max(uFogEnd - uFogStart, 1e-4);
        colorHDR = mix(colorHDR, uFogColor, 1.0 - exp(-(3.0 / rg) * fd));
    }
#endif

#ifdef EFFECT_GLITCH
    {
        vec3 q = floor(worldPos * 4096.0 + uTime * 60.0);
        float offset = (hash_float(q) - 0.5) * uMatGlitch;
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

    colorHDR = max(colorHDR, vec3(0.0));

    return colorHDR;
}

float wboit_weight(float eye_depth, float alpha) {
    float z = eye_depth;
    float w = 10.0 / (1e-5 + pow(z / 200.0, 4.0) + pow(z / 200.0, 2.0));
    return alpha * clamp(w, 1e-2, 3e3);
}

#if defined(WBOIT_PASS)
layout(location = 0) out vec4 outAccumulation;
layout(location = 1) out vec4 outRevealage;
#else
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 outNormal;
#endif

void main() {
#ifdef ALPHA_PASS_FRONT
    float tz = texelFetch(uTransmissiveDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (tz < 1.0 && gl_FragCoord.z >= tz) discard;
#endif
    vec3 colorHDR = shade_surface(vNormal, vWorldPos, vLocalPos);
    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = clamp(uMatAlpha, 0.0, 1.0);
#endif
#ifdef WBOIT_PASS
    float w = wboit_weight(vEyeDepth, alpha);
    outAccumulation = vec4(colorHDR * w, w);
    outRevealage = vec4(alpha);
#else
    vec3 Ng = normalize(vNormal);
    if (!gl_FrontFacing) Ng = -Ng;
    outNormal = vec4(normalize((uView * vec4(Ng, 0.0)).xyz) * 0.5 + 0.5, 0.0);
    FragColor = vec4(colorHDR, alpha);
#endif
}

#endif /* DEPTH_ONLY */