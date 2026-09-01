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
in vec3 vTangent;
in vec3 vBitangent;

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
    float uSheenExponent;
    float uSheenStrength;
    float uMatAnisotropic;
    vec3  uMatTransmissionTint;
};

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

// ---- Hash and noise ----
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

// ---- Bump and roughness ----
void apply_bump(inout vec3 N, vec3 worldPos, vec3 localPos) {
#ifdef EFFECT_BUMP
    float freq = uMatBumpFrequency;
    float speed = uMatBumpSpeed;
    float time = uTime;

    vec3 p = localPos * freq;
    float noise = value_noise(p * 0.1);

    float phase1 = p.x * 1.0 + p.z * 0.5 + time * speed + noise * 2.0;
    float phase2 = p.y * 0.7 + p.x * 0.3 + time * speed * 0.7 + 1.2 + noise * 1.5;
    float height = sin(phase1) * 0.6 + sin(phase2) * 0.4;

    float eps = 0.01;
    vec3 p_eps;
    p_eps = p + vec3(eps, 0.0, 0.0);
    float noise_x = value_noise(p_eps * 0.1);
    float phase1_x = p_eps.x * 1.0 + p_eps.z * 0.5 + time * speed + noise_x * 2.0;
    float phase2_x = p_eps.y * 0.7 + p_eps.x * 0.3 + time * speed * 0.7 + 1.2 + noise_x * 1.5;
    float hx = sin(phase1_x) * 0.6 + sin(phase2_x) * 0.4;

    p_eps = p + vec3(0.0, eps, 0.0);
    float noise_y = value_noise(p_eps * 0.1);
    float phase1_y = p_eps.x * 1.0 + p_eps.z * 0.5 + time * speed + noise_y * 2.0;
    float phase2_y = p_eps.y * 0.7 + p_eps.x * 0.3 + time * speed * 0.7 + 1.2 + noise_y * 1.5;
    float hy = sin(phase1_y) * 0.6 + sin(phase2_y) * 0.4;

    p_eps = p + vec3(0.0, 0.0, eps);
    float noise_z = value_noise(p_eps * 0.1);
    float phase1_z = p_eps.x * 1.0 + p_eps.z * 0.5 + time * speed + noise_z * 2.0;
    float phase2_z = p_eps.y * 0.7 + p_eps.x * 0.3 + time * speed * 0.7 + 1.2 + noise_z * 1.5;
    float hz = sin(phase1_z) * 0.6 + sin(phase2_z) * 0.4;

    vec3 gradient = vec3(hx - height, hy - height, hz - height) / eps;
    gradient *= uMatBumpAmplitude;
    N = normalize(N + gradient);
#endif

#ifdef EFFECT_ROUGHNESS
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 p_rough = localPos * 16.0;
    float eps_rough = 0.01;
    float h0 = value_noise(p_rough);
    float hx_rough = value_noise(p_rough + vec3(eps_rough, 0.0, 0.0));
    float hy_rough = value_noise(p_rough + vec3(0.0, eps_rough, 0.0));
    float grad_u = (hx_rough - h0) / eps_rough;
    float grad_v = (hy_rough - h0) / eps_rough;

    float strength = uMatRoughness * 0.5;
    vec3 V = normalize(uCamEye - worldPos);
    float NdotV = max(dot(N, V), 0.0);
    float grazing = 1.0 - NdotV;
    strength *= (0.5 + 0.5 * (1.0 - grazing));

    vec3 perturb = tangent * grad_u * strength + bitangent * grad_v * strength;
    N = normalize(N + perturb);
#endif
}

// ---- GGX D ----
float GGX_D(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float denom = NdotH * NdotH * (alpha - 1.0) + 1.0;
    return alpha / (3.14159265 * denom * denom);
}

// ---- Height-correlated Smith Visibility (safe, energy conserving) ----
float Vis_SmithGGXCorrelated(float NdotL, float NdotV, float roughness) {
    float a = roughness * roughness;
    float lambdaV = NdotL * sqrt(max(0.0, NdotV * (NdotV - NdotV * a) + a));
    float lambdaL = NdotV * sqrt(max(0.0, NdotL * (NdotL - NdotL * a) + a));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

// ---- Original Smith G with clamping (kept for reference, not used now) ----
float Smith_G_GGX(float NdotL, float NdotV, float roughness) {
    float alpha = roughness * roughness;
    float G1L = 2.0 * NdotL / (NdotL + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotL * NdotL));
    float G1V = 2.0 * NdotV / (NdotV + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotV * NdotV));
    return G1L * G1V;
}

// ---- Corrected Anisotropic Smith G (used for main specular with visibility) ----
float Smith_G_aniso_vis(float NdotV, float VdotX, float VdotY, float ax, float ay) {
    float denom = NdotV + sqrt(ax * ax * VdotX * VdotX + ay * ay * VdotY * VdotY + NdotV * NdotV);
    return denom; // returns denominator, not G itself
}

// ---- Burley diffuse (returns BRDF without NdotL and without lightCol) ----
vec3 burley_diffuse(vec3 N, vec3 V, vec3 L, vec3 baseColor, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float LdotV = max(dot(L, V), 0.0);

    float FD90 = 0.5 + 2.0 * roughness * LdotV * LdotV;
    float diff = (1.0 + (FD90 - 1.0) * pow(1.0 - NdotL, 5.0)) *
                 (1.0 + (FD90 - 1.0) * pow(1.0 - NdotV, 5.0));
    return diff * (baseColor / 3.14159265);
}

// ---- Accumulate light ----
void accumulate_light(vec3 N, vec3 V, vec3 L, vec3 lightCol,
                      vec3 F0,
                      vec3 Tangent, vec3 Bitangent,
                      inout vec3 diffuse, inout vec3 specular,
                      inout vec3 clearcoat, inout vec3 sheen,
                      inout vec3 rim, inout vec3 backGlow,
                      float ndotv)
{
    float NdotL_raw = dot(N, L);
    float NdotL = max(NdotL_raw, 0.0);
    float NdotV = ndotv;

    float roughness = clamp(uMatSurfaceRoughness, 0.001, 1.0);
    vec3 diffuseColor = uMatColor * (1.0 - uMatMetallic);

    // Clamp clearcoat strength to [0,1] to ensure energy conservation
    float clearcoatStrength = clamp(uClearcoatStrength, 0.0, 1.0);

    // ---- Diffuse BRDF (no cosine, no lightCol) ----
    vec3 diffuseBRDF;

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
    float diff = A + B * cos_phi_diff * sin_alpha * tan_beta;
    diff = max(diff, 0.0);
    diffuseBRDF = diff * (diffuseColor / 3.14159265);

#else
    #ifdef EFFECT_MINNAERT
        float diff = pow(NdotL, uMatMinnaertK) * pow(NdotV, 1.0 - uMatMinnaertK);
        #ifdef EFFECT_DIFFUSE_WRAP
            float t = NdotL;
            diff = t * t * (3.0 - 2.0 * t);
        #endif
        #ifdef EFFECT_CEL_SHADING
            float inv = 1.0 / float(uMatCelBands);
            diff = min(1.0, floor(diff * float(uMatCelBands)) * inv);
        #endif
        diffuseBRDF = diff * (diffuseColor / 3.14159265);
    #else
        diffuseBRDF = burley_diffuse(N, V, L, diffuseColor, roughness);
        #ifdef EFFECT_DIFFUSE_WRAP
            float t = NdotL;
            float wrapFactor = t * t * (3.0 - 2.0 * t);
            if (NdotL > 0.001) diffuseBRDF *= (wrapFactor / NdotL);
            else diffuseBRDF = vec3(0.0);
        #endif
        #ifdef EFFECT_CEL_SHADING
            float inv = 1.0 / float(uMatCelBands);
            float celFactor = min(1.0, floor(NdotL * float(uMatCelBands)) * inv);
            if (NdotL > 0.001) diffuseBRDF *= (celFactor / NdotL);
            else diffuseBRDF = vec3(0.0);
        #endif
    #endif // EFFECT_MINNAERT
#endif // EFFECT_OREN_NAYAR

    // ---- Subsurface (blends with diffuse) ----
#ifdef EFFECT_SUBSURFACE
    float sssStrength = clamp(uMatSubsurfaceStrength, 0.0, 2.0);
    float wrap = 0.5;
    float NdotL_sss = (NdotL_raw + wrap) / (1.0 + wrap);
    float sss = pow(clamp(NdotL_sss, 0.0, 1.0), 2.0);
    vec3 sssBRDF = sss * (uMatColor / 3.14159265);
    float blend = clamp(sssStrength * sss, 0.0, 1.0);
    diffuseBRDF = mix(diffuseBRDF, sssBRDF, blend);
#endif

    // ---- Fresnel for diffuse (always active) ----
    float cosThetaDiff = clamp(NdotV, 0.0, 1.0);
    vec3 F_diffuse = F0 + (1.0 - F0) * pow(1.0 - cosThetaDiff, 5.0);

    // ---- Base diffuse contribution (local) ----
    vec3 diffuseContrib = diffuseBRDF * NdotL * lightCol * (1.0 - F_diffuse);

    // ---- Main specular contribution (local, without lightCol) ----
    vec3 specContrib = vec3(0.0);
    if (NdotL > 0.0 && NdotV > 0.0) {
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);

#ifdef EFFECT_FRESNEL
        float cosTheta = max(dot(V, H), 0.0);
        cosTheta = min(cosTheta, 1.0);
        float exp = max(uMatFresnelExponent, 0.1);
        vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, exp);
#else
        vec3 fresnel = F0;
#endif

        float D;
        float denomL, denomV;

#ifdef EFFECT_ANISOTROPIC
        float anisotropy = clamp(uMatAnisotropic, -1.0, 1.0);
        float aspect = sqrt(1.0 - 0.9 * anisotropy);
        float ax = roughness * roughness / aspect;
        float ay = roughness * roughness * aspect;
        ax = max(ax, 0.001);
        ay = max(ay, 0.001);

        float HdotT = dot(H, Tangent);
        float HdotB = dot(H, Bitangent);
        float HdotN = dot(H, N);
        float denomDist = (HdotT * HdotT) / (ax * ax) + (HdotB * HdotB) / (ay * ay) + HdotN * HdotN;
        D = 1.0 / (3.14159265 * ax * ay * denomDist * denomDist);

        float LdotT = dot(L, Tangent);
        float LdotB = dot(L, Bitangent);
        float VdotT = dot(V, Tangent);
        float VdotB = dot(V, Bitangent);

        denomL = NdotL + sqrt(ax * ax * LdotT * LdotT + ay * ay * LdotB * LdotB + NdotL * NdotL);
        denomV = NdotV + sqrt(ax * ax * VdotT * VdotT + ay * ay * VdotB * VdotB + NdotV * NdotV);
#else
        float alpha = roughness * roughness;
        D = GGX_D(NdotH, roughness);
        denomL = NdotL + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotL * NdotL);
        denomV = NdotV + sqrt(alpha * alpha + (1.0 - alpha * alpha) * NdotV * NdotV);
#endif

        specContrib = D * fresnel / (denomL * denomV);
        specContrib *= uMatSpecularTint;
    }

    // ---- Transmission contribution (local) ----
    vec3 transContrib = vec3(0.0);
#ifdef EFFECT_TRANSMISSION
    float transStrength = clamp(uMatTransmissionStrength, 0.0, 1.0);
    if (transStrength > 0.001 && NdotL > 0.0 && NdotV > 0.0) {
        float transRoughness = clamp(roughness * 0.8, 0.01, 1.0);
        float transAlpha = transRoughness * transRoughness;
        vec3 Ht = normalize(-V + L);
        float NdotHt = max(dot(N, Ht), 0.0);
        float Dt = GGX_D(NdotHt, transRoughness);
        float tDenomL = NdotL + sqrt(transAlpha * transAlpha + (1.0 - transAlpha * transAlpha) * NdotL * NdotL);
        float tDenomV = NdotV + sqrt(transAlpha * transAlpha + (1.0 - transAlpha * transAlpha) * NdotV * NdotV);
        transContrib = vec3(Dt / (tDenomL * tDenomV));
        transContrib *= uMatTransmissionTint * transStrength;
    }
#endif

    // ---- Clearcoat contribution (local, raw BRDF) ----
    vec3 ccContrib = vec3(0.0);
#ifdef EFFECT_CLEARCOAT
    if (clearcoatStrength > 0.0 && NdotL > 0.0 && NdotV > 0.0) {
        vec3 H = normalize(L + V);
        float cosTheta = max(dot(V, H), 0.0);
        cosTheta = min(cosTheta, 1.0);
        float exp = max(uMatFresnelExponent, 0.1);
        vec3 clearcoatF0 = vec3(0.04);
        vec3 ccFresnel = clearcoatF0 + (1.0 - clearcoatF0) * pow(1.0 - cosTheta, exp);
        float NdotH = max(dot(N, H), 0.0);
        float ccRoughness = clamp(uClearcoatRoughness, 0.001, 1.0);
        float ccD = GGX_D(NdotH, ccRoughness);
        float ccVis = Vis_SmithGGXCorrelated(NdotL, NdotV, ccRoughness);
        ccContrib = ccD * ccVis * ccFresnel;
    }
#endif

    // ---- Sheen contribution (local, without lightCol) ----
    vec3 sheenContrib = vec3(0.0);
#ifdef EFFECT_SHEEN
    if (NdotL > 0.0 && NdotV > 0.0) {
        vec3 H = normalize(L + V);
        float ndoth = dot(N, H);
        float sheenVal = pow(saturate(1.0 - ndoth), uSheenExponent);
        sheenContrib = uSheenColor * sheenVal * uSheenStrength * NdotL;
    }
#endif

    // ---- Back glow contribution (local) ----
    vec3 backGlowContrib = vec3(0.0);
#ifdef EFFECT_BACK_GLOW
    backGlowContrib = uMatBackGlowColor * max(dot(N, -L), 0.0);
#endif

    // ---- Rim contribution (local) ----
    vec3 rimContrib = vec3(0.0);
#ifdef EFFECT_RIM
    rimContrib = uMatRimColor * pow(1.0 - ndotv, uMatRimExponent);
#endif

    // ---- Apply energy conservation scaling to base lobes ----
#ifdef EFFECT_CLEARCOAT
    float clearcoatScale = 1.0 - clearcoatStrength;
    diffuseContrib *= clearcoatScale;
    specContrib   *= clearcoatScale;
#endif

#ifdef EFFECT_TRANSMISSION
    float transScale = 1.0 - clamp(uMatTransmissionStrength, 0.0, 1.0);
    diffuseContrib *= transScale;
    specContrib   *= transScale;
#endif

    // ---- Accumulate everything ----
    diffuse  += diffuseContrib;                          // already includes lightCol
    specular += specContrib * lightCol;                  // specContrib did not include lightCol
    clearcoat += ccContrib * lightCol * uClearcoatColor * clearcoatStrength;
    sheen     += sheenContrib * lightCol;                // sheenContrib did not include lightCol
    backGlow  += backGlowContrib * lightCol;
    rim       += rimContrib * lightCol;

    // Add transmission contribution to specular
#ifdef EFFECT_TRANSMISSION
    specular += transContrib * lightCol;
#endif
}

// ---- Main shading ----
vec3 shade_surface(vec3 N, vec3 worldPos, vec3 localPos) {
    vec3 V = normalize(uCamEye - worldPos);
    vec3 N_bumped = N;
    apply_bump(N_bumped, worldPos, localPos);
    N_bumped = normalize(N_bumped);

    float NdotV = max(dot(N_bumped, V), 0.0);

    float metallic = clamp(uMatMetallic, 0.0, 1.0);
    float ior = uMatIor;
    if (ior <= 0.0) ior = 1.5;
    float ratio = (ior - 1.0) / (ior + 1.0);
    vec3 dielectricF0 = vec3(ratio * ratio);
    vec3 F0 = mix(dielectricF0, uMatColor, metallic);

    float cavity = 1.0;
#ifdef EFFECT_ROUGHNESS
    vec3 p = localPos * 32.0;
    float noise = value_noise(p);
    float cavityStrength = uMatRoughness * 0.5;
    cavity = 1.0 - cavityStrength * (0.5 + 0.5 * noise);
    cavity = max(cavity, 0.3);
#endif

    vec3 Tangent = vec3(0.0);
    vec3 Bitangent = vec3(0.0);
#ifdef EFFECT_ANISOTROPIC
    Tangent = normalize(vTangent - N_bumped * dot(vTangent, N_bumped));
    Bitangent = normalize(cross(N_bumped, Tangent));
#endif

    vec3 totalDiffuse   = vec3(0.0);
    vec3 totalSpec      = vec3(0.0);
    vec3 totalClearcoat = vec3(0.0);
    vec3 totalSheen     = vec3(0.0);
    vec3 totalRim       = vec3(0.0);
    vec3 totalBackGlow  = vec3(0.0);

    vec3 weightedDir = vec3(0.0);
    float totalWeight = 0.0;

    // ---- Light loop ----
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

        if (lightType == 0) {
            lightDir = normalize(L.dir.xyz);
        } else if (lightType == 1) {
            vec3 toLight = lightPos - worldPos;
            float dist = max(length(toLight), 0.01); // avoid division by zero
            if (dist > range) continue;
            float distRatio = dist / range;
            float atten = max(0.0, 1.0 - distRatio * distRatio);
            atten *= atten;
            atten /= (dist * dist + 0.01); // inverse square with epsilon
            lightDir = normalize(toLight);
            lightCol *= atten;
            intensity *= atten;
        } else if (lightType == 2) {
            vec3 toLight = lightPos - worldPos;
            float dist = max(length(toLight), 0.01);
            if (dist > range) continue;
            float distRatio = dist / range;
            float atten = max(0.0, 1.0 - distRatio * distRatio);
            atten *= atten;
            atten /= (dist * dist + 0.01);
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

        accumulate_light(N_bumped, V, lightDir, lightCol, F0,
                         Tangent, Bitangent,
                         totalDiffuse, totalSpec, totalClearcoat, totalSheen,
                         totalRim, totalBackGlow,
                         NdotV);

        weightedDir += lightDir * intensity;
        totalWeight += intensity;
    }

    // ---- Combine ----
    vec3 diffuseColor = uMatColor * (1.0 - metallic);
    vec3 baseColor = uAmbientCol * diffuseColor * uMatAmbientLightFactor + totalDiffuse;
    baseColor *= cavity;

    totalSpec *= cavity;
    totalClearcoat *= cavity;

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

    color = max(color, vec3(0.0));

    // ---- Tone mapping ----
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);

    // ---- Post-effects ----
#ifdef EFFECT_EMISSIVE
    vec3 emissive = uMatEmissiveColor;
#ifdef EFFECT_EMISSIVE_PULSE
    float pulse = 1.0 + uMatEmissivePulseAmplitude * sin(uTime * uMatEmissivePulseFrequency + uMatEmissivePulsePhase);
    emissive *= pulse;
#endif
    color += emissive;
#endif

#ifdef EFFECT_STROBE
    float s = sin(uTime * uMatStrobeFrequency + uMatStrobePhase) * 0.5 + 0.5;
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
        float s_ir = sin(angle);
        float rot0 = 0.299 + 0.701*c + 0.168*s_ir;
        float rot1 = 0.587 - 0.587*c + 0.330*s_ir;
        float rot2 = 0.114 - 0.114*c - 0.497*s_ir;
        float rot3 = 0.299 - 0.299*c - 0.328*s_ir;
        float rot4 = 0.587 + 0.413*c + 0.035*s_ir;
        float rot5 = 0.114 - 0.114*c + 0.292*s_ir;
        float rot6 = 0.299 - 0.300*c + 1.250*s_ir;
        float rot7 = 0.587 - 0.588*c - 1.050*s_ir;
        float rot8 = 0.114 + 0.886*c - 0.203*s_ir;
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
        float t = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
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
    color = shade_surface(vNormal, vWorldPos, vLocalPos);
#endif

    float alpha = 1.0;
#ifdef EFFECT_ALPHA
    alpha = uMatAlpha;
#endif
    FragColor = vec4(color, alpha);
}

#endif /* DEPTH_ONLY */