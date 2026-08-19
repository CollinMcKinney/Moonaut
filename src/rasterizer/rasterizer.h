/*
 * rasterizer.h – GPU‑accelerated renderer with upscaling
 *
 * Supports: Wireframe, Flat, Gouraud, Phong, Quadratic, Cubic.
 * All shading modes run on the GPU, including Quadratic/Cubic.
 *
 * FEATURES:
 *   - Frustum culling (CPU)
 *   - Backface culling (GPU hardware)
 *   - Transparent sorting (CPU buffer, back‑to‑front)
 *   - **Upscaling** – render at a lower internal resolution
 *     and stretch to the window with nearest‑neighbour filtering.
 *
 * Shaders are loaded from external files:
 *   - render.vert  (vertex shader)
 *   - render.geom  (geometry shader)
 *   - render.frag  (fragment shader)
 *
 * Usage:
 *   #define RASTERIZER_IMPLEMENTATION
 *   #include "rasterizer.h"
 *
 *   render_init(800, 600, &gl_ctx);           // window size
 *   render_set_render_resolution(512, 288);   // optional: lower internal res
 *   ... draw ...
 *   render_finish();  // automatically upscales and swaps buffers
 */

#ifndef RASTERIZER_H
#define RASTERIZER_H

#include "common.h"
#include "../tags/material.h"

#define C89GL_IMPLEMENTATION
#include "../../libs/C89FW/C89GL.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public API ---- */
int  render_init(i32 window_width, i32 window_height, C89GL_Context* ctx);
void render_shutdown(void);
void render_set_light(vec3 dir, vec3 col, vec3 amb);
void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect);
void render_set_fog(vec3 color, real start, real end);
void render_set_time(real t);
void render_clear(u8 r, u8 g, u8 b);
void render_clear_color(real r, real g, real b);
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat
);
void render_finish(void);
const u32* render_get_fb(void);   /* returns NULL in GPU mode */
int render_resize(i32 new_w, i32 new_h);

/* ---- Upscaling control ---- */
void render_set_render_resolution(i32 render_width, i32 render_height);

/* ---- Get current render resolution (for aspect ratio etc.) ---- */
i32 render_get_render_width(void);
i32 render_get_render_height(void);

static INLINE u8 color_to_u8(real x);

#ifdef __cplusplus
}
#endif

#endif /* RASTERIZER_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#ifdef RASTERIZER_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
   Internal state
   --------------------------------------------------------------------------- */
static i32 g_win_width  = 0;
static i32 g_win_height = 0;
static i32 g_render_width  = 0;
static i32 g_render_height = 0;
static GLuint g_program = 0;
static C89GL_Context* g_swap_ctx = NULL;

/* ---- Frustum culling state ---- */
#define FRUSTUM_PLANES 6
typedef struct {
    vec3 normal;
    real d;
} frustum_plane_t;

static frustum_plane_t g_frustum[FRUSTUM_PLANES];
static mat4 g_view, g_proj, g_view_proj;
static vec3 g_cam_eye;

/* ---- Transparent triangle buffer ---- */
#define MAX_TRANSPARENT_TRIS 8192

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    vec3 l0, l1, l2;
    const struct material_definition *mat;
    float depth;
} transparent_tri_t;

static transparent_tri_t g_transparent_tris[MAX_TRANSPARENT_TRIS];
static i32 g_transparent_count = 0;

/* ---- FBO (for upscaling) ---- */
static GLuint g_fbo = 0;
static GLuint g_color_tex = 0;
static GLuint g_depth_rb = 0;
static GLint g_default_fbo = 0;

/* ---- Uniform locations ---- */
static GLint u_view_proj   = -1;
static GLint u_light_dir   = -1;
static GLint u_light_col   = -1;
static GLint u_ambient_col = -1;
static GLint u_cam_eye     = -1;
static GLint u_time        = -1;
static GLint u_fog_color   = -1;
static GLint u_fog_start   = -1;
static GLint u_fog_end     = -1;

static GLint u_mat_color          = -1;
static GLint u_mat_tint           = -1;
static GLint u_mat_alpha          = -1;
static GLint u_mat_mode           = -1;
static GLint u_mat_effects        = -1;
static GLint u_mat_emissive_color = -1;
static GLint u_mat_emissive_pulse_amp   = -1;
static GLint u_mat_emissive_pulse_freq  = -1;
static GLint u_mat_emissive_pulse_phase = -1;
static GLint u_mat_specular_exponent    = -1;
static GLint u_mat_specular_color       = -1;
static GLint u_mat_specular_threshold   = -1;
static GLint u_mat_rim_color       = -1;
static GLint u_mat_rim_exponent    = -1;
static GLint u_mat_fresnel_color   = -1;
static GLint u_mat_fresnel_exponent= -1;
static GLint u_mat_gooch_cool      = -1;
static GLint u_mat_gooch_warm      = -1;
static GLint u_mat_ambient_light_factor = -1;
static GLint u_mat_oren_nayar_sigma     = -1;
static GLint u_mat_minnaert_k           = -1;
static GLint u_mat_saturation           = -1;
static GLint u_mat_iridescence_strength = -1;
static GLint u_mat_back_glow_color      = -1;
static GLint u_mat_bump_amplitude       = -1;
static GLint u_mat_bump_frequency       = -1;
static GLint u_mat_bump_speed           = -1;
static GLint u_mat_roughness            = -1;
static GLint u_mat_fringe_intensity     = -1;
static GLint u_mat_cel_bands            = -1;
static GLint u_mat_glitch_intensity     = -1;
static GLint u_mat_posterize_levels     = -1;
static GLint u_mat_strobe_color         = -1;
static GLint u_mat_strobe_frequency     = -1;
static GLint u_mat_strobe_phase         = -1;

static GLuint g_vao = 0;
static GLuint g_vbo = 0;

static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

/* ---------------------------------------------------------------------------
   Frustum culling (ported from software rasterizer)
   --------------------------------------------------------------------------- */
static void extract_frustum_planes(void) {
    vec4 c0 = g_view_proj.columns[0];
    vec4 c1 = g_view_proj.columns[1];
    vec4 c2 = g_view_proj.columns[2];
    vec4 c3 = g_view_proj.columns[3];
    i32 i;

    g_frustum[0].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    g_frustum[0].d = c3.components[3] + c0.components[3];

    g_frustum[1].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    g_frustum[1].d = c3.components[3] - c0.components[3];

    g_frustum[2].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    g_frustum[2].d = c3.components[3] + c1.components[3];

    g_frustum[3].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    g_frustum[3].d = c3.components[3] - c1.components[3];

    g_frustum[4].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    g_frustum[4].d = c3.components[3] + c2.components[3];

    g_frustum[5].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    g_frustum[5].d = c3.components[3] - c2.components[3];

    for (i = 0; i < FRUSTUM_PLANES; i++) {
        real len = vec3_magnitude(g_frustum[i].normal);
        if (len > 0.0f) {
            g_frustum[i].normal = vec3_div_scalar(g_frustum[i].normal, len);
            g_frustum[i].d /= len;
        }
    }
}

static INLINE i32 triangle_outside_frustum(vec3 v0, vec3 v1, vec3 v2) {
    i32 i;
    for (i = 0; i < FRUSTUM_PLANES; i++) {
        i32 o0 = (vec3_dot(g_frustum[i].normal, v0) + g_frustum[i].d) < 0.0f;
        i32 o1 = (vec3_dot(g_frustum[i].normal, v1) + g_frustum[i].d) < 0.0f;
        i32 o2 = (vec3_dot(g_frustum[i].normal, v2) + g_frustum[i].d) < 0.0f;
        if (o0 && o1 && o2) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
   Helper: read file into memory
   --------------------------------------------------------------------------- */
static char* read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("ERROR: Failed to open shader file: %s\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc(len + 1);
    if (!data) { fclose(f); return NULL; }
    size_t read_len = fread(data, 1, len, f);
    data[read_len] = '\0';
    fclose(f);
    return data;
}

/* ---------------------------------------------------------------------------
   Helper: compile shader from file
   --------------------------------------------------------------------------- */
static GLuint compile_shader_file(GLenum type, const char* filename) {
    printf("Compiling shader: %s\n", filename);
    char* source = read_file(filename);
    if (!source) {
        printf("ERROR: Shader file %s not found or empty\n", filename);
        return 0;
    }
    GLuint shader = C89GL_glCreateShader(type);
    C89GL_glShaderSource(shader, 1, (const char**)&source, NULL);
    C89GL_glCompileShader(shader);
    free(source);
    GLint status;
    C89GL_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        C89GL_glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("Shader %s compilation error:\n%s\n", filename, log);
        C89GL_glDeleteShader(shader);
        return 0;
    }
    printf("Shader %s compiled successfully (status=%d)\n", filename, status);
    return shader;
}

/* ---------------------------------------------------------------------------
   Internal draw: upload VBO, set uniforms, draw (binds FBO)
   --------------------------------------------------------------------------- */
static void draw_one_triangle(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    float vertices[30];
    int idx = 0;
#define PACK_VERTEX(pos, norm, local) \
    vertices[idx++] = pos.position.x; vertices[idx++] = pos.position.y; vertices[idx++] = pos.position.z; \
    vertices[idx++] = norm.position.x; vertices[idx++] = norm.position.y; vertices[idx++] = norm.position.z; \
    vertices[idx++] = local.position.x; vertices[idx++] = local.position.y; vertices[idx++] = local.position.z; \
    vertices[idx++] = 1.0f;
    PACK_VERTEX(v0, n0, l0);
    PACK_VERTEX(v1, n1, l1);
    PACK_VERTEX(v2, n2, l2);

    /* ---- Upload ---- */
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    C89GL_glUseProgram(g_program);

    /* ---- Material uniforms ---- */
    C89GL_glUniform3fv(u_mat_color, 1, (float*)&mat->color);
    C89GL_glUniform3fv(u_mat_tint, 1, (float*)&mat->tint);
    C89GL_glUniform1f(u_mat_alpha, mat->alpha);
    C89GL_glUniform1i(u_mat_mode, mat->mode);
    C89GL_glUniform1i(u_mat_effects, mat->effects);
    C89GL_glUniform3fv(u_mat_emissive_color, 1, (float*)&mat->emissive_color);
    C89GL_glUniform1f(u_mat_emissive_pulse_amp, mat->emissive_pulse_amplitude);
    C89GL_glUniform1f(u_mat_emissive_pulse_freq, mat->emissive_pulse_frequency);
    C89GL_glUniform1f(u_mat_emissive_pulse_phase, mat->emissive_pulse_phase);
    C89GL_glUniform1f(u_mat_specular_exponent, mat->specular_exponent);
    C89GL_glUniform3fv(u_mat_specular_color, 1, (float*)&mat->specular_color);
    C89GL_glUniform1f(u_mat_specular_threshold, mat->specular_threshold);
    C89GL_glUniform3fv(u_mat_rim_color, 1, (float*)&mat->rim_color);
    C89GL_glUniform1f(u_mat_rim_exponent, mat->rim_exponent);
    C89GL_glUniform3fv(u_mat_fresnel_color, 1, (float*)&mat->fresnel_color);
    C89GL_glUniform1f(u_mat_fresnel_exponent, mat->fresnel_exponent);
    C89GL_glUniform3fv(u_mat_gooch_cool, 1, (float*)&mat->gooch_cool);
    C89GL_glUniform3fv(u_mat_gooch_warm, 1, (float*)&mat->gooch_warm);
    C89GL_glUniform1f(u_mat_ambient_light_factor, mat->ambient_light_factor);
    C89GL_glUniform1f(u_mat_oren_nayar_sigma, mat->oren_nayar_sigma);
    C89GL_glUniform1f(u_mat_minnaert_k, mat->minnaert_k);
    C89GL_glUniform1f(u_mat_saturation, mat->saturation);
    C89GL_glUniform1f(u_mat_iridescence_strength, mat->iridescence_strength);
    C89GL_glUniform3fv(u_mat_back_glow_color, 1, (float*)&mat->back_glow_color);
    C89GL_glUniform1f(u_mat_bump_amplitude, mat->bump_amplitude);
    C89GL_glUniform1f(u_mat_bump_frequency, mat->bump_frequency);
    C89GL_glUniform1f(u_mat_bump_speed, mat->bump_speed);
    C89GL_glUniform1f(u_mat_roughness, mat->roughness);
    C89GL_glUniform1f(u_mat_fringe_intensity, mat->fringe_intensity);
    C89GL_glUniform1i(u_mat_cel_bands, mat->cel_bands);
    C89GL_glUniform1f(u_mat_glitch_intensity, mat->glitch_intensity);
    C89GL_glUniform1i(u_mat_posterize_levels, mat->posterize_levels);
    C89GL_glUniform3fv(u_mat_strobe_color, 1, (float*)&mat->strobe_color);
    C89GL_glUniform1f(u_mat_strobe_frequency, mat->strobe_frequency);
    C89GL_glUniform1f(u_mat_strobe_phase, mat->strobe_phase);

    /* ---- Draw ---- */
    if (mat->mode == SHADE_WIREFRAME) {
        C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        C89GL_glBindVertexArray(g_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
        C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
        C89GL_glBindVertexArray(g_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    C89GL_glBindVertexArray(0);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* ---------------------------------------------------------------------------
   Transparent sort comparator (back‑to‑front)
   --------------------------------------------------------------------------- */
static int transparent_compare(const void* a, const void* b) {
    const transparent_tri_t* ta = (const transparent_tri_t*)a;
    const transparent_tri_t* tb = (const transparent_tri_t*)b;
    if (ta->depth > tb->depth) return -1;   /* far first */
    if (ta->depth < tb->depth) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
   Flush transparent buffer (draw all sorted transparent triangles)
   --------------------------------------------------------------------------- */
static void flush_transparent_tris(void) {
    if (g_transparent_count == 0) return;

    /* Sort far -> near */
    qsort(g_transparent_tris, g_transparent_count, sizeof(transparent_tri_t), transparent_compare);

    /* Disable depth writes, enable blending (already enabled in init) */
    C89GL_glDepthMask(GL_FALSE);

    for (i32 i = 0; i < g_transparent_count; i++) {
        transparent_tri_t* t = &g_transparent_tris[i];
        draw_one_triangle(t->v0, t->v1, t->v2,
                          t->n0, t->n1, t->n2,
                          t->l0, t->l1, t->l2,
                          t->mat);
    }

    C89GL_glDepthMask(GL_TRUE);
    g_transparent_count = 0;
}

/* ---------------------------------------------------------------------------
   Bind the FBO (set viewport, render target)
   --------------------------------------------------------------------------- */
static void bind_fbo(void) {
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    C89GL_glViewport(0, 0, g_render_width, g_render_height);
}

/* ---------------------------------------------------------------------------
   render_init
   --------------------------------------------------------------------------- */
int render_init(i32 window_width, i32 window_height, C89GL_Context* ctx) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (g_program) {
        printf("render_init: g_program already exists, returning 1\n");
        return 1;
    }

    g_swap_ctx = ctx;
    g_win_width = window_width;
    g_win_height = window_height;
    g_render_width = window_width;
    g_render_height = window_height;
    g_transparent_count = 0;

    /* ---- Load shaders ---- */
    printf("Compiling vertex shader...\n");
    GLuint vs = compile_shader_file(GL_VERTEX_SHADER, "render.vert");
    printf("Compiling geometry shader...\n");
    GLuint gs = compile_shader_file(GL_GEOMETRY_SHADER, "render.geom");
    printf("Compiling fragment shader...\n");
    GLuint fs = compile_shader_file(GL_FRAGMENT_SHADER, "render.frag");
    if (!vs || !gs  || !fs) {
        if (vs) C89GL_glDeleteShader(vs);
        if (gs) C89GL_glDeleteShader(gs);
        if (fs) C89GL_glDeleteShader(fs);
        printf("ERROR: One or more shaders failed to compile\n");
        return 0;
    }
    printf("All shaders compiled OK\n");

    printf("Creating program...\n");
    g_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(g_program, vs);
    C89GL_glAttachShader(g_program, gs);
    C89GL_glAttachShader(g_program, fs);
    C89GL_glLinkProgram(g_program);

    GLint link_status;
    C89GL_glGetProgramiv(g_program, GL_LINK_STATUS, &link_status);
    printf("Program link status: %d\n", link_status);
    if (!link_status) {
        char log[512];
        C89GL_glGetProgramInfoLog(g_program, sizeof(log), NULL, log);
        printf("Program link error:\n%s\n", log);
        C89GL_glDeleteProgram(g_program);
        g_program = 0;
        return 0;
    }

    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(gs);
    C89GL_glDeleteShader(fs);

    printf("Getting uniform locations...\n");
    u_view_proj     = C89GL_glGetUniformLocation(g_program, "uViewProj");
    u_light_dir     = C89GL_glGetUniformLocation(g_program, "uLightDir");
    u_light_col     = C89GL_glGetUniformLocation(g_program, "uLightCol");
    u_ambient_col   = C89GL_glGetUniformLocation(g_program, "uAmbientCol");
    u_cam_eye       = C89GL_glGetUniformLocation(g_program, "uCamEye");
    u_time          = C89GL_glGetUniformLocation(g_program, "uTime");
    u_fog_color     = C89GL_glGetUniformLocation(g_program, "uFogColor");
    u_fog_start     = C89GL_glGetUniformLocation(g_program, "uFogStart");
    u_fog_end       = C89GL_glGetUniformLocation(g_program, "uFogEnd");

    u_mat_color          = C89GL_glGetUniformLocation(g_program, "uMatColor");
    u_mat_tint           = C89GL_glGetUniformLocation(g_program, "uMatTint");
    u_mat_alpha          = C89GL_glGetUniformLocation(g_program, "uMatAlpha");
    u_mat_mode           = C89GL_glGetUniformLocation(g_program, "uMatMode");
    u_mat_effects        = C89GL_glGetUniformLocation(g_program, "uMatEffects");
    u_mat_emissive_color = C89GL_glGetUniformLocation(g_program, "uMatEmissiveColor");
    u_mat_emissive_pulse_amp   = C89GL_glGetUniformLocation(g_program, "uMatEmissivePulseAmplitude");
    u_mat_emissive_pulse_freq  = C89GL_glGetUniformLocation(g_program, "uMatEmissivePulseFrequency");
    u_mat_emissive_pulse_phase = C89GL_glGetUniformLocation(g_program, "uMatEmissivePulsePhase");
    u_mat_specular_exponent    = C89GL_glGetUniformLocation(g_program, "uMatSpecularExponent");
    u_mat_specular_color       = C89GL_glGetUniformLocation(g_program, "uMatSpecularColor");
    u_mat_specular_threshold   = C89GL_glGetUniformLocation(g_program, "uMatSpecularThreshold");
    u_mat_rim_color            = C89GL_glGetUniformLocation(g_program, "uMatRimColor");
    u_mat_rim_exponent         = C89GL_glGetUniformLocation(g_program, "uMatRimExponent");
    u_mat_fresnel_color        = C89GL_glGetUniformLocation(g_program, "uMatFresnelColor");
    u_mat_fresnel_exponent     = C89GL_glGetUniformLocation(g_program, "uMatFresnelExponent");
    u_mat_gooch_cool           = C89GL_glGetUniformLocation(g_program, "uMatGoochCool");
    u_mat_gooch_warm           = C89GL_glGetUniformLocation(g_program, "uMatGoochWarm");
    u_mat_ambient_light_factor = C89GL_glGetUniformLocation(g_program, "uMatAmbientLightFactor");
    u_mat_oren_nayar_sigma     = C89GL_glGetUniformLocation(g_program, "uMatOrenNayarSigma");
    u_mat_minnaert_k           = C89GL_glGetUniformLocation(g_program, "uMatMinnaertK");
    u_mat_saturation           = C89GL_glGetUniformLocation(g_program, "uMatSaturation");
    u_mat_iridescence_strength = C89GL_glGetUniformLocation(g_program, "uMatIridescenceStrength");
    u_mat_back_glow_color      = C89GL_glGetUniformLocation(g_program, "uMatBackGlowColor");
    u_mat_bump_amplitude       = C89GL_glGetUniformLocation(g_program, "uMatBumpAmplitude");
    u_mat_bump_frequency       = C89GL_glGetUniformLocation(g_program, "uMatBumpFrequency");
    u_mat_bump_speed           = C89GL_glGetUniformLocation(g_program, "uMatBumpSpeed");
    u_mat_roughness            = C89GL_glGetUniformLocation(g_program, "uMatRoughness");
    u_mat_fringe_intensity     = C89GL_glGetUniformLocation(g_program, "uMatFringeIntensity");
    u_mat_cel_bands            = C89GL_glGetUniformLocation(g_program, "uMatCelBands");
    u_mat_glitch_intensity     = C89GL_glGetUniformLocation(g_program, "uMatGlitchIntensity");
    u_mat_posterize_levels     = C89GL_glGetUniformLocation(g_program, "uMatPosterizeLevels");
    u_mat_strobe_color         = C89GL_glGetUniformLocation(g_program, "uMatStrobeColor");
    u_mat_strobe_frequency     = C89GL_glGetUniformLocation(g_program, "uMatStrobeFrequency");
    u_mat_strobe_phase         = C89GL_glGetUniformLocation(g_program, "uMatStrobePhase");

    printf("Uniform locations:\n");
    printf("  u_ambient_col: %d\n", u_ambient_col);
    printf("  u_light_dir: %d\n", u_light_dir);
    printf("  u_light_col: %d\n", u_light_col);
    printf("  u_mat_ambient_light_factor: %d\n", u_mat_ambient_light_factor);
    printf("  u_mat_color: %d\n", u_mat_color);
    printf("  u_mat_effects: %d\n", u_mat_effects);
    printf("  u_view_proj: %d\n", u_view_proj);

    /* ---- Create VAO and VBO ---- */
    printf("Creating VAO/VBO...\n");
    C89GL_glGenVertexArrays(1, &g_vao);
    C89GL_glBindVertexArray(g_vao);
    C89GL_glGenBuffers(1, &g_vbo);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    C89GL_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    C89GL_glEnableVertexAttribArray(0);
    C89GL_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(1);
    C89GL_glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(2);
    C89GL_glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(9 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(3);

    C89GL_glBindVertexArray(0);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* ---- Create FBO for upscaling ---- */
    printf("Creating FBO...\n");
    g_default_fbo = 0;
    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_default_fbo);

    C89GL_glGenFramebuffers(1, &g_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

    /* Color texture */
    C89GL_glGenTextures(1, &g_color_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, g_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_render_width, g_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_color_tex, 0);

    /* Depth renderbuffer */
    C89GL_glGenRenderbuffers(1, &g_depth_rb);
    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_render_width, g_render_height);
    C89GL_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth_rb);

    GLenum fbo_status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO status: 0x%x\n", fbo_status);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO incomplete!\n");
        return 0;
    }
    printf("FBO complete.\n");

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_default_fbo);

    /* ---- Global GL state ---- */
    printf("Setting global GL state...\n");
    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    C89GL_glEnable(GL_CULL_FACE);
    C89GL_glFrontFace(GL_CCW);

    printf("render_init returning 1 (success)\n");
    return 1;
}

/* ---------------------------------------------------------------------------
   render_set_render_resolution – change internal render target size
   --------------------------------------------------------------------------- */
void render_set_render_resolution(i32 render_width, i32 render_height) {
    if (render_width <= 0 || render_height <= 0) return;
    if (g_render_width == render_width && g_render_height == render_height) return;

    g_render_width = render_width;
    g_render_height = render_height;

    /* Resize FBO textures */
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

    C89GL_glBindTexture(GL_TEXTURE_2D, g_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_render_width, g_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_render_width, g_render_height);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_default_fbo);

    /* If we are currently bound to the FBO (i.e., during drawing), we need to re-bind */
    /* That will happen on the next draw/clear call. */
}

/* ---------------------------------------------------------------------------
   render_get_render_width / render_get_render_height
   --------------------------------------------------------------------------- */
i32 render_get_render_width(void) {
    return g_render_width;
}
i32 render_get_render_height(void) {
    return g_render_height;
}

/* ---------------------------------------------------------------------------
   render_shutdown
   --------------------------------------------------------------------------- */
void render_shutdown(void) {
    if (g_program) { C89GL_glDeleteProgram(g_program); g_program = 0; }
    if (g_vbo) { C89GL_glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_vao) { C89GL_glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_fbo) { C89GL_glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_color_tex) { C89GL_glDeleteTextures(1, &g_color_tex); g_color_tex = 0; }
    if (g_depth_rb) { C89GL_glDeleteRenderbuffers(1, &g_depth_rb); g_depth_rb = 0; }
    g_swap_ctx = NULL;
    g_transparent_count = 0;
}

/* ---------------------------------------------------------------------------
   Scene uniforms
   --------------------------------------------------------------------------- */
void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    if (!g_program) return;
    C89GL_glUseProgram(g_program);
    C89GL_glUniform3fv(u_light_dir, 1, (float*)&dir);
    C89GL_glUniform3fv(u_light_col, 1, (float*)&col);
    C89GL_glUniform3fv(u_ambient_col, 1, (float*)&amb);

    /* DEBUG: Print values being uploaded 
    printf("Uploaded light:\n");
    printf("  Dir: (%f, %f, %f)\n", dir.position.x, dir.position.y, dir.position.z);
    printf("  Col: (%f, %f, %f)\n", col.position.x, col.position.y, col.position.z);
    printf("  Amb: (%f, %f, %f)\n", amb.position.x, amb.position.y, amb.position.z); */
}

void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    if (!g_program) return;

    g_cam_eye = eye;

    g_view = mat4_lookat(eye, center, up);
    g_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    g_view_proj = mat4_mul(g_proj, g_view);

    extract_frustum_planes();

    C89GL_glUseProgram(g_program);
    C89GL_glUniformMatrix4fv(u_view_proj, 1, GL_TRUE, (float*)&g_view_proj);
    C89GL_glUniform3fv(u_cam_eye, 1, (float*)&eye);
}

void render_set_fog(vec3 color, real start, real end) {
    if (!g_program) return;
    C89GL_glUseProgram(g_program);
    C89GL_glUniform3fv(u_fog_color, 1, (float*)&color);
    C89GL_glUniform1f(u_fog_start, start);
    C89GL_glUniform1f(u_fog_end, end);
}

void render_set_time(real t) {
    if (!g_program) return;
    C89GL_glUseProgram(g_program);
    C89GL_glUniform1f(u_time, t);
}

void render_clear(u8 r, u8 g, u8 b) {
    render_clear_color(r / 255.0f, g / 255.0f, b / 255.0f);
}

void render_clear_color(real r, real g, real b) {
    if (!g_fbo) return;
    bind_fbo();
    C89GL_glClearColor(r, g, b, 1.0f);
    C89GL_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* ---------------------------------------------------------------------------
   draw_triangle_shaded – full pipeline
   --------------------------------------------------------------------------- */
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    if (!g_program || !g_vao) return;

    /* ---- 1. Frustum culling ---- */
    if (triangle_outside_frustum(v0, v1, v2)) {
        return;
    }

    /* ---- 2. Backface culling (GPU hardware) ---- */
    if (mat->double_sided) {
        C89GL_glDisable(GL_CULL_FACE);
    } else {
        C89GL_glEnable(GL_CULL_FACE);
    }

    /* ---- 3. Ensure FBO is bound ---- */
    bind_fbo();

    /* ---- 4. Transparent handling ---- */
    if (mat->effects & EFFECT_ALPHA) {
        if (g_transparent_count >= MAX_TRANSPARENT_TRIS) {
            flush_transparent_tris();
        }

        vec3 center = vec3_add(vec3_add(v0, v1), v2);
        center = vec3_div_scalar(center, 3.0f);
        vec4 temp_vec4 = vec4_init_from_4(center.position.x, center.position.y, center.position.z, 1.0f);
        vec4 view_center = mat4_mul_vec4(g_view, temp_vec4);
        float depth = -view_center.position.z;

        transparent_tri_t* tri = &g_transparent_tris[g_transparent_count++];
        tri->v0 = v0; tri->v1 = v1; tri->v2 = v2;
        tri->n0 = n0; tri->n1 = n1; tri->n2 = n2;
        tri->l0 = l0; tri->l1 = l1; tri->l2 = l2;
        tri->mat = mat;
        tri->depth = depth;
        return;
    }

    /* ---- 5. Opaque / Wireframe: draw immediately ---- */
    draw_one_triangle(v0, v1, v2, n0, n1, n2, l0, l1, l2, mat);
}

/* ---------------------------------------------------------------------------
   render_finish – upscale (blit FBO to screen) and swap buffers
   --------------------------------------------------------------------------- */
void render_finish(void) {
    flush_transparent_tris();

    /* ---- Upscale: blit FBO to default framebuffer with nearest neighbour ---- */
    C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_default_fbo);
    C89GL_glBlitFramebuffer(
        0, 0, g_render_width, g_render_height,
        0, 0, g_win_width, g_win_height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    /* ---- Swap buffers ---- */
    if (g_swap_ctx) {
        C89GL_swap_buffers(g_swap_ctx);
    }
}

/* ---------------------------------------------------------------------------
   render_get_fb – returns NULL in GPU mode
   --------------------------------------------------------------------------- */
const u32* render_get_fb(void) {
    return NULL;
}

/* ---------------------------------------------------------------------------
   render_resize – update window size (viewport for the blit)
   --------------------------------------------------------------------------- */
int render_resize(i32 new_w, i32 new_h) {
    g_win_width = new_w;
    g_win_height = new_h;
    /* The FBO size stays the same; only the blit target changes */
    return 0;
}

#endif /* RASTERIZER_IMPLEMENTATION */