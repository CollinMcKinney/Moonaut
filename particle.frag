#version 330 core

in vec4 vColor;
in vec2 vCorner;

#ifdef WBOIT_PASS
// Matches material.frag's WBOIT outputs. Attachment 0 is the accumulation
// target (RGBA16F): RGB is sum(colour * weight), A is sum(alpha * weight).
// Attachment 1 is the revealage target (R8): product of (1 - alpha).
layout(location = 0) out vec4  outAccumulation;
layout(location = 1) out float outRevealage;
#else
out vec4 FragColor;
#endif

void main() {
    float d = length(vCorner);
    if (d > 1.0) discard;

    float alpha = vColor.a * (1.0 - smoothstep(0.0, 1.0, d));

#ifdef WBOIT_PASS
    // Same McGuire & Bavoil weighting function material.frag uses, so
    // particles and transparent material batches produce comparable
    // weights in the accumulation buffer.
    float depth_z = gl_FragCoord.z;
    float t = 1.0 - depth_z;
    float w = alpha * clamp(3e3 * t * t * t, 1e-2, 1e4);

    outAccumulation = vec4(vColor.rgb * w, alpha * w);
    outRevealage    = alpha;
#else
    FragColor = vec4(vColor.rgb, alpha);
#endif
}