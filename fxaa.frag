#version 430 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec2 resolution;

/* 
 * ANTI_ALIASING_MODE
 * 0 = Disabled
 * 1 = FXAA (Fast Approximate Anti-Aliasing)
 * 2 = ??
 * 3 = ??
 */
#define ANTI_ALIASING_MODE 1

// --- Helper Functions ---
float rgb2luma(vec3 rgb) {
    // Standard sRGB to luminance conversion
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

// --- 1. FXAA Implementation ---
vec4 applyFXAA(vec2 uv) {
    vec2 inverse_res = 1.0 / resolution;
    
    vec3 rgbM = texture(screenTexture, uv).rgb;
    vec3 rgbN = texture(screenTexture, uv + vec2(0.0, -inverse_res.y)).rgb;
    vec3 rgbS = texture(screenTexture, uv + vec2(0.0, inverse_res.y)).rgb;
    vec3 rgbE = texture(screenTexture, uv + vec2(inverse_res.x, 0.0)).rgb;
    vec3 rgbW = texture(screenTexture, uv + vec2(-inverse_res.x, 0.0)).rgb;

    float lumaM = rgb2luma(rgbM);
    float lumaN = rgb2luma(rgbN);
    float lumaS = rgb2luma(rgbS);
    float lumaE = rgb2luma(rgbE);
    float lumaW = rgb2luma(rgbW);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    
    // Contrast threshold test - skip if not an edge
    if (lumaMax - lumaMin < max(0.03125, lumaMax * 0.125)) {
        return vec4(rgbM, 1.0);
    }

    vec2 dir;
    dir.x = -((lumaN + lumaS) - (lumaE + lumaW));
    dir.y =  ((lumaN + lumaE) - (lumaS + lumaW));

    float dirReduce = max((lumaN + lumaS + lumaE + lumaW) * (0.25 * 0.125), 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(vec2(8.0, 8.0), max(vec2(-8.0, -8.0), dir * rcpDirMin)) * inverse_res;

    // First blend stage
    vec3 rgbA = 0.5 * (
        texture(screenTexture, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(screenTexture, uv + dir * (2.0 / 3.0 - 0.5)).rgb
    );

    // Second blend stage
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(screenTexture, uv + dir * (0.0 / 3.0 - 0.5)).rgb +
        texture(screenTexture, uv + dir * (3.0 / 3.0 - 0.5)).rgb
    );

    float lumaB = rgb2luma(rgbB);
    
    // Output check to prevent over-blurring past the local contrast maximums
    if ((lumaB < lumaMin) || (lumaB > lumaMax)) {
        return vec4(rgbA, 1.0);
    }
    return vec4(rgbB, 1.0);
}

void main() {
    #if ANTI_ALIASING_MODE == 1
        FragColor = applyFXAA(TexCoords);
    #else
        FragColor = texture(screenTexture, TexCoords);
    #endif
}