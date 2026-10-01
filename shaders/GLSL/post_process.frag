#version 430 core

// =============================================================================
// post_process.frag — HDR to display-referred resolve
// =============================================================================
//
// Reads the composited linear HDR scene from gl_color_tex and produces
// display-referred output for the default framebuffer. The only pass in
// the pipeline where a display-referred operation occurs.
//
// Pipeline order, SDR modes (0, 1, 3, 4, 5):
//
//   1. Bloom composite (added in HDR, before the curve)
//   2. Tone map (uExposure already applied — see tone_map)
//   3. Artist color grade (LUT stub)
//   4. sRGB encode
//   5. User gamma (display calibration)
//   6. Display controls: black level, brightness, contrast, saturation, vibrance
//   7. Colorblind correction
//   8. Clamp
//
// =============================================================================

#define PP_DEBUG_SOURCE 0
#ifndef PP_DEBUG_SOURCE
#define PP_DEBUG_SOURCE 0
#endif

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
// Tone mapping operator
// =============================================================================
//
//   0 = Hable-custom
//   1 = GT7 tone mapping, SDR (250 nit paper white)
//   2 = Hable
//   3 = Khronos PBR Neutral
//   4 = Reinhard, per-channel
//
// Colour space: all of these take and return linear Rec. 709, which is what
// the scene buffer holds. Only GT7 differs - it is specified in Rec. 2020 and
// converts internally, see gt7_tone_map.
// =============================================================================

#define PP_TONE_MAPPING_MODE 1

// =============================================================================
// HDR configuration
/* Highest scene luminance allowed into the operator.
 *
 * gl_color_tex and gl_bloom_tex are GL_RGBA16F, so anything above 65504 is
 * already stored as +Inf and no clamp downstream can recover the value. Inf is
 * worse than merely out-of-range for the GT7 operator specifically:
 * gt7_inverse_eotf_st2084 evaluates log2(c1 + c2*ym) - log2(1 + c3*ym), and
 * with ym = Inf both terms are Inf, so the difference is NaN. That NaN then
 * passes through every remaining min/max untouched — GLSL defines
 * max(x,y) as `y > x ? y : x`, so max(NaN, 0.0) yields NaN, not 0.0 — and the
 * brightest pixels in the frame arrive at the display black instead of white.
 *
 * The ceiling is well above anything an SDR display can show, so clamping here
 * discards nothing visible while still bounding the curve input to a finite
 * value every operator below can evaluate. */
#define PP_SCENE_MAX_NITS 10000.0


/* =============================================================================
 * sanitize
 * =============================================================================
 *
 * Replaces non-finite components with a finite stand-in.
 *
 * Written as an explicit component test rather than clamp(), because clamp() is
 * defined as min(max(x, lo), hi) and GLSL's max/min propagate NaN — they do not
 * launder it. A clamp alone cannot make a NaN finite.
 *
 * Both non-finite cases are mapped to PP_SCENE_MAX_NITS, i.e. display peak, NOT
 * to zero. That distinction is the whole point: zero is black, so a NaN mapped to
 * zero renders as a black hole exactly where the image should be brightest. This
 * is the symptom that sent us looking for a NaN in the first place — and it is
 * also why the tone mapper must never be handed a non-finite value, since one
 * NaN propagates through every remaining operation in gt7_tone_map.
 *
 * The test covers Inf as well as NaN. notEqual(c, c) alone detects only NaN,
 * because Inf equals itself, so isinf() is needed for the other half of the
 * non-finite range.
 *
 * isinf() is a GLSL 4.30 builtin and must be used here. INFINITY is NOT — it is
 * a C preprocessor macro, and naming it in GLSL fails to compile. That mistake
 * cost a full debugging session: post_process.frag stopped compiling, and
 * because a failed program leaves gl_post_process_program at 0, the engine fell
 * back to blitting the raw scene buffer, bypassing tone mapping altogether. Every
 * PP_TONE_MAPPING_MODE then produced an identical image.
 * ============================================================================= */
vec3 sanitize(vec3 c) {
    bvec3 nan_ = equal(c, c);                  /* false only for NaN */
    bvec3 inf_ = isinf(c);
    bvec3 bad  = bvec3(!nan_.x || inf_.x,
                       !nan_.y || inf_.y,
                       !nan_.z || inf_.z);
    return vec3(bad.x ? PP_SCENE_MAX_NITS : c.r,
                bad.y ? PP_SCENE_MAX_NITS : c.g,
                bad.z ? PP_SCENE_MAX_NITS : c.b);
}

/* =============================================================================
 * Inputs
 * ============================================================================= */
layout(binding = 0) uniform sampler2D uColorHDR;
layout(binding = 1) uniform sampler2D uBloomTex;

uniform vec2  uScreenSize;
uniform float uExposure;
uniform float uGamma;
uniform float uBloomIntensity;

out vec4 FragColor;

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

// =============================================================================
// The Hable curve, and the operators built on it
// =============================================================================
//
// filmic_base() is the bare rational curve from "Filmic Tone Mapping for
// Real-Time Rendering" (Hable, SIGGRAPH 2002). Which operator wraps it, with
// which constants, is chosen by PP_TONE_MAPPING_MODE at the top of this file.
//
// The curve is per-channel, so it differs from a luma-only operator in the way
// it handles chroma: a highlight whose channels are far apart compresses
// unevenly and drifts toward white on its own. Mode 0 works around that by
// curving luma and restoring chroma separately; mode 2 does not, which is what
// the published operator does.
//
// The two use *different* A..F constants, so mode 2 is not a simplification of
// mode 0 — they are different curves.
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

// =============================================================================
// GT7 tone mapping (Polyphony Digital, MIT — see LICENSE note at end of file)
// =============================================================================
//
// Port of the sample operator the GT7 developers published alongside their
// tone-mapping talk. Structure, and the reasons for it:
//
//   1. Convert linear Rec.709 -> linear Rec.2020, then to a perceptual UCS
//      (ICtCp by default) so luminance and chroma can be treated separately.
//   2. Tone map each channel with the GT curve V2 (toe, linear, convergent
//      exponential shoulder) to get a "skewed" colour, and re-encode it.
//   3. Scale the original chroma down as luminance approaches peak, so the
//      shoulder desaturates rather than clipping to arbitrary hues.
//   4. Blend the per-channel result with the chroma-scaled result, and clamp
//      to the target.
//
// The curve constants below are the ones the GT7 sample uses, unmodified. They
// were tuned with an SDR paper white of 250 nits, which is why SDR mode ends up
// with a 0.4 correction factor: 1.0 in the framebuffer is 100 nits, so the
// result has to be scaled to land 250 nits at display 1.0.
//
// -----------------------------------------------------------------------------
// Colour space notes
// -----------------------------------------------------------------------------
//
// The reference implementation takes linear Rec.2020. This engine's scene
// buffer is linear Rec.709 (see LUMA_REC709 above and material.frag), so the
// primaries are converted on the way in and back on the way out. Without
// those two matrices the ICtCp matrices below would be fed Rec.709 values,
// which does not error — it just silently shifts every hue.
//
// Note also that ICtCp round-trips through PQ twice per pixel, and PQ is
// steeply curved near black. That is numerically harsh for 32-bit floats: very
// dark pixels can pick up a visible tint. This is inherent to the operator,
// not to the port.
//
// -----------------------------------------------------------------------------
// GT7_INPUT_GAIN
// -----------------------------------------------------------------------------
//
// Deliberate deviation from the reference. The reference assumes 1.0 in the
// framebuffer is 100 nits, so scene-white authored at 1.0 comes out as a
// comfortable mid-bright (~0.66 after sRGB encode) rather than display white.
// That is correct in GT, where the SDR grade is built around 250-nit paper
// white. Here the existing Hable path maps 1.0 to 1.0, so matching that
// perceptual contract means pre-scaling by 2.5 to treat scene-white as
// 250-nit paper white. Set to 1.0 for the literal reference behaviour.
#define GT7_INPUT_GAIN 1.0

#define GT7_REFERENCE_LUMINANCE      100.0
#define GT7_SDR_PAPER_WHITE          250.0

/* Note GT7's framebuffer scale is its own: 1.0 = 100 nits, so its SDR paper
 * white of 250 nits is 2.5 in curve units, not 1.0. That is why gt7_init_sdr
 * divides by its own GT7_SDR_PAPER_WHITE through sdrCorrectionFactor rather
 * than reusing a general-purpose white constant.
 *
 * gt7_init_hdr() below is retained because it is part of the port and is the
 * natural entry point if HDR output is ever wired up, but nothing selects it
 * now — see the mode list at the top of this file. */

/* Perceptual colour space the operator works in.
 *
 * 0 = ICtCp (ITU-T T.302)  -- the reference's default
 * 1 = Jzazbz               -- the reference's alternative
 *
 * The two are not interchangeable in effect. They share the same structure --
 * PQ-encode LMS, separate luminance from chroma, scale chroma, reconstruct -- but
 * Jzazbz applies an exponentScaleFactor of 1.7 to the PQ curve, which stretches
 * the midtones: at a PQ value of 0.4 it reconstructs 6.4x more luminance than
 * ICtCp does. So the chromaScale fade, which is driven by a ratio of luminance to
 * target, lands at a different point in the range for each. They are near
 * equivalent on neutral greys (both hold them neutral to ~1e-4) and diverge on
 * saturated colour.
 *
 * This must NOT be keyed off PP_TONE_MAPPING_MODE. It is a property of the
 * operator, not of which output mode was selected. */
#define GT7_UCS_JZAZBZ 0

/* Written column-major: GLSL's mat3(a,b,c, d,e,f, g,h,i) fills the first COLUMN
 * from (a,b,c), so the familiar row-major figures have to be supplied here in
 * column order. Getting this backwards is silent — the matrix still compiles
 * and the image still renders, it just applies the inverse primaries and
 * turns every neutral grey into a colour cast. Column sums must be 1. */
const mat3 REC709_TO_REC2020 = mat3(
    0.6274039, 0.0690970, 0.0163916,
    0.3292830, 0.9195404, 0.0880132,
    0.0433131, 0.0113623, 0.8955950);

const mat3 REC2020_TO_REC709 = mat3(
     1.6604910, -0.1245505, -0.0181507,
    -0.5876410,  1.1328999, -0.1005788,
    -0.0728498, -0.0083494,  1.1187297);

/* Argument order matches the reference's smoothStep(x, edge0, edge1), which is
 * the reverse of the GLSL built-in. */
float gt7_smoothstep(float x, float edge0, float edge1) {
    return smoothstep(edge0, edge1, x);
}

float gt7_chroma_curve(float x, float a, float b) {
    return 1.0 - gt7_smoothstep(x, a, b);
}

// --- PQ (SMPTE ST 2084) -------------------------------------------------------

const float PQ_M1 = 0.1593017578125;
const float PQ_M2 = 78.84375;
const float PQ_C1 = 0.8359375;
const float PQ_C2 = 18.8515625;
const float PQ_C3 = 18.6875;
const float PQ_MAX = 10000.0;

float gt7_fb_to_physical(float fb) { return fb * GT7_REFERENCE_LUMINANCE; }
float gt7_physical_to_fb(float p)  { return p / GT7_REFERENCE_LUMINANCE; }

/* PQ (0..1) -> linear framebuffer scale. exponentScaleFactor is 1.0 for
 * ICtCp and JZAZBZ_EXPONENT_SCALE for Jzazbz. */
float gt7_eotf_st2084(float n, float exponentScaleFactor) {
    n = clamp(n, 0.0, 1.0);
    float np = pow(n, 1.0 / (PQ_M2 * exponentScaleFactor));
    float l  = np - PQ_C1;
    l = max(l, 0.0);
    l = l / (PQ_C2 - PQ_C3 * np);
    l = pow(max(l, 0.0), 1.0 / PQ_M1);
    return gt7_physical_to_fb(l * PQ_MAX);
}

/* Inverse of the above: linear framebuffer scale -> PQ (0..1). */
float gt7_inverse_eotf_st2084(float v, float exponentScaleFactor) {
    float y  = gt7_fb_to_physical(v) / PQ_MAX;
    /* PQ's domain is y in [0, 1]; ym = 1.0 is exactly PQ_MAX nits, the signal's
     * saturation point. Clamping ym there keeps the log2 difference below from
     * ever evaluating Inf - Inf, which is NaN, and a NaN here is unrecoverable
     * because GLSL min/max propagate it (see sanitize). */
    float ym = min(pow(max(y, 0.0), PQ_M1), 1.0);
    return exp2(PQ_M2 * exponentScaleFactor *
                (log2(PQ_C1 + PQ_C2 * ym) - log2(1.0 + PQ_C3 * ym)));
}

// --- UCS ---------------------------------------------------------------------

#if GT7_UCS_JZAZBZ

#define JZAZBZ_EXPONENT_SCALE 1.7

vec3 gt7_rgb_to_ucs(vec3 rgb) {
    float l = dot(rgb, vec3(0.530004, 0.355704, 0.086090));
    float m = dot(rgb, vec3(0.289388, 0.525395, 0.157481));
    float s = dot(rgb, vec3(0.091098, 0.147588, 0.734234));

    vec3 pq = vec3(gt7_inverse_eotf_st2084(l, JZAZBZ_EXPONENT_SCALE),
                   gt7_inverse_eotf_st2084(m, JZAZBZ_EXPONENT_SCALE),
                   gt7_inverse_eotf_st2084(s, JZAZBZ_EXPONENT_SCALE));

    float iz = 0.5 * pq.x + 0.5 * pq.y;
    return vec3((0.44 * iz) / (1.0 - 0.56 * iz) - 1.6295499532821566e-11,
                3.524000 * pq.x - 4.066708 * pq.y + 0.542708 * pq.z,
                0.199076 * pq.x + 1.096799 * pq.y - 1.295875 * pq.z);
}

vec3 gt7_ucs_to_rgb(vec3 ucs) {
    float jz = ucs.x + 1.6295499532821566e-11;
    float iz = jz / (0.44 + 0.56 * jz);
    vec3 p = vec3(iz, ucs.y, ucs.z);

    float l = p.x + p.y *  1.386050432715393e-1 + p.z *  5.804731615611869e-2;
    float m = p.x + p.y * -1.386050432715393e-1 + p.z * -5.804731615611869e-2;
    float s = p.x + p.y * -9.601924202631895e-2 + p.z * -8.118918960560390e-1;

    vec3 lin = vec3(gt7_eotf_st2084(l, JZAZBZ_EXPONENT_SCALE),
                    gt7_eotf_st2084(m, JZAZBZ_EXPONENT_SCALE),
                    gt7_eotf_st2084(s, JZAZBZ_EXPONENT_SCALE));

    return vec3(lin.x *  2.990669 + lin.y * -2.049742 + lin.z *  0.088977,
                lin.x * -1.634525 + lin.y *  3.145627 + lin.z * -0.483037,
                lin.x * -0.042505 + lin.y * -0.377983 + lin.z *  1.448019);
}

#else /* ICtCp, ITU-T T.302 */

vec3 gt7_rgb_to_ucs(vec3 rgb) {
    float l = dot(rgb, vec3(1688.0, 2146.0, 262.0) / 4096.0);
    float m = dot(rgb, vec3( 683.0, 2951.0, 462.0) / 4096.0);
    float s = dot(rgb, vec3(  99.0,  309.0, 3688.0) / 4096.0);

    vec3 pq = vec3(gt7_inverse_eotf_st2084(l, 1.0),
                   gt7_inverse_eotf_st2084(m, 1.0),
                   gt7_inverse_eotf_st2084(s, 1.0));

    return vec3((2048.0 * pq.x + 2048.0 * pq.y) / 4096.0,
                ( 6610.0 * pq.x - 13613.0 * pq.y + 7003.0 * pq.z) / 4096.0,
                (17933.0 * pq.x - 17390.0 * pq.y -  543.0 * pq.z) / 4096.0);
}

vec3 gt7_ucs_to_rgb(vec3 ucs) {
    float l = ucs.x + 0.00860904 * ucs.y + 0.11103    * ucs.z;
    float m = ucs.x - 0.00860904 * ucs.y - 0.11103    * ucs.z;
    float s = ucs.x + 0.560031   * ucs.y - 0.320627   * ucs.z;

    vec3 lin = vec3(gt7_eotf_st2084(l, 1.0),
                    gt7_eotf_st2084(m, 1.0),
                    gt7_eotf_st2084(s, 1.0));

    /* The reference clamps each channel to >= 0 here; the clamp is not a
     * no-op, because the inverse matrices can return small negatives for
     * out-of-gamut input. */
    return max(vec3(lin.x *  3.43661 + lin.y * -2.50645 + lin.z *  0.0698454,
                    lin.x * -0.79133 + lin.y *  1.9836  + lin.z * -0.192271,
                    lin.x * -0.0259499 + lin.y * -0.0989137 + lin.z * 1.12486),
               vec3(0.0));
}

#endif

// --- The curve ---------------------------------------------------------------

/* GTToneMappingCurveV2. The shoulder constants are precomputed in init rather
 * than per pixel, as in the reference. */
struct GTToneMappingCurveV2 {
    float peakIntensity;
    float alpha;
    float midPoint;
    float linearSection;
    float toeStrength;
    float kA;
    float kB;
    float kC;
};

/* Returns the curve rather than writing through a parameter: GLSL has no
 * reference or out parameters for structs, so an in-place variant would
 * mutate a local copy and silently leave the caller's struct uninitialized. */
GTToneMappingCurveV2 gt7_curve_init(float peakIntensity, float alpha,
                                    float midPoint, float linearSection,
                                    float toeStrength) {
    GTToneMappingCurveV2 c;
    c.peakIntensity = peakIntensity;
    c.alpha         = alpha;
    c.midPoint      = midPoint;
    c.linearSection = linearSection;
    c.toeStrength   = toeStrength;

    float k = (c.linearSection - 1.0) / (c.alpha - 1.0);
    c.kA = c.peakIntensity * c.linearSection + c.peakIntensity * k;
    c.kB = -c.peakIntensity * k * exp(c.linearSection / k);
    c.kC = -1.0 / (k * c.peakIntensity);
    return c;
}

float gt7_curve_eval(GTToneMappingCurveV2 c, float x) {
    if (x < 0.0) return 0.0;

    float weightLinear = gt7_smoothstep(x, 0.0, c.midPoint);
    float weightToe    = 1.0 - weightLinear;

    if (x < c.linearSection * c.peakIntensity) {
        float toeMapped = c.midPoint * pow(x / c.midPoint, c.toeStrength);
        return weightToe * toeMapped + weightLinear * x;
    }
    return c.kA + c.kB * exp(x * c.kC);
}

// --- Operator ----------------------------------------------------------------

struct GT7ToneMapping {
    float sdrCorrectionFactor;
    float target;        /* framebuffer scale */
    float targetUcs;     /* target luminance in UCS */
    float blendRatio;
    float fadeStart;
    float fadeEnd;
    GTToneMappingCurveV2 curve;
};

GT7ToneMapping gt7_init_parameters(float physicalTargetLuminance) {
    GT7ToneMapping tm;
    tm.target = gt7_physical_to_fb(physicalTargetLuminance);

    /* Curve parameters as published; slightly different from GT Sport's. */
    tm.curve = gt7_curve_init(tm.target, 0.25, 0.538, 0.444, 1.280);

    tm.blendRatio = 0.6;
    tm.fadeStart  = 0.98;
    tm.fadeEnd    = 1.16;

    tm.targetUcs = gt7_rgb_to_ucs(vec3(tm.target)).x;
    return tm;
}

GT7ToneMapping gt7_init_sdr() {
    GT7ToneMapping tm = gt7_init_parameters(GT7_SDR_PAPER_WHITE);
    tm.sdrCorrectionFactor = 1.0 / gt7_physical_to_fb(GT7_SDR_PAPER_WHITE);
    return tm;
}

GT7ToneMapping gt7_init_hdr(float peakNits) {
    GT7ToneMapping tm = gt7_init_parameters(peakNits);
    tm.sdrCorrectionFactor = 1.0;
    return tm;
}

/* Linear Rec.709 in, linear Rec.709 out. */
vec3 gt7_tone_map(vec3 rgb709, GT7ToneMapping tm) {
    vec3 rgb2020 = REC709_TO_REC2020 * (max(rgb709, vec3(0.0)) * GT7_INPUT_GAIN);

    vec3 ucs = gt7_rgb_to_ucs(rgb2020);

    /* Per-channel tone map, then back through the UCS so its luminance can be
     * recombined with the separately-scaled chroma below. */
    vec3 skewedRgb = vec3(gt7_curve_eval(tm.curve, rgb2020.r),
                          gt7_curve_eval(tm.curve, rgb2020.g),
                          gt7_curve_eval(tm.curve, rgb2020.b));
    vec3 skewedUcs = gt7_rgb_to_ucs(skewedRgb);

    float chromaScale = gt7_chroma_curve(ucs.x / tm.targetUcs, tm.fadeStart, tm.fadeEnd);

    vec3 scaledUcs = vec3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale);
    vec3 scaledRgb = gt7_ucs_to_rgb(scaledUcs);

    vec3 blended = mix(skewedRgb, scaledRgb, tm.blendRatio);

    /* SDR mode's correction factor is 1.0 in HDR mode, so this is safe to
     * apply unconditionally. */
    return REC2020_TO_REC709 * (tm.sdrCorrectionFactor *
                                min(blended, vec3(tm.target)));
}

// =============================================================================
// Operators for modes 0, 2, 3, 4 — all linear Rec. 709 in and out
// =============================================================================

// -----------------------------------------------------------------------------
// Mode 0 — the original: Halo 3 style
// -----------------------------------------------------------------------------
//
// Not a plain Hable curve. Three things are layered onto a Hable curve:
//
//   1. The curve is applied to the Rec. 709 luma alone, then chroma is restored
//      by scaling the original colour by Lm/L. This is the Jim Rush / Halo 3
//      move, and it is the part that keeps hue: a per-channel curve applied to
//      a saturated highlight compresses the channels by different amounts and
//      drifts the colour toward white.
//   2. An optional chroma compression. Disabled at CHROMA_COMPRESS = 0.
//   3. A soft-knee exponential and a small shadow toe, both below.
//
// The A..F constants are the Halo-era ones, not the canonical Uncharted values
// that tonemap_hable_true below uses — B, F and W all differ. So mode 2 is not
// reachable from mode 0 by simplification; they are different curves.
// -----------------------------------------------------------------------------
vec3 tone_map_halo3(vec3 color) {
    const float A = 0.15;
    const float B = 0.55;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.35;
    const float W = 10.0;

    // uExposure has already been applied in main, before the bloom composite,
    // because the bloom prefilter thresholds in exposed units too — applying it
    // here as well would double it.
    color = max(color, vec3(0.0));

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

// -----------------------------------------------------------------------------
// Mode 2 — Hable / Uncharted 2, literal
// -----------------------------------------------------------------------------
//
// The curve exactly as published in "Filmic Tone Mapping for Real-Time
// Rendering" (Hable, SIGGRAPH 2002) and repeated in the SIGGRAPH 2010 course
// notes, with the canonical constants:
//
//   A = 0.15  B = 0.50  C = 0.10  D = 0.20  E = 0.02  F = 0.30  W = 11.2
//
// Evaluated per channel and normalised by the curve's own value at W, so input
// W maps to exactly 1.0 and the midtones land where the published figures say
// they do. No exposure bias is applied here: uExposure has already been
// applied in main, and the reference's exposureBias defaults to 1.0.
//
// Because it is per-channel, this desaturates highlights toward white on its
// own and does not preserve hue the way mode 0 does. That is the expected
// behaviour of the operator, not a defect — it is why mode 0 exists.
// -----------------------------------------------------------------------------
vec3 tonemap_hable_true(vec3 c) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2;

    c = max(c, vec3(0.0));

    vec3 curve = ((c * (A * c + C * B) + D * E)
                / (c * (A * c + B)     + D * F)) - E / F;

    float white = ((W * (A * W + C * B) + D * E)
                 / (W * (A * W + B)     + D * F)) - E / F;

    return curve / white;
}

// -----------------------------------------------------------------------------
// Mode 3 — Khronos PBR Neutral
// -----------------------------------------------------------------------------
//
// From KhronosGroup/ToneMapping, PBR_Neutral/pbrNeutral.glsl. Constants and
// structure verbatim from that file; do not retune them.
//
// Designed to get sRGB output that matches the authored sRGB baseColor under
// grayscale lighting, which makes it the right choice for product viewing and
// for content where albedo accuracy matters more than a filmic look. It is the
// most recent addition of the group, intended as a modern alternative to
// switching tone mapping off entirely.
//
// Linear Rec. 709 in and out, no primaries conversion: the spec assumes a PBR
// workflow whose input colour textures and lighting are both Rec. 709, which is
// exactly the case here (glTF baseColorFactor is Rec. 709). It deliberately
// applies no gamut mapping for that reason.
//
// Two stages. First an offset that lifts near-black slightly and clamps small
// values, so the compression below cannot crush dark saturated colours. Then,
// above startCompression, a highlight rolloff that pulls the peak toward 1.0
// and blends toward neutral grey by g — that desaturation is what removes the
// hue twist on bright highlights, and it is the whole point of the operator.
// -----------------------------------------------------------------------------
vec3 PBRNeutralToneMapping(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation      = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

// -----------------------------------------------------------------------------
// Mode 4 — Reinhard, per-channel
// -----------------------------------------------------------------------------
//
// Reinhard 2002, operator T(x) = x / (1 + x). Included as the baseline the
// others are read against: it is the simplest operator that still behaves, and
// it shows plainly why the others bother.
//
// It is per-channel, so it darkens and desaturates as a function of the
// brightest channel — a saturated colour goes grey long before it goes white,
// and it never quite reaches 1.0, so anything already bright stays dull. The
// global variant T(x) = (1 + x/Lw^2) / (1 + x) — luma-weighted, with a white
// point — avoids that and is what Reinhard's paper actually recommends. If a
// Reinhard mode ever needs to look decent, it should be that one.
// -----------------------------------------------------------------------------
vec3 tonemap_reinhard(vec3 c) {
    c = max(c, vec3(0.0));
    return c / (1.0 + c);
}

// -----------------------------------------------------------------------------
// Dispatcher
// -----------------------------------------------------------------------------
//
// Declared after the operators on purpose: GLSL requires a definition before
// the call site, so this has to sit below all of them.
//
// uExposure has already been applied in main, before the bloom composite,
// because the bloom prefilter thresholds in exposed units too — applying it
// here as well would double it. Every operator therefore takes already-exposed
// linear Rec. 709.
// -----------------------------------------------------------------------------
vec3 tone_map(vec3 color) {
#if PP_TONE_MAPPING_MODE == 0
    return tone_map_halo3(color);
#elif PP_TONE_MAPPING_MODE == 1
    return gt7_tone_map(color, gt7_init_sdr());
#elif PP_TONE_MAPPING_MODE == 2
    return tonemap_hable_true(color);
#elif PP_TONE_MAPPING_MODE == 3
    return PBRNeutralToneMapping(color);
#elif PP_TONE_MAPPING_MODE == 4
    return tonemap_reinhard(color);
#else
#error "PP_TONE_MAPPING_MODE must be 0..4"
#endif
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
// Entry point
// =============================================================================
void main() {
    vec2 uv = gl_FragCoord.xy / uScreenSize;

    /* Raw, unexposed scene. Read before anything else so PP_DEBUG_SOURCE below
     * still shows the scene buffer itself rather than an already-graded value. */
    vec3 sceneRaw = texture(uColorHDR, uv).rgb;

#if PP_DEBUG_SOURCE
    /* Visualise the RAW scene buffer, bypassing every display-referred
     * operation. This must inspect sceneRaw, not the sanitized copy: sanitizing
     * first would replace exactly the NaN and Inf this mode exists to reveal,
     * and the diagnostic would report a clean buffer no matter how broken the
     * scene really was.
     *
     *   magenta  non-finite (NaN or Inf) coming out of gl_color_tex — the bug
     *            is upstream, in the material pass or the light setup
     *   green    finite but very large (> PP_SCENE_MAX_NITS): overflowing the
     *            half-float range, i.e. a genuinely super-bright light
     *   grey     ordinary values, scaled into view
     *
     * Set POST_PROCESS_DEBUG_SOURCE to 1 in src/rasterizer_GL.h to enable. */
    bvec3 nf = bvec3(isnan(sceneRaw.r) || isinf(sceneRaw.r),
                     isnan(sceneRaw.g) || isinf(sceneRaw.g),
                     isnan(sceneRaw.b) || isinf(sceneRaw.b));
    vec3 dbg = clamp(sceneRaw, 0.0, 1.0);
    dbg = mix(dbg, vec3(0.0, 1.0, 0.0),
              vec3(nf.x ? 0.0 : (sceneRaw.r > PP_SCENE_MAX_NITS ? 1.0 : 0.0),
                   nf.y ? 0.0 : (sceneRaw.g > PP_SCENE_MAX_NITS ? 1.0 : 0.0),
                   nf.z ? 0.0 : (sceneRaw.b > PP_SCENE_MAX_NITS ? 1.0 : 0.0)));
    FragColor = vec4(mix(dbg, vec3(1.0, 0.0, 1.0), vec3(nf.x ? 1.0 : 0.0,
                                                          nf.y ? 1.0 : 0.0,
                                                          nf.z ? 1.0 : 0.0)),
                     1.0);
    return;
#endif

    vec3 sceneHDR = sanitize(sceneRaw);

    // Exposure first, in linear HDR. The bloom prefilter applied the same
    // exposure when it built the chain, so the glow is in the same units as the
    // scene here and the two can simply be summed before the curve.
    //
    // The PP_SCENE_MAX_NITS ceiling is load-bearing, not tidiness: sanitize()
    // makes the samples finite, but a finite value can still exceed what PQ can
    // represent, and the GT7 operator's log2 difference goes to NaN as soon as
    // it does. Clamping before the tone curve bounds the operator's input and
    // costs nothing a display could have shown.
    vec3 exposed = min(max(sanitize(texture(uColorHDR, uv).rgb) * uExposure,
                           vec3(0.0)),
                       vec3(PP_SCENE_MAX_NITS));

    // Bloom is added in HDR, before the tone curve, not after. Added after, it
    // would be compressed by a curve that was not designed for it and would
    // never reach white the way a real highlight does; added before, it lifts
    // the scene into the shoulder and is subject to the same highlight
    // desaturation, which is what makes a bloomed highlight read as bright
    // rather than as a coloured haze sitting on top of the image.
    vec3 bloom = min(sanitize(texture(uBloomTex, uv).rgb) * uBloomIntensity,
                     vec3(PP_SCENE_MAX_NITS));
    vec3 colorHDR = exposed + bloom;

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

    // Bring back into range. Dithering is not done here: this pass runs at
    // internal resolution and is followed by the AA pass, so the noise would
    // be filtered away before it reached the backbuffer. dither.frag owns it.
    colorLDR = clamp(colorLDR, 0.0, 1.0);

    FragColor = vec4(colorLDR, 1.0);
}
