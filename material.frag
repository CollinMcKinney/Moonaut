#version 430 core

#ifdef DEPTH_ONLY
void main() { }
#else

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocalPos;
in vec3 vVertexColor;
flat in vec3 vFlatColor;
flat in vec3 vWorldCentroid;
flat in vec3 vLocalCentroid;
flat in vec3 vWorldFaceNormal;
flat in vec3 vLocalFaceNormal;

uniform vec3 uAmbientCol;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uGouraudBlend;

layout(std140) uniform MaterialUniforms {
    vec3  uMatColor;
    vec3  uMatTint;
    float uMatAlpha;
    vec3  uMatEmissiveColor;
    float uMatEmissivePulseAmplitude;
    float uMatEmissivePulseFrequency;
    float uMatEmissivePulsePhase;
    float uMatSpecularExponent;
    vec3  uMatSpecularColor;
    float uMatSpecularThreshold;
    vec3  uMatRimColor;
    float uMatRimExponent;
    vec3  uMatFresnelColor;
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
    float uClearcoatExponent;
    float uClearcoatStrength;
    vec3  uSheenColor;
    float uSheenExponent;
    float uSheenStrength;
};

// ---- Clustered definitions ----
#define CLUSTER_TILE_SIZE 16
#define CLUSTER_DEPTH_SLICES 24
#define CLUSTER_MAX_LIGHTS_PER 64

struct Light {
    vec4 pos;
    vec4 dir;
    vec4 color;
    float range;
    float inner_cos;
    float outer_cos;
    float falloff;
};

uniform sampler2D uDepthTex;
uniform vec2 uScreenSize;
uniform int uNumLights;
uniform int uNumTilesX;
uniform int uNumTilesY;

layout(std430, binding = 0) buffer LightBuffer { Light lights[]; };
layout(std430, binding = 1) buffer ClusterBuffer { uint clusterLights[]; };
layout(std430, binding = 2) buffer ClusterOffsetBuffer { uint clusterOffsets[]; };

float saturate(float x) { return clamp(x, 0.0, 1.0); }

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

// ---- 3D value noise (smooth) ----
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

void apply_bump(inout vec3 N, vec3 worldPos, vec3 localPos) {
#ifdef EFFECT_BUMP
    float fx = worldPos.x * uMatBumpFrequency;
    float fy = worldPos.y * uMatBumpFrequency;
    float fz = worldPos.z * uMatBumpFrequency;
    float t = uTime * uMatBumpSpeed;
    vec3 bump = vec3(sin(fy + fz + t), sin(fz + fx + t), sin(fx + fy + t)) * uMatBumpAmplitude;
    N = normalize(N + bump);
#endif

#ifdef EFFECT_ROUGHNESS
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 p = localPos * 32.0;
    float eps = 0.01;
    float h0 = value_noise(p);
    float hx = value_noise(p + vec3(eps, 0.0, 0.0));
    float hy = value_noise(p + vec3(0.0, eps, 0.0));
    float grad_u = (hx - h0) / eps;
    float grad_v = (hy - h0) / eps;

    float strength = uMatRoughness * 1.0; // adjust as needed
    N += tangent * grad_u * strength + bitangent * grad_v * strength;
    N = normalize(N);
#endif
}

// ---- GGX distribution ----
float GGX_D(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float denom = NdotH * NdotH * (alpha - 1.0) + 1.0;
    return alpha / (3.14159265 * denom * denom);
}

// ---- Anisotropic GGX ----
float GGX_D_anisotropic(float NdotH, float HdotT, float HdotB, float roughness_t, float roughness_b) {
    float alpha_t = roughness_t * roughness_t;
    float alpha_b = roughness_b * roughness_b;
    float denom = NdotH * NdotH * (alpha_t * HdotT * HdotT + alpha_b * HdotB * HdotB) + 1.0;
    return 1.0 / (3.14159265 * alpha_t * alpha_b * denom * denom);
}

// ---- Smith G ----
float Smith_G_GGX(float NdotL, float NdotV, float roughness) {
    float alpha = roughness * roughness;
    float G1L = 2.0 * NdotL / (NdotL + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotL * NdotL));
    float G1V = 2.0 * NdotV / (NdotV + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotV * NdotV));
    return G1L * G1V;
}

// ---- Disney diffuse ----
#ifdef EFFECT_DISNEY_DIFFUSE
vec3 disney_diffuse(vec3 N, vec3 V, vec3 L, vec3 lightCol, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float LdotV = max(dot(L, V), 0.0);

    float FD90 = 0.5 + 2.0 * roughness * LdotV * LdotV;
    float diff = (1.0 + (FD90 - 1.0) * pow(1.0 - NdotL, 5.0)) *
                 (1.0 + (FD90 - 1.0) * pow(1.0 - NdotV, 5.0));
    return lightCol * diff * uMatColor * uMatAmbientLightFactor;
}
#endif

// ------------------------------------------------------------------
// Per‑light accumulation (Fresnel corrected)
// ------------------------------------------------------------------
void accumulate_light(vec3 N, vec3 V, vec3 L, vec3 lightCol,
                      inout vec3 diffuse, inout vec3 specular,
                      inout vec3 clearcoat, inout vec3 sheen,
                      inout vec3 rim, inout vec3 backGlow,
                      inout vec3 subsurface,
                      float ndotv)
{
    float ndotl_raw = dot(N, L);
    float ndotl = max(ndotl_raw, 0.0);
    float NdotL = ndotl;
    float NdotV = ndotv;

    // ---- Metallic (placeholder) ----
#ifdef EFFECT_METALLIC
    float metallic = uMatAmbientLightFactor;
    vec3 baseColor = mix(uMatColor, uMatSpecularColor, metallic);
    float diffScale = 1.0 - metallic;
#else
    vec3 baseColor = uMatColor;
    float diffScale = 1.0;
#endif

    float roughness = clamp(uMatRoughness, 0.001, 1.0);

    // ---- Diffuse ----
    vec3 diffuseContrib;

#ifdef EFFECT_DISNEY_DIFFUSE
    diffuseContrib = disney_diffuse(N, V, L, lightCol, roughness);
    diffuseContrib *= diffScale;
#else
    float diff = NdotL;
#ifdef EFFECT_DIFFUSE_WRAP
    float t = NdotL;
    diff = t * t * (3.0 - 2.0 * t);
#endif
#ifdef EFFECT_CEL_SHADING
    float inv = 1.0 / float(uMatCelBands);
    diff = min(1.0, floor(diff * float(uMatCelBands)) * inv);
#endif
#ifdef EFFECT_MINNAERT
    diff = pow(NdotL, uMatMinnaertK) * pow(NdotV, 1.0 - uMatMinnaertK);
#endif
#ifdef EFFECT_OREN_NAYAR
    float sigma = uMatOrenNayarSigma;
    float sigma2 = sigma * sigma;
    float A = 1.0 - 0.5 * sigma2 / (sigma2 + 0.33);
    float B = 0.45 * sigma2 / (sigma2 + 0.09);
    float cos_phi_diff = 0.0;
    float sin_alpha = 0.0, tan_beta = 0.0;
    if (NdotL > 0.0 && NdotV > 0.0) {
        vec3 L_proj = normalize(L - N * NdotL);
        vec3 V_proj = normalize(V - N * NdotV);
        cos_phi_diff = max(0.0, dot(L_proj, V_proj));
        float sin_l = sqrt(1.0 - NdotL * NdotL);
        float sin_v = sqrt(1.0 - NdotV * NdotV);
        if (NdotL > NdotV) {
            sin_alpha = sin_v;
            tan_beta = sin_l / NdotL;
        } else {
            sin_alpha = sin_l;
            tan_beta = sin_v / NdotV;
        }
    }
    diff = NdotL * (A + B * cos_phi_diff * sin_alpha * tan_beta);
    diff = max(diff, 0.0);
#endif
    diffuseContrib = lightCol * diff * baseColor * uMatAmbientLightFactor * diffScale;
#endif // EFFECT_DISNEY_DIFFUSE

    // ---- Subsurface ----
#ifdef EFFECT_SUBSURFACE
    vec3 subsurfaceColor = uMatEmissiveColor;   // temporary
    float subsurfaceScale = uMatAmbientLightFactor;
    float thickness = 0.5 + 0.5 * sin(localPos.x * 3.0 + localPos.y * 5.0 + localPos.z * 7.0);
    float wrap = max(0.0, (ndotl_raw + 0.5) / 1.5);
    float sss = (1.0 - wrap) * thickness * subsurfaceScale;
    subsurface += lightCol * subsurfaceColor * sss * uMatColor * uMatAmbientLightFactor;
#endif

    // ---- Specular ----
    if (NdotL > 0.0) {
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);

#ifdef EFFECT_ANISOTROPIC
        float anisotropy = clamp(uMatRoughness, 0.0, 1.0);
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(up, N));
        vec3 bitangent = cross(N, tangent);
        float HdotT = dot(H, tangent);
        float HdotB = dot(H, bitangent);
        float anisotropyStrength = anisotropy * 0.9;
        float roughness_t = roughness / sqrt(1.0 - anisotropyStrength);
        float roughness_b = roughness * sqrt(1.0 - anisotropyStrength);
        roughness_t = clamp(roughness_t, 0.001, 1.0);
        roughness_b = clamp(roughness_b, 0.001, 1.0);
        float D = GGX_D_anisotropic(NdotH, HdotT, HdotB, roughness_t, roughness_b);
#else
        float D = GGX_D(NdotH, roughness);
#endif

        // ---- Fresnel (corrected) ----
#ifdef EFFECT_FRESNEL
        float cosTheta = max(dot(V, H), 0.0);
        cosTheta = min(cosTheta, 1.0);
        vec3 F0;
#ifdef EFFECT_METALLIC
        F0 = uMatSpecularColor;
#else
        F0 = uMatFresnelColor;
        if (length(F0) < 0.001) F0 = vec3(0.04);
#endif
        float exp = uMatFresnelExponent;
        vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, exp);
        float fresnelLuma = max(max(fresnel.r, fresnel.g), fresnel.b);
        diffuseContrib *= (1.0 - fresnelLuma);   // energy conservation
#else
        vec3 fresnel = uMatSpecularColor;
#endif

        float G = Smith_G_GGX(NdotL, NdotV, roughness);
        vec3 specContrib = D * G * max(fresnel, vec3(0.0)) / (4.0 * max(NdotV, 0.001));
        specular += specContrib * lightCol;
    }

    // ---- Clearcoat (Fresnel corrected) ----
#ifdef EFFECT_CLEARCOAT
    if (NdotL > 0.0) {
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float ccRoughness = sqrt(2.0 / (uClearcoatExponent + 2.0));
        ccRoughness = clamp(ccRoughness, 0.001, 1.0);
        float ccD = GGX_D(NdotH, ccRoughness);
        float ccG = Smith_G_GGX(NdotL, NdotV, ccRoughness);

#ifdef EFFECT_FRESNEL
        float cosThetaCC = max(dot(V, H), 0.0);
        cosThetaCC = min(cosThetaCC, 1.0);
        vec3 F0_cc;
#ifdef EFFECT_METALLIC
        F0_cc = uMatSpecularColor;
#else
        F0_cc = uMatFresnelColor;
        if (length(F0_cc) < 0.001) F0_cc = vec3(0.04);
#endif
        float exp = uMatFresnelExponent;
        vec3 ccFresnel = F0_cc + (1.0 - F0_cc) * pow(1.0 - cosThetaCC, exp);
#else
        vec3 ccFresnel = uClearcoatColor;
#endif

        vec3 ccContrib = ccD * ccG * max(ccFresnel, vec3(0.0)) / (4.0 * max(NdotV, 0.001));
        clearcoat += ccContrib * lightCol * uClearcoatStrength;
    }
#endif

    // ---- Sheen ----
#ifdef EFFECT_SHEEN
    if (NdotL > 0.0) {
        vec3 H = normalize(L + V);
        float ndoth = dot(N, H);
        float sheenVal = pow(saturate(1.0 - ndoth), uSheenExponent);
        sheen += uSheenColor * sheenVal * uSheenStrength * NdotL * lightCol;
    }
#endif

    // ---- Rim, Back‑Glow ----
#ifdef EFFECT_BACK_GLOW
    float backNdotL = max(dot(N, -L), 0.0);
    backGlow += uMatBackGlowColor * backNdotL * lightCol;
#endif

#ifdef EFFECT_RIM
    float rimf = pow(1.0 - ndotv, uMatRimExponent);
    rim += uMatRimColor * rimf * lightCol;
#endif

    diffuse += diffuseContrib;
}

// ------------------------------------------------------------------
// Main shading
// ------------------------------------------------------------------
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    vec3 V = normalize(uCamEye - worldPos);
    vec3 N_bumped = N;
    apply_bump(N_bumped, worldPos, localPos);
    N_bumped = normalize(N_bumped);

    float NdotV = max(dot(N_bumped, V), 0.0);
    float t, s;
    vec3 q;
    float offset;

    float cavity = 1.0;
#ifdef EFFECT_ROUGHNESS
    vec3 p = localPos * 32.0;
    float noise = value_noise(p);
    float cavityStrength = uMatRoughness * 0.5;
    cavity = 1.0 - cavityStrength * (0.5 + 0.5 * noise);
    cavity = max(cavity, 0.3);
#endif

    vec3 totalDiffuse   = vec3(0.0);
    vec3 totalSpec      = vec3(0.0);
    vec3 totalClearcoat = vec3(0.0);
    vec3 totalSheen     = vec3(0.0);
    vec3 totalRim       = vec3(0.0);
    vec3 totalBackGlow  = vec3(0.0);
    vec3 totalSubsurface = vec3(0.0);

    vec3 weightedDir = vec3(0.0);
    float totalWeight = 0.0;

    // ---- Clustered light loop ----
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 tile = pixel / CLUSTER_TILE_SIZE;
    float depth = gl_FragCoord.z;
    float near = 0.05;
    float far = 1000.0;
    float logDepth = log2(depth) * 1.0 / log2(far/near);
    int slice = int(floor(logDepth * CLUSTER_DEPTH_SLICES));
    slice = clamp(slice, 0, CLUSTER_DEPTH_SLICES - 1);

    uint clusterIndex = uint(tile.y * uNumTilesX + tile.x);
    uint offsetIdx = clusterIndex * CLUSTER_DEPTH_SLICES + uint(slice);
    uint count = clusterOffsets[offsetIdx];
    uint base = offsetIdx * CLUSTER_MAX_LIGHTS_PER;

    for (uint i = 0; i < count; i++) {
        uint lightIdx = clusterLights[base + i];
        Light L = lights[lightIdx];
        vec3 lightPos = L.pos.xyz;
        int lightType = int(L.pos.w);
        vec3 lightDir;
        vec3 lightCol = L.color.xyz;
        float range = L.range;

        float intensity = length(lightCol);
        if (intensity < 0.001) continue;

        if (lightType == 0) { // directional
            lightDir = normalize(L.dir.xyz);
        } else if (lightType == 1) { // point
            vec3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            if (dist > range) continue;
            float atten = 1.0 - (dist / range);
            atten = atten * atten;
            lightDir = normalize(toLight);
            lightCol *= atten;
            intensity *= atten;
        } else if (lightType == 2) { // spot
            vec3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            if (dist > range) continue;
            float atten = 1.0 - (dist / range);
            atten = atten * atten;
            lightDir = normalize(toLight);
            float cosAngle = dot(-lightDir, normalize(L.dir.xyz));
            float inner = L.inner_cos;
            float outer = L.outer_cos;
            if (cosAngle < outer) continue;
            float spot = clamp((cosAngle - outer) / (inner - outer), 0.0, 1.0);
            spot = pow(spot, L.falloff);
            lightCol *= atten * spot;
            intensity *= atten * spot;
        } else {
            continue;
        }

        accumulate_light(N_bumped, V, lightDir, lightCol,
                         totalDiffuse, totalSpec, totalClearcoat, totalSheen,
                         totalRim, totalBackGlow, totalSubsurface,
                         NdotV);

        weightedDir += lightDir * intensity;
        totalWeight += intensity;
    }

    // ---- Combine ----
    vec3 baseColor = uAmbientCol * uMatColor * uMatAmbientLightFactor + totalDiffuse;
    baseColor *= cavity;

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

    vec3 color = baseColor;
    color += totalSpec + totalClearcoat + totalSheen + totalRim + totalBackGlow;
#ifdef EFFECT_SUBSURFACE
    color += totalSubsurface;
#endif

    color = max(color, vec3(0.0));

    // ---- Tone mapping ----
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);

    // ---- Post‑effects ----
#ifdef EFFECT_EMISSIVE
    vec3 emissive = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
    float pulse = 1.0 + uMatEmissivePulseAmplitude * sin(uTime * uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
    emissive *= pulse;
#endif
    color += emissive;
#endif

#ifdef EFFECT_STROBE
    s = sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5;
    color += uMatStrobeColor * s;
#endif

#ifdef EFFECT_SATURATION
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, uMatSaturation);
#endif

#ifdef EFFECT_IRIDESCENCE
    {
        float angle = NdotV * 2.0 * 3.14159265;
        float c = cos(angle);
        s = sin(angle);
        float rot0 = 0.299 + 0.701*c + 0.168*s;
        float rot1 = 0.587 - 0.587*c + 0.330*s;
        float rot2 = 0.114 - 0.114*c - 0.497*s;
        float rot3 = 0.299 - 0.299*c - 0.328*s;
        float rot4 = 0.587 + 0.413*c + 0.035*s;
        float rot5 = 0.114 - 0.114*c + 0.292*s;
        float rot6 = 0.299 - 0.300*c + 1.250*s;
        float rot7 = 0.587 - 0.588*c - 1.050*s;
        float rot8 = 0.114 + 0.886*c - 0.203*s;
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
    q = floor(worldPos * 4096.0 + uTime * 60.0);
    offset = (hash_float(q) - 0.5) * uMatGlitchIntensity;
    color.r += offset;
    color.g += offset * 0.7;
    color.b -= offset;
#endif

#ifdef EFFECT_FRINGE
    float fringe = pow(1.0 - NdotV, 3.0) * uMatFringeIntensity;
    color.r += fringe;
    color.b -= fringe;
#endif

#ifdef EFFECT_POSTERIZE
    float levels = float(uMatPosterizeLevels);
    color = floor(color * levels + 0.5) / levels;
#endif

#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float dist = length(worldPos - uCamEye);
        t = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
        color = mix(color, uFogColor, t);
    }
#endif

    color *= uMatTint;

    return color;
}

// ------------------------------------------------------------------
out vec4 FragColor;

void main() {
    vec3 color;

#ifdef MODE_WIREFRAME
    color = vFlatColor;
#elif defined(MODE_FLAT)
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
    FragColor = vec4(color, alpha);
}

#endif /* DEPTH_ONLY */