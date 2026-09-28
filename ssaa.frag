#version 430 core

/* ========================================================================
 * ssaa.frag — supersampling resolve
 * ========================================================================
 *
 * Used instead of FXAA when gl_resolution_scale > 1.0, i.e. when the scene
 * was rendered with more pixels than the window has. That is supersampling,
 * so FXAA is skipped: the extra samples already resolved the edges and FXAA
 * would only soften them. This pass averages the source pixels that fall
 * inside each output pixel, at window resolution, and is the last filter in
 * the chain.
 *
 * A single bilinear blit is not good enough for a downsample: it reads at
 * most four texels, so 2x only just gets there and anything beyond that
 * undersamples and aliases. The tap grid below is sized from the actual
 * downsample ratio instead.
 *
 * Sample positions are at fixed fractions of each output pixel's footprint,
 * not a moving box, so the pattern is identical on every pixel: a box filter
 * whose size changed per pixel would shimmer as the camera moves.
 *
 * No dither here: dither.frag runs after this pass, at window resolution, as
 * the last thing before the backbuffer.
 * ===================================================================== */

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec2 uSrcSize;   /* internal resolution */
uniform vec2 uDstSize;   /* window resolution */

/* Source texels per output pixel, per axis. 1.0 or below means this pass was
 * reached by mistake; a single tap is then the right answer. */
#ifndef SSAA_MAX_TAPS
#define SSAA_MAX_TAPS 4
#endif

void main() {
    vec2 ratio = uSrcSize / uDstSize;

    int taps = int(clamp(ceil(max(ratio.x, ratio.y)), 1.0, float(SSAA_MAX_TAPS)));
    float invTaps = 1.0 / float(taps);

    /* gl_FragCoord.xy is the centre of this output pixel (x + 0.5). */
    vec2 centre = gl_FragCoord.xy * ratio;

    vec3 sum = vec3(0.0);
    for (int y = 0; y < taps; ++y) {
        for (int x = 0; x < taps; ++x) {
            /* Fixed fractions of the footprint, so taps land on the source
             * texel centres exactly when the ratio is a whole number. */
            vec2 frac = (vec2(float(x), float(y)) + 0.5) * invTaps - 0.5;
            vec2 uv = (centre + frac * ratio) / uSrcSize;
            sum += texture(screenTexture, uv).rgb;
        }
    }

    FragColor = vec4(sum / float(taps * taps), 1.0);
}

