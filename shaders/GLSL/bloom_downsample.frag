#version 430 core

// =============================================================================
// bloom_downsample.frag — 13-tap reduction for the bloom mip chain
// =============================================================================
//
// Second and later stages of the chain. Each level halves the resolution of the
// one above it, which is what gives the bloom its width: six levels reach a
// 1/64-scale blur, and a single separable Gaussian at full resolution cannot
// afford that radius without a prohibitively wide kernel.
//
// The 13-tap footprint is the downsample filter from "Next Generation Post
// Processing in Call of Duty" (Jimenez, SIGGRAPH 2014). It is used here because
// it is a true box filter over the full 2x2 source footprint with partial
// weights filling in the rest of the kernel — so each level attenuates by
// exactly 1/4 in total energy regardless of the pattern, and no frequency
// survives the chain that was not present in the source. A plain 2x2 box
// aliases badly at these ratios, and a 4-tap bilinear "box" is not a box at
// all: it varies with subtexel phase and leaves a visible grid in the widest
// mips.
//
// Group A is the inner 2x2 (full weight), groups B and C are the axis-aligned
// ring, and groups D and E are the corner ring.
// =============================================================================

layout(binding = 0) uniform sampler2D uSourceTex;

uniform vec2 uTexelSize;    // 1 / source size
uniform vec2 uDstSize;      // destination size

out vec4 FragColor;

void main() {
    /* Normalise by the destination size: gl_FragCoord counts destination
     * pixels, so uTexelSize (the source's) is only correct for the tap
     * offsets below. */
    vec2 uv = gl_FragCoord.xy / uDstSize;
    vec2 t  = uTexelSize;

    vec3 a = texture(uSourceTex, uv + vec2(-2.0 * t.x,  2.0 * t.y)).rgb;
    vec3 b = texture(uSourceTex, uv + vec2( 0.0,        2.0 * t.y)).rgb;
    vec3 c = texture(uSourceTex, uv + vec2( 2.0 * t.x,  2.0 * t.y)).rgb;
    vec3 d = texture(uSourceTex, uv + vec2(-2.0 * t.x,  0.0       )).rgb;
    vec3 e = texture(uSourceTex, uv                          ).rgb;
    vec3 f = texture(uSourceTex, uv + vec2( 2.0 * t.x,  0.0       )).rgb;
    vec3 g = texture(uSourceTex, uv + vec2(-2.0 * t.x, -2.0 * t.y)).rgb;
    vec3 h = texture(uSourceTex, uv + vec2( 0.0,       -2.0 * t.y)).rgb;
    vec3 i = texture(uSourceTex, uv + vec2( 2.0 * t.x, -2.0 * t.y)).rgb;

    vec3 j = texture(uSourceTex, uv + vec2(-1.0 * t.x,  1.0 * t.y)).rgb;
    vec3 k = texture(uSourceTex, uv + vec2( 1.0 * t.x,  1.0 * t.y)).rgb;
    vec3 l = texture(uSourceTex, uv + vec2(-1.0 * t.x, -1.0 * t.y)).rgb;
    vec3 m = texture(uSourceTex, uv + vec2( 1.0 * t.x, -1.0 * t.y)).rgb;

    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    FragColor = vec4(result, 1.0);
}
