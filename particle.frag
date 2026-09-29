#version 330 core

// =============================================================================
// particle.frag — Particle fragment shader for the WBOIT transparency pipeline
// =============================================================================
//
// Drawn once per frame, in the single WBOIT pass, after the transmissive pass.
// The transmissive pass writes depth, so particles behind glass or water are
// depth-rejected and simply occluded; particles in front blend over it. There
// is no second alpha-pass variant, because there is no second pass for it to
// draw into.
//
// Compiles with WBOIT_PASS, so it writes two outputs — the accumulation pair —
// instead of a single colour. Order-independence means particles no longer need
// to be sorted against transparent material geometry; the composite pass
// resolves them all at once.
// =============================================================================

in vec4  vColor;
in vec2  vCorner;
in float vEyeDepth;

#ifdef WBOIT_PASS
layout(location = 0) out vec4 outAccumulation;
layout(location = 1) out vec4 outRevealage;
#else
out vec4 FragColor;
#endif

// -----------------------------------------------------------------------------
// WBOIT weight
// -----------------------------------------------------------------------------
//
// McGuire & Bavoil 2013. Input is linear eye-space depth (vEyeDepth, from
// the vertex shader's gl_Position.w), not gl_FragCoord.z. See material.frag
// for the full derivation and tuning notes. Both shaders must use the same
// weight function so particles and transparent material geometry composite
// consistently against each other.
float wboit_weight(float eye_depth, float alpha) {
    float z = eye_depth;
    float w = 10.0 / (1e-5 + pow(z / 200.0, 4.0) + pow(z / 200.0, 2.0));
    return alpha * clamp(w, 1e-2, 3e3);
}

void main() {
    // Round sprite mask. A soft falloff is applied to alpha below.
    float d = length(vCorner);
    if (d > 1.0) discard;

    // No transmissive-depth cull here. The BEHIND/FRONT split it existed to
    // implement is gone; the depth test in the WBOIT pass now covers it, since
    // the transmissive pass writes depth.

    float alpha = vColor.a * (1.0 - smoothstep(0.0, 1.0, d));

#ifdef WBOIT_PASS
    // Same weight function as material.frag, on linear eye depth.
    float w = wboit_weight(vEyeDepth, alpha);

    outAccumulation = vec4(vColor.rgb * w, w);
    outRevealage    = vec4(alpha);
#else
    FragColor = vec4(vColor.rgb, alpha);
#endif
}