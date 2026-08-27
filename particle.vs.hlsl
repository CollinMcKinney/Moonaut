cbuffer Globals : register(b1) {
    float4x4 uViewProj;
    float4 uLightDir;
    float4 uLightCol;
    float4 uAmbientCol;
    float4 uCamEye;
    float4 uCamRight;
    float4 uCamUp;
    float uTime;
    float4 uFogColor;
    float uFogStart;
    float uFogEnd;
}

struct VSOut {
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;
    float4 color : COLOR;
};

VSOut mainVS(uint id : SV_VertexID, float3 center : CENTER, float4 color : COLOR, float size : SIZE) {
    VSOut o;
    int corner = id % 6;
    float2 v;
    if (corner == 0) v = float2(-1, -1);
    else if (corner == 1) v = float2( 1, -1);
    else if (corner == 2) v = float2(-1,  1);
    else if (corner == 3) v = float2(-1,  1);
    else if (corner == 4) v = float2( 1, -1);
    else v = float2(1,1);

    float halfSize = size * 0.5f;
    float3 camR = (float3)uCamRight.xyz;
    float3 camU = (float3)uCamUp.xyz;
    float3 world = center + camR * (v.x * halfSize) + camU * (v.y * halfSize);
    float4 clip = mul(uViewProj, float4(world, 1.0));
    clip.z = (clip.z + clip.w) * 0.5f;
    o.position = clip;
    o.corner = v;
    o.color = color;
    return o;
}
