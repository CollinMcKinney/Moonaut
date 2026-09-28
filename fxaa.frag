#version 430 core

/* ========================================================================
 * This pass runs at internal resolution, 1:1 with the image FXAA is
 * filtering: its taps are texel steps, so the texel grid and the output
 * pixel grid have to be the same. The upscale to the window happens
 * afterwards, and the dither is a separate pass (dither.frag) so that it
 * lands after every filter no matter which AA path ran.
 *
 * Supersampling (gl_resolution_scale > 1.0) does not use this shader; see
 * ssaa.frag.
 * ===================================================================== */

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec2 resolution;

/* ========================================================================
 * FXAA 3.11 - PATH SELECTION
 * Mirrors the FXAA3_11.h integration defines. At most one path may be 1.
 *   FXAA_PC_CONSOLE = 1  ->  FXAA3 CONSOLE (2 tap, cheapest, no subpixel AA)
 *   FXAA_PC          = 1  ->  FXAA3 QUALITY PC (preset driven, edge search)
 *   neither = 1          ->  no FXAA, the pass is a passthrough
 * All defines below use #ifndef so the host can override them through the
 * injected define block (see init_fxaa_resources in src/rasterizer_GL.h).
 * ===================================================================== */
#ifndef FXAA_PC
#define FXAA_PC 1
#endif
#ifndef FXAA_PC_CONSOLE
#define FXAA_PC_CONSOLE 0
#endif
#if (FXAA_PC == 1) && (FXAA_PC_CONSOLE == 1)
#error FXAA_PC and FXAA_PC_CONSOLE are mutually exclusive
#endif

/* ========================================================================
 * LUMA INPUT
 * The FXAA3 CONSOLE / QUALITY shaders expect luma to be either packed in
 * alpha by the previous pass, or taken from green.
 *   FXAA_LUMA_IN_ALPHA = 1  ->  previous pass wrote luma into alpha
 *   FXAA_GREEN_AS_LUMA = 1  ->  use green (non-linear color only)
 * Neither: luma is computed from RGB, which is what the default framebuffer
 * chain (RGBA8, luma not written) needs.
 * ===================================================================== */
#ifndef FXAA_LUMA_IN_ALPHA
#define FXAA_LUMA_IN_ALPHA 0
#endif
#ifndef FXAA_GREEN_AS_LUMA
#define FXAA_GREEN_AS_LUMA 0
#endif
#if (FXAA_LUMA_IN_ALPHA == 1) && (FXAA_GREEN_AS_LUMA == 1)
#error FXAA_LUMA_IN_ALPHA and FXAA_GREEN_AS_LUMA are mutually exclusive
#endif

/* ========================================================================
 * INTEGRATION KNOBS
 * ===================================================================== */
#ifndef FXAA_EARLY_EXIT
/* 1 = return the untouched center color on flat areas (cheaper, blurrier) */
#define FXAA_EARLY_EXIT 1
#endif
#ifndef FXAA_DISCARD
/* 1 = discard() instead of returning the untouched color on early exits */
#define FXAA_DISCARD 0
#endif

/* ========================================================================
 * FXAA3 CONSOLE - TUNING KNOBS
 * ===================================================================== */
#ifndef FXAA_CONSOLE__EDGE_SHARPNESS
/* 8.0 sharper (default) | 4.0 softer | 2.0 very soft (vector graphics) */
#define FXAA_CONSOLE__EDGE_SHARPNESS 8.0
#endif
#ifndef FXAA_CONSOLE__EDGE_THRESHOLD
/* 0.125 softer, less aliasing (default) | 0.25 sharper, more aliasing */
#define FXAA_CONSOLE__EDGE_THRESHOLD 0.125
#endif
#ifndef FXAA_CONSOLE__EDGE_THRESHOLD_MIN
/* 0.05 default | 0.06 faster, more aliasing in darks | 0.04 slower, less */
#define FXAA_CONSOLE__EDGE_THRESHOLD_MIN 0.05
#endif
#ifndef FXAA_CONSOLE__OPT_FRAME
/* fxaaConsoleRcpFrameOpt: span of the first (normalized dir) tap pair, in texels */
#define FXAA_CONSOLE__OPT_FRAME 0.5
#endif
#ifndef FXAA_CONSOLE__OPT2_FRAME
/* fxaaConsoleRcpFrameOpt2: span of the second (sharpness scaled) tap pair, in texels */
#define FXAA_CONSOLE__OPT2_FRAME 2.0
#endif

/* ========================================================================
 * FXAA3 QUALITY (PC) - TUNING KNOBS
 * ===================================================================== */
#ifndef FXAA_QUALITY__SUBPIX
/* Sub-pixel aliasing removal / edge sharpness.
 * 1.00 upper limit (softest) | 0.75 default | 0.50 sharper
 * 0.25 almost off | 0.00 off
 */
#define FXAA_QUALITY__SUBPIX 0.0
#endif
#ifndef FXAA_QUALITY__EDGE_THRESHOLD
/* Minimum local contrast required to run the algorithm.
 * 0.333 too little (faster) | 0.166 default | 0.125 high | 0.063 overkill
 */
#define FXAA_QUALITY__EDGE_THRESHOLD 0.166
#endif
#ifndef FXAA_QUALITY__EDGE_THRESHOLD_MIN
/* Trims processing of darks.
 * 0.0833 default | 0.0625 faster | 0.0312 slower (visible limit)
 */
#define FXAA_QUALITY__EDGE_THRESHOLD_MIN 0.0833
#endif

/* ========================================================================
 * FXAA3 QUALITY - PRESET
 * Compiled in, since it changes the generated code.
 * 10-15  medium dither (10 fastest, 15 highest quality)
 * 20-29  low dither, more expensive
 * 39     no dither, very expensive
 * 12 = slightly faster than FXAA 3.9 with higher edge quality (default)
 * 23 = closest to FXAA 3.9 visually and performance wise
 * The lowest digit tracks performance, the highest digit tracks style.
 * ===================================================================== */
#ifndef FXAA_QUALITY__PRESET
#define FXAA_QUALITY__PRESET 39
#endif

/* --- medium dither presets --- */
#if (FXAA_QUALITY__PRESET == 10)
    #define FXAA_QUALITY__PS 3
    #define FXAA_QUALITY__P0 1.5
    #define FXAA_QUALITY__P1 3.0
    #define FXAA_QUALITY__P2 12.0
#endif
#if (FXAA_QUALITY__PRESET == 11)
    #define FXAA_QUALITY__PS 4
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 3.0
    #define FXAA_QUALITY__P3 12.0
#endif
#if (FXAA_QUALITY__PRESET == 12)
    #define FXAA_QUALITY__PS 5
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 4.0
    #define FXAA_QUALITY__P4 12.0
#endif
#if (FXAA_QUALITY__PRESET == 13)
    #define FXAA_QUALITY__PS 6
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 4.0
    #define FXAA_QUALITY__P5 12.0
#endif
#if (FXAA_QUALITY__PRESET == 14)
    #define FXAA_QUALITY__PS 7
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 4.0
    #define FXAA_QUALITY__P6 12.0
#endif
#if (FXAA_QUALITY__PRESET == 15)
    #define FXAA_QUALITY__PS 8
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 4.0
    #define FXAA_QUALITY__P7 12.0
#endif

/* --- low dither presets --- */
#if (FXAA_QUALITY__PRESET == 20)
    #define FXAA_QUALITY__PS 3
    #define FXAA_QUALITY__P0 1.5
    #define FXAA_QUALITY__P1 2.0
    #define FXAA_QUALITY__P2 8.0
#endif
#if (FXAA_QUALITY__PRESET == 21)
    #define FXAA_QUALITY__PS 4
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 8.0
#endif
#if (FXAA_QUALITY__PRESET == 22)
    #define FXAA_QUALITY__PS 5
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 8.0
#endif
#if (FXAA_QUALITY__PRESET == 23)
    #define FXAA_QUALITY__PS 6
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 8.0
#endif
#if (FXAA_QUALITY__PRESET == 24)
    #define FXAA_QUALITY__PS 7
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 3.0
    #define FXAA_QUALITY__P6 8.0
#endif
#if (FXAA_QUALITY__PRESET == 25)
    #define FXAA_QUALITY__PS 8
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 4.0
    #define FXAA_QUALITY__P7 8.0
#endif
#if (FXAA_QUALITY__PRESET == 26)
    #define FXAA_QUALITY__PS 9
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 2.0
    #define FXAA_QUALITY__P7 4.0
    #define FXAA_QUALITY__P8 8.0
#endif
#if (FXAA_QUALITY__PRESET == 27)
    #define FXAA_QUALITY__PS 10
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 2.0
    #define FXAA_QUALITY__P7 2.0
    #define FXAA_QUALITY__P8 4.0
    #define FXAA_QUALITY__P9 8.0
#endif
#if (FXAA_QUALITY__PRESET == 28)
    #define FXAA_QUALITY__PS 11
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 2.0
    #define FXAA_QUALITY__P7 2.0
    #define FXAA_QUALITY__P8 2.0
    #define FXAA_QUALITY__P9 4.0
    #define FXAA_QUALITY__P10 8.0
#endif
#if (FXAA_QUALITY__PRESET == 29)
    #define FXAA_QUALITY__PS 12
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 2.0
    #define FXAA_QUALITY__P5 2.0
    #define FXAA_QUALITY__P6 2.0
    #define FXAA_QUALITY__P7 2.0
    #define FXAA_QUALITY__P8 2.0
    #define FXAA_QUALITY__P9 2.0
    #define FXAA_QUALITY__P10 4.0
    #define FXAA_QUALITY__P11 8.0
#endif

/* --- extreme quality --- */
#if (FXAA_QUALITY__PRESET == 39)
    #define FXAA_QUALITY__PS 12
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.0
    #define FXAA_QUALITY__P2 1.0
    #define FXAA_QUALITY__P3 1.0
    #define FXAA_QUALITY__P4 1.0
    #define FXAA_QUALITY__P5 1.5
    #define FXAA_QUALITY__P6 2.0
    #define FXAA_QUALITY__P7 2.0
    #define FXAA_QUALITY__P8 2.0
    #define FXAA_QUALITY__P9 2.0
    #define FXAA_QUALITY__P10 4.0
    #define FXAA_QUALITY__P11 8.0
#endif

#if (FXAA_PC == 1)
#if !defined(FXAA_QUALITY__PS)
#error Unsupported FXAA_QUALITY__PRESET (use 10-15, 20-29 or 39)
#endif
#endif

/* --- Helper Functions --- */
float rgb2luma(vec3 rgb) {
    // Standard sRGB to luminance conversion
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

float fxaaLuma(vec4 c) {
#if (FXAA_LUMA_IN_ALPHA == 1)
    return c.a;
#elif (FXAA_GREEN_AS_LUMA == 1)
    return c.y;
#else
    return rgb2luma(c.rgb);
#endif
}

float fxaaSat(float x) {
    return clamp(x, 0.0, 1.0);
}

/* ========================================================================
 * FXAA3 CONSOLE - 2 tap, cheapest path
 * ===================================================================== */
#if (FXAA_PC_CONSOLE == 1)
vec4 fxaaConsolePath(vec2 posM, vec2 rcpFrame) {
    vec2 rcpOpt  = vec2(FXAA_CONSOLE__OPT_FRAME)  * rcpFrame;
    vec2 rcpOpt2 = vec2(FXAA_CONSOLE__OPT2_FRAME) * rcpFrame;

    /* fxaaConsolePosPos: the four pixel corners around this pixel center */
    vec4 cNW = texture(screenTexture, posM + vec2(-0.5, -0.5) * rcpFrame);
    vec4 cSW = texture(screenTexture, posM + vec2(-0.5,  0.5) * rcpFrame);
    vec4 cNE = texture(screenTexture, posM + vec2( 0.5, -0.5) * rcpFrame);
    vec4 cSE = texture(screenTexture, posM + vec2( 0.5,  0.5) * rcpFrame);
    vec4 rgbyM = texture(screenTexture, posM);

    float lumaNw = fxaaLuma(cNW);
    float lumaSw = fxaaLuma(cSW);
    float lumaNe = fxaaLuma(cNE) + 1.0 / 384.0;
    float lumaSe = fxaaLuma(cSE);
    float lumaM  = fxaaLuma(rgbyM);

    float lumaMaxNwSw = max(lumaNw, lumaSw);
    float lumaMinNwSw = min(lumaNw, lumaSw);
    float lumaMaxNeSe = max(lumaNe, lumaSe);
    float lumaMinNeSe = min(lumaNe, lumaSe);
    float lumaMax = max(lumaMaxNeSe, lumaMaxNwSw);
    float lumaMin = min(lumaMinNeSe, lumaMinNwSw);

    float lumaMaxScaled = lumaMax * FXAA_CONSOLE__EDGE_THRESHOLD;
    float lumaMinM = min(lumaMin, lumaM);
    float lumaMaxScaledClamped = max(FXAA_CONSOLE__EDGE_THRESHOLD_MIN, lumaMaxScaled);
    float lumaMaxM = max(lumaMax, lumaM);
    float dirSwMinusNe = lumaSw - lumaNe;
    float dirSeMinusNw = lumaSe - lumaNw;
    float lumaMaxSubMinM = lumaMaxM - lumaMinM;

#if (FXAA_EARLY_EXIT == 1)
    if (lumaMaxSubMinM < lumaMaxScaledClamped) return rgbyM;
#endif

    vec2 dir;
    dir.x = dirSwMinusNe + dirSeMinusNw;
    dir.y = dirSwMinusNe - dirSeMinusNw;

    vec2 dir1 = normalize(dir);
    vec4 rgbyN1 = texture(screenTexture, posM - dir1 * rcpOpt);
    vec4 rgbyP1 = texture(screenTexture, posM + dir1 * rcpOpt);

    float dirAbsMinTimesC = min(abs(dir1.x), abs(dir1.y)) * FXAA_CONSOLE__EDGE_SHARPNESS;
    vec2 dir2 = clamp(dir1 / dirAbsMinTimesC, -2.0, 2.0);
    vec4 rgbyN2 = texture(screenTexture, posM - dir2 * rcpOpt2);
    vec4 rgbyP2 = texture(screenTexture, posM + dir2 * rcpOpt2);

    vec4 rgbyA = rgbyN1 + rgbyP1;
    vec4 rgbyB = ((rgbyN2 + rgbyP2) * 0.25) + (rgbyA * 0.25);

    float lumaB = fxaaLuma(rgbyB);
    bool twoTap = (lumaB < lumaMin) || (lumaB > lumaMax);
    if (twoTap) rgbyB.rgb = rgbyA.rgb * 0.5;

    return vec4(rgbyB.rgb, 1.0);
}
#endif

/* ========================================================================
 * FXAA3 QUALITY - PC, preset driven edge search
 * ===================================================================== */
#if (FXAA_PC == 1)

/* One edge-search iteration: re-sample the two span ends, test the gradient
 * and step them further out by "span" texels. Returns doneNP.
 * The P0 positions are sampled on the first call. */
bool fxaaPCStep(vec2 offNP,
                inout vec2 posN, inout vec2 posP,
                inout bool doneN, inout bool doneP,
                float lumaNN, float gradientScaled, float span) {
    if (!doneN) {
        float lumaEndN = fxaaLuma(texture(screenTexture, posN)) - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
    }
    if (!doneP) {
        float lumaEndP = fxaaLuma(texture(screenTexture, posP)) - lumaNN * 0.5;
        doneP = abs(lumaEndP) >= gradientScaled;
    }
    if (!doneN) { posN.x -= offNP.x * span; posN.y -= offNP.y * span; }
    if (!doneP) { posP.x += offNP.x * span; posP.y += offNP.y * span; }
    return (!doneN) || (!doneP);
}

vec4 fxaaPCPath(vec2 posM, vec2 rcpFrame) {
    vec4 rgbyM = texture(screenTexture, posM);
    float lumaM = fxaaLuma(rgbyM);
    float lumaS = fxaaLuma(texture(screenTexture, posM + vec2( 0.0,  1.0) * rcpFrame));
    float lumaE = fxaaLuma(texture(screenTexture, posM + vec2( 1.0,  0.0) * rcpFrame));
    float lumaN = fxaaLuma(texture(screenTexture, posM + vec2( 0.0, -1.0) * rcpFrame));
    float lumaW = fxaaLuma(texture(screenTexture, posM + vec2(-1.0,  0.0) * rcpFrame));

    float maxSM = max(lumaS, lumaM);
    float minSM = min(lumaS, lumaM);
    float maxESM = max(lumaE, maxSM);
    float minESM = min(lumaE, minSM);
    float maxWN = max(lumaN, lumaW);
    float minWN = min(lumaN, lumaW);
    float rangeMax = max(maxWN, maxESM);
    float rangeMin = min(minWN, minESM);
    float rangeMaxScaled = rangeMax * FXAA_QUALITY__EDGE_THRESHOLD;
    float range = rangeMax - rangeMin;
    float rangeMaxClamped = max(FXAA_QUALITY__EDGE_THRESHOLD_MIN, rangeMaxScaled);
    bool earlyExit = range < rangeMaxClamped;

    if (earlyExit) {
#if (FXAA_DISCARD == 1)
        discard;
#else
        return rgbyM;
#endif
    }

    float lumaNW = fxaaLuma(texture(screenTexture, posM + vec2(-1.0, -1.0) * rcpFrame));
    float lumaSE = fxaaLuma(texture(screenTexture, posM + vec2( 1.0,  1.0) * rcpFrame));
    float lumaNE = fxaaLuma(texture(screenTexture, posM + vec2( 1.0, -1.0) * rcpFrame));
    float lumaSW = fxaaLuma(texture(screenTexture, posM + vec2(-1.0,  1.0) * rcpFrame));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0 / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = (-2.0 * lumaM) + lumaNS;
    float edgeVert1 = (-2.0 * lumaM) + lumaWE;

    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
    float edgeVert2 = (-2.0 * lumaN) + lumaNWNE;

    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    float edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
    float edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    float edgeVert = abs(edgeVert3) + edgeVert4;

    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = rcpFrame.x;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

    if (!horzSpan) lumaN = lumaW;
    if (!horzSpan) lumaS = lumaE;
    if (horzSpan)  lengthSign = rcpFrame.y;
    float subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

    float gradientN = lumaN - lumaM;
    float gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    float lumaSS = lumaS + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    float subpixC = fxaaSat(abs(subpixB) * subpixRcpRange);

    vec2 posB = posM;
    vec2 offNP;
    offNP.x = (!horzSpan) ? 0.0 : rcpFrame.x;
    offNP.y = ( horzSpan) ? 0.0 : rcpFrame.y;
    if (!horzSpan) posB.x += lengthSign * 0.5;
    if ( horzSpan) posB.y += lengthSign * 0.5;

    vec2 posN = posB - offNP * FXAA_QUALITY__P0;
    vec2 posP = posB + offNP * FXAA_QUALITY__P0;
    float subpixD = ((-2.0) * subpixC) + 3.0;
    float subpixE = subpixC * subpixC;

    if (!pairN) lumaNN = lumaSS;
    float gradientScaled = gradient * 0.25;
    float lumaMM = lumaM - lumaNN * 0.5;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;

    bool doneN = false;
    bool doneP = false;
    bool doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P1);

#if (FXAA_QUALITY__PS > 3)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P2);
#if (FXAA_QUALITY__PS > 4)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P3);
#if (FXAA_QUALITY__PS > 5)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P4);
#if (FXAA_QUALITY__PS > 6)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P5);
#if (FXAA_QUALITY__PS > 7)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P6);
#if (FXAA_QUALITY__PS > 8)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P7);
#if (FXAA_QUALITY__PS > 9)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P8);
#if (FXAA_QUALITY__PS > 10)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P9);
#if (FXAA_QUALITY__PS > 11)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P10);
#if (FXAA_QUALITY__PS > 12)
    if (doneNP) doneNP = fxaaPCStep(offNP, posN, posP, doneN, doneP, lumaNN, gradientScaled, FXAA_QUALITY__P11);
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

    float lumaEndN = fxaaLuma(texture(screenTexture, posN)) - lumaNN * 0.5;
    float lumaEndP = fxaaLuma(texture(screenTexture, posP)) - lumaNN * 0.5;

    float dstN = posM.x - posN.x;
    float dstP = posP.x - posM.x;
    if (!horzSpan) dstN = posM.y - posN.y;
    if (!horzSpan) dstP = posP.y - posM.y;

    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    float spanLength = (dstP + dstN);
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLengthRcp = 1.0 / spanLength;

    bool directionN = dstN < dstP;
    float dstMin = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float subpixG = subpixF * subpixF;
    float pixelOffset = (dstMin * (-spanLengthRcp)) + 0.5;
    float subpixH = subpixG * FXAA_QUALITY__SUBPIX;

    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if ( horzSpan) posM.y += pixelOffsetSubpix * lengthSign;

    return vec4(texture(screenTexture, posM).rgb, 1.0);
}
#endif

void main() {

    #if FXAA_PC == 1
        vec2 rcpFrame = 1.0 / resolution;
        FragColor = fxaaPCPath(TexCoords, rcpFrame);
    #elif FXAA_PC_CONSOLE == 1
        vec2 rcpFrame = 1.0 / resolution;
        FragColor = fxaaConsolePath(TexCoords, rcpFrame);
    #else
        FragColor = texture(screenTexture, TexCoords);
    #endif
}

