#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;

uniform mat4 uViewProj;

/* Lighting uniforms */
uniform vec3 uLightDir;
uniform vec3 uLightCol;
uniform vec3 uAmbientCol;
uniform vec3 uCamEye;
uniform float uTime;
uniform int   uMatMode;
uniform int   uMatEffects;

uniform vec3  uMatColor;
uniform vec3  uMatTint;
uniform float uMatAlpha;
uniform vec3  uMatEmissiveColor;
uniform float uMatEmissivePulseAmplitude;
uniform float uMatEmissivePulseFrequency;
uniform float uMatEmissivePulsePhase;
uniform float uMatSpecularExponent;
uniform vec3  uMatSpecularColor;
uniform float uMatSpecularThreshold;
uniform vec3  uMatRimColor;
uniform float uMatRimExponent;
uniform vec3  uMatFresnelColor;
uniform float uMatFresnelExponent;
uniform vec3  uMatGoochCool;
uniform vec3  uMatGoochWarm;
uniform float uMatAmbientLightFactor;
uniform float uMatOrenNayarSigma;
uniform float uMatMinnaertK;
uniform float uMatSaturation;
uniform float uMatIridescenceStrength;
uniform vec3  uMatBackGlowColor;
uniform float uMatBumpAmplitude;
uniform float uMatBumpFrequency;
uniform float uMatBumpSpeed;
uniform float uMatRoughness;
uniform float uMatFringeIntensity;
uniform int   uMatCelBands;
uniform float uMatGlitchIntensity;
uniform int   uMatPosterizeLevels;
uniform vec3  uMatStrobeColor;
uniform float uMatStrobeFrequency;
uniform float uMatStrobePhase;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

#define EFFECT_BUMP                (1<<0)
#define EFFECT_DIFFUSE_WRAP        (1<<1)
#define EFFECT_CEL_SHADING         (1<<2)
#define EFFECT_MINNAERT            (1<<3)
#define EFFECT_OREN_NAYAR          (1<<4)
#define EFFECT_AMBIENT_LIGHT       (1<<5)
#define EFFECT_GOOCH               (1<<6)
#define EFFECT_BACK_GLOW           (1<<7)
#define EFFECT_RIM                 (1<<8)
#define EFFECT_FRESNEL             (1<<9)
#define EFFECT_EMISSIVE            (1<<10)
#define EFFECT_EMISSIVE_PULSE      (1<<11)
#define EFFECT_STROBE              (1<<12)
#define EFFECT_SPECULAR            (1<<13)
#define EFFECT_SPECULAR_THRESH     (1<<14)
#define EFFECT_SATURATION          (1<<15)
#define EFFECT_IRIDESCENCE         (1<<16)
#define EFFECT_GLITCH              (1<<17)
#define EFFECT_ROUGHNESS           (1<<18)
#define EFFECT_FRINGE              (1<<19)
#define EFFECT_POSTERIZE           (1<<20)
#define EFFECT_FOG                 (1<<21)
#define EFFECT_ALPHA               (1<<22)

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

/* ---- Full lighting function (identical to original geometry shader) ---- */
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    N = normalize(N);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamEye - worldPos);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V));

    if ((uMatEffects & EFFECT_BUMP) != 0) {
        float fx = worldPos.x * uMatBumpFrequency;
        float fy = worldPos.y * uMatBumpFrequency;
        float fz = worldPos.z * uMatBumpFrequency;
        float t = uTime * uMatBumpSpeed;
        vec3 bump = vec3(sin(fy+fz+t), sin(fz+fx+t), sin(fx+fy+t)) * uMatBumpAmplitude;
        N = normalize(N + bump);
        ndotl = saturate(dot(N, L));
        ndotv = saturate(dot(N, V));
    }

    float diffuse_term = ndotl;

    if ((uMatEffects & EFFECT_DIFFUSE_WRAP) != 0) {
        float t = ndotl;
        diffuse_term = t*t*(3.0-2.0*t);
    }
    if ((uMatEffects & EFFECT_CEL_SHADING) != 0) {
        float inv = 1.0 / float(uMatCelBands);
        diffuse_term = min(1.0, floor(diffuse_term * float(uMatCelBands)) * inv);
    }
    if ((uMatEffects & EFFECT_MINNAERT) != 0) {
        diffuse_term = pow(ndotl, uMatMinnaertK) * pow(ndotv, 1.0-uMatMinnaertK);
    }
    if ((uMatEffects & EFFECT_OREN_NAYAR) != 0) {
        float sigma = uMatOrenNayarSigma;
        float sigma_sq = sigma*sigma;
        float a = 1.0 - 0.5*sigma_sq/(sigma_sq+0.33);
        float b = 0.45*sigma_sq/(sigma_sq+0.09);
        float cos_phi_diff=0.0, sin_alpha=0.0, tan_beta=0.0;
        if (ndotl>0.0 && ndotv>0.0) {
            vec3 Lproj = normalize(L - N*ndotl);
            vec3 Vproj = normalize(V - N*ndotv);
            cos_phi_diff = max(0.0, dot(Lproj, Vproj));
            float sin_l = sqrt(1.0-ndotl*ndotl);
            float sin_v = sqrt(1.0-ndotv*ndotv);
            if (ndotl > ndotv) { sin_alpha = sin_v; tan_beta = sin_l/ndotl; }
            else               { sin_alpha = sin_l; tan_beta = sin_v/ndotv; }
        }
        diffuse_term = ndotl * (a + b*cos_phi_diff*sin_alpha*tan_beta);
        diffuse_term = saturate(diffuse_term);
    }

    vec3 color = (uAmbientCol + uLightCol * diffuse_term) * uMatAmbientLightFactor;
    color = color * uMatColor;

    if ((uMatEffects & EFFECT_GOOCH) != 0) {
        float t = (ndotl+1.0)*0.5;
        vec3 gooch = mix(uMatGoochCool, uMatGoochWarm, t);
        color = color * gooch;
    }

    if ((uMatEffects & EFFECT_BACK_GLOW) != 0) {
        float ndotl_neg = dot(N, -L);
        color += uMatBackGlowColor * max(0.0, ndotl_neg);
    }

    if ((uMatEffects & EFFECT_RIM) != 0) {
        float rim = pow(1.0-ndotv, uMatRimExponent);
        color += uMatRimColor * rim;
    }

    if ((uMatEffects & EFFECT_FRESNEL) != 0) {
        float fresnel = pow(1.0-ndotv, uMatFresnelExponent);
        color = mix(color, uMatFresnelColor, fresnel);
    }

    if ((uMatEffects & EFFECT_EMISSIVE) != 0) {
        vec3 emissive = uMatEmissiveColor;
        if ((uMatEffects & EFFECT_EMISSIVE_PULSE) != 0) {
            float pulse = 1.0 + uMatEmissivePulseAmplitude * sin(uTime*uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
            emissive *= pulse;
        }
        color += emissive;
    }

    if ((uMatEffects & EFFECT_STROBE) != 0) {
        float s = sin(uTime*uMatStrobeFrequency + uMatStrobePhase)*0.5 + 0.5;
        color += uMatStrobeColor * s;
    }

    if ((uMatEffects & EFFECT_SPECULAR) != 0) {
        vec3 H = normalize(L+V);
        float nh = max(0.0, dot(N,H));
        float spec = pow(nh, uMatSpecularExponent);
        if ((uMatEffects & EFFECT_SPECULAR_THRESH) != 0) {
            spec = (spec > uMatSpecularThreshold) ? 1.0 : 0.0;
        }
        color += uMatSpecularColor * spec;
    }

    if ((uMatEffects & EFFECT_SATURATION) != 0) {
        float luma = dot(color, vec3(0.299,0.587,0.114));
        color = mix(vec3(luma), color, uMatSaturation);
    }

    if ((uMatEffects & EFFECT_IRIDESCENCE) != 0) {
        float angle = ndotv * 2.0 * 3.14159265;
        float c = cos(angle);
        float s = sin(angle);
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

    if ((uMatEffects & EFFECT_GLITCH) != 0) {
        vec3 q = floor(worldPos * 4096.0);
        float offset = (hash_float(q)-0.5) * uMatGlitchIntensity;
        color.r += offset;
        color.g += offset*0.7;
        color.b -= offset;
    }

    if ((uMatEffects & EFFECT_ROUGHNESS) != 0) {
        vec3 q = floor(localPos * 256.0);
        float offset = (hash_float(q)-0.5) * uMatRoughness;
        color += offset * 0.25;
    }

    if ((uMatEffects & EFFECT_FRINGE) != 0) {
        float fringe = pow(1.0-ndotv, 3.0) * uMatFringeIntensity;
        color.r += fringe;
        color.b -= fringe;
    }

    if ((uMatEffects & EFFECT_POSTERIZE) != 0) {
        float levels = float(uMatPosterizeLevels);
        color = floor(color*levels + 0.5) / levels;
    }

    if ((uMatEffects & EFFECT_FOG) != 0 && uFogEnd > uFogStart) {
        float dist = length(worldPos - uCamEye);
        float t = clamp((dist-uFogStart)/(uFogEnd-uFogStart), 0.0, 1.0);
        color = mix(color, uFogColor, t);
    }

    color *= uMatTint;
    return clamp(color, 0.0, 1.0);
}

/* ---- Outputs ---- */
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vVertexColor;   /* for Gouraud shading */

void main() {
    vWorldPos = aPos;
    vNormal = aNormal;
    vLocalPos = aLocalPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);

    /* Pre‑compute Gouraud colour for all vertices (used if uMatMode == SHADE_GOURAUD) */
    vVertexColor = shade_surface(normalize(aNormal), aPos, aLocalPos);
}