cbuffer MaterialCB : register(b0) {
    float4 uMatData[15];
}

cbuffer GlobalsCB : register(b1) {
    float4x4 uViewProj;
    float4 uLightDir;
    float4 uLightCol;
    float4 uAmbientCol;
    float4 uCamEye;
    float uTime;
    float4 uFogColor;
    float uFogStart;
    float uFogEnd;
    float pad[5];
}

cbuffer ModelCB : register(b2) {
    float4x4 uModels[1024];
}

#define uMatColor uMatData[0].xyz
#define uMatTint uMatData[1].xyz
#define uMatAlpha uMatData[1].w
#define uMatEmissiveColor uMatData[2].xyz
#define uMatEmissivePulseAmplitude uMatData[2].w
#define uMatEmissivePulseFrequency uMatData[3].x
#define uMatEmissivePulsePhase uMatData[3].y
#define uMatSpecularExponent uMatData[3].z
#define uMatSpecularColor uMatData[4].xyz
#define uMatSpecularThreshold uMatData[4].w
#define uMatRimColor uMatData[5].xyz
#define uMatRimExponent uMatData[5].w
#define uMatFresnelColor uMatData[6].xyz
#define uMatFresnelExponent uMatData[6].w
#define uMatGoochCool uMatData[7].xyz
#define uMatGoochWarm uMatData[8].xyz
#define uMatAmbientLightFactor uMatData[8].w
#define uMatOrenNayarSigma uMatData[9].x
#define uMatMinnaertK uMatData[9].y
#define uMatSaturation uMatData[9].z
#define uMatIridescenceStrength uMatData[9].w
#define uMatBackGlowColor uMatData[10].xyz
#define uMatBumpAmplitude uMatData[10].w
#define uMatBumpFrequency uMatData[11].x
#define uMatBumpSpeed uMatData[11].y
#define uMatRoughness uMatData[11].z
#define uMatFringeIntensity uMatData[11].w
#define uMatCelBands int(uMatData[12].x)
#define uMatGlitchIntensity uMatData[12].y
#define uMatPosterizeLevels int(uMatData[12].z)
#define uMatStrobeColor uMatData[13].xyz
#define uMatStrobeFrequency uMatData[13].w
#define uMatStrobePhase uMatData[14].x

struct VS_INPUT {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 localPos : TEXCOORD0;
    float modelIndex : TEXCOORD1;
    float3 localFaceNormal : TEXCOORD2;
    float3 localCentroid : TEXCOORD3;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 localPos : TEXCOORD2;
    float3 vertexColor : TEXCOORD3;          // for Gouraud only
    nointerpolation float3 localFaceNormal : TEXCOORD4;
    nointerpolation float3 localCentroid : TEXCOORD5;
    nointerpolation float3 worldFaceNormal : TEXCOORD6;
    nointerpolation float3 worldCentroid : TEXCOORD7;
};

float saturate(float x) { return clamp(x, 0.0f, 1.0f); }

uint hash(uint x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

float hash_float(float3 p) {
    uint h = hash(asuint(p.x));
    h = hash(h ^ asuint(p.y));
    h = hash(h ^ asuint(p.z));
    return float(h) / 4294967296.0f;
}

float3 shade_surface(float3 N, float3 worldPos, float3 localPos) {
    N = normalize(N);
    float3 L = normalize(uLightDir.xyz);
    float3 V = normalize(uCamEye.xyz - worldPos);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V));

#ifdef EFFECT_BUMP
    {
        float fx = worldPos.x * uMatBumpFrequency;
        float fy = worldPos.y * uMatBumpFrequency;
        float fz = worldPos.z * uMatBumpFrequency;
        float t = uTime * uMatBumpSpeed;
        float3 bump = float3(sin(fy+fz+t), sin(fz+fx+t), sin(fx+fy+t)) * uMatBumpAmplitude;
        N = normalize(N + bump);
        ndotl = saturate(dot(N, L));
        ndotv = saturate(dot(N, V));
    }
#endif

    float diffuse_term = ndotl;

#ifdef EFFECT_DIFFUSE_WRAP
    { float t = ndotl; diffuse_term = t*t*(3.0f-2.0f*t); }
#endif
#ifdef EFFECT_CEL_SHADING
    { float inv = 1.0f / float(uMatCelBands); diffuse_term = min(1.0f, floor(diffuse_term * float(uMatCelBands)) * inv); }
#endif
#ifdef EFFECT_MINNAERT
    { diffuse_term = pow(ndotl, uMatMinnaertK) * pow(ndotv, 1.0f-uMatMinnaertK); }
#endif
#ifdef EFFECT_OREN_NAYAR
    {
        float sigma = uMatOrenNayarSigma;
        float sigma_sq = sigma*sigma;
        float a = 1.0f - 0.5f*sigma_sq/(sigma_sq+0.33f);
        float b = 0.45f*sigma_sq/(sigma_sq+0.09f);
        float cos_phi_diff = 0.0f, sin_alpha = 0.0f, tan_beta = 0.0f;
        if (ndotl > 0.0f && ndotv > 0.0f) {
            float3 Lproj = normalize(L - N*ndotl);
            float3 Vproj = normalize(V - N*ndotv);
            cos_phi_diff = max(0.0f, dot(Lproj, Vproj));
            float sin_l = sqrt(1.0f-ndotl*ndotl);
            float sin_v = sqrt(1.0f-ndotv*ndotv);
            if (ndotl > ndotv) { sin_alpha = sin_v; tan_beta = sin_l/ndotl; }
            else { sin_alpha = sin_l; tan_beta = sin_v/ndotv; }
        }
        diffuse_term = ndotl * (a + b*cos_phi_diff*sin_alpha*tan_beta);
        diffuse_term = saturate(diffuse_term);
    }
#endif

    float3 color = (uAmbientCol.xyz + uLightCol.xyz * diffuse_term) * uMatAmbientLightFactor;

#ifndef EFFECT_GOOCH
    color = color * uMatColor;
#endif

#ifdef EFFECT_GOOCH
    { float t = (ndotl+1.0f)*0.5f; color *= lerp(uMatGoochCool, uMatGoochWarm, t); }
#endif

#ifdef EFFECT_BACK_GLOW
    { float ndotl_neg = dot(N, -L); color += uMatBackGlowColor * max(0.0f, ndotl_neg); }
#endif
#ifdef EFFECT_RIM
    { float rim = pow(1.0f-ndotv, uMatRimExponent); color += uMatRimColor * rim; }
#endif
#ifdef EFFECT_FRESNEL
    { float fresnel = pow(1.0f-ndotv, uMatFresnelExponent); color = lerp(color, uMatFresnelColor, fresnel); }
#endif
#ifdef EFFECT_EMISSIVE
    {
        float3 emissive = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
        { float pulse = 1.0f + uMatEmissivePulseAmplitude * sin(uTime*uMatEmissivePulseFrequency + uMatEmissivePulsePhase); emissive *= pulse; }
#endif
        color += emissive;
    }
#endif
#ifdef EFFECT_STROBE
    { float s = sin(uTime*uMatStrobeFrequency + uMatStrobePhase)*0.5f + 0.5f; color += uMatStrobeColor * s; }
#endif
#ifdef EFFECT_SPECULAR
    {
        float3 H = normalize(L+V);
        float nh = max(0.0f, dot(N,H));
        float spec = pow(nh, uMatSpecularExponent);
#ifdef EFFECT_SPECULAR_THRESH
        { spec = (spec > uMatSpecularThreshold) ? 1.0f : 0.0f; }
#endif
        color += uMatSpecularColor * spec;
    }
#endif
#ifdef EFFECT_SATURATION
    { float luma = dot(color, float3(0.299f,0.587f,0.114f)); color = lerp(float3(luma,luma,luma), color, uMatSaturation); }
#endif
#ifdef EFFECT_IRIDESCENCE
    {
        float angle = ndotv * 2.0f * 3.14159265f;
        float c = cos(angle); float s = sin(angle);
        float3x3 rot = { 0.299f+0.701f*c+0.168f*s, 0.587f-0.587f*c+0.330f*s, 0.114f-0.114f*c-0.497f*s,
                         0.299f-0.299f*c-0.328f*s, 0.587f+0.413f*c+0.035f*s, 0.114f-0.114f*c+0.292f*s,
                         0.299f-0.300f*c+1.250f*s, 0.587f-0.588f*c-1.050f*s, 0.114f+0.886f*c-0.203f*s };
        float3 mixed = mul(rot, color);
        float strength = uMatIridescenceStrength;
        color = lerp(color, mixed, strength);
    }
#endif
#ifdef EFFECT_GLITCH
    {
        float3 q = floor(worldPos * 4096.0f + uTime * 60.0f);
        float offset = (hash_float(q)-0.5f) * uMatGlitchIntensity;
        color.r += offset;
        color.g += offset*0.7f;
        color.b -= offset;
    }
#endif
#ifdef EFFECT_ROUGHNESS
    {
        float3 q = floor(localPos * 256.0f);
        float offset = (hash_float(q)-0.5f) * uMatRoughness;
        color += offset * 0.25f;
    }
#endif
#ifdef EFFECT_FRINGE
    { float fringe = pow(1.0f-ndotv, 3.0f) * uMatFringeIntensity; color.r += fringe; color.b -= fringe; }
#endif
#ifdef EFFECT_POSTERIZE
    { float levels = float(uMatPosterizeLevels); color = floor(color*levels + 0.5f) / levels; }
#endif
#ifdef EFFECT_FOG
    if (uFogEnd > uFogStart) {
        float dist = length(worldPos - uCamEye.xyz);
        float t = clamp((dist-uFogStart)/(uFogEnd-uFogStart), 0.0f, 1.0f);
        color = lerp(color, uFogColor.xyz, t);
    }
#endif

    color *= uMatTint;
    return clamp(color, 0.0f, 1.0f);
}

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    float4x4 model = uModels[int(input.modelIndex)];
    float4 worldPos = mul(model, float4(input.pos, 1.0f));

    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul((float3x3)model, input.normal));
    output.localPos = input.localPos;

    output.localFaceNormal = normalize(input.localFaceNormal);
    output.localCentroid = input.localCentroid;

    /* Compute world‑space face normal and centroid from local data */
    float3x3 rotMatrix = (float3x3)model;
    output.worldFaceNormal = normalize(mul(rotMatrix, output.localFaceNormal));
    output.worldCentroid = mul(model, float4(output.localCentroid, 1.0f)).xyz;

    // Gouraud shading uses per‑vertex world data
#ifdef MODE_GOURAUD
    output.vertexColor = shade_surface(normalize(output.normal), output.worldPos, output.localPos);
#else
    output.vertexColor = float3(0.0f, 0.0f, 0.0f);
#endif

    float4 clip = mul(uViewProj, worldPos);
    // Convert depth from OpenGL [-1,1] to DirectX [0,1]
    clip.z = (clip.z + clip.w) * 0.5f;
    output.pos = clip;
    return output;
}