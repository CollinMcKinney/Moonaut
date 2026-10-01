#version 430 core

// =============================================================================
// dither.frag — final present pass
// =============================================================================
//
// Runs at window resolution, 1:1 with the pre-present image, and is the last
// pass before the backbuffer. That position is the whole point: dither is a
// trick for hiding 8-bit quantization, so it has to be the last thing applied
// to the signal. Earlier in the chain it gets averaged away by the AA pass or
// by the upscale, which is exactly what it was doing in post_process.frag and
// what made it worth its own pass.
//
// Every mode converges here: the post-process output goes through FXAA (or
// the supersampling resolve, or straight through when AA is off) into
// gl_present_tex, and this pass presents it.
//
// The source is GL_NEAREST, so this is a straight copy; the only thing added
// is the noise. gl_FragCoord is in window pixels, so the noise lands on
// display pixels.
//
// Noise is interleaved gradient noise, the same construction TAA uses for its
// sub-pixel jitter. It matters here for two reasons, and neither is about
// dithering *effectiveness* — white noise decorrelates the rounding error
// just as well. First, IGN's energy sits at high spatial frequencies, which
// the eye averages away, where white noise also puts energy in the mid range
// that reads as grain. Second, it is cheaper: this is four ALU ops against
// the uint hash's three rounds with two multiplies. IGN is the right default
// at any amplitude; at 0.5/255 the visual difference is small either way.
// =============================================================================

uniform sampler2D screenTexture;
uniform float uTime;

in vec2 TexCoords;
out vec4 FragColor;

#ifndef DITHER_ENABLED
#define DITHER_ENABLED 1
#endif

float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

void main() {
    vec3 color = texture(screenTexture, TexCoords).rgb;

#if (DITHER_ENABLED == 1)
    /* Drift the pattern over time so the eye integrates it away. The rate
     * only decides how fast it moves, not its quality. */
    vec2 pixel = gl_FragCoord.xy + uTime * vec2(5.588238, 11.235269);
    color += (interleavedGradientNoise(pixel) - 0.5) / 255.0;
#endif

    FragColor = vec4(color, 1.0);
}
