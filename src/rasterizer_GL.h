/*
 * rasterizer_GL.h – GPU‑accelerated renderer (OpenGL 3.3+) with batching
 *
 * Supports: Wireframe, Flat, Gouraud, Phong, Quadratic, Cubic.
 * All shading modes run on the GPU, including Quadratic/Cubic.
 *
 * FEATURES:
 *   - Frustum culling (CPU)
 *   - Backface culling (CPU, per‑triangle – avoids state changes)
 *   - Transparent sorting (CPU buffer, back‑to‑front) + batching by material
 *   - **Upscaling** – render at a lower internal resolution
 *     and stretch to the window with nearest‑neighbour filtering.
 *   - **Batching** – one draw call per material, not per triangle.
 *   - **No geometry shader** – uses dFdx/dFdy for flat shading.
 *   - **Entity‑level sorting** for transparent objects – eliminates flicker.
 *
 * Shaders are loaded from external files:
 *   - render.vert  (vertex shader)
 *   - render.frag  (fragment shader)
 *
 * Usage:
 *   #define RASTERIZER_GL_IMPLEMENTATION
 *   #include "rasterizer_GL.h"
 *
 *   render_init(window_width, window_height);   // window size
 *   render_set_render_resolution(512, 288);     // optional: lower internal res
 *   ... draw ...
 *   render_finish();  // automatically upscales and swaps buffers
 */

#ifndef RASTERIZER_GL_H
#define RASTERIZER_GL_H

#include "common.h"
#include "tags/material.h"
#include "tags/entity.h"
#include "tags/model.h"

#define C89GL_IMPLEMENTATION
#include "../libs/C89FW/C89GL.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public API ---- */

/* Initialize renderer with the given window (creates its own GL context) */
int  render_init(i32 window_width, i32 window_height);

void render_shutdown(void);
void render_set_light(vec3 dir, vec3 col, vec3 amb);
void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect);
void render_set_fog(vec3 color, real start, real end);
void render_set_time(real t);
void render_clear(u8 r, u8 g, u8 b);
void render_clear_color(real r, real g, real b);

/* Legacy per‑triangle draw (backward compatible) */
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat
);

/* Entity‑level draw – uses entity depth for stable transparent sorting */
void render_draw_entity(const struct entity_definition *ent);

/* Batch draw – sorts entities by distance before drawing */
void render_draw_entities(struct entity_definition **entities, int count);

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

#endif /* RASTERIZER_GL_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#ifdef RASTERIZER_GL_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
   Internal state
   --------------------------------------------------------------------------- */

/* ---- GL context (owned by the rasterizer) ---- */
static C89GL_Context g_gl_ctx;

/* ---- Window state ---- */
static i32 g_win_width  = 0;
static i32 g_win_height = 0;
static i32 g_render_width  = 0;
static i32 g_render_height = 0;

/* ---- GL program and shader handles ---- */
static GLuint g_program = 0;

/* ---- Frustum culling state ---- */
#define FRUSTUM_PLANES 6
typedef struct {
    vec3 normal;
    real d;
} frustum_plane_t;

static frustum_plane_t g_frustum[FRUSTUM_PLANES];
static mat4 g_view, g_proj, g_view_proj;
static vec3 g_cam_eye;

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

/* ---- VAO / VBO (batch VBO) ---- */
static GLuint g_vao = 0;
static GLuint g_batch_vbo = 0;
static size_t g_batch_vbo_capacity_bytes = 0;

/* ---- FBO (for upscaling) ---- */
static GLuint g_fbo = 0;
static GLuint g_color_tex = 0;
static GLuint g_depth_rb = 0;
static GLint g_default_fbo = 0;

/* ---- Batching state ---- */
#define MAX_BATCH_VERTICES  (1024 * 1024)   /* 1M vertices = ~333k triangles */
#define MAX_BATCHES         128
#define MAX_TRANSPARENT_TRIS 8192

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    vec3 l0, l1, l2;
    const struct material_definition *mat;
    float depth;          /* farthest triangle depth (for internal sorting) */
    float entity_depth;   /* distance of the entity from camera */
    int   id;             /* unique ID per frame for stable sorting */
} transparent_tri_t;

typedef struct {
    const struct material_definition *mat;
    size_t vertex_offset;   /* offset into g_vertex_pool (in floats) */
    int    vertex_count;
    int    mode;            /* SHADE_WIREFRAME, FLAT, GOURAUD, PHONG */
    int    is_transparent;
} batch_t;

static float *g_vertex_pool = NULL;
static size_t g_pool_capacity_floats = 0;
static size_t g_pool_used_floats = 0;

static batch_t g_batches[MAX_BATCHES];
static int g_batch_count = 0;

static transparent_tri_t g_transparent_tris[MAX_TRANSPARENT_TRIS];
static i32 g_transparent_count = 0;
static i32 g_transparent_triangle_id = 0;   /* increments per transparent triangle */

/* Helper for sorting entities by depth */
typedef struct {
    struct entity_definition *ent;
    float depth;
} entity_sort_t;

/* Comparator for qsort – farthest first */
static int entity_sort_compare(const void* a, const void* b) {
    const entity_sort_t *sa = (const entity_sort_t*)a;
    const entity_sort_t *sb = (const entity_sort_t*)b;
    if (sa->depth > sb->depth) return -1;
    if (sa->depth < sb->depth) return 1;
    return 0;
}

static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

/* ---- Frustum culling helpers (unchanged) ---- */
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

/* ---- Helper: read file (unchanged) ---- */
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

/* ---- Helper: compile shader (unchanged) ---- */
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

/* ---- Helper: set material uniforms (refactored) ---- */
static void set_material_uniforms(const struct material_definition *mat) {
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
}

/* ---- Transparent sort comparator (entity depth first, then triangle depth, then material, then ID) ---- */
static int transparent_compare(const void* a, const void* b) {
    const transparent_tri_t* ta = (const transparent_tri_t*)a;
    const transparent_tri_t* tb = (const transparent_tri_t*)b;
    /* Primary: entity depth (farthest first) */
    if (ta->entity_depth > tb->entity_depth) return -1;
    if (ta->entity_depth < tb->entity_depth) return 1;
    /* Secondary: triangle depth (farthest first) */
    if (ta->depth > tb->depth) return -1;
    if (ta->depth < tb->depth) return 1;
    /* Tertiary: material (group same) */
    if (ta->mat < tb->mat) return -1;
    if (ta->mat > tb->mat) return 1;
    /* Final: unique ID for stable ordering */
    return (ta->id < tb->id) ? -1 : (ta->id > tb->id) ? 1 : 0;
}

/* ---- Flush transparent triangles into the batch pool (sorted and batched) ---- */
static void flush_transparent_batches(void) {
    if (g_transparent_count == 0) return;

    qsort(g_transparent_tris, g_transparent_count, sizeof(transparent_tri_t), transparent_compare);

    {
        int i = 0;
        while (i < g_transparent_count) {
            const material_definition *mat = g_transparent_tris[i].mat;
            int start = i;
            while (i < g_transparent_count && g_transparent_tris[i].mat == mat) i++;
            {
                int count = i - start;
                if (g_batch_count < MAX_BATCHES) {
                    batch_t *b = &g_batches[g_batch_count++];
                    int j;
                    b->mat = mat;
                    b->mode = mat->mode;
                    b->vertex_offset = g_pool_used_floats;
                    b->vertex_count = 0;
                    b->is_transparent = 1;

                    for (j = start; j < i; j++) {
                        transparent_tri_t *t = &g_transparent_tris[j];
                        /* Ensure pool capacity */
                        if (g_pool_used_floats + 30 > g_pool_capacity_floats) {
                            size_t new_cap = g_pool_capacity_floats ? g_pool_capacity_floats * 2 : 1024 * 10;
                            float *new_pool = (float*)realloc(g_vertex_pool, new_cap * sizeof(float));
                            if (!new_pool) { /* out of memory – abort */ return; }
                            g_vertex_pool = new_pool;
                            g_pool_capacity_floats = new_cap;
                        }
                        {
                            float *ptr = &g_vertex_pool[g_pool_used_floats];
                            #define PACK_V(v, n, l) \
                                *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
                                *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
                                *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
                                *(ptr++) = 1.0f;
                            PACK_V(t->v0, t->n0, t->l0);
                            PACK_V(t->v1, t->n1, t->l1);
                            PACK_V(t->v2, t->n2, t->l2);
                            #undef PACK_V
                            g_pool_used_floats += 30;
                            b->vertex_count += 3;
                        }
                    }
                }
            }
        }
    }
    g_transparent_count = 0;
    g_transparent_triangle_id = 0;   /* reset for next frame */
}

/* ---- Bind FBO ---- */
static void bind_fbo(void) {
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    C89GL_glViewport(0, 0, g_render_width, g_render_height);
}

/* ================================================================
   INTERNAL DRAW HELPER – takes entity depth for transparent sorting
   ================================================================ */
static void draw_triangle_internal(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat,
    float entity_depth)
{
    /* Frustum culling */
    if (triangle_outside_frustum(v0, v1, v2)) return;

    /* Backface culling (CPU) */
    if (!mat->double_sided) {
        vec3 fn = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 center = vec3_div_scalar(vec3_add(vec3_add(v0, v1), v2), 3.0f);
        if (vec3_dot(fn, vec3_sub(g_cam_eye, center)) <= 0.0f) return;
    }

    /* Transparent: store for later sorting (using entity depth) */
    if (mat->effects & EFFECT_ALPHA) {
        if (g_transparent_count >= MAX_TRANSPARENT_TRIS) {
            flush_transparent_batches();
        }
        {
            transparent_tri_t *t = &g_transparent_tris[g_transparent_count++];
            t->v0 = v0; t->v1 = v1; t->v2 = v2;
            t->n0 = n0; t->n1 = n1; t->n2 = n2;
            t->l0 = l0; t->l1 = l1; t->l2 = l2;
            t->mat = mat;
            t->entity_depth = entity_depth;
            t->id = g_transparent_triangle_id++;

            /* Compute view-space depths for each vertex (farthest depth) */
            {
                vec4 c0 = mat4_mul_vec4(g_view, vec4_init_from_4(v0.position.x, v0.position.y, v0.position.z, 1.0f));
                vec4 c1 = mat4_mul_vec4(g_view, vec4_init_from_4(v1.position.x, v1.position.y, v1.position.z, 1.0f));
                vec4 c2 = mat4_mul_vec4(g_view, vec4_init_from_4(v2.position.x, v2.position.y, v2.position.z, 1.0f));
                float d0 = -c0.position.z;
                float d1 = -c1.position.z;
                float d2 = -c2.position.z;
                t->depth = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
            }
        }
        return;
    }

    /* Opaque: find or create batch */
    {
        int batch_idx = -1;
        int i;
        for (i = 0; i < g_batch_count; i++) {
            if (g_batches[i].mat == mat && g_batches[i].mode == mat->mode) {
                batch_idx = i;
                break;
            }
        }
        if (batch_idx == -1) {
            if (g_batch_count >= MAX_BATCHES) {
                render_finish(); /* force flush */
                return;
            }
            batch_idx = g_batch_count++;
            g_batches[batch_idx].mat = mat;
            g_batches[batch_idx].mode = mat->mode;
            g_batches[batch_idx].vertex_offset = g_pool_used_floats;
            g_batches[batch_idx].vertex_count = 0;
            g_batches[batch_idx].is_transparent = 0;
        }

        {
            batch_t *b = &g_batches[batch_idx];
            /* Ensure pool capacity */
            if (g_pool_used_floats + 30 > g_pool_capacity_floats) {
                size_t new_cap = g_pool_capacity_floats ? g_pool_capacity_floats * 2 : 1024 * 10;
                float *new_pool = (float*)realloc(g_vertex_pool, new_cap * sizeof(float));
                if (!new_pool) {
                    render_finish();
                    return;
                }
                g_vertex_pool = new_pool;
                g_pool_capacity_floats = new_cap;
            }

            {
                float *ptr = &g_vertex_pool[g_pool_used_floats];
                #define PACK_V(v, n, l) \
                    *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
                    *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
                    *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
                    *(ptr++) = 1.0f;

                PACK_V(v0, n0, l0);
                PACK_V(v1, n1, l1);
                PACK_V(v2, n2, l2);
                #undef PACK_V

                g_pool_used_floats += 30;
                b->vertex_count += 3;
            }
        }
    }
}

/* ================================================================
   PUBLIC API IMPLEMENTATION
   ================================================================ */

int render_init(i32 window_width, i32 window_height) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (g_program) {
        printf("render_init: g_program already exists, returning 1\n");
        return 1;
    }

    g_win_width = window_width;
    g_win_height = window_height;
    g_render_width = window_width;
    g_render_height = window_height;
    g_transparent_count = 0;
    g_batch_count = 0;
    g_pool_used_floats = 0;

    /* ---- Create GL context from the window ---- */
    if (!C89GL_create_context(window_get(), &g_gl_ctx)) {
        printf("ERROR: Failed to create OpenGL context\n");
        return 0;
    }
    C89GL_make_current(&g_gl_ctx);
    if (!C89GL_load_functions()) {
        printf("ERROR: Failed to load OpenGL functions\n");
        return 0;
    }

    printf("OpenGL version: %s\n", C89GL_glGetString(GL_VERSION));
    printf("OpenGL vendor: %s\n", C89GL_glGetString(GL_VENDOR));
    printf("OpenGL renderer: %s\n", C89GL_glGetString(GL_RENDERER));

    /* ---- Load shaders (vertex + fragment, no geometry) ---- */
    {
        GLuint vs, fs;
        printf("Compiling vertex shader...\n");
        vs = compile_shader_file(GL_VERTEX_SHADER, "render.vert");
        printf("Compiling fragment shader...\n");
        fs = compile_shader_file(GL_FRAGMENT_SHADER, "render.frag");
        if (!vs || !fs) {
            if (vs) C89GL_glDeleteShader(vs);
            if (fs) C89GL_glDeleteShader(fs);
            printf("ERROR: One or more shaders failed to compile\n");
            return 0;
        }

        printf("Creating program...\n");
        g_program = C89GL_glCreateProgram();
        C89GL_glAttachShader(g_program, vs);
        C89GL_glAttachShader(g_program, fs);
        C89GL_glLinkProgram(g_program);

        {
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
        }

        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);
    }

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

    /* ---- Create VAO and VBO (batch VBO) ---- */
    printf("Creating VAO/VBO...\n");
    C89GL_glGenVertexArrays(1, &g_vao);
    C89GL_glBindVertexArray(g_vao);

    C89GL_glGenBuffers(1, &g_batch_vbo);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER, 1, NULL, GL_DYNAMIC_DRAW);
    g_batch_vbo_capacity_bytes = 0;

    /* Vertex attribs: 10 floats per vertex (pos, normal, local, pad) */
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

    /* ---- FBO (for upscaling) ---- */
    printf("Creating FBO...\n");
    g_default_fbo = 0;
    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_default_fbo);

    C89GL_glGenFramebuffers(1, &g_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

    C89GL_glGenTextures(1, &g_color_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, g_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_render_width, g_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_color_tex, 0);

    C89GL_glGenRenderbuffers(1, &g_depth_rb);
    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_render_width, g_render_height);
    C89GL_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth_rb);

    {
        GLenum fbo_status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("FBO status: 0x%x\n", fbo_status);
        if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
            printf("FBO incomplete!\n");
            return 0;
        }
        printf("FBO complete.\n");
    }

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

void render_shutdown(void) {
    if (g_program) { C89GL_glDeleteProgram(g_program); g_program = 0; }
    if (g_batch_vbo) { C89GL_glDeleteBuffers(1, &g_batch_vbo); g_batch_vbo = 0; }
    if (g_vao) { C89GL_glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_fbo) { C89GL_glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_color_tex) { C89GL_glDeleteTextures(1, &g_color_tex); g_color_tex = 0; }
    if (g_depth_rb) { C89GL_glDeleteRenderbuffers(1, &g_depth_rb); g_depth_rb = 0; }
    if (g_vertex_pool) { free(g_vertex_pool); g_vertex_pool = NULL; }
    if (g_gl_ctx.initialized) { C89GL_destroy_context(&g_gl_ctx); }
    g_transparent_count = 0;
    g_batch_count = 0;
    g_pool_used_floats = 0;
}

void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    if (!g_program) return;
    C89GL_glUseProgram(g_program);
    C89GL_glUniform3fv(u_light_dir, 1, (float*)&dir);
    C89GL_glUniform3fv(u_light_col, 1, (float*)&col);
    C89GL_glUniform3fv(u_ambient_col, 1, (float*)&amb);
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

/* ---- Legacy per‑triangle draw (computes centroid depth for compatibility) ---- */
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    if (!g_program) return;
    /* Compute centroid depth in view space */
    {
        vec3 center = vec3_div_scalar(vec3_add(vec3_add(v0, v1), v2), 3.0f);
        vec4 c = mat4_mul_vec4(g_view, vec4_init_from_4(center.position.x, center.position.y, center.position.z, 1.0f));
        float entity_depth = -c.position.z;
        draw_triangle_internal(v0, v1, v2, n0, n1, n2, l0, l1, l2, mat, entity_depth);
    }
}

/* ---- Entity‑level draw (recommended for transparent objects) ---- */
void render_draw_entity(const struct entity_definition *ent) {
    if (!g_program || !ent) return;
    if (ent->model.handle < 0) return;

    {
        model_definition *mod = (model_definition*)tag_get(ent->model.handle, TAG_model);
        if (!mod) return;

        /* Compute entity depth once for all primitives */
        float entity_depth;
        {
            vec4 c = mat4_mul_vec4(g_view, vec4_init_from_4(ent->position.position.x, ent->position.position.y, ent->position.position.z, 1.0f));
            entity_depth = -c.position.z;
        }

        {
            u32 p;
            for (p = 0; p < mod->primitives.count; ++p) {
                model_primitive *prim = TAG_BLOCK_GET_ELEMENT(&mod->primitives, p, model_primitive);
                if (prim->vertices.count == 0 || prim->indices.count == 0) continue;

                material_definition *mat = NULL;
                if (prim->material_index >= 0 && mod->materials.address) {
                    tag_reference *refs = (tag_reference*)mod->materials.address;
                    i32 mat_handle = refs[prim->material_index].handle;
                    if (mat_handle >= 0)
                        mat = (material_definition*)tag_get(mat_handle, TAG_material);
                }
                if (!mat) {
                    static material_definition fallback = DEFAULT_MATERIAL_BRICK;
                    mat = &fallback;
                }

                {
                    model_vertex *verts = (model_vertex*)prim->vertices.address;
                    u16 *indices = (u16*)prim->indices.address;
                    u32 tri_count = prim->indices.count / 3;
                    u32 t;
                    for (t = 0; t < tri_count; ++t) {
                        u16 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
                        vec3 local_v0 = verts[i0].position, local_v1 = verts[i1].position, local_v2 = verts[i2].position;
                        vec3 v0, v1, v2;
                        vec3 n0, n1, n2;
                        v0 = vec3_add(ent->position, quat_rotate_vec3(ent->orientation, local_v0));
                        v1 = vec3_add(ent->position, quat_rotate_vec3(ent->orientation, local_v1));
                        v2 = vec3_add(ent->position, quat_rotate_vec3(ent->orientation, local_v2));
                        n0 = quat_rotate_vec3(ent->orientation, verts[i0].normal);
                        n1 = quat_rotate_vec3(ent->orientation, verts[i1].normal);
                        n2 = quat_rotate_vec3(ent->orientation, verts[i2].normal);

                        draw_triangle_internal(v0, v1, v2, n0, n1, n2,
                                               local_v0, local_v1, local_v2,
                                               mat, entity_depth);
                    }
                }
            }
        }
    }
}

/* ---- Batch draw – sorts entities by depth before drawing ---- */
void render_draw_entities(struct entity_definition **entities, int count) {
    if (!g_program || !entities || count <= 0) return;

    {
        entity_sort_t *sorted = (entity_sort_t*)malloc(count * sizeof(entity_sort_t));
        if (!sorted) return;

        {
            int i;
            for (i = 0; i < count; i++) {
                sorted[i].ent = entities[i];
                {
                    vec4 c = mat4_mul_vec4(g_view, vec4_init_from_4(
                        entities[i]->position.position.x,
                        entities[i]->position.position.y,
                        entities[i]->position.position.z,
                        1.0f
                    ));
                    sorted[i].depth = -c.position.z;
                }
            }
        }

        qsort(sorted, count, sizeof(entity_sort_t), entity_sort_compare);

        {
            int i;
            for (i = 0; i < count; i++) {
                render_draw_entity(sorted[i].ent);
            }
        }

        free(sorted);
    }
}

/* ================================================================
   RENDER_FINISH – upload all vertices, draw all batches, blit, swap
   ================================================================ */
void render_finish(void) {
    /* 1. Flush transparent triangles into the pool */
    flush_transparent_batches();

    /* 2. If there are batches, upload and draw them */
    if (g_batch_count > 0) {
        size_t total_bytes = g_pool_used_floats * sizeof(float);
        if (g_batch_vbo_capacity_bytes < total_bytes) {
            C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
            C89GL_glBufferData(GL_ARRAY_BUFFER, total_bytes, NULL, GL_DYNAMIC_DRAW);
            g_batch_vbo_capacity_bytes = total_bytes;
        }
        C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
        C89GL_glBufferSubData(GL_ARRAY_BUFFER, 0, total_bytes, g_vertex_pool);
        C89GL_glBindVertexArray(g_vao);

        {
            int i;
            for (i = 0; i < g_batch_count; i++) {
                batch_t *b = &g_batches[i];
                set_material_uniforms(b->mat);
                if (b->mode == SHADE_WIREFRAME) {
                    C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    C89GL_glDrawArrays(GL_TRIANGLES, (GLint)(b->vertex_offset / 10), b->vertex_count);
                    C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                } else {
                    if (b->is_transparent) {
                        C89GL_glDepthMask(GL_FALSE);
                        C89GL_glEnable(GL_BLEND);
                        C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    } else {
                        C89GL_glDepthMask(GL_TRUE);
                        C89GL_glDisable(GL_BLEND);
                    }
                    C89GL_glDrawArrays(GL_TRIANGLES, (GLint)(b->vertex_offset / 10), b->vertex_count);
                }
            }
        }
        C89GL_glBindVertexArray(0);
        C89GL_glBindBuffer(GL_ARRAY_BUFFER, 0);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glDisable(GL_BLEND);
    }

    /* 3. Blit FBO to default framebuffer (upscaling) */
    C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_default_fbo);
    C89GL_glBlitFramebuffer(
        0, 0, g_render_width, g_render_height,
        0, 0, g_win_width, g_win_height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_default_fbo);

    /* 4. Swap buffers */
    C89GL_swap_buffers(&g_gl_ctx);

    /* 5. Reset for next frame */
    g_pool_used_floats = 0;
    g_batch_count = 0;
    g_transparent_count = 0;
    g_transparent_triangle_id = 0;
}

/* ---- Other API functions (unchanged) ---- */
const u32* render_get_fb(void) { return NULL; }

int render_resize(i32 new_w, i32 new_h) {
    g_win_width = new_w;
    g_win_height = new_h;
    return 0;
}

void render_set_render_resolution(i32 render_width, i32 render_height) {
    if (render_width <= 0 || render_height <= 0) return;
    if (g_render_width == render_width && g_render_height == render_height) return;
    g_render_width = render_width;
    g_render_height = render_height;
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, g_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_render_width, g_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_render_width, g_render_height);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, g_default_fbo);
}

i32 render_get_render_width(void) { return g_render_width; }
i32 render_get_render_height(void) { return g_render_height; }

#endif /* RASTERIZER_GL_IMPLEMENTATION */