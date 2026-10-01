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
// Compiles with WBOIT_PASS, so it writes three outputs — the accumulation pair
// plus the shared emissive attachment — instead of a single colour.
// Order-independence means particles no longer need to be sorted against
// transparent material geometry; the composite pass resolves the colour pair all
// at once.
//
// PARTICLES REACH THE BLOOM SOURCE DIRECTLY, via COLOR_ATTACHMENT2 of gl_oit_fbo.
// That attachment is gl_emissive_tex itself, shared with the main scene FBO, so
// there is no second particle draw and nothing for the composite to resolve.
// The bloom source is therefore written additively and is order-dependent: two
// overlapping particles sum rather than average, which is what light does.
//
// EMISSION IS DRIVEN BY THE PARTICLE'S OWN COLOUR: the rgb written to the bloom
// source is the same rgb that blends into the scene, premultiplied by the same
// falloff. A white/warm particle glows, a dark one does not, and there is no
// separate control to keep in sync with the colour.
//
// The per-emitter `emissive` field that used to scale this was removed. It
// defaulted to 0, which meant the default emitter contributed nothing to bloom
// even though its colour was a warm spark, and a second knob for "how much of my
// colour is emissive" is the kind of thing that drifts out of sync with the
// colour it is supposed to describe. If the system later needs dim-glow vs
// full-glow emitters, that is better expressed as an actual colour on the
// emitter than as a multiplier over the colour.
// =============================================================================

in vec4  vColor;
in vec2  vCorner;
in float vEyeDepth;

#ifdef WBOIT_PASS
layout(location = 0) out vec4 outAccumulation;
layout(location = 1) out vec4 outRevealage;
/* This pass runs against gl_oit_fbo with three draw buffers selected, and
 * COLOR_ATTACHMENT2 of that FBO is the real bloom source — the same
 * gl_emissive_tex the opaque and sky passes write. So this output is not a
 * staging copy the composite resolves later; it lands directly in the frame the
 * bloom chain reads. oit_composite.frag is deliberately unaware of it. */
layout(location = 2) out vec4 outEmissive;
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

    // Bloom source
    outEmissive = vec4(vColor.rgb * alpha, alpha);
#else
    FragColor = vec4(vColor.rgb, alpha);
#endif
}