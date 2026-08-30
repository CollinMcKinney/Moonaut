#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;
layout(location = 3) in float aModelIndex;
layout(location = 4) in vec3 aLocalFaceNormal;
layout(location = 5) in vec3 aLocalCentroid;

uniform mat4 uViewProj;
uniform vec3 uLightDir;
uniform vec3 uLightCol;
uniform vec3 uAmbientCol;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

/* ---- Extra directional lights ---- */
#define MAX_EXTRA_DIR_LIGHTS 16
uniform int  uNumDirLights;
uniform vec4 uDirDir[MAX_EXTRA_DIR_LIGHTS];
uniform vec4 uDirCol[MAX_EXTRA_DIR_LIGHTS];

/* ---- Point lights ---- */
#define MAX_EXTRA_POINT_LIGHTS 64
uniform int  uNumPointLights;
uniform vec4 uPointPos[MAX_EXTRA_POINT_LIGHTS];
uniform vec4 uPointCol[MAX_EXTRA_POINT_LIGHTS];
uniform float uPointRange[MAX_EXTRA_POINT_LIGHTS];

/* ---- Spot lights ---- */
#define MAX_EXTRA_SPOT_LIGHTS 64
uniform int  uNumSpotLights;
uniform vec4 uSpotPos[MAX_EXTRA_SPOT_LIGHTS];
uniform vec4 uSpotDir[MAX_EXTRA_SPOT_LIGHTS];
uniform vec4 uSpotCol[MAX_EXTRA_SPOT_LIGHTS];
uniform float uSpotRange[MAX_EXTRA_SPOT_LIGHTS];
uniform float uSpotInnerCos[MAX_EXTRA_SPOT_LIGHTS];
uniform float uSpotOuterCos[MAX_EXTRA_SPOT_LIGHTS];
uniform float uSpotFalloff[MAX_EXTRA_SPOT_LIGHTS];

/* ---- Material UBO ---- */
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

layout(std140, row_major) uniform ModelMatrices {
    mat4 uModels[1024];
};

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vVertexColor;
flat out vec3 vFlatColor;
out vec3 vAccumLightDir;
out vec3 vAccumLightColor;
flat out vec3 vWorldCentroid;
flat out vec3 vLocalCentroid;
flat out vec3 vWorldFaceNormal;
flat out vec3 vLocalFaceNormal;

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

void apply_bump(inout vec3 N, vec3 worldPos, vec3 localPos) {
#ifdef EFFECT_BUMP
    float fx = worldPos.x * uMatBumpFrequency;
    float fy = worldPos.y * uMatBumpFrequency;
    float fz = worldPos.z * uMatBumpFrequency;
    float t = uTime * uMatBumpSpeed;
    vec3 bump = vec3(sin(fy + fz + t), sin(fz + fx + t), sin(fx + fy + t)) * uMatBumpAmplitude;
    N = normalize(N + bump);
#else
    worldPos; localPos;
#endif
}

void accumulate_lights(vec3 N, vec3 worldPos, out vec3 accumLightDir, out vec3 accumLightColor) {
    N = normalize(N);
    vec3 V = normalize(uCamEye - worldPos);

    vec3 dir_sum = vec3(0.0);
    vec3 col_sum = vec3(0.0);

    /* Global directional light */
    {
        vec3 L = normalize(uLightDir);
        float raw_ndotl = max(dot(N, L), 0.0);
        dir_sum += L * raw_ndotl;
        col_sum += uLightCol * raw_ndotl;
    }

    /* Extra directional lights */
    for (int i = 0; i < uNumDirLights; i++) {
        vec3 L = normalize(uDirDir[i].xyz);
        float raw_ndotl = max(dot(N, L), 0.0);
        vec3 col = uDirCol[i].xyz;
        dir_sum += L * raw_ndotl;
        col_sum += col * raw_ndotl;
    }

    /* Point lights */
    for (int i = 0; i < uNumPointLights; i++) {
        vec3 plPos = uPointPos[i].xyz;
        vec3 L = plPos - worldPos;
        float dist = length(L);
        float rng = uPointRange[i];
        if (dist < rng) {
            L = normalize(L);
            float atten = 1.0 - (dist / rng);
            float raw_ndotl = max(dot(N, L), 0.0);
            vec3 col = uPointCol[i].xyz;
            dir_sum += L * raw_ndotl * atten;
            col_sum += col * raw_ndotl * atten;
        }
    }

    /* Spot lights */
    for (int i = 0; i < uNumSpotLights; i++) {
        vec3 slPos = uSpotPos[i].xyz;
        vec3 L = slPos - worldPos;
        float dist = length(L);
        float rng = uSpotRange[i];
        if (dist < rng) {
            L = normalize(L);
            vec3 lightDir = normalize(uSpotDir[i].xyz);
            float cos_angle = dot(-L, lightDir);
            float cos_inner = uSpotInnerCos[i];
            float cos_outer = uSpotOuterCos[i];
            if (cos_angle >= cos_outer) {
                float spot = clamp((cos_angle - cos_outer) / (cos_inner - cos_outer), 0.0, 1.0);
                spot = pow(spot, uSpotFalloff[i]);
                float atten = 1.0 - (dist / rng);
                float raw_ndotl = max(dot(N, L), 0.0);
                vec3 col = uSpotCol[i].xyz;
                dir_sum += L * raw_ndotl * atten * spot;
                col_sum += col * raw_ndotl * atten * spot;
            }
        }
    }

    accumLightDir = dir_sum;
    accumLightColor = col_sum;
}

vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos,
                   vec3 accumLightDir, vec3 accumLightColor) {
    vec3 N_bumped = N;
    apply_bump(N_bumped, worldPos, localPos);
    N_bumped = normalize(N_bumped);

    vec3 V = normalize(uCamEye - worldPos);

    float dir_len = length(accumLightDir);
    vec3 L = (dir_len > 0.001) ? (accumLightDir / dir_len) : normalize(vec3(0.0, 0.0, 1.0));
    vec3 lightColor = accumLightColor;

    float ndotl = max(dot(N_bumped, L), 0.0);
    float ndotv = max(dot(N_bumped, V), 0.0);
    float diff = ndotl;

    // ----- Variables that are used in multiple effects, declared once -----
    vec3 H;
    float spec;
    float t;
    float s;
    vec3 q;
    float offset;
    float a;   // Oren‑Nayar coefficient, can be reused elsewhere if needed
    float b;   // Oren‑Nayar coefficient OR blue channel in Iridescence

#ifdef EFFECT_DIFFUSE_WRAP
    t = ndotl;
    diff = t * t * (3.0 - 2.0 * t);
#endif
#ifdef EFFECT_CEL_SHADING
    float inv = 1.0 / float(uMatCelBands);
    diff = min(1.0, floor(diff * float(uMatCelBands)) * inv);
#endif
#ifdef EFFECT_MINNAERT
    diff = pow(ndotl, uMatMinnaertK) * pow(ndotv, 1.0 - uMatMinnaertK);
#endif
#ifdef EFFECT_OREN_NAYAR
    float sigma = uMatOrenNayarSigma;
    float sigma_sq = sigma * sigma;
    a = 1.0 - 0.5 * sigma_sq / (sigma_sq + 0.33);
    b = 0.45 * sigma_sq / (sigma_sq + 0.09);
    float cos_phi_diff = 0.0, sin_alpha = 0.0, tan_beta = 0.0;
    if (ndotl > 0.0 && ndotv > 0.0) {
        vec3 Lproj = normalize(L - N_bumped * ndotl);
        vec3 Vproj = normalize(V - N_bumped * ndotv);
        cos_phi_diff = max(0.0, dot(Lproj, Vproj));
        float sin_l = sqrt(1.0 - ndotl * ndotl);
        float sin_v = sqrt(1.0 - ndotv * ndotv);
        if (ndotl > ndotv) {
            sin_alpha = sin_v;
            tan_beta = sin_l / ndotl;
        } else {
            sin_alpha = sin_l;
            tan_beta = sin_v / ndotv;
        }
    }
    diff = ndotl * (a + b * cos_phi_diff * sin_alpha * tan_beta);
    diff = saturate(diff);
#endif

    vec3 color = (uAmbientCol + lightColor * diff) * uMatColor * uMatAmbientLightFactor;

    float raw_dotNL = dot(N_bumped, L);
    float ndotl_eff = max(raw_dotNL, 0.0);
    float ndotv_eff = max(dot(N_bumped, V), 0.0);

#ifdef EFFECT_GOOCH
    t = (raw_dotNL + 1.0) * 0.5;
    vec3 gooch = mix(uMatGoochCool, uMatGoochWarm, t);
    color *= gooch;
#endif

#ifdef EFFECT_BACK_GLOW
    float ndotl_neg = dot(N_bumped, -L);
    color += uMatBackGlowColor * max(0.0, ndotl_neg) * lightColor;
#endif

#ifdef EFFECT_RIM
    float rim = pow(1.0 - ndotv_eff, uMatRimExponent);
    color += uMatRimColor * rim * lightColor;
#endif

#ifdef EFFECT_FRESNEL
    float fresnel = pow(1.0 - ndotv_eff, uMatFresnelExponent);
    color = mix(color, uMatFresnelColor * lightColor, fresnel);
#endif

#ifdef EFFECT_EMISSIVE
    vec3 emissive = uMatEmissiveColor;
    #ifdef EFFECT_EMISSIVE_PULSE
    {
        float pulse = 1.0 + uMatEmissivePulseAmplitude * sin(uTime * uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
        emissive *= pulse;
    }
    #endif
    color += emissive;
#endif

#ifdef EFFECT_STROBE
    s = sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5;
    color += uMatStrobeColor * s;
#endif

    // ---- Specular (always present) ----
    H = normalize(L + V);
    spec = pow(max(dot(N_bumped, H), 0.0), uMatSpecularExponent);
#ifdef EFFECT_SPECULAR_THRESH
    spec = (spec > uMatSpecularThreshold) ? 1.0 : 0.0;
#endif
    color += uMatSpecularColor * spec * lightColor;

#ifdef EFFECT_CLEARCOAT
    H = normalize(L + V);
    spec = pow(max(dot(N_bumped, H), 0.0), uClearcoatExponent);
    color += uClearcoatColor * spec * uClearcoatStrength * lightColor;
#endif

#ifdef EFFECT_SHEEN
    H = normalize(L + V);
    float ndoth = dot(N_bumped, H);
    float sheen = pow(saturate(1.0 - ndoth), uSheenExponent);
    color += uSheenColor * sheen * uSheenStrength * ndotl_eff * lightColor;
#endif

#ifdef EFFECT_SATURATION
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, uMatSaturation);
#endif

#ifdef EFFECT_IRIDESCENCE
    float angle = ndotv_eff * 2.0 * 3.14159265;
    s = sin(angle);
    float c = cos(angle);
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
    b = color.r * rot6 + color.g * rot7 + color.b * rot8;   // reuse global 'b'
    float strength = uMatIridescenceStrength;
    color.r = r * strength + color.r * (1.0 - strength);
    color.g = g * strength + color.g * (1.0 - strength);
    color.b = b * strength + color.b * (1.0 - strength);
#endif

#ifdef EFFECT_GLITCH
    q = floor(worldPos * 4096.0 + uTime * 60.0);
    offset = (hash_float(q) - 0.5) * uMatGlitchIntensity;
    color.r += offset;
    color.g += offset * 0.7;
    color.b -= offset;
#endif

#ifdef EFFECT_ROUGHNESS
    q = floor(localPos * 256.0);
    offset = (hash_float(q) - 0.5) * uMatRoughness;
    color += offset * 0.25;
#endif

#ifdef EFFECT_FRINGE
    float fringe = pow(1.0 - ndotv_eff, 3.0) * uMatFringeIntensity;
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
    return clamp(color, 0.0, 1.0);
}

void main() {
    mat4 model = uModels[int(aModelIndex)];
    vec4 worldPos = model * vec4(aPos, 1.0);

    vWorldPos = worldPos.xyz;
    vNormal   = normalize(mat3(model) * aNormal);
    vLocalPos = aLocalPos;

    vLocalFaceNormal = normalize(aLocalFaceNormal);
    vLocalCentroid   = aLocalCentroid;

    vWorldFaceNormal = normalize(mat3(model) * vLocalFaceNormal);
    vWorldCentroid   = (model * vec4(vLocalCentroid, 1.0)).xyz;

    gl_Position = uViewProj * worldPos;

    vec3 N = normalize(vNormal);
    vec3 wp = vWorldPos;
    vec3 lp = vLocalPos;

    vec3 accumDir, accumLightColor;
    accumulate_lights(N, wp, accumDir, accumLightColor);

    vec3 finalColor = shade_surface(N, wp, lp, accumDir, accumLightColor);

    vAccumLightDir   = accumDir;
    vAccumLightColor = accumLightColor;

#ifdef MODE_GOURAUD
    vVertexColor = finalColor;
#else
    vVertexColor = vec3(0.0);
#endif

#ifdef MODE_FLAT
    vec3 cN = vWorldFaceNormal;
    vec3 cWorld = vWorldCentroid;
    vec3 cLocal = vLocalCentroid;
    vec3 cAccumDir, cAccumLightColor;
    accumulate_lights(cN, cWorld, cAccumDir, cAccumLightColor);
    vFlatColor = shade_surface(cN, cWorld, cLocal, cAccumDir, cAccumLightColor);
#else
    vFlatColor = vec3(0.0);
#endif
}