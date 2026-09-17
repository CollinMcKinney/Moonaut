#version 330 core

// =============================================================================
// particle.frag — Particle fragment shader for the WBOIT transparency pipeline
// =============================================================================
//
// The particle geometry is drawn twice per frame, once into each WBOIT alpha
// pass. The two draws use different compiled variants of this shader, selected
// by the ALPHA_PASS_BEHIND / ALPHA_PASS_FRONT defines injected by the C side.
//
//   ALPHA_PASS_BEHIND — drawn before the copy to gl_refraction_src. Discards
//                       fragments that are not strictly behind the frontmost
//                       transmissive surface. The WBOIT composite of this
//                       pass is captured by gl_refraction_src and refracted
//                       by the transmissive colour pass, so smoke behind
//                       glass/water/ice is visible through it.
//
//   ALPHA_PASS_FRONT  — drawn after the transmissive colour pass. Depth
//                       testing against the combined opaque + transmissive
//                       depth buffer culls fragments that ALPHA_PASS_BEHIND
//                       already drew. This variant handles smoke in front of
//                       a transmissive surface and smoke with no transmissive
//                       surface behind it.
//
// Both variants compile with WBOIT_PASS, so they write two outputs — the
// accumulation pair — instead of a single colour. Order-independence means
// particles no longer need to be sorted against transparent material
// geometry; the composite pass resolves them all at once.
//
// =============================================================================

in vec4  vColor;
in vec2  vCorner;
in float vEyeDepth;

#ifdef ALPHA_PASS_BEHIND
uniform sampler2D uTransmissiveDepthTex;
#endif

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

    // -------------------------------------------------------------------------
    // ALPHA_PASS_BEHIND culling.
    //
    // uTransmissiveDepthTex holds the frontmost transmissive surface's
    // gl_FragCoord.z, or 1.0 where no transmissive surface is present. A
    // particle survives this pass only when it is strictly behind that
    // surface. Particles in front of it, or in empty space, fall through to
    // ALPHA_PASS_FRONT.
    // -------------------------------------------------------------------------
#ifdef ALPHA_PASS_BEHIND
    float transmissive_z = texelFetch(uTransmissiveDepthTex,
                                      ivec2(gl_FragCoord.xy), 0).r;
    if (transmissive_z >= 1.0) discard;             // no transmissive here
    if (gl_FragCoord.z < transmissive_z) discard;   // in front of it
#endif

    // -------------------------------------------------------------------------
    // ALPHA_PASS_FRONT: no explicit branch.
    //
    // Depth testing against the combined opaque + transmissive depth buffer
    // culls particles that ALPHA_PASS_BEHIND already drew. No shader-side
    // test is needed.
    // -------------------------------------------------------------------------

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