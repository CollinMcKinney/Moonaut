#version 430 core

// =============================================================================
// post_process.frag — HDR to display-referred resolve
// =============================================================================
//
// Reads the composited linear HDR scene from gl_color_tex and produces
// display-referred output for the default framebuffer. The only pass in
// the pipeline where a display-referred operation occurs.
//
// Pipeline order:
//
//   1. Tone map (Hable filmic, at uExposure)
//   2. Artist color grade (LUT stub)
//   3. sRGB encode
//   4. User gamma (display calibration)
//   5. Display controls: black level, brightness, contrast, saturation,
//      vibrance
//   6. Colorblind correction
//   7. Clamp
//   8. Dither
//
// =============================================================================


// TODO: Promote to uniforms.
// =============================================================================
// Configuration
// =============================================================================
//
//   COLORBLIND_MODE      0 = off, 1 = protan, 2 = deutan, 3 = tritan
//   COLORBLIND_STRENGTH  0.0 = none, 1.0 = full
//   DISPLAY_BLACK_LEVEL  0.0 = neutral (positive lifts, negative crushes)
//   DISPLAY_BRIGHTNESS   1.0 = neutral (multiply)
//   DISPLAY_CONTRAST     1.0 = neutral (pivot at 0.5)
//   DISPLAY_SATURATION   1.0 = neutral (luma-ratio)
//   DISPLAY_VIBRANCE     1.0 = neutral (weighted by unsaturation)
//
// The display controls are always applied; no #if guards. The compiler
// folds each operation away when its macro is at neutral, so the
// neutral configuration has zero runtime cost. Floating-point equality
// comparisons in #if directives are unreliable across GLSL compilers
// (the spec truncates to integer), so the guards were removed rather
// than made more elaborate.
// =============================================================================

#define COLORBLIND_MODE 0
#define COLORBLIND_STRENGTH 0.45

#define DISPLAY_BLACK_LEVEL   0.0
#define DISPLAY_BRIGHTNESS    1.0
#define DISPLAY_CONTRAST      1.0
#define DISPLAY_SATURATION    1.0
#define DISPLAY_VIBRANCE      1.0


// =============================================================================
// Inputs
// =============================================================================
layout(binding = 0) uniform sampler2D uColorHDR;

uniform vec2  uScreenSize;
uniform float uExposure;
uniform float uGamma;
uniform float uTime;

out vec4 FragColor;

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

// =============================================================================
// Tone mapping (Hable / Uncharted 2)
// =============================================================================
float filmic_base(float x, float A, float B, float C,
                  float D, float E, float F) {
    return ((x * (A * x + C * B) + D * E)
          / (x * (A * x + B) + D * F)) - E / F;
}

vec3 soft_knee_exponential(vec3 c, float knee) {
    float M = max(c.r, max(c.g, c.b));
    if (M <= knee) return c;
    float t  = (M - knee) / (1.0 - knee);
    float Mc = 1.0 - (1.0 - knee) * exp(-t);
    return c * (Mc / M);
}

vec3 tone_map(vec3 color) {
    const float A = 0.15;
    const float B = 0.55;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.35;
    const float W = 10.0;

    color = max(color * uExposure, vec3(0.0));

    float L  = dot(color, LUMA_REC709);
    float Lm = filmic_base(L, A, B, C, D, E, F)
             / filmic_base(W, A, B, C, D, E, F);

    // Chroma compression: pulls bright colors toward grey. 0.0 disables;
    // the per-channel Hable curve still produces mild highlight
    // desaturation on its own.
    const float CHROMA_COMPRESS = 0.0;
    float chromaScale = 1.0 - CHROMA_COMPRESS * smoothstep(0.70, 1.0, Lm);

    vec3 grey   = vec3(Lm);
    vec3 scaled = color * (Lm / max(L, 1e-5));
    vec3 mapped = grey + (scaled - grey) * chromaScale;

    const float KNEE = 0.80;
    mapped = soft_knee_exponential(mapped, KNEE);

    const float TOE_AMOUNT = 0.006;
    const float TOE_RADIUS = 0.15;
    float L_final = dot(mapped, LUMA_REC709);
    mapped += TOE_AMOUNT * (1.0 - smoothstep(0.0, TOE_RADIUS, L_final));

    return mapped;
}

// =============================================================================
// Artist color grade (LUT stub)
// =============================================================================
vec3 apply_color_grade(vec3 c) {
    // TODO: Add a colorgrading LUT uniform.
    return c;
}

// =============================================================================
// sRGB encode
// =============================================================================
//
// The default framebuffer is GL_LINEAR, so the encode happens here. If
// the framebuffer is ever changed to GL_SRGB8_ALPHA8, delete this and
// its call site.
vec3 linear_to_srgb(vec3 c) {
    c = max(c, vec3(0.0));
    vec3 low  = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), c));
}

// =============================================================================
// Colorblind correction
// =============================================================================
//
// Shifts a fraction of the confused channel into a visible channel:
//
//   protan: red -> blue      reds become purple-ish
//   deutan: green -> blue    greens become cyan-ish
//   tritan: blue -> red      blues become magenta-ish
//
// Output may exceed [0, 1] (e.g., shifting into an already-near-1.0
// destination channel); the caller clamps after all display controls.
// Not daltonization — that algorithm is more correct but produces
// more severe out-of-gamut values that a hard clamp would collapse.
vec3 apply_colorblind_mode(vec3 c) {
#if COLORBLIND_MODE == 0
    return c;

#elif COLORBLIND_MODE == 1
    float d = c.r * COLORBLIND_STRENGTH;
    c.r -= d;
    c.b += d;
    return c;

#elif COLORBLIND_MODE == 2
    float d = c.g * COLORBLIND_STRENGTH;
    c.g -= d;
    c.b += d;
    return c;

#elif COLORBLIND_MODE == 3
    float d = c.b * COLORBLIND_STRENGTH;
    c.b -= d;
    c.r += d;
    return c;

#else
    return c;
#endif
}

// =============================================================================
// Display controls
// =============================================================================
//
// Display-referred, sRGB-encoded operations. Same semantics as a
// monitor's OSD controls. No #if guards — the compiler folds each
// operation away when its macro is at neutral.

// Black level: power curve on the shadows. Black stays black regardless
// of setting; positive lifts, negative crushes.
vec3 apply_black_level(vec3 c) {
    float exponent = 1.0 / clamp(1.0 + DISPLAY_BLACK_LEVEL, 0.1, 10.0);
    return pow(c, vec3(exponent));
}

// Brightness: multiply. Preserves black. Users who want a lift should
// use black level instead.
vec3 apply_brightness(vec3 c) {
    return c * DISPLAY_BRIGHTNESS;
}

// Contrast: pivot around display mid-grey (0.5). Midtones stay fixed;
// the ends move.
vec3 apply_contrast(vec3 c) {
    return mix(vec3(0.5), c, DISPLAY_CONTRAST);
}

// Saturation: constant chroma multiply. Treats every pixel equally, so
// vivid colors over-saturate before muted ones react.
vec3 apply_saturation(vec3 c) {
    float luma = dot(c, LUMA_REC709);
    return mix(vec3(luma), c, DISPLAY_SATURATION);
}

// Vibrance: saturation weighted by unsaturation. Vivid pixels are
// protected; muted pixels are boosted.
vec3 apply_vibrance(vec3 c) {
    float maxc   = max(c.r, max(c.g, c.b));
    float weight = 1.0 - maxc;
    float amount = mix(1.0, DISPLAY_VIBRANCE, weight);
    float luma   = dot(c, LUMA_REC709);
    return mix(vec3(luma), c, amount);
}

// =============================================================================
// Dither hash
// =============================================================================
uint hash(uint x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

float hash_float(vec3 p) {
    uint h = hash(floatBitsToUint(p.x));
    h = hash(h ^ floatBitsToUint(p.y));
    h = hash(h ^ floatBitsToUint(p.z));
    return float(h) / 4294967296.0;
}

// =============================================================================
// Entry point
// =============================================================================
void main() {
    vec2 uv = gl_FragCoord.xy / uScreenSize;

    vec3 colorHDR = texture(uColorHDR, uv).rgb;

    // Tone map and grade.
    vec3 colorLDR = tone_map(colorHDR);
    colorLDR = apply_color_grade(colorLDR);

    // Encode to display-referred values.
    colorLDR = linear_to_srgb(colorLDR);

    // Display calibration, before the user's preferences so the sliders
    // below operate on the calibrated signal and mean what they say.
    colorLDR = pow(colorLDR, vec3(1.0 / max(uGamma, 0.1)));

    // Player preferences.
    colorLDR = apply_black_level(colorLDR);
    colorLDR = apply_brightness(colorLDR);
    colorLDR = apply_contrast(colorLDR);
    colorLDR = apply_saturation(colorLDR);
    colorLDR = apply_vibrance(colorLDR);

    // Colorblind correction runs on the image the user has chosen to
    // see, so its channel shifts are not amplified by the sliders above.
    colorLDR = apply_colorblind_mode(colorLDR);

    // Bring back into range before dithering.
    colorLDR = clamp(colorLDR, 0.0, 1.0);

    // Dither must be last.
    float dither = (hash_float(vec3(gl_FragCoord.xy, uTime)) - 0.5) / 255.0;
    colorLDR += dither;

    FragColor = vec4(colorLDR, 1.0);
}