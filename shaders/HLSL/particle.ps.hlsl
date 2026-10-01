struct PSIn {
    float4 position : SV_POSITION;
    float2 corner   : TEXCOORD0;
    float4 color    : COLOR;
};

float4 mainPS(PSIn i) : SV_TARGET {
    float d = length(i.corner);
    /* Match GL smoothstep-based falloff: alpha = 1.0 - smoothstep(0.0, 1.0, d) */
    float t = saturate(d);
    float s = t * t * (3.0 - 2.0 * t); /* smoothstep */
    float a = 1.0 - s;
    if (d > 1.0) discard;
    return float4(i.color.rgb, i.color.a * a);
}
