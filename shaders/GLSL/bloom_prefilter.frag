#version 430 core

// =============================================================================
// bloom_prefilter.frag — highlight extraction for the bloom mip chain
// =============================================================================
//
// First stage of the chain. Runs at half the render resolution and does two
// things in one pass: a 4-tap box reduction of the HDR scene, and a soft-knee
// threshold on the result.
//
// The threshold is applied in exposure-multiplied units, so the same scene value
// blooms identically regardless of the exposure setting, and raising exposure
// blooms more of the frame. That means exposure has to be applied here as well
// as in post_process.frag, and post_process must not apply it twice — see the
// note on tone_map there.
//
// The knee is a quadratic ramp across [threshold-knee, threshold+knee] rather
// than a hard step, so a highlight that sits just above the threshold fades in
// instead of popping. The curve is the standard soft-knee form used for
// firefly-resistant thresholding: it is C1 continuous at both ends, which
// matters because this runs every frame and any discontinuity reads as temporal
// flicker on moving emissive geometry.
//
// uClamp bounds a single pixel's contribution before the blur. Without it a
// very small, very bright triangle (a specular hit on a distant lamp, a
// particle) becomes a firefly that the blur smears into a visible blob. 8.0 is
// well above any legitimate value at these exposure levels.
// =============================================================================

layout(binding = 0) uniform sampler2D uSourceTex;

uniform vec2  uTexelSize;    // 1 / source size
uniform vec2  uDstSize;      // destination size
uniform float uThreshold;
uniform float uKnee;
uniform float uClamp;
uniform float uExposure;

out vec4 FragColor;

void main() {
    /* gl_FragCoord is in destination pixels, so the destination's own size is
     * what normalises it. uTexelSize is the source's, and is only correct for
     * the tap offsets below. Using it here instead would map the half-res
     * target onto the top-left quadrant of the source. */
    vec2 uv = gl_FragCoord.xy / uDstSize;

    /* Half of a source texel, not a full one: the four taps straddle the centre
     * of the destination texel, so this is a box filter aligned to the 2x
     * reduction rather than a shifted point sample. */
    vec2 o = uTexelSize * 0.5;

    vec3 c = min(texture(uSourceTex, uv + vec2(-o.x, -o.y)).rgb * uExposure, vec3(uClamp))
           + min(texture(uSourceTex, uv + vec2( o.x, -o.y)).rgb * uExposure, vec3(uClamp))
           + min(texture(uSourceTex, uv + vec2(-o.x,  o.y)).rgb * uExposure, vec3(uClamp))
           + min(texture(uSourceTex, uv + vec2( o.x,  o.y)).rgb * uExposure, vec3(uClamp));
    c *= 0.25;

    /* Soft knee. Below threshold-knee nothing is emitted; above
     * threshold+knee everything is; the quadratic between them is the
     * transition. Brighter components contribute more, so a pixel is emitted
     * in proportion to how far past the threshold it is. */
    float br   = max(c.r, max(c.g, c.b));
    float soft = br - uThreshold + uKnee;
    soft = clamp(soft, 0.0, 2.0 * uKnee);
    soft = soft * soft / (4.0 * uKnee + 1e-5);
    float contribution = max(soft, br - uThreshold) / max(br, 1e-5);

    FragColor = vec4(c * contribution, 1.0);
}
