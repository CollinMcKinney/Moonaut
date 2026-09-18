#version 430 core

// =============================================================================
// vbao_blur.frag — Edge-aware bilateral blur (fragment stage, half-res)
//
// Bit-exact with the original. Only the loop bounds are hoisted; the fetch
// pattern is the plain texelFetch(spix, ...) form.
//
// NOTE: do NOT replace these with texelFetchOffset — the offset parameter is
// required to be a constant expression in GLSL, and dx/dy are loop variables,
// so the shader fails to compile on strict drivers (produces black output).
// =============================================================================

layout(binding = 0) uniform sampler2D uAOTex;     // half-res
layout(binding = 1) uniform sampler2D uDepthTex;  // full-res

layout(std140, binding = 0) uniform BlurUniforms {
    vec2  uScreenSize;       // half-res size
    float uDepthThreshold;
    float _pad;
};

out float FragAO;

void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);   // half-res coord
    ivec2 fp  = pix * 2;                  // full-res top-left of 2x2 block
    ivec2 size = ivec2(uScreenSize);      // hoisted

    float centre_depth = texelFetch(uDepthTex, fp, 0).r;
    float centre_ao    = texelFetch(uAOTex,    pix, 0).r;

    const int RADIUS = 2;
    float sum   = 0.0;
    float w_sum = 0.0;

    for (int dy = -RADIUS; dy <= RADIUS; ++dy) {
        for (int dx = -RADIUS; dx <= RADIUS; ++dx) {
            ivec2 spix = pix + ivec2(dx, dy);
            if (spix.x < 0 || spix.x >= size.x ||
                spix.y < 0 || spix.y >= size.y) continue;

            float sdepth = texelFetch(uDepthTex, spix * 2, 0).r;
            if (abs(sdepth - centre_depth) > uDepthThreshold) continue;

            float sw = float((RADIUS + 1) - abs(dx))
                     * float((RADIUS + 1) - abs(dy));
            float sao = texelFetch(uAOTex, spix, 0).r;
            sum   += sao * sw;
            w_sum += sw;
        }
    }

    FragAO = (w_sum > 0.0) ? (sum / w_sum) : centre_ao;
}