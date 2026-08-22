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

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 localPos : TEXCOORD2;
    float3 vertexColor : TEXCOORD3;
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
        float3 q = floor(worldPos * 4096.0f);
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

float4 main(PS_INPUT input) : SV_TARGET {
    float3 color;

#ifdef MODE_WIREFRAME
    color = uMatColor;
#elif defined(MODE_FLAT)
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 N = normalize(cross(dx, dy));
    float3 V = normalize(uCamEye.xyz - input.worldPos);
    if (dot(N, V) < 0.0f) N = -N;
    color = shade_surface(N, input.worldPos, input.localPos);
#elif defined(MODE_GOURAUD)
    color = input.vertexColor;
#else
    color = shade_surface(normalize(input.normal), input.worldPos, input.localPos);
#endif

    float alpha = 1.0f;
#ifdef EFFECT_ALPHA
    alpha = uMatAlpha;
#endif
    return float4(color, alpha);
}