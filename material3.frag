#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocalPos;

in vec3 vVertexColor;             // Gouraud colour
flat in vec3 vFlatColor;
flat in vec3 vWorldCentroid;
flat in vec3 vLocalCentroid;
flat in vec3 vWorldFaceNormal;
flat in vec3 vLocalFaceNormal;

#define NUM_BINS 12
in vec3 vBinCol[NUM_BINS];
in float vBinWeight[NUM_BINS];

uniform vec3 uAmbientCol;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform float uGouraudBlend = 0;      // 0 = pure Phong, 1 = pure Gouraud

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

const vec3 AXIS[NUM_BINS] = vec3[](
    vec3( 1.0,  0.0,  0.0),
    vec3(-1.0,  0.0,  0.0),
    vec3( 0.0,  1.0,  0.0),
    vec3( 0.0, -1.0,  0.0),
    vec3( 0.0,  0.0,  1.0),
    vec3( 0.0,  0.0, -1.0),
    normalize(vec3( 1.0,  1.0,  0.0)),
    normalize(vec3(-1.0,  1.0,  0.0)),
    normalize(vec3( 1.0, -1.0,  0.0)),
    normalize(vec3(-1.0, -1.0,  0.0)),
    normalize(vec3( 0.0,  1.0,  1.0)),
    normalize(vec3( 0.0, -1.0,  1.0))
);

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

// ---- Phong shading (per‑pixel) ----
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos,
                   vec3 binCol[NUM_BINS], float binWeight[NUM_BINS])
{
    vec3 V = normalize(uCamEye - worldPos);
    vec3 N_bumped = N;
    apply_bump(N_bumped, worldPos, localPos);
    N_bumped = normalize(N_bumped);

    float NdotV = max(dot(N_bumped, V), 0.0);
    float t, s;
    vec3 q;
    float offset;

    vec3 totalDiffuse = vec3(0.0);
    vec3 totalSpec = vec3(0.0);
    vec3 totalClearcoat = vec3(0.0);
    vec3 totalSheen = vec3(0.0);
    vec3 totalRim = vec3(0.0);
    vec3 totalFresnel = vec3(0.0);
    vec3 totalBackGlow = vec3(0.0);
    vec3 totalGooch = vec3(0.0);

    for (int i = 0; i < NUM_BINS; i++) {
        vec3 L = AXIS[i];
        vec3 lightCol = binCol[i];
        if (length(lightCol) < 1e-5) continue;

        float ndotl_raw = dot(N_bumped, L);
        float ndotl_clamped = max(ndotl_raw, 0.0);

        // ---- Diffuse ----
        float diff = ndotl_clamped;
#ifdef EFFECT_DIFFUSE_WRAP
        t = ndotl_clamped;
        diff = t * t * (3.0 - 2.0 * t);
#endif
#ifdef EFFECT_CEL_SHADING
        float inv = 1.0 / float(uMatCelBands);
        diff = min(1.0, floor(diff * float(uMatCelBands)) * inv);
#endif
#ifdef EFFECT_MINNAERT
        diff = pow(ndotl_clamped, uMatMinnaertK) * pow(NdotV, 1.0 - uMatMinnaertK);
#endif
#ifdef EFFECT_OREN_NAYAR
        float sigma = uMatOrenNayarSigma;
        float sigma_sq = sigma * sigma;
        float A = 1.0 - 0.5 * sigma_sq / (sigma_sq + 0.33);
        float B = 0.45 * sigma_sq / (sigma_sq + 0.09);
        float cos_phi_diff = 0.0, sin_alpha = 0.0, tan_beta = 0.0;
        if (ndotl_clamped > 0.0 && NdotV > 0.0) {
            vec3 Lproj = normalize(L - N_bumped * ndotl_clamped);
            vec3 Vproj = normalize(V - N_bumped * NdotV);
            cos_phi_diff = max(0.0, dot(Lproj, Vproj));
            float sin_l = sqrt(1.0 - ndotl_clamped * ndotl_clamped);
            float sin_v = sqrt(1.0 - NdotV * NdotV);
            if (ndotl_clamped > NdotV) {
                sin_alpha = sin_v;
                tan_beta = sin_l / ndotl_clamped;
            } else {
                sin_alpha = sin_l;
                tan_beta = sin_v / NdotV;
            }
        }
        diff = ndotl_clamped * (A + B * cos_phi_diff * sin_alpha * tan_beta);
        diff = saturate(diff);
#endif
        totalDiffuse += lightCol * diff * uMatColor * uMatAmbientLightFactor;

        // ---- Specular ----
        if (ndotl_clamped > 0.0) {
            vec3 H = normalize(L + V);
            float NdotH = max(dot(N_bumped, H), 0.0);
            float spec = pow(NdotH, uMatSpecularExponent);
#ifdef EFFECT_SPECULAR_THRESH
            spec = (spec > uMatSpecularThreshold) ? 1.0 : 0.0;
#endif
            totalSpec += uMatSpecularColor * spec * lightCol;
        }

        // ---- Other per‑bin effects ----
#ifdef EFFECT_GOOCH
        t = (ndotl_raw + 1.0) * 0.5;
        vec3 gooch = mix(uMatGoochCool, uMatGoochWarm, t);
        totalGooch += lightCol * gooch * uMatColor * uMatAmbientLightFactor;
#endif

#ifdef EFFECT_BACK_GLOW
        float backNdotL = max(dot(N_bumped, -L), 0.0);
        totalBackGlow += uMatBackGlowColor * backNdotL * lightCol;
#endif

#ifdef EFFECT_RIM
        float rim = pow(1.0 - NdotV, uMatRimExponent);
        totalRim += uMatRimColor * rim * lightCol;
#endif

#ifdef EFFECT_FRESNEL
        totalFresnel += uMatFresnelColor * lightCol;
#endif

#ifdef EFFECT_CLEARCOAT
        if (ndotl_clamped > 0.0) {
            vec3 H = normalize(L + V);
            float NdotH = max(dot(N_bumped, H), 0.0);
            float ccSpec = pow(NdotH, uClearcoatExponent);
            totalClearcoat += uClearcoatColor * ccSpec * uClearcoatStrength * lightCol;
        }
#endif

#ifdef EFFECT_SHEEN
        if (ndotl_clamped > 0.0) {
            vec3 H = normalize(L + V);
            float ndoth = dot(N_bumped, H);
            float sheen = pow(saturate(1.0 - ndoth), uSheenExponent);
            totalSheen += uSheenColor * sheen * uSheenStrength * ndotl_clamped * lightCol;
        }
#endif
    }

    // ---- Combine ----
    vec3 color = uAmbientCol * uMatColor * uMatAmbientLightFactor;
#ifdef EFFECT_GOOCH
    color += totalGooch;
#else
    color += totalDiffuse;
#endif
    color += totalSpec + totalClearcoat + totalSheen + totalRim + totalBackGlow;

#ifdef EFFECT_FRESNEL
    float fresnelFactor = pow(1.0 - NdotV, uMatFresnelExponent);
    color = mix(color, totalFresnel, fresnelFactor);
#endif

    // Tone mapping.
    color = color / (color + vec3(1.0));

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

    // ---- Classic iridescence (applied to full colour, as in old shaders) ----
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

#ifdef EFFECT_ROUGHNESS
    q = floor(localPos * 256.0);
    offset = (hash_float(q) - 0.5) * uMatRoughness;
    color += offset * 0.25;
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
    // Phong with Gouraud blend
    vec3 binCol[NUM_BINS];
    float binWeight[NUM_BINS];
    for (int i = 0; i < NUM_BINS; i++) {
        binCol[i] = vBinCol[i];
        binWeight[i] = vBinWeight[i];
    }
    vec3 phongColor = shade_surface(vNormal, vWorldPos, vLocalPos, binCol, binWeight);
    color = mix(phongColor, vVertexColor, uGouraudBlend);
#endif

    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = uMatAlpha;
#endif
    FragColor = vec4(color, alpha);
}