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
 *   - **Shader variants** – compiles specialised shaders per material key.
 *   - **Hash table cache** – O(1) average lookup for thousands of variants.
 *   - **Uniform Buffer Objects** – reduces per‑draw uniform upload overhead.
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

/* ---- UBO binding point ---- */
#define MATERIAL_UBO_BINDING 0

/*
 * Correct std140 layout of MaterialUniforms as seen in shader.
 * Offsets are verified to match GLSL’s std140 rules.
 * Size = 240 bytes.
 */
typedef struct {
    /* offset 0  */ float uMatColor[3];
    /* offset 12 */ float _pad0;
    /* offset 16 */ float uMatTint[3];
    /* offset 28 */ float uMatAlpha;

    /* offset 32 */ float uMatEmissiveColor[3];
    /* offset 44 */ float uMatEmissivePulseAmplitude;
    /* offset 48 */ float uMatEmissivePulseFrequency;
    /* offset 52 */ float uMatEmissivePulsePhase;
    /* offset 56 */ float uMatSpecularExponent;
    /* offset 60 */ float _pad1;
    /* offset 64 */ float uMatSpecularColor[3];
    /* offset 76 */ float uMatSpecularThreshold;

    /* offset 80 */ float uMatRimColor[3];
    /* offset 92 */ float uMatRimExponent;
    /* offset 96 */ float uMatFresnelColor[3];
    /* offset 108*/ float uMatFresnelExponent;

    /* offset 112*/ float uMatGoochCool[3];
    /* offset 124*/ float _pad2;
    /* offset 128*/ float uMatGoochWarm[3];
    /* offset 140*/ float uMatAmbientLightFactor;
    /* offset 144*/ float uMatOrenNayarSigma;
    /* offset 148*/ float uMatMinnaertK;
    /* offset 152*/ float uMatSaturation;
    /* offset 156*/ float uMatIridescenceStrength;

    /* offset 160*/ float uMatBackGlowColor[3];
    /* offset 172*/ float uMatBumpAmplitude;
    /* offset 176*/ float uMatBumpFrequency;
    /* offset 180*/ float uMatBumpSpeed;
    /* offset 184*/ float uMatRoughness;
    /* offset 188*/ float uMatFringeIntensity;
    /* offset 192*/ int   uMatCelBands;
    /* offset 196*/ float uMatGlitchIntensity;
    /* offset 200*/ int   uMatPosterizeLevels;
    /* offset 204*/ float _pad3;

    /* offset 208*/ float uMatStrobeColor[3];
    /* offset 220*/ float uMatStrobeFrequency;
    /* offset 224*/ float uMatStrobePhase;
    /* offset 228*/ float _pad4[3];   /* final padding to 240 bytes */
} material_ubo_t;

/* ---- Shader variant cache (hash table) ---- */
typedef struct {
    render_method key;
    GLuint program;
    int hit_logged;                 /* for first‑hit logging only */
    /* Non‑material uniform locations – material uniforms are in the UBO */
    GLint u_view_proj;
    GLint u_light_dir;
    GLint u_light_col;
    GLint u_ambient_col;
    GLint u_cam_eye;
    GLint u_time;
    GLint u_fog_color;
    GLint u_fog_start;
    GLint u_fog_end;
} shader_variant_t;

/* Hash table parameters */
#define SHADER_CACHE_INITIAL_SIZE 64
#define SHADER_CACHE_MAX_LOAD_FACTOR 0.7f

static shader_variant_t *g_shader_cache = NULL;
static int g_shader_cache_size = 0;
static int g_shader_cache_count = 0;
static int g_shader_compilations = 0;        /* counter for logging */

/* ---- UBO handle ---- */
static GLuint g_material_ubo = 0;

/* ---- Frustum culling state ---- */
#define FRUSTUM_PLANES 6
typedef struct {
    vec3 normal;
    real d;
} frustum_plane_t;

static frustum_plane_t g_frustum[FRUSTUM_PLANES];
static mat4 g_view, g_proj, g_view_proj;
static vec3 g_cam_eye;

/* ---- Lighting and environment globals (set by render_set_* and used later) ---- */
static vec3 g_light_dir;
static vec3 g_light_col;
static vec3 g_ambient_col;
static vec3 g_fog_color;
static real g_fog_start;
static real g_fog_end;
static real g_time;

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
    int    mode;            /* MODE_* (from render_method) */
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

/* ---- Helper: compile shader with prepended defines ---- */
static GLuint compile_shader_with_defines(GLenum type, const char* filename, const char* defines) {
    char* source = read_file(filename);
    if (!source) return 0;

    /* The defines already contain the #version directive, so we remove it from the source */
    char* source_without_version = source;
    if (strncmp(source, "#version", 8) == 0) {
        /* Find the first newline after #version and skip it */
        char* p = strchr(source, '\n');
        if (p) {
            source_without_version = p + 1;
        }
    }

    /* Combine defines + source */
    size_t def_len = strlen(defines);
    size_t src_len = strlen(source_without_version);
    char* combined = (char*)malloc(def_len + src_len + 1);
    if (!combined) { free(source); return 0; }
    strcpy(combined, defines);
    strcat(combined, source_without_version);
    free(source);

    GLuint shader = C89GL_glCreateShader(type);
    C89GL_glShaderSource(shader, 1, (const char**)&combined, NULL);
    C89GL_glCompileShader(shader);
    free(combined);

    GLint status;
    C89GL_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        C89GL_glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("Shader %s compilation error with defines:\n%s\n", filename, log);
        C89GL_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* ---- Helper: generate preprocessor defines from render_method ---- */
static void generate_defines(render_method key, char* out, size_t out_size) {
    char* p = out;
    size_t remaining = out_size;
    int n;

    /* #version must be first – we put it in the define string */
    n = snprintf(p, remaining, "#version 330 core\n");
    p += n; remaining -= n;

    /* Mode define */
    u32 mode = (u32)key & 0x7;
    switch (mode) {
        case MODE_WIREFRAME: n = snprintf(p, remaining, "#define MODE_WIREFRAME\n"); break;
        case MODE_FLAT:      n = snprintf(p, remaining, "#define MODE_FLAT\n"); break;
        case MODE_GOURAUD:   n = snprintf(p, remaining, "#define MODE_GOURAUD\n"); break;
        case MODE_QUADRATIC: n = snprintf(p, remaining, "#define MODE_QUADRATIC\n"); break;
        case MODE_CUBIC:     n = snprintf(p, remaining, "#define MODE_CUBIC\n"); break;
        case MODE_PHONG:     n = snprintf(p, remaining, "#define MODE_PHONG\n"); break;
        default:             n = snprintf(p, remaining, "#define MODE_PHONG\n"); break;
    }
    p += n; remaining -= n;

    /* Effects: we define macros for each effect that is set */
    u32 effects = (u32)key & ~0x7;  /* clear mode bits */
    if (effects & EFFECT_BUMP)            { n = snprintf(p, remaining, "#define EFFECT_BUMP\n"); p += n; remaining -= n; }
    if (effects & EFFECT_DIFFUSE_WRAP)    { n = snprintf(p, remaining, "#define EFFECT_DIFFUSE_WRAP\n"); p += n; remaining -= n; }
    if (effects & EFFECT_CEL_SHADING)     { n = snprintf(p, remaining, "#define EFFECT_CEL_SHADING\n"); p += n; remaining -= n; }
    if (effects & EFFECT_MINNAERT)        { n = snprintf(p, remaining, "#define EFFECT_MINNAERT\n"); p += n; remaining -= n; }
    if (effects & EFFECT_OREN_NAYAR)      { n = snprintf(p, remaining, "#define EFFECT_OREN_NAYAR\n"); p += n; remaining -= n; }
    if (effects & EFFECT_AMBIENT_LIGHT)   { n = snprintf(p, remaining, "#define EFFECT_AMBIENT_LIGHT\n"); p += n; remaining -= n; }
    if (effects & EFFECT_GOOCH)           { n = snprintf(p, remaining, "#define EFFECT_GOOCH\n"); p += n; remaining -= n; }
    if (effects & EFFECT_BACK_GLOW)       { n = snprintf(p, remaining, "#define EFFECT_BACK_GLOW\n"); p += n; remaining -= n; }
    if (effects & EFFECT_RIM)             { n = snprintf(p, remaining, "#define EFFECT_RIM\n"); p += n; remaining -= n; }
    if (effects & EFFECT_FRESNEL)         { n = snprintf(p, remaining, "#define EFFECT_FRESNEL\n"); p += n; remaining -= n; }
    if (effects & EFFECT_EMISSIVE)        { n = snprintf(p, remaining, "#define EFFECT_EMISSIVE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_EMISSIVE_PULSE)  { n = snprintf(p, remaining, "#define EFFECT_EMISSIVE_PULSE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_STROBE)          { n = snprintf(p, remaining, "#define EFFECT_STROBE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SPECULAR)        { n = snprintf(p, remaining, "#define EFFECT_SPECULAR\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SPECULAR_THRESH) { n = snprintf(p, remaining, "#define EFFECT_SPECULAR_THRESH\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SATURATION)      { n = snprintf(p, remaining, "#define EFFECT_SATURATION\n"); p += n; remaining -= n; }
    if (effects & EFFECT_IRIDESCENCE)     { n = snprintf(p, remaining, "#define EFFECT_IRIDESCENCE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_GLITCH)          { n = snprintf(p, remaining, "#define EFFECT_GLITCH\n"); p += n; remaining -= n; }
    if (effects & EFFECT_ROUGHNESS)       { n = snprintf(p, remaining, "#define EFFECT_ROUGHNESS\n"); p += n; remaining -= n; }
    if (effects & EFFECT_FRINGE)          { n = snprintf(p, remaining, "#define EFFECT_FRINGE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_POSTERIZE)       { n = snprintf(p, remaining, "#define EFFECT_POSTERIZE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_FOG)             { n = snprintf(p, remaining, "#define EFFECT_FOG\n"); p += n; remaining -= n; }
    if (effects & EFFECT_ALPHA)           { n = snprintf(p, remaining, "#define EFFECT_ALPHA\n"); p += n; remaining -= n; }
}

/* ---- Helper: resize the hash table ---- */
static void shader_cache_resize(int new_size) {
    shader_variant_t *old_cache = g_shader_cache;
    int old_size = g_shader_cache_size;
    int i;

    g_shader_cache = (shader_variant_t*)calloc(new_size, sizeof(shader_variant_t));
    g_shader_cache_size = new_size;
    g_shader_cache_count = 0;

    for (i = 0; i < old_size; i++) {
        if (old_cache[i].program != 0) {
            /* Rehash into new table */
            int index = (unsigned)old_cache[i].key % new_size;
            while (g_shader_cache[index].program != 0) {
                index = (index + 1) % new_size;
            }
            g_shader_cache[index] = old_cache[i];
            g_shader_cache_count++;
        }
    }
    free(old_cache);
}

/* ---- Helper: get or compile shader variant (hash table lookup) ---- */
static shader_variant_t* get_program_for_method(render_method key) {
    /* Initialise cache if not yet created */
    if (!g_shader_cache) {
        g_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        g_shader_cache = (shader_variant_t*)calloc(g_shader_cache_size, sizeof(shader_variant_t));
        g_shader_cache_count = 0;
    }

    /* Hash lookup */
    int index = (unsigned)key % g_shader_cache_size;
    while (g_shader_cache[index].program != 0) {
        if (g_shader_cache[index].key == key) {
            /* Hit – log only once */
            if (!g_shader_cache[index].hit_logged) {
                printf("[SHADER CACHE] Hit for key 0x%x (program %u)\n", (unsigned)key, g_shader_cache[index].program);
                g_shader_cache[index].hit_logged = 1;
            }
            return &g_shader_cache[index];
        }
        index = (index + 1) % g_shader_cache_size;
    }

    /* Miss – compile new variant */
    printf("[SHADER CACHE] Miss for key 0x%x – compiling new variant...\n", (unsigned)key);
    char defines[4096];
    generate_defines(key, defines, sizeof(defines));

    GLuint vs = compile_shader_with_defines(GL_VERTEX_SHADER, "render.vert", defines);
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, "render.frag", defines);
    if (!vs || !fs) {
        if (vs) C89GL_glDeleteShader(vs);
        if (fs) C89GL_glDeleteShader(fs);
        printf("[SHADER CACHE] ERROR: Failed to compile shaders for key 0x%x\n", (unsigned)key);
        return NULL;
    }

    GLuint prog = C89GL_glCreateProgram();
    C89GL_glAttachShader(prog, vs);
    C89GL_glAttachShader(prog, fs);
    C89GL_glLinkProgram(prog);

    GLint link_status;
    C89GL_glGetProgramiv(prog, GL_LINK_STATUS, &link_status);
    if (!link_status) {
        char log[512];
        C89GL_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("[SHADER CACHE] ERROR: Program link failed for key 0x%x:\n%s\n", (unsigned)key, log);
        C89GL_glDeleteProgram(prog);
        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);
        return NULL;
    }

    /* Bind the uniform block to the binding point */
    GLuint blockIndex = C89GL_glGetUniformBlockIndex(prog, "MaterialUniforms");
    if (blockIndex != GL_INVALID_INDEX) {
        C89GL_glUniformBlockBinding(prog, blockIndex, MATERIAL_UBO_BINDING);
    }

    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);

    /* Resize if load factor exceeds threshold */
    if ((float)(g_shader_cache_count + 1) / g_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        shader_cache_resize(g_shader_cache_size * 2);
        /* Recompute insertion index after resize */
        index = (unsigned)key % g_shader_cache_size;
        while (g_shader_cache[index].program != 0) {
            index = (index + 1) % g_shader_cache_size;
        }
    }

    /* Insert into the empty slot at `index` */
    shader_variant_t *entry = &g_shader_cache[index];
    entry->key = key;
    entry->program = prog;
    entry->hit_logged = 0;

    /* Query non‑material uniform locations (material uniforms are in the UBO) */
    entry->u_view_proj = C89GL_glGetUniformLocation(prog, "uViewProj");
    entry->u_light_dir = C89GL_glGetUniformLocation(prog, "uLightDir");
    entry->u_light_col = C89GL_glGetUniformLocation(prog, "uLightCol");
    entry->u_ambient_col = C89GL_glGetUniformLocation(prog, "uAmbientCol");
    entry->u_cam_eye = C89GL_glGetUniformLocation(prog, "uCamEye");
    entry->u_time = C89GL_glGetUniformLocation(prog, "uTime");
    entry->u_fog_color = C89GL_glGetUniformLocation(prog, "uFogColor");
    entry->u_fog_start = C89GL_glGetUniformLocation(prog, "uFogStart");
    entry->u_fog_end = C89GL_glGetUniformLocation(prog, "uFogEnd");

    g_shader_cache_count++;
    g_shader_compilations++;
    printf("[SHADER CACHE] Compiled new variant #%d for key 0x%x (program %u)\n",
           g_shader_compilations, (unsigned)key, prog);

    return entry;
}

/* ---- Helper: update the material UBO (FIXED alignment) ---- */
static void update_material_ubo(const material_definition *mat) {
    material_ubo_t ubo;
    memset(&ubo, 0, sizeof(ubo));  /* zero all padding */

    /* uMatColor (vec3) */
    ubo.uMatColor[0] = mat->color.position.x;
    ubo.uMatColor[1] = mat->color.position.y;
    ubo.uMatColor[2] = mat->color.position.z;

    /* uMatTint (vec3) */
    ubo.uMatTint[0] = mat->tint.position.x;
    ubo.uMatTint[1] = mat->tint.position.y;
    ubo.uMatTint[2] = mat->tint.position.z;

    ubo.uMatAlpha = mat->alpha;

    /* uMatEmissiveColor (vec3) */
    ubo.uMatEmissiveColor[0] = mat->emissive_color.position.x;
    ubo.uMatEmissiveColor[1] = mat->emissive_color.position.y;
    ubo.uMatEmissiveColor[2] = mat->emissive_color.position.z;

    ubo.uMatEmissivePulseAmplitude = mat->emissive_pulse_amplitude;
    ubo.uMatEmissivePulseFrequency = mat->emissive_pulse_frequency;
    ubo.uMatEmissivePulsePhase     = mat->emissive_pulse_phase;

    ubo.uMatSpecularExponent = mat->specular_exponent;

    /* uMatSpecularColor (vec3) */
    ubo.uMatSpecularColor[0] = mat->specular_color.position.x;
    ubo.uMatSpecularColor[1] = mat->specular_color.position.y;
    ubo.uMatSpecularColor[2] = mat->specular_color.position.z;

    ubo.uMatSpecularThreshold = mat->specular_threshold;

    /* uMatRimColor (vec3) */
    ubo.uMatRimColor[0] = mat->rim_color.position.x;
    ubo.uMatRimColor[1] = mat->rim_color.position.y;
    ubo.uMatRimColor[2] = mat->rim_color.position.z;

    ubo.uMatRimExponent = mat->rim_exponent;

    /* uMatFresnelColor (vec3) */
    ubo.uMatFresnelColor[0] = mat->fresnel_color.position.x;
    ubo.uMatFresnelColor[1] = mat->fresnel_color.position.y;
    ubo.uMatFresnelColor[2] = mat->fresnel_color.position.z;

    ubo.uMatFresnelExponent = mat->fresnel_exponent;

    /* uMatGoochCool (vec3) */
    ubo.uMatGoochCool[0] = mat->gooch_cool.position.x;
    ubo.uMatGoochCool[1] = mat->gooch_cool.position.y;
    ubo.uMatGoochCool[2] = mat->gooch_cool.position.z;

    /* uMatGoochWarm (vec3) */
    ubo.uMatGoochWarm[0] = mat->gooch_warm.position.x;
    ubo.uMatGoochWarm[1] = mat->gooch_warm.position.y;
    ubo.uMatGoochWarm[2] = mat->gooch_warm.position.z;

    ubo.uMatAmbientLightFactor = mat->ambient_light_factor;
    ubo.uMatOrenNayarSigma     = mat->oren_nayar_sigma;
    ubo.uMatMinnaertK          = mat->minnaert_k;
    ubo.uMatSaturation         = mat->saturation;
    ubo.uMatIridescenceStrength = mat->iridescence_strength;

    /* uMatBackGlowColor (vec3) */
    ubo.uMatBackGlowColor[0] = mat->back_glow_color.position.x;
    ubo.uMatBackGlowColor[1] = mat->back_glow_color.position.y;
    ubo.uMatBackGlowColor[2] = mat->back_glow_color.position.z;

    ubo.uMatBumpAmplitude = mat->bump_amplitude;
    ubo.uMatBumpFrequency = mat->bump_frequency;
    ubo.uMatBumpSpeed     = mat->bump_speed;
    ubo.uMatRoughness     = mat->roughness;
    ubo.uMatFringeIntensity = mat->fringe_intensity;
    ubo.uMatCelBands      = mat->cel_bands;
    ubo.uMatGlitchIntensity = mat->glitch_intensity;
    ubo.uMatPosterizeLevels = mat->posterize_levels;

    /* uMatStrobeColor (vec3) */
    ubo.uMatStrobeColor[0] = mat->strobe_color.position.x;
    ubo.uMatStrobeColor[1] = mat->strobe_color.position.y;
    ubo.uMatStrobeColor[2] = mat->strobe_color.position.z;

    ubo.uMatStrobeFrequency = mat->strobe_frequency;
    ubo.uMatStrobePhase     = mat->strobe_phase;

    /* Upload to GPU – size is now exactly 240 bytes */
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, g_material_ubo);
    C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(material_ubo_t), &ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/* ---- Helper: set non‑material uniforms and bind the UBO ---- */
static void set_uniforms_for_variant(shader_variant_t* variant, const material_definition* mat) {
    (void)mat; /* mat not used directly – UBO is updated separately */

    /* Non‑material uniforms (still per‑draw) */
    if (variant->u_view_proj != -1)
        C89GL_glUniformMatrix4fv(variant->u_view_proj, 1, GL_TRUE, (float*)&g_view_proj);
    if (variant->u_light_dir != -1)
        C89GL_glUniform3fv(variant->u_light_dir, 1, (float*)&g_light_dir);
    if (variant->u_light_col != -1)
        C89GL_glUniform3fv(variant->u_light_col, 1, (float*)&g_light_col);
    if (variant->u_ambient_col != -1)
        C89GL_glUniform3fv(variant->u_ambient_col, 1, (float*)&g_ambient_col);
    if (variant->u_cam_eye != -1)
        C89GL_glUniform3fv(variant->u_cam_eye, 1, (float*)&g_cam_eye);
    if (variant->u_time != -1)
        C89GL_glUniform1f(variant->u_time, g_time);
    if (variant->u_fog_color != -1)
        C89GL_glUniform3fv(variant->u_fog_color, 1, (float*)&g_fog_color);
    if (variant->u_fog_start != -1)
        C89GL_glUniform1f(variant->u_fog_start, g_fog_start);
    if (variant->u_fog_end != -1)
        C89GL_glUniform1f(variant->u_fog_end, g_fog_end);

    /* Bind the UBO – this is now shared across all variants */
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MATERIAL_UBO_BINDING, g_material_ubo);
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
                    b->mode = (mat->render_method & 0x7); /* extract mode */
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
    if (mat->render_method & EFFECT_ALPHA) {
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
        u32 mode = mat->render_method & 0x7;
        for (i = 0; i < g_batch_count; i++) {
            if (g_batches[i].mat == mat && g_batches[i].mode == mode) {
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
            g_batches[batch_idx].mode = mode;
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
    /* If already initialized, just return */
    if (g_vao) return 1;

    g_win_width = window_width;
    g_win_height = window_height;
    g_render_width = window_width;
    g_render_height = window_height;
    g_transparent_count = 0;
    g_batch_count = 0;
    g_pool_used_floats = 0;
    g_shader_compilations = 0;
    g_shader_cache = NULL;
    g_shader_cache_size = 0;
    g_shader_cache_count = 0;

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

    /* ---- Create material UBO ---- */
    printf("Creating material UBO...\n");
    C89GL_glGenBuffers(1, &g_material_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, g_material_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(material_ubo_t), NULL, GL_DYNAMIC_DRAW);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

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
    if (g_batch_vbo) { C89GL_glDeleteBuffers(1, &g_batch_vbo); g_batch_vbo = 0; }
    if (g_vao) { C89GL_glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_fbo) { C89GL_glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_color_tex) { C89GL_glDeleteTextures(1, &g_color_tex); g_color_tex = 0; }
    if (g_depth_rb) { C89GL_glDeleteRenderbuffers(1, &g_depth_rb); g_depth_rb = 0; }
    if (g_material_ubo) { C89GL_glDeleteBuffers(1, &g_material_ubo); g_material_ubo = 0; }
    if (g_vertex_pool) { free(g_vertex_pool); g_vertex_pool = NULL; }
    /* Free shader cache */
    if (g_shader_cache) {
        /* Delete all programs */
        int i;
        for (i = 0; i < g_shader_cache_size; i++) {
            if (g_shader_cache[i].program)
                C89GL_glDeleteProgram(g_shader_cache[i].program);
        }
        free(g_shader_cache);
        g_shader_cache = NULL;
        g_shader_cache_size = 0;
        g_shader_cache_count = 0;
    }
    if (g_gl_ctx.initialized) { C89GL_destroy_context(&g_gl_ctx); }
    g_transparent_count = 0;
    g_batch_count = 0;
    g_pool_used_floats = 0;
}

void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    g_light_dir = dir;
    g_light_col = col;
    g_ambient_col = amb;
}

void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    g_cam_eye = eye;
    g_view = mat4_lookat(eye, center, up);
    g_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    g_view_proj = mat4_mul(g_proj, g_view);
    extract_frustum_planes();
}

void render_set_fog(vec3 color, real start, real end) {
    g_fog_color = color;
    g_fog_start = start;
    g_fog_end = end;
}

void render_set_time(real t) {
    g_time = t;
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
    if (!mat) return;
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
    if (!ent) return;
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
    if (!entities || count <= 0) return;

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
                /* Get shader variant for this material */
                shader_variant_t *variant = get_program_for_method(b->mat->render_method);
                if (!variant) continue; /* should not happen */

                C89GL_glUseProgram(variant->program);

                /* Update material UBO for this batch */
                update_material_ubo(b->mat);

                /* Set non‑material uniforms and bind the UBO */
                set_uniforms_for_variant(variant, b->mat);

                if (b->mode == MODE_WIREFRAME) {
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