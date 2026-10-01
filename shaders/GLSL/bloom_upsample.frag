#version 430 core

// =============================================================================
// bloom_upsample.frag — 3x3 tent filter, additively combined up the chain
// =============================================================================
//
// Walks the chain back up, adding each level into the one above it. Blending
// additively (GL_ONE, GL_ONE) rather than sampling a wider kernel at the final
// resolution is what keeps the whole chain affordable: each level only has to
// resolve one octave of the blur, and the sum reconstructs the wide kernel.
//
// The tent is the 3x3 filter from the same Call of Duty post-processing work.
// The four axial taps carry half the weight of the corners because a separable
// tent is the product of a 1D [1 2 1] kernel with itself, and splitting the
// diagonals into two bilinear fetches each is what lets the footprint be 9 taps
// while behaving like a 5x5.
//
// uRadius scales the tap offsets. 1.0 is the tent at its native spacing, which
// is the tightest the chain can resolve; values above 1.0 spread the mips
// further apart and read as a softer, more diffuse glow at the cost of some
// structure. This is the bloom "radius" control.
// =============================================================================

layout(binding = 0) uniform sampler2D uSourceTex;

uniform vec2  uTexelSize;   // 1 / source size
uniform vec2  uDstSize;     // destination size
uniform float uRadius;

out vec4 FragColor;

void main() {
    /* gl_FragCoord counts destination pixels, so it is normalised by the
     * destination size; uTexelSize (the source's) scales the taps. */
    vec2 uv = gl_FragCoord.xy / uDstSize;
    vec2 t  = uTexelSize * uRadius;

    vec3 a = texture(uSourceTex, uv + vec2(-t.x,  t.y)).rgb;
    vec3 b = texture(uSourceTex, uv + vec2( 0.0,  t.y)).rgb;
    vec3 c = texture(uSourceTex, uv + vec2( t.x,  t.y)).rgb;
    vec3 d = texture(uSourceTex, uv + vec2(-t.x,  0.0)).rgb;
    vec3 e = texture(uSourceTex, uv                  ).rgb;
    vec3 f = texture(uSourceTex, uv + vec2( t.x,  0.0)).rgb;
    vec3 g = texture(uSourceTex, uv + vec2(-t.x, -t.y)).rgb;
    vec3 h = texture(uSourceTex, uv + vec2( 0.0, -t.y)).rgb;
    vec3 i = texture(uSourceTex, uv + vec2( t.x, -t.y)).rgb;

    vec3 result = e * 0.25;
    result += (b + d + f + h) * 0.125;
    result += (a + c + g + i) * 0.0625;

    FragColor = vec4(result, 1.0);
}
