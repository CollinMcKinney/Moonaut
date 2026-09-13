#version 430 core

// Weighted-blended OIT composite pass.
//
// Reads the two accumulation targets written by the transparent WBOIT pass,
// resolves them into a single premultiplied-ish colour with an effective
// alpha, and blends that over the existing opaque scene using standard
// alpha blending (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
//
// The maths is straight from McGuire & Bavoil 2013, "Weighted Blended
// Order-Independent Transparency", section 4:
//
//   averageColor = accum.rgb / accum.a      (if accum.a > 0)
//   finalAlpha   = 1 - revealage
//   composited   = (averageColor, finalAlpha)
//
// The caller renders this to the main colour FBO with standard alpha
// blending, which performs the mix with the opaque background.

uniform sampler2D uAccumTexture;
uniform sampler2D uRevealTexture;

out vec4 FragColor;

void main() {
    ivec2 coords = ivec2(gl_FragCoord.xy);

    vec4  accum    = texelFetch(uAccumTexture,  coords, 0);
    float revealage = texelFetch(uRevealTexture, coords, 0).r;

    // Avoid division by zero for pixels with no transparent fragments.
    // If accum.a is tiny, averageColor is clamped to zero and the final
    // alpha is (1 - revealage), which for an untouched pixel is 0, so the
    // composite contributes nothing.
    const float EPSILON = 1e-5;
    vec3 averageColor = accum.rgb / max(accum.a, EPSILON);

    FragColor = vec4(averageColor, 1.0 - revealage);
}