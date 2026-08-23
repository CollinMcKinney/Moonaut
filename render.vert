#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aLocalPos;
layout(location = 3) in float aModelIndex;
layout(location = 4) in vec3 aLocalFaceNormal;
layout(location = 5) in vec3 aLocalCentroid;

/* ---- Standard uniforms (non‑UBO) ---- */
uniform mat4 uViewProj;
uniform vec3 uLightDir;
uniform vec3 uLightCol;
uniform vec3 uAmbientCol;
uniform vec3 uCamEye;
uniform float uTime;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

/* ---- Material uniform buffer (std140) ---- */
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

    /* Clearcoat */
    vec3  uClearcoatColor;
    float uClearcoatExponent;
    float uClearcoatStrength;

    /* Sheen */
    vec3  uSheenColor;
    float uSheenExponent;
    float uSheenStrength;
};

/* ---- Model matrices UBO (up to 1024 models) ---- */
layout(std140, row_major) uniform ModelMatrices {
    mat4 uModels[1024];
};

/* ---- Outputs to fragment shader ---- */
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vVertexColor;          /* for Gouraud only */
flat out vec3 vLocalFaceNormal;
flat out vec3 vLocalCentroid;
flat out vec3 vWorldFaceNormal;
flat out vec3 vWorldCentroid;

/* ---- Utility functions ---- */
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

/* ---- Lighting function (identical to fragment) ---- */
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    N = normalize(N);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamEye - worldPos);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V));

#ifdef EFFECT_BUMP
    {
        float fx = worldPos.x * uMatBumpFrequency;
        float fy = worldPos.y * uMatBumpFrequency;
        float fz = worldPos.z * uMatBumpFrequency;
        float t = uTime * uMatBumpSpeed;
        vec3 bump = vec3(sin(fy+fz+t), sin(fz+fx+t), sin(fx+fy+t)) * uMatBumpAmplitude;
        N = normalize(N + bump);
        ndotl = saturate(dot(N, L));
        ndotv = saturate(dot(N, V));
    }
#endif

    float diffuse_term = ndotl;

#ifdef EFFECT_DIFFUSE_WRAP
    {
        float t = ndotl;
        diffuse_term = t*t*(3.0-2.0*t);
    }
#endif
#ifdef EFFECT_CEL_SHADING
    {
        float inv = 1.0 / float(uMatCelBands);
        diffuse_term = min(1.0, floor(diffuse_term * float(uMatCelBands)) * inv);
    }
#endif
#ifdef EFFECT_MINNAERT
    {
        diffuse_term = pow(ndotl, uMatMinnaertK) * pow(ndotv, 1.0-uMatMinnaertK);
    }
#endif
#ifdef EFFECT_OREN_NAYAR
    {
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
#endif

    vec3 color = (uAmbientCol + uLightCol * diffuse_term) * uMatAmbientLightFactor;

#ifndef EFFECT_GOOCH
    color = color * uMatColor;
#endif

#ifdef EFFECT_GOOCH
    {
        float t = (ndotl+1.0)*0.5;
        vec3 gooch = mix(uMatGoochCool, uMatGoochWarm, t);
        color = color * gooch;
    }
#endif

#ifdef EFFECT_BACK_GLOW
    {
        float ndotl_neg = dot(N, -L);
        color += uMatBackGlowColor * max(0.0, ndotl_neg);
    }
#endif

#ifdef EFFECT_RIM
    {
        float rim = pow(1.0-ndotv, uMatRimExponent);
        color += uMatRimColor * rim;
    }
#endif

#ifdef EFFECT_FRESNEL
    {
        float fresnel = pow(1.0-ndotv, uMatFresnelExponent);
        color = mix(color, uMatFresnelColor, fresnel);
    }
#endif

#ifdef EFFECT_EMISSIVE
    {
        vec3 emissive = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
        {
            float pulse = 1.0 + uMatEmissivePulseAmplitude * sin(uTime*uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
            emissive *= pulse;
        }
#endif
        color += emissive;
    }
#endif

#ifdef EFFECT_STROBE
    {
        float s = sin(uTime*uMatStrobeFrequency + uMatStrobePhase)*0.5 + 0.5;
        color += uMatStrobeColor * s;
    }
#endif

#ifdef EFFECT_SPECULAR
    {
        vec3 H = normalize(L+V);
        float nh = max(0.0, dot(N,H));
        float spec = pow(nh, uMatSpecularExponent);
#ifdef EFFECT_SPECULAR_THRESH
        {
            spec = (spec > uMatSpecularThreshold) ? 1.0 : 0.0;
        }
#endif
        color += uMatSpecularColor * spec;
    }
#endif

/* ---- Clearcoat ---- */
#ifdef EFFECT_CLEARCOAT
    {
        vec3 H = normalize(L + V);
        float nh = max(dot(N, H), 0.0);
        float spec = pow(nh, uClearcoatExponent);
        color += uClearcoatColor * spec * uClearcoatStrength;
    }
#endif

/* ---- Sheen ---- */
#ifdef EFFECT_SHEEN
    {
        vec3 H = normalize(L + V);
        float ndoth = dot(N, H);
        /* Use the existing ndotl (already computed) - do NOT redeclare it */
        float sheen = pow(saturate(1.0 - ndoth), uSheenExponent);
        color += uSheenColor * sheen * uSheenStrength * ndotl;
    }
#endif

#ifdef EFFECT_SATURATION
    {
        float luma = dot(color, vec3(0.299,0.587,0.114));
        color = mix(vec3(luma), color, uMatSaturation);
    }
#endif

#ifdef EFFECT_IRIDESCENCE
    {
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
#endif

#ifdef EFFECT_GLITCH
    {
        vec3 q = floor(worldPos * 4096.0 + uTime * 60.0);
        float offset = (hash_float(q)-0.5) * uMatGlitchIntensity;
        color.r += offset;
        color.g += offset*0.7;
        color.b -= offset;
    }
#endif

#ifdef EFFECT_ROUGHNESS
    {
        vec3 q = floor(localPos * 256.0);
        float offset = (hash_float(q)-0.5) * uMatRoughness;
        color += offset * 0.25;
    }
#endif

#ifdef EFFECT_FRINGE
    {
        float fringe = pow(1.0-ndotv, 3.0) * uMatFringeIntensity;
        color.r += fringe;
        color.b -= fringe;
    }
#endif

#ifdef EFFECT_POSTERIZE
    {
        float levels = float(uMatPosterizeLevels);
        color = floor(color*levels + 0.5) / levels;
    }
#endif

#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float dist = length(worldPos - uCamEye);
        float t = clamp((dist-uFogStart)/(uFogEnd-uFogStart), 0.0, 1.0);
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

    /* Compute world-space face normal and centroid from local data */
    vWorldFaceNormal = normalize(mat3(model) * vLocalFaceNormal);
    vWorldCentroid   = (model * vec4(vLocalCentroid, 1.0)).xyz;

    gl_Position = uViewProj * worldPos;

#ifdef MODE_GOURAUD
    vVertexColor = shade_surface(normalize(vNormal), vWorldPos, vLocalPos);
#else
    vVertexColor = vec3(0.0);
#endif
}