#version 430 core

// =============================================================================
// post_process.frag — HDR scene resolve to display-referred output
// =============================================================================
//
// Exposure -> bloom composite -> tonemap -> colour grade -> sRGB encode ->
// gamma -> black level -> brightness -> contrast -> saturation -> vibrance ->
// colorblind correction -> clamp.

layout(binding = 0) uniform sampler2D uColorHDR;
layout(binding = 1) uniform sampler2D uBloomTex;

uniform vec2  uScreenSize;
uniform float uExposure;
uniform float uGamma;
uniform float uBloomIntensity;

out vec4 FragColor;

// =============================================================================
// Tuning parameters
// =============================================================================

// Colorblind correction. Not daltonization: shifting the confused channel into
// a visible one is cruder but stays closer to gamut.
#define COLORBLIND_MODE     0        // 0 off, 1 protan, 2 deutan, 3 tritan
#define COLORBLIND_STRENGTH 0.45     // 0 none, 1 full

// Display controls, monitor OSD semantics. Values shown are neutral.
#define DISPLAY_BLACK_LEVEL 0.0      // >0 lifts shadows, <0 crushes
#define DISPLAY_BRIGHTNESS  1.0      // multiply
#define DISPLAY_CONTRAST    1.0      // pivot at 0.5
#define DISPLAY_SATURATION  1.0      // constant chroma scale
#define DISPLAY_VIBRANCE    1.0      // chroma scale weighted by how muted

// Tone mapping operator.
#define TONE_MAP_GT7                    0
#define TONE_MAP_CUSTOM                 1
#define TONE_MAP_HABLE                  2
#define TONE_MAP_KHRONOS_PBR_NEUTRAL    3
#define TONE_MAP_REINHARD               4

#define TONE_MAP_MODE TONE_MAP_CUSTOM

// =============================================================================
// Shared helpers
// =============================================================================

const vec3 LUMA_REC709 = vec3(0.2126, 0.7152, 0.0722);

// Highest scene luminance allowed into the tone curve.
#define PP_SCENE_MAX_NITS 10000.0

// Replaces non-finite components with a finite stand-in.
vec3 sanitize(vec3 c) {
    bvec3 nan_ = equal(c, c); /* false only for NaN */
    bvec3 inf_ = isinf(c);
    bvec3 bad  = bvec3(!nan_.x || inf_.x,
                       !nan_.y || inf_.y,
                       !nan_.z || inf_.z);
    return vec3(bad.x ? PP_SCENE_MAX_NITS : c.r,
                bad.y ? PP_SCENE_MAX_NITS : c.g,
                bad.z ? PP_SCENE_MAX_NITS : c.b);
}

// -----------------------------------------------------------------------------
// The GT curve V2 and the GT7 operator below are derived from the sample
// Polyphony Digital published with their tone-mapping talk. Ported to GLSL and to
// this engine's linear Rec.709 scene buffer; SDR only.
// -----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Polyphony Digital Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// -----------------------------------------------------------------------------

// Argument order is the reference's, reversed from the GLSL built-in.
float gt7_smoothstep(float x, float edge0, float edge1) {
    return smoothstep(edge0, edge1, x);
}

// GT Curve V2. Shared between modes 0 and 1: mode 0 applies it per channel,
// mode 1 to luma, so the two cannot drift apart.
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

// Returns the curve rather than filling a parameter: GLSL structs have no out
// parameters, so an in-place variant would mutate a local copy.
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

// GT7 keeps its own framebuffer scale where 1.0 = 100 nits, so its SDR paper
// white of 250 nits is 2.5 in curve units.
#define GT7_REFERENCE_LUMINANCE 100.0
#define GT7_SDR_PAPER_WHITE     250.0

// Pre-scales GT7's input so scene-white authored at 1.0 reads as 250-nit paper
// white, matching this engine's "1.0 maps to 1.0" contract. 2.5 does that;
// 1.0 is the literal reference behaviour.
#define GT7_INPUT_GAIN 1.0

// Perceptual space GT7 works in: 0 = ICtCp (ITU-T T.302), 1 = Jzazbz.
#define GT7_UCS_JZAZBZ 0


// =============================================================================
// GT7
// =============================================================================
//
// Port of the sample operator Polyphony Digital published alongside their
// tone-mapping talk (MIT licensed):
//
//   1. Rec.709 -> Rec.2020 -> a perceptual UCS (ICtCp), so luminance and chroma
//      can be handled separately.
//   2. Curve each channel to get a "skewed" colour.
//   3. Scale the original chroma down as luminance approaches peak, so the
//      shoulder desaturates rather than clipping to arbitrary hues.
//   4. Blend the per-channel result with the chroma-scaled one and clamp.
//
// The reference takes linear Rec.2020; this engine's scene buffer is linear
// Rec.709 (see LUMA_REC709 and material.frag), so the primaries are converted in
// and out. Without those matrices the UCS would be fed Rec.709 values, which
// does not error — it just shifts every hue.
//
// ICtCp round-trips through PQ twice per pixel and PQ is steeply curved near
// black, so very dark pixels can pick up a visible tint. That is inherent to the
// operator, not to the port.

// Column-major: mat3 fills its first COLUMN from (a,b,c), so the familiar
// row-major figures have to be supplied transposed. Backwards applies the
// inverse primaries and casts every grey — silently.
const mat3 REC709_TO_REC2020 = mat3(
    0.6274039, 0.0690970, 0.0163916,
    0.3292830, 0.9195404, 0.0880132,
    0.0433131, 0.0113623, 0.8955950);

const mat3 REC2020_TO_REC709 = mat3(
     1.6604910, -0.1245505, -0.0181507,
    -0.5876410,  1.1328999, -0.1005788,
    -0.0728498, -0.0083494,  1.1187297);

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

// PQ (0..1) -> linear framebuffer scale.
float gt7_eotf_st2084(float n, float exponentScaleFactor) {
    n = clamp(n, 0.0, 1.0);
    float np = pow(n, 1.0 / (PQ_M2 * exponentScaleFactor));
    float l  = np - PQ_C1;
    l = max(l, 0.0);
    l = l / (PQ_C2 - PQ_C3 * np);
    l = pow(max(l, 0.0), 1.0 / PQ_M1);
    return gt7_physical_to_fb(l * PQ_MAX);
}

// Inverse of the above: linear framebuffer scale -> PQ (0..1).
float gt7_inverse_eotf_st2084(float v, float exponentScaleFactor) {
    float y  = gt7_fb_to_physical(v) / PQ_MAX;
    // Clamping ym at 1.0 keeps the log2 difference from ever evaluating Inf-Inf.
    float ym = min(pow(max(y, 0.0), PQ_M1), 1.0);
    return exp2(PQ_M2 * exponentScaleFactor *
                (log2(PQ_C1 + PQ_C2 * ym) - log2(1.0 + PQ_C3 * ym)));
}

// --- Perceptual space --------------------------------------------------------

#if GT7_UCS_JZAZBZ

// Jzazbz applies a 1.7 exponent scale to PQ, which stretches the midtones and
// so moves the chroma fade: near equivalent on greys, divergent on colour.

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
    float s = dot(rgb, vec3( 99.0,  309.0, 3688.0) / 4096.0);

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

    // The reference clamps to >= 0 here, and it is not a no-op: the inverse
    // matrices return small negatives for out-of-gamut input.
    return max(vec3(lin.x *  3.43661 + lin.y * -2.50645 + lin.z *  0.0698454,
                    lin.x * -0.79133 + lin.y *  1.9836  + lin.z * -0.192271,
                    lin.x * -0.0259499 + lin.y * -0.0989137 + lin.z * 1.12486),
               vec3(0.0));
}

#endif

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

// Linear Rec.709 in, linear Rec.709 out. Works internally in Rec.2020.
//
// The clamp must precede the Rec.2020 -> Rec.709 matrix, because min() does not
// commute with it. It bounds the top of the range but not the bottom, and the
// matrix can push a saturated colour's channel below zero — measured at -0.0063
// linear on dark saturated green, an out-of-gamut sRGB code value. Mode 1 cannot
// do this: its uniform luma scale cannot cross zero.
vec3 tonemap_gt7(vec3 rgb709, GT7ToneMapping tm) {
    vec3 rgb2020 = REC709_TO_REC2020 * (max(rgb709, vec3(0.0)) * GT7_INPUT_GAIN);

    vec3 ucs = gt7_rgb_to_ucs(rgb2020);

    // Curve each channel, then re-encode so its luminance can be recombined
    // with the separately-scaled chroma below.
    vec3 skewedRgb = vec3(gt7_curve_eval(tm.curve, rgb2020.r),
                          gt7_curve_eval(tm.curve, rgb2020.g),
                          gt7_curve_eval(tm.curve, rgb2020.b));
    vec3 skewedUcs = gt7_rgb_to_ucs(skewedRgb);

    float chromaScale = gt7_chroma_curve(ucs.x / tm.targetUcs, tm.fadeStart, tm.fadeEnd);

    vec3 scaledUcs = vec3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale);
    vec3 scaledRgb = gt7_ucs_to_rgb(scaledUcs);

    vec3 blended = mix(skewedRgb, scaledRgb, tm.blendRatio);

    // SDR mode's correction factor is 1.0 in HDR mode, so this is safe to
    // apply unconditionally.
    return REC2020_TO_REC709 * (tm.sdrCorrectionFactor *
                                min(blended, vec3(tm.target)));
}

// =============================================================================
// Mode 1 — Custom: GT7's curve on luma
// =============================================================================
//
// GT7 curves per channel, which is exactly what desaturates its highlights and
// drifts their hue. This applies the identical curve to Rec.709 luma alone and
// carries chroma through on a uniform scale, so channel ratios — and therefore
// hue — are exact. On neutrals the two agree exactly, so this reproduces GT7's
// SDR tone response to 0.0000 display units from 0.002 to 2.0. Above that they
// diverge deliberately: GT7 hard-clamps to white, this stays on the knee, so an
// overbright primary keeps its identity.
// Peak intensity the shared curve is evaluated against, in GT7 curve units.
#define GT7_CURVE_PEAK (GT7_SDR_PAPER_WHITE / GT7_REFERENCE_LUMINANCE)

// How far a luma-driven scale may run ahead of GT7 on saturated colour.
#define SAT_DARKEN 0.22

// Highlight bleed. See highlight_knee_bleed.
#define BLEED_KNEE       0.80   // where the asymptote starts
#define BLEED_AMOUNT     0.85   // how far it travels toward the asymptote
#define BLEED_CAP        0.50   // bound on total weight for two-channel input
#define BLEED_WHITE      1.00   // red/green asymptote, fraction of the peak
#define BLEED_BLUE_WHITE 0.50   // blue's, lower so it crosses to cyan sooner

// Asymptote tint per dominant channel, as a fraction of the peak in linear
// space. Red drifts orange, green yellow-green, blue azure. Chosen display
// referred: half the peak's display value is about 0.21 linear, not 0.50.
#define TINT_R_G 0.22
#define TINT_R_B 0.030
#define TINT_G_R 0.15
#define TINT_G_B 0.045
#define TINT_B_R 0.035
#define TINT_B_G 0.50


// =============================================================================
// Highlight knee and bleed (mode 1)
// =============================================================================
//
// Soft knee plus a partial shift toward a hue-tinted asymptote, so an overbright
// colour keeps its identity instead of flattening to white. Takes an
// already-tone-mapped colour and the scene luminance that drove it. Shadows are
// untouched: the ramp is zero below the threshold.

vec3 soft_knee_exponential(vec3 c, float knee) {
    float M = max(c.r, max(c.g, c.b));
    if (M <= knee) return c;
    float t  = (M - knee) / (1.0 - knee);
    float Mc = 1.0 - (1.0 - knee) * exp(-t);
    return c * (Mc / M);
}

vec3 highlight_knee_bleed(vec3 mapped, float scene_luma) {
    // Asymptotic to 1, so nothing clips.
    mapped = soft_knee_exponential(mapped, BLEED_KNEE);

    float peak = max(max(mapped.r, mapped.g), mapped.b);
    if (peak > 1e-6) {
        // Dominance weights, needed first because the ramp threshold itself
        // depends on which channel dominates.
        vec3 d = max(vec3(0.0), mapped) / peak;
        vec3 e = d * d * d * d * d * d * d * d;              // ^8, sharpness
        float es = e.r + e.g + e.b;
        vec3 p = es > 1e-9 ? e / es : vec3(0.0);

        // Nothing until the scene passes the threshold, then a smooth approach
        // to 1. The threshold blends with dominance, so a colour between two
        // asymptotes gets a threshold between two.
        float white_pt = BLEED_WHITE * (p.r + p.g) + BLEED_BLUE_WHITE * p.b;
        float bleed = 1.0 - exp(-max(0.0, scene_luma / max(white_pt, 1e-5) - 1.0));

        if (bleed > 1e-6) {
            // Smooth partition of unity rather than a hard pick: perturbing one
            // channel by 0.02% across a dominance tie popped the output by 0.042,
            // visible as a hard edge in a smooth gradient.
            vec3 w = p.r * vec3(0.0,    0.8908, 0.1092)
                   + p.g * vec3(0.8393, 0.0,    0.1607)
                   + p.b * vec3(0.3396, 0.6604, 0.0   );
            vec3 tint = p.r * vec3(1.0, TINT_R_G, TINT_R_B)
                      + p.g * vec3(TINT_G_R, 1.0, TINT_G_B)
                      + p.b * vec3(TINT_B_R, TINT_B_G, 1.0);

            // Deficit: 0 for a channel at the peak, 1 for one at zero. A neutral
            // zeroes all of these, so greys are untouched and the peak channel
            // never moves.
            vec3 wt = max(vec3(0.0), (vec3(peak) - mapped) / peak) * w;
            int dom = (mapped.r >= mapped.g && mapped.r >= mapped.b) ? 0
                    : (mapped.g >= mapped.b) ? 1 : 2;
            wt[dom] = 0.0;

            float total = wt.r + wt.g + wt.b;

            if (total > 1e-6) {
                // Scalar clamp on absolute weights. Dividing per channel by the
                // total makes the bleed scale-invariant, so a pixel a thousandth
                // below white got its tiny deficit stretched to fill the whole
                // tint: (100, 100, 99.9) came out at hue 109 deg with 0.80
                // saturation. A per-channel min() here reintroduces that, since
                // it divides to 1 whenever only one channel has weight.
                float scale = min(1.0, BLEED_CAP / total);

                // Per-channel geometric approach to the asymptote.
                vec3 target = tint * peak;
                vec3 t = vec3(bleed * BLEED_AMOUNT * scale) * wt;
                mapped = mapped + t * (target - mapped);
            }
        }
    }

    return max(mapped, vec3(0.0));
}

vec3 tonemap_custom(vec3 color) {
    GTToneMappingCurveV2 c =
        gt7_curve_init(GT7_CURVE_PEAK, 0.25, 0.538, 0.444, 1.280);

    // uExposure is already applied in main, before the bloom composite, because
    // the bloom prefilter thresholds in exposed units too.
    color = max(color, vec3(0.0));

    float L  = dot(color, LUMA_REC709);
    float Lm = gt7_curve_eval(c, L) * (1.0 / GT7_CURVE_PEAK);

    // GT7 lands saturated colours darker than a luma-driven scale does while its
    // greys land in the same place, because per channel the dominant channel is
    // compressed further than the same curve applied to that colour's luma.
    // Pulling luma back by saturation closes the gap without touching greys,
    // which have no saturation to adjust. Still a uniform scale, so no hue.
    float cmax = max(max(color.r, color.g), color.b);
    float cmin = min(min(color.r, color.g), color.b);
    Lm *= 1.0 - SAT_DARKEN * ((cmax - cmin) / max(cmax, 1e-5));

    vec3 mapped = color * (Lm / max(L, 1e-5));
    return highlight_knee_bleed(mapped, L);
}

// =============================================================================
// Mode 2 — Hable / Uncharted 2
// =============================================================================
//
// Hable, "Filmic Tone Mapping for Real-Time Rendering", SIGGRAPH 2002, using
// the Uncharted 2 constants. Per-channel, normalised so W maps to exactly 1.0.
vec3 tonemap_hable(vec3 c) {
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

// =============================================================================
// Mode 3 — Khronos PBR Neutral
// =============================================================================
//
// KhronosGroup/ToneMapping, PBR_Neutral/pbrNeutral.glsl. Two stages: an offset
// that lifts near-black so the compression cannot crush dark saturated colours,
// then a highlight rolloff that desaturates toward neutral by g — which is what
// removes the hue twist. Deliberately no gamut mapping; this engine's inputs are
// Rec.709 throughout.
vec3 tonemap_khronos_pbr_neutral(vec3 color) {
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

// =============================================================================
// Mode 4 — Reinhard, per-channel
// =============================================================================
//
// Reinhard et al., "Photographic Tone Reproduction for Digital Images",
// SIGGRAPH 2002, simple per-channel form.
vec3 tonemap_reinhard(vec3 c) {
    c = max(c, vec3(0.0));
    return c / (1.0 + c);
}

// =============================================================================
// Tonemap dispatcher
// =============================================================================
vec3 tone_map(vec3 color) {
#if TONE_MAP_MODE == TONE_MAP_CUSTOM
    return tonemap_custom(color);
#elif TONE_MAP_MODE == TONE_MAP_GT7
    return tonemap_gt7(color, gt7_init_sdr());
#elif TONE_MAP_MODE == TONE_MAP_HABLE
    return tonemap_hable(color);
#elif TONE_MAP_MODE == TONE_MAP_KHRONOS_PBR_NEUTRAL
    return tonemap_khronos_pbr_neutral(color);
#elif TONE_MAP_MODE == TONE_MAP_REINHARD
    return tonemap_reinhard(color);
#else
#error "TONE_MAP_MODE must be 0..4"
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
// The default framebuffer is GL_LINEAR, so the encode happens here. If the
// framebuffer ever becomes GL_SRGB8_ALPHA8, delete this and its call site.
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
// Shifts a fraction of the confused channel into a visible one:
// protan red -> blue (reds go purple), deutan green -> blue (greens go cyan),
// tritan blue -> red (blues go magenta). Output may exceed [0, 1]; the caller
// clamps after all display controls.
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
// Display-referred, sRGB-encoded, monitor OSD semantics. No #if guards — the
// compiler folds each away when its macro is neutral.

// Black stays black regardless of setting; positive lifts, negative crushes.
vec3 apply_black_level(vec3 c) {
    float exponent = 1.0 / clamp(1.0 + DISPLAY_BLACK_LEVEL, 0.1, 10.0);
    return pow(c, vec3(exponent));
}

// Multiply. Preserves black; use black level to lift instead.
vec3 apply_brightness(vec3 c) {
    return c * DISPLAY_BRIGHTNESS;
}

// Pivot at display mid-grey. Midtones stay fixed, the ends move.
vec3 apply_contrast(vec3 c) {
    return mix(vec3(0.5), c, DISPLAY_CONTRAST);
}

// Constant chroma scale, so vivid colours over-saturate before muted ones react.
vec3 apply_saturation(vec3 c) {
    float luma = dot(c, LUMA_REC709);
    return mix(vec3(luma), c, DISPLAY_SATURATION);
}

// Chroma scale weighted by how muted, so vivid pixels are protected.
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

    // Exposure first, in linear HDR. The bloom prefilter applied the same
    // exposure when it built the chain, so both are in the same units and sum
    // before the curve. The PP_SCENE_MAX_NITS ceiling is load-bearing rather
    // than tidiness: sanitize() makes samples finite, but a finite value can
    // still exceed what PQ can represent.
    vec3 exposed = min(max(sanitize(texture(uColorHDR, uv).rgb) * uExposure,
                           vec3(0.0)),
                        vec3(PP_SCENE_MAX_NITS));

    // Bloom is added before the tone curve, not after. After, it would be
    // compressed by a curve not designed for it and never reach white the way a
    // real highlight does; before, it lifts the scene into the shoulder and picks
    // up the same highlight desaturation.
    vec3 bloom = min(sanitize(texture(uBloomTex, uv).rgb) * uBloomIntensity,
                     vec3(PP_SCENE_MAX_NITS));
    vec3 colorHDR = exposed + bloom;

    vec3 colorLDR = tone_map(colorHDR);
    colorLDR = apply_color_grade(colorLDR);

    // Encode to display-referred values.
    colorLDR = linear_to_srgb(colorLDR);

    // Display calibration, before the user preferences below so the sliders
    // operate on the calibrated signal and mean what they say.
    colorLDR = pow(colorLDR, vec3(1.0 / max(uGamma, 0.1)));

    colorLDR = apply_black_level(colorLDR);
    colorLDR = apply_brightness(colorLDR);
    colorLDR = apply_contrast(colorLDR);
    colorLDR = apply_saturation(colorLDR);
    colorLDR = apply_vibrance(colorLDR);

    // Last, so its channel shifts are not amplified by the sliders above.
    colorLDR = apply_colorblind_mode(colorLDR);

    colorLDR = clamp(colorLDR, 0.0, 1.0);

    FragColor = vec4(colorLDR, 1.0);
}