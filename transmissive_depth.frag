#version 430 core

// =============================================================================
// transmissive_depth.frag — Transmissive depth pass
// =============================================================================
//
// Discards fragments that lie behind the opaque scene, and writes
// gl_FragCoord.z to the color target otherwise. The result is a single-
// channel image holding the frontmost transmissive depth per pixel, or
// 1.0 where no transmissive surface was drawn.
//
// Consumed by the ALPHA_PASS_BEHIND cull test in material.frag and
// particle.frag. Transmissive-vs-transmissive ordering is handled by the
// FBO's own depth attachment, not here.
// =============================================================================

uniform sampler2D uOpaqueDepthTex;

out vec4 FragColor;

void main() {
    float opaque = texelFetch(uOpaqueDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (gl_FragCoord.z > opaque) discard;
    FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}