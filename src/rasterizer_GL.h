/*
 * rasterizer_GL.h – GPU‑accelerated renderer (OpenGL 3.3+) with batching
 *
 * Supports: Wireframe, Flat, Gouraud, Phong, Quadratic, Cubic.
 * All shading modes run on the GPU, including Quadratic/Cubic.
 *
 * FEATURES:
 *   - Frustum culling (CPU)
 *   - Backface culling (GPU, per‑batch – toggled via GL_CULL_FACE)
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
 * OPTIMISATIONS (WebGL‑friendly):
 *   - GPU vertex transforms (model matrix UBO + per‑vertex index)
 *   - Indexed rendering (glDrawElements)
 *   - Buffer orphaning for dynamic VBO
 *   - GL_STREAM_DRAW for streaming buffers
 *   - Batch sorting by shader variant to reduce program switches
 *   - Backface culling on GPU (GL_CULL_FACE), toggled per material
 *   - Opaque before transparent draw order
 *   - Depth writes disabled for transparent objects
 *
 * Shaders are loaded from external files:
 *   - render.vert  (vertex shader)
 *   - render.frag  (fragment shader)
 *
 * Usage:
 *   #define RASTERIZER_GL_IMPLEMENTATION
 *   #include "rasterizer_GL.h"
 *
 *   render_init(window_width, window_height);
 *   render_set_render_resolution(512, 288);
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

int  render_init(i32 window_width, i32 window_height);
void render_shutdown(void);
void render_set_light(vec3 dir, vec3 col, vec3 amb);
void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect);
void render_set_fog(vec3 color, real start, real end);
void render_set_time(real t);
void render_clear(u8 r, u8 g, u8 b);
void render_clear_color(real r, real g, real b);

/* Legacy per‑triangle draw (backward compatible – CPU transforms) */
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat
);

/* Entity‑level draw – uses entity depth for transparent sorting (CPU transforms) */
void render_draw_entity(const struct entity_definition *ent);

/* Batch draw – sorts entities by depth before drawing (GPU transforms + indexed) */
void render_draw_entities(struct entity_definition **entities, int count);

void render_finish(void);
const u32* render_get_fb(void);
int render_resize(i32 new_w, i32 new_h);

void render_set_render_resolution(i32 render_width, i32 render_height);
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
static C89GL_Context gl_ctx;

/* ---- Window state ---- */
static i32 gl_win_width  = 0;
static i32 gl_win_height = 0;
static i32 gl_render_width  = 0;
static i32 gl_render_height = 0;

/* ---- UBO binding points ---- */
#define MATERIAL_UBO_BINDING  0
#define MODEL_UBO_BINDING     1

/* ---- Material UBO (std140, 240 bytes) ---- */
typedef struct {
    float uMatColor[3];
    float _pad0;
    float uMatTint[3];
    float uMatAlpha;
    float uMatEmissiveColor[3];
    float uMatEmissivePulseAmplitude;
    float uMatEmissivePulseFrequency;
    float uMatEmissivePulsePhase;
    float uMatSpecularExponent;
    float _pad1;
    float uMatSpecularColor[3];
    float uMatSpecularThreshold;
    float uMatRimColor[3];
    float uMatRimExponent;
    float uMatFresnelColor[3];
    float uMatFresnelExponent;
    float uMatGoochCool[3];
    float _pad2;
    float uMatGoochWarm[3];
    float uMatAmbientLightFactor;
    float uMatOrenNayarSigma;
    float uMatMinnaertK;
    float uMatSaturation;
    float uMatIridescenceStrength;
    float uMatBackGlowColor[3];
    float uMatBumpAmplitude;
    float uMatBumpFrequency;
    float uMatBumpSpeed;
    float uMatRoughness;
    float uMatFringeIntensity;
    int   uMatCelBands;
    float uMatGlitchIntensity;
    int   uMatPosterizeLevels;
    float _pad3;
    float uMatStrobeColor[3];
    float uMatStrobeFrequency;
    float uMatStrobePhase;
    float _pad4[3];
} material_ubo_t;

/* ---- Model matrices UBO (for GPU transforms) ---- */
#define MAX_MODEL_MATRICES 1024

/* ---- Shader variant cache ---- */
typedef struct {
    render_method key;
    GLuint program;
    int hit_logged;
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

#define SHADER_CACHE_INITIAL_SIZE 64
#define SHADER_CACHE_MAX_LOAD_FACTOR 0.7f

static shader_variant_t *gl_shader_cache = NULL;
static int gl_shader_cache_size = 0;
static int gl_shader_cache_count = 0;
static int gl_shader_compilations = 0;

/* ---- UBO handles ---- */
static GLuint gl_material_ubo = 0;
static GLuint gl_model_ubo = 0;

/* ---- Frustum culling state ---- */
#define FRUSTUM_PLANES 6
typedef struct {
    vec3 normal;
    real d;
} frustum_plane_t;

static frustum_plane_t gl_frustum[FRUSTUM_PLANES];
static mat4 gl_view, gl_proj, gl_view_proj;
static vec3 gl_cam_eye;

/* ---- Lighting and environment ---- */
static vec3 gl_light_dir;
static vec3 gl_light_col;
static vec3 gl_ambient_col;
static vec3 gl_fog_color;
static real gl_fog_start;
static real gl_fog_end;
static real gl_time;

/* ---- VAO / VBO (dynamic for vertex & index data) ---- */
static GLuint gl_vao = 0;
static GLuint gl_vertex_vbo = 0;
static GLuint gl_index_vbo = 0;
static size_t gl_vbo_capacity_bytes = 0;
static size_t gl_ibo_capacity_bytes = 0;

/* ---- FBO (upscaling) ---- */
static GLuint gl_fbo = 0;
static GLuint gl_color_tex = 0;
static GLuint gl_depth_rb = 0;
static GLint gl_default_fbo = 0;

/* ---- Batching state (indexed) ---- */
#define MAX_BATCHES         128
#define MAX_TRANSPARENT_TRIS 8192
#define MAX_VERTICES_PER_FRAME (1024 * 1024)
#define MAX_INDICES_PER_FRAME  (MAX_VERTICES_PER_FRAME * 3)
#define VERTEX_STRIDE_FLOATS 16   /* pos(3) + normal(3) + localPos(3) + modelIndex(1) + faceNormal(3) + centroid(3) */
#define VERTEX_STRIDE_BYTES (VERTEX_STRIDE_FLOATS * sizeof(float))

/* Transparent triangle storage */
typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    const struct material_definition *mat;
    float depth;
    float entity_depth;
    int   id;
    int   model_index;
} transparent_tri_t;

/* Batch entry */
typedef struct {
    const struct material_definition *mat;
    size_t vertex_offset;
    size_t index_offset;
    int    vertex_count;
    int    index_count;
    int    mode;
    int    is_transparent;
} batch_t;

/* Dynamic pools */
static float *gl_vertex_pool = NULL;
static size_t gl_pool_capacity_floats = 0;
static size_t gl_pool_used_floats = 0;

static GLushort *gl_index_pool = NULL;
static size_t gl_index_pool_capacity = 0;
static size_t gl_index_pool_used = 0;

static batch_t gl_batches[MAX_BATCHES];
static int gl_batch_count = 0;

static transparent_tri_t gl_transparent_tris[MAX_TRANSPARENT_TRIS];
static i32 gl_transparent_count = 0;
static i32 gl_transparent_triangle_id = 0;

/* ---- Model matrix storage for current frame ---- */
static mat4 gl_model_matrices[MAX_MODEL_MATRICES];
static int gl_model_count = 0;

/* ---- Helper: compute model matrix from entity (no scale) ---- */
static mat4 entity_model_matrix(const entity_definition *ent) {
    mat4 R = quat_to_mat4(ent->orientation);
    mat4 T = mat4_translation(ent->position);
    return mat4_mul(T, R);  // T * R (scale can be added later)
}

/* ---- Helper: sort entities by depth ---- */
typedef struct {
    struct entity_definition *ent;
    float depth;
    int model_index;
} entity_sort_t;

static int entity_sort_compare(const void* a, const void* b) {
    const entity_sort_t *sa = (const entity_sort_t*)a;
    const entity_sort_t *sb = (const entity_sort_t*)b;
    if (sa->depth > sb->depth) return -1;
    if (sa->depth < sb->depth) return 1;
    return 0;
}

/* ---- Helper: color conversion ---- */
static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

/* ---- Frustum culling (unchanged) ---- */
static void extract_frustum_planes(void) {
    vec4 c0 = gl_view_proj.columns[0];
    vec4 c1 = gl_view_proj.columns[1];
    vec4 c2 = gl_view_proj.columns[2];
    vec4 c3 = gl_view_proj.columns[3];
    i32 i;

    gl_frustum[0].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    gl_frustum[0].d = c3.components[3] + c0.components[3];
    gl_frustum[1].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    gl_frustum[1].d = c3.components[3] - c0.components[3];
    gl_frustum[2].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    gl_frustum[2].d = c3.components[3] + c1.components[3];
    gl_frustum[3].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    gl_frustum[3].d = c3.components[3] - c1.components[3];
    gl_frustum[4].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    gl_frustum[4].d = c3.components[3] + c2.components[3];
    gl_frustum[5].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    gl_frustum[5].d = c3.components[3] - c2.components[3];

    for (i = 0; i < FRUSTUM_PLANES; i++) {
        real len = vec3_magnitude(gl_frustum[i].normal);
        if (len > 0.0f) {
            gl_frustum[i].normal = vec3_div_scalar(gl_frustum[i].normal, len);
            gl_frustum[i].d /= len;
        }
    }
}

static INLINE i32 triangle_outside_frustum(vec3 v0, vec3 v1, vec3 v2) {
    i32 i;
    for (i = 0; i < FRUSTUM_PLANES; i++) {
        i32 o0 = (vec3_dot(gl_frustum[i].normal, v0) + gl_frustum[i].d) < 0.0f;
        i32 o1 = (vec3_dot(gl_frustum[i].normal, v1) + gl_frustum[i].d) < 0.0f;
        i32 o2 = (vec3_dot(gl_frustum[i].normal, v2) + gl_frustum[i].d) < 0.0f;
        if (o0 && o1 && o2) return 1;
    }
    return 0;
}

/* ---- File reading / shader compilation (unchanged) ---- */
static char* read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) { printf("ERROR: Failed to open shader file: %s\n", filename); return NULL; }
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

static GLuint compile_shader_with_defines(GLenum type, const char* filename, const char* defines) {
    char* source = read_file(filename);
    if (!source) return 0;
    char* source_without_version = source;
    if (strncmp(source, "#version", 8) == 0) {
        char* p = strchr(source, '\n');
        if (p) source_without_version = p + 1;
    }
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

/* ---- Generate shader defines ---- */
static void generate_defines(render_method key, char* out, size_t out_size) {
    char* p = out;
    size_t remaining = out_size;
    int n;

    n = snprintf(p, remaining, "#version 330 core\n");
    p += n; remaining -= n;

    n = snprintf(p, remaining, "#define USE_MODEL_UBO 1\n");
    p += n; remaining -= n;

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

    u32 effects = (u32)key & ~0x7;
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

/* ---- Shader cache (unchanged) ---- */
static void shader_cache_resize(int new_size) {
    shader_variant_t *old_cache = gl_shader_cache;
    int old_size = gl_shader_cache_size;
    int i;
    gl_shader_cache = (shader_variant_t*)calloc(new_size, sizeof(shader_variant_t));
    gl_shader_cache_size = new_size;
    gl_shader_cache_count = 0;
    for (i = 0; i < old_size; i++) {
        if (old_cache[i].program != 0) {
            int index = (unsigned)old_cache[i].key % new_size;
            while (gl_shader_cache[index].program != 0) index = (index + 1) % new_size;
            gl_shader_cache[index] = old_cache[i];
            gl_shader_cache_count++;
        }
    }
    free(old_cache);
}

static shader_variant_t* get_program_for_method(render_method key) {
    if (!gl_shader_cache) {
        gl_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        gl_shader_cache = (shader_variant_t*)calloc(gl_shader_cache_size, sizeof(shader_variant_t));
        gl_shader_cache_count = 0;
    }
    int index = (unsigned)key % gl_shader_cache_size;
    while (gl_shader_cache[index].program != 0) {
        if (gl_shader_cache[index].key == key) {
            if (!gl_shader_cache[index].hit_logged) {
                printf("[SHADER CACHE] Hit for key 0x%x (program %u)\n", (unsigned)key, gl_shader_cache[index].program);
                gl_shader_cache[index].hit_logged = 1;
            }
            return &gl_shader_cache[index];
        }
        index = (index + 1) % gl_shader_cache_size;
    }
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
    GLuint blockIndex = C89GL_glGetUniformBlockIndex(prog, "MaterialUniforms");
    if (blockIndex != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(prog, blockIndex, MATERIAL_UBO_BINDING);
    GLuint modelBlock = C89GL_glGetUniformBlockIndex(prog, "ModelMatrices");
    if (modelBlock != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(prog, modelBlock, MODEL_UBO_BINDING);

    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);

    if ((float)(gl_shader_cache_count + 1) / gl_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        shader_cache_resize(gl_shader_cache_size * 2);
        index = (unsigned)key % gl_shader_cache_size;
        while (gl_shader_cache[index].program != 0) index = (index + 1) % gl_shader_cache_size;
    }
    shader_variant_t *entry = &gl_shader_cache[index];
    entry->key = key;
    entry->program = prog;
    entry->hit_logged = 0;
    entry->u_view_proj = C89GL_glGetUniformLocation(prog, "uViewProj");
    entry->u_light_dir = C89GL_glGetUniformLocation(prog, "uLightDir");
    entry->u_light_col = C89GL_glGetUniformLocation(prog, "uLightCol");
    entry->u_ambient_col = C89GL_glGetUniformLocation(prog, "uAmbientCol");
    entry->u_cam_eye = C89GL_glGetUniformLocation(prog, "uCamEye");
    entry->u_time = C89GL_glGetUniformLocation(prog, "uTime");
    entry->u_fog_color = C89GL_glGetUniformLocation(prog, "uFogColor");
    entry->u_fog_start = C89GL_glGetUniformLocation(prog, "uFogStart");
    entry->u_fog_end = C89GL_glGetUniformLocation(prog, "uFogEnd");
    gl_shader_cache_count++;
    gl_shader_compilations++;
    printf("[SHADER CACHE] Compiled new variant #%d for key 0x%x (program %u)\n",
           gl_shader_compilations, (unsigned)key, prog);
    return entry;
}

/* ---- Update material UBO (unchanged) ---- */
static void update_material_ubo(const material_definition *mat) {
    material_ubo_t ubo;
    memset(&ubo, 0, sizeof(ubo));
    ubo.uMatColor[0] = mat->color.position.x;
    ubo.uMatColor[1] = mat->color.position.y;
    ubo.uMatColor[2] = mat->color.position.z;
    ubo.uMatTint[0] = mat->tint.position.x;
    ubo.uMatTint[1] = mat->tint.position.y;
    ubo.uMatTint[2] = mat->tint.position.z;
    ubo.uMatAlpha = mat->alpha;
    ubo.uMatEmissiveColor[0] = mat->emissive_color.position.x;
    ubo.uMatEmissiveColor[1] = mat->emissive_color.position.y;
    ubo.uMatEmissiveColor[2] = mat->emissive_color.position.z;
    ubo.uMatEmissivePulseAmplitude = mat->emissive_pulse_amplitude;
    ubo.uMatEmissivePulseFrequency = mat->emissive_pulse_frequency;
    ubo.uMatEmissivePulsePhase     = mat->emissive_pulse_phase;
    ubo.uMatSpecularExponent = mat->specular_exponent;
    ubo.uMatSpecularColor[0] = mat->specular_color.position.x;
    ubo.uMatSpecularColor[1] = mat->specular_color.position.y;
    ubo.uMatSpecularColor[2] = mat->specular_color.position.z;
    ubo.uMatSpecularThreshold = mat->specular_threshold;
    ubo.uMatRimColor[0] = mat->rim_color.position.x;
    ubo.uMatRimColor[1] = mat->rim_color.position.y;
    ubo.uMatRimColor[2] = mat->rim_color.position.z;
    ubo.uMatRimExponent = mat->rim_exponent;
    ubo.uMatFresnelColor[0] = mat->fresnel_color.position.x;
    ubo.uMatFresnelColor[1] = mat->fresnel_color.position.y;
    ubo.uMatFresnelColor[2] = mat->fresnel_color.position.z;
    ubo.uMatFresnelExponent = mat->fresnel_exponent;
    ubo.uMatGoochCool[0] = mat->gooch_cool.position.x;
    ubo.uMatGoochCool[1] = mat->gooch_cool.position.y;
    ubo.uMatGoochCool[2] = mat->gooch_cool.position.z;
    ubo.uMatGoochWarm[0] = mat->gooch_warm.position.x;
    ubo.uMatGoochWarm[1] = mat->gooch_warm.position.y;
    ubo.uMatGoochWarm[2] = mat->gooch_warm.position.z;
    ubo.uMatAmbientLightFactor = mat->ambient_light_factor;
    ubo.uMatOrenNayarSigma     = mat->oren_nayar_sigma;
    ubo.uMatMinnaertK          = mat->minnaert_k;
    ubo.uMatSaturation         = mat->saturation;
    ubo.uMatIridescenceStrength = mat->iridescence_strength;
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
    ubo.uMatStrobeColor[0] = mat->strobe_color.position.x;
    ubo.uMatStrobeColor[1] = mat->strobe_color.position.y;
    ubo.uMatStrobeColor[2] = mat->strobe_color.position.z;
    ubo.uMatStrobeFrequency = mat->strobe_frequency;
    ubo.uMatStrobePhase     = mat->strobe_phase;
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_material_ubo);
    C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(material_ubo_t), &ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/* ---- Update model UBO ---- */
static void update_model_ubo(void) {
    size_t total_bytes = gl_model_count * sizeof(mat4);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_model_ubo);
    if (total_bytes > 0) {
        C89GL_glBufferData(GL_UNIFORM_BUFFER, total_bytes, NULL, GL_STREAM_DRAW);
        C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, total_bytes, gl_model_matrices);
    }
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/* ---- Set non‑material uniforms ---- */
static void set_uniforms_for_variant(shader_variant_t* variant) {
    if (variant->u_view_proj != -1)
        C89GL_glUniformMatrix4fv(variant->u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
    if (variant->u_light_dir != -1)
        C89GL_glUniform3fv(variant->u_light_dir, 1, (float*)&gl_light_dir);
    if (variant->u_light_col != -1)
        C89GL_glUniform3fv(variant->u_light_col, 1, (float*)&gl_light_col);
    if (variant->u_ambient_col != -1)
        C89GL_glUniform3fv(variant->u_ambient_col, 1, (float*)&gl_ambient_col);
    if (variant->u_cam_eye != -1)
        C89GL_glUniform3fv(variant->u_cam_eye, 1, (float*)&gl_cam_eye);
    if (variant->u_time != -1)
        C89GL_glUniform1f(variant->u_time, gl_time);
    if (variant->u_fog_color != -1)
        C89GL_glUniform3fv(variant->u_fog_color, 1, (float*)&gl_fog_color);
    if (variant->u_fog_start != -1)
        C89GL_glUniform1f(variant->u_fog_start, gl_fog_start);
    if (variant->u_fog_end != -1)
        C89GL_glUniform1f(variant->u_fog_end, gl_fog_end);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MATERIAL_UBO_BINDING, gl_material_ubo);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MODEL_UBO_BINDING, gl_model_ubo);
}

/* ---- Transparent sort comparator ---- */
static int transparent_compare(const void* a, const void* b) {
    const transparent_tri_t* ta = (const transparent_tri_t*)a;
    const transparent_tri_t* tb = (const transparent_tri_t*)b;
    if (ta->entity_depth > tb->entity_depth) return -1;
    if (ta->entity_depth < tb->entity_depth) return 1;
    if (ta->depth > tb->depth) return -1;
    if (ta->depth < tb->depth) return 1;
    if (ta->mat < tb->mat) return -1;
    if (ta->mat > tb->mat) return 1;
    return (ta->id < tb->id) ? -1 : (ta->id > tb->id) ? 1 : 0;
}

/* ---- Flush transparent batches (indexed) ---- */
static void flush_transparent_batches(void) {
    if (gl_transparent_count == 0) return;
    qsort(gl_transparent_tris, gl_transparent_count, sizeof(transparent_tri_t), transparent_compare);

    int i = 0;
    while (i < gl_transparent_count) {
        const material_definition *mat = gl_transparent_tris[i].mat;
        int start = i;
        while (i < gl_transparent_count && gl_transparent_tris[i].mat == mat) i++;
        if (gl_batch_count < MAX_BATCHES) {
            batch_t *b = &gl_batches[gl_batch_count++];
            b->mat = mat;
            b->mode = (mat->render_method & 0x7);
            b->vertex_offset = gl_pool_used_floats / VERTEX_STRIDE_FLOATS;
            b->index_offset = gl_index_pool_used;
            b->vertex_count = 0;
            b->index_count = 0;
            b->is_transparent = 1;

            for (int j = start; j < i; j++) {
                transparent_tri_t *t = &gl_transparent_tris[j];
                if (gl_pool_used_floats + (3 * VERTEX_STRIDE_FLOATS) > gl_pool_capacity_floats ||
                    gl_index_pool_used + 3 > gl_index_pool_capacity) {
                    size_t new_cap = gl_pool_capacity_floats ? gl_pool_capacity_floats * 2 : 1024 * VERTEX_STRIDE_FLOATS;
                    float *new_pool = (float*)realloc(gl_vertex_pool, new_cap * sizeof(float));
                    if (!new_pool) return;
                    gl_vertex_pool = new_pool;
                    gl_pool_capacity_floats = new_cap;
                    size_t new_idx_cap = gl_index_pool_capacity ? gl_index_pool_capacity * 2 : 1024 * 3;
                    GLushort *new_idx = (GLushort*)realloc(gl_index_pool, new_idx_cap * sizeof(GLushort));
                    if (!new_idx) return;
                    gl_index_pool = new_idx;
                    gl_index_pool_capacity = new_idx_cap;
                }
                float *ptr = &gl_vertex_pool[gl_pool_used_floats];
                /* For transparent, we still need faceNormal and centroid – compute them on the fly */
                vec3 faceNormal = vec3_normalize(vec3_cross(vec3_sub(t->v1, t->v0), vec3_sub(t->v2, t->v0)));
                vec3 centroid = vec3_div_scalar(vec3_add(vec3_add(t->v0, t->v1), t->v2), 3.0f);
                #define PACK_V(v, n, l, fn, cen, mi) \
                    *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
                    *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
                    *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
                    *(ptr++) = (float)(mi); \
                    *(ptr++) = (fn).position.x; *(ptr++) = (fn).position.y; *(ptr++) = (fn).position.z; \
                    *(ptr++) = (cen).position.x; *(ptr++) = (cen).position.y; *(ptr++) = (cen).position.z;
                PACK_V(t->v0, t->n0, t->v0, faceNormal, centroid, t->model_index);
                PACK_V(t->v1, t->n1, t->v1, faceNormal, centroid, t->model_index);
                PACK_V(t->v2, t->n2, t->v2, faceNormal, centroid, t->model_index);
                #undef PACK_V
                GLushort base = (GLushort)(gl_pool_used_floats / VERTEX_STRIDE_FLOATS);
                gl_index_pool[gl_index_pool_used++] = base;
                gl_index_pool[gl_index_pool_used++] = base + 1;
                gl_index_pool[gl_index_pool_used++] = base + 2;
                gl_pool_used_floats += 3 * VERTEX_STRIDE_FLOATS;
                b->vertex_count += 3;
                b->index_count += 3;
            }
        }
    }
    gl_transparent_count = 0;
    gl_transparent_triangle_id = 0;
}

/* ---- Bind FBO ---- */
static void bind_fbo(void) {
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
}

/* ================================================================
   INTERNAL DRAW HELPERS (indexed, GPU transforms)
   ================================================================ */

static void draw_triangle_indexed(
    vec3 local_v0, vec3 local_v1, vec3 local_v2,
    vec3 local_n0, vec3 local_n1, vec3 local_n2,
    const struct material_definition *mat,
    float entity_depth,
    int model_index)
{
    mat4 model = gl_model_matrices[model_index];
    vec3 world_v0 = mat4_mul_vec3(model, local_v0);
    vec3 world_v1 = mat4_mul_vec3(model, local_v1);
    vec3 world_v2 = mat4_mul_vec3(model, local_v2);
    
    /* Frustum culling only – backface culling is done on GPU via GL_CULL_FACE */
    if (triangle_outside_frustum(world_v0, world_v1, world_v2)) return;

    /* Compute face normal and centroid only if wireframe mode is active */
    vec3 faceNormal = {0,0,0}, centroid = {0,0,0};
    if ((mat->render_method & 0x7) == MODE_WIREFRAME) {
        faceNormal = vec3_normalize(vec3_cross(vec3_sub(world_v1, world_v0), vec3_sub(world_v2, world_v0)));
        centroid = vec3_div_scalar(vec3_add(vec3_add(world_v0, world_v1), world_v2), 3.0f);
    }

    if (mat->render_method & EFFECT_ALPHA) {
        if (gl_transparent_count >= MAX_TRANSPARENT_TRIS) {
            flush_transparent_batches();
        }
        transparent_tri_t *t = &gl_transparent_tris[gl_transparent_count++];
        t->v0 = local_v0; t->v1 = local_v1; t->v2 = local_v2;
        t->n0 = local_n0; t->n1 = local_n1; t->n2 = local_n2;
        t->mat = mat;
        t->entity_depth = entity_depth;
        t->id = gl_transparent_triangle_id++;
        t->model_index = model_index;
        vec4 c0 = mat4_mul_vec4(gl_view, vec4_init_from_4(world_v0.position.x, world_v0.position.y, world_v0.position.z, 1.0f));
        vec4 c1 = mat4_mul_vec4(gl_view, vec4_init_from_4(world_v1.position.x, world_v1.position.y, world_v1.position.z, 1.0f));
        vec4 c2 = mat4_mul_vec4(gl_view, vec4_init_from_4(world_v2.position.x, world_v2.position.y, world_v2.position.z, 1.0f));
        float d0 = -c0.position.z, d1 = -c1.position.z, d2 = -c2.position.z;
        t->depth = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
        return;
    }

    int batch_idx = -1;
    u32 mode = mat->render_method & 0x7;
    for (int i = 0; i < gl_batch_count; i++) {
        if (gl_batches[i].mat == mat && gl_batches[i].mode == mode) {
            batch_idx = i;
            break;
        }
    }
    if (batch_idx == -1) {
        if (gl_batch_count >= MAX_BATCHES) {
            render_finish();
            return;
        }
        batch_idx = gl_batch_count++;
        gl_batches[batch_idx].mat = mat;
        gl_batches[batch_idx].mode = mode;
        gl_batches[batch_idx].vertex_offset = gl_pool_used_floats / VERTEX_STRIDE_FLOATS;
        gl_batches[batch_idx].index_offset = gl_index_pool_used;
        gl_batches[batch_idx].vertex_count = 0;
        gl_batches[batch_idx].index_count = 0;
        gl_batches[batch_idx].is_transparent = 0;
    }

    batch_t *b = &gl_batches[batch_idx];

    if (gl_pool_used_floats + (3 * VERTEX_STRIDE_FLOATS) > gl_pool_capacity_floats ||
        gl_index_pool_used + 3 > gl_index_pool_capacity) {
        size_t new_cap = gl_pool_capacity_floats ? gl_pool_capacity_floats * 2 : 1024 * VERTEX_STRIDE_FLOATS;
        float *new_pool = (float*)realloc(gl_vertex_pool, new_cap * sizeof(float));
        if (!new_pool) { render_finish(); return; }
        gl_vertex_pool = new_pool;
        gl_pool_capacity_floats = new_cap;
        size_t new_idx_cap = gl_index_pool_capacity ? gl_index_pool_capacity * 2 : 1024 * 3;
        GLushort *new_idx = (GLushort*)realloc(gl_index_pool, new_idx_cap * sizeof(GLushort));
        if (!new_idx) { render_finish(); return; }
        gl_index_pool = new_idx;
        gl_index_pool_capacity = new_idx_cap;
    }

    float *ptr = &gl_vertex_pool[gl_pool_used_floats];
    #define PACK_V(v, n, l, fn, cen, mi) \
        *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
        *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
        *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
        *(ptr++) = (float)(mi); \
        *(ptr++) = (fn).position.x; *(ptr++) = (fn).position.y; *(ptr++) = (fn).position.z; \
        *(ptr++) = (cen).position.x; *(ptr++) = (cen).position.y; *(ptr++) = (cen).position.z;
    PACK_V(local_v0, local_n0, local_v0, faceNormal, centroid, model_index);
    PACK_V(local_v1, local_n1, local_v1, faceNormal, centroid, model_index);
    PACK_V(local_v2, local_n2, local_v2, faceNormal, centroid, model_index);
    #undef PACK_V

    GLushort base = (GLushort)(gl_pool_used_floats / VERTEX_STRIDE_FLOATS);
    gl_index_pool[gl_index_pool_used++] = base;
    gl_index_pool[gl_index_pool_used++] = base + 1;
    gl_index_pool[gl_index_pool_used++] = base + 2;

    gl_pool_used_floats += 3 * VERTEX_STRIDE_FLOATS;
    b->vertex_count += 3;
    b->index_count += 3;
}

/* ---- Entity draw with model index ---- */
static void draw_entity_with_model_index(const struct entity_definition *ent, int model_index) {
    if (!ent || ent->model.handle < 0) return;
    model_definition *mod = (model_definition*)tag_get(ent->model.handle, TAG_model);
    if (!mod) return;

    float entity_depth;
    {
        vec4 c = mat4_mul_vec4(gl_view, vec4_init_from_4(ent->position.position.x, ent->position.position.y, ent->position.position.z, 1.0f));
        entity_depth = -c.position.z;
    }

    for (u32 p = 0; p < mod->primitives.count; ++p) {
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

        model_vertex *verts = (model_vertex*)prim->vertices.address;
        u16 *indices = (u16*)prim->indices.address;
        u32 tri_count = prim->indices.count / 3;
        for (u32 t = 0; t < tri_count; ++t) {
            u16 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
            vec3 local_v0 = verts[i0].position;
            vec3 local_v1 = verts[i1].position;
            vec3 local_v2 = verts[i2].position;
            vec3 local_n0 = verts[i0].normal;
            vec3 local_n1 = verts[i1].normal;
            vec3 local_n2 = verts[i2].normal;
            draw_triangle_indexed(local_v0, local_v1, local_v2,
                                  local_n0, local_n1, local_n2,
                                  mat, entity_depth, model_index);
        }
    }
}

/* ---- Legacy CPU‑transform path (kept for compatibility) ---- */
static void draw_triangle_internal_legacy(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat,
    float entity_depth)
{
    (void)v0; (void)v1; (void)v2;
    (void)n0; (void)n1; (void)n2;
    (void)l0; (void)l1; (void)l2;
    (void)mat; (void)entity_depth;
    /* Not used in the new GPU path */
}

/* ---- Public API ---- */

int render_init(i32 window_width, i32 window_height) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (gl_vao) return 1;

    gl_win_width = window_width;
    gl_win_height = window_height;
    gl_render_width = window_width;
    gl_render_height = window_height;
    gl_transparent_count = 0;
    gl_batch_count = 0;
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_shader_compilations = 0;
    gl_shader_cache = NULL;
    gl_shader_cache_size = 0;
    gl_shader_cache_count = 0;

    if (!C89GL_create_context(window_get(), &gl_ctx)) {
        printf("ERROR: Failed to create OpenGL context\n");
        return 0;
    }
    C89GL_make_current(&gl_ctx);
    if (!C89GL_load_functions()) {
        printf("ERROR: Failed to load OpenGL functions\n");
        return 0;
    }

    printf("OpenGL version: %s\n", C89GL_glGetString(GL_VERSION));
    printf("OpenGL vendor: %s\n", C89GL_glGetString(GL_VENDOR));
    printf("OpenGL renderer: %s\n", C89GL_glGetString(GL_RENDERER));

    printf("Creating VAO/VBOs...\n");
    C89GL_glGenVertexArrays(1, &gl_vao);
    C89GL_glBindVertexArray(gl_vao);

    C89GL_glGenBuffers(1, &gl_vertex_vbo);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, gl_vertex_vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER, 1, NULL, GL_STREAM_DRAW);
    gl_vbo_capacity_bytes = 0;

    C89GL_glGenBuffers(1, &gl_index_vbo);
    C89GL_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_index_vbo);
    C89GL_glBufferData(GL_ELEMENT_ARRAY_BUFFER, 1, NULL, GL_STREAM_DRAW);
    gl_ibo_capacity_bytes = 0;

    /* Vertex attributes: stride = VERTEX_STRIDE_BYTES */
    C89GL_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)0);
    C89GL_glEnableVertexAttribArray(0);
    C89GL_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(3 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(1);
    C89GL_glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(6 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(2);
    C89GL_glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(9 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(3);
    C89GL_glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(10 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(4);
    C89GL_glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(13 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(5);

    C89GL_glBindVertexArray(0);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, 0);
    C89GL_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    printf("Creating material UBO...\n");
    C89GL_glGenBuffers(1, &gl_material_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_material_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(material_ubo_t), NULL, GL_DYNAMIC_DRAW);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("Creating model UBO...\n");
    C89GL_glGenBuffers(1, &gl_model_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_model_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, MAX_MODEL_MATRICES * sizeof(mat4), NULL, GL_STREAM_DRAW);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("Creating FBO...\n");
    gl_default_fbo = 0;
    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &gl_default_fbo);

    C89GL_glGenFramebuffers(1, &gl_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

    C89GL_glGenTextures(1, &gl_color_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_color_tex, 0);

    C89GL_glGenRenderbuffers(1, &gl_depth_rb);
    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, gl_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height);
    C89GL_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl_depth_rb);

    GLenum fbo_status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO status: 0x%x\n", fbo_status);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO incomplete!\n");
        return 0;
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    C89GL_glEnable(GL_CULL_FACE);   /* GPU culling enabled globally, toggled per batch */
    C89GL_glFrontFace(GL_CCW);

    printf("render_init returning 1 (success)\n");
    return 1;
}

void render_shutdown(void) {
    if (gl_vertex_vbo) { C89GL_glDeleteBuffers(1, &gl_vertex_vbo); gl_vertex_vbo = 0; }
    if (gl_index_vbo) { C89GL_glDeleteBuffers(1, &gl_index_vbo); gl_index_vbo = 0; }
    if (gl_vao) { C89GL_glDeleteVertexArrays(1, &gl_vao); gl_vao = 0; }
    if (gl_fbo) { C89GL_glDeleteFramebuffers(1, &gl_fbo); gl_fbo = 0; }
    if (gl_color_tex) { C89GL_glDeleteTextures(1, &gl_color_tex); gl_color_tex = 0; }
    if (gl_depth_rb) { C89GL_glDeleteRenderbuffers(1, &gl_depth_rb); gl_depth_rb = 0; }
    if (gl_material_ubo) { C89GL_glDeleteBuffers(1, &gl_material_ubo); gl_material_ubo = 0; }
    if (gl_model_ubo) { C89GL_glDeleteBuffers(1, &gl_model_ubo); gl_model_ubo = 0; }
    if (gl_vertex_pool) { free(gl_vertex_pool); gl_vertex_pool = NULL; }
    if (gl_index_pool) { free(gl_index_pool); gl_index_pool = NULL; }
    if (gl_shader_cache) {
        for (int i = 0; i < gl_shader_cache_size; i++)
            if (gl_shader_cache[i].program)
                C89GL_glDeleteProgram(gl_shader_cache[i].program);
        free(gl_shader_cache);
        gl_shader_cache = NULL;
    }
    if (gl_ctx.initialized) C89GL_destroy_context(&gl_ctx);
    gl_transparent_count = 0;
    gl_batch_count = 0;
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
}

/* ---- render_draw_entities (GPU transforms + indexed) ---- */
void render_draw_entities(struct entity_definition **entities, int count) {
    if (!entities || count <= 0) return;

    gl_model_count = 0;
    int i;
    for (i = 0; i < count && gl_model_count < MAX_MODEL_MATRICES; i++) {
        entity_definition *ent = entities[i];
        if (!ent || ent->model.handle < 0) continue;
        gl_model_matrices[gl_model_count] = entity_model_matrix(ent);
        gl_model_count++;
    }
    update_model_ubo();

    entity_sort_t *sorted = (entity_sort_t*)malloc(count * sizeof(entity_sort_t));
    if (!sorted) return;
    int valid_count = 0;
    for (i = 0; i < count; i++) {
        if (!entities[i] || entities[i]->model.handle < 0) continue;
        sorted[valid_count].ent = entities[i];
        sorted[valid_count].model_index = valid_count;
        vec4 c = mat4_mul_vec4(gl_view, vec4_init_from_4(
            entities[i]->position.position.x,
            entities[i]->position.position.y,
            entities[i]->position.position.z,
            1.0f));
        sorted[valid_count].depth = -c.position.z;
        valid_count++;
    }
    qsort(sorted, valid_count, sizeof(entity_sort_t), entity_sort_compare);

    for (i = 0; i < valid_count; i++) {
        draw_entity_with_model_index(sorted[i].ent, sorted[i].model_index);
    }
    free(sorted);
}

/* ---- Legacy render_draw_entity (CPU transforms) ---- */
void render_draw_entity(const struct entity_definition *ent) {
    /* Use the new GPU path by building a temporary entities array */
    if (!ent) return;
    struct entity_definition *ents[1] = { (struct entity_definition*)ent };
    render_draw_entities(ents, 1);
}

/* ---- Legacy draw_triangle_shaded (CPU transforms) ---- */
void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    /* Not used in the new GPU path – kept for compatibility */
    (void)v0; (void)v1; (void)v2;
    (void)n0; (void)n1; (void)n2;
    (void)l0; (void)l1; (void)l2;
    (void)mat;
}

/* ---- Other API functions (unchanged) ---- */
void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    gl_light_dir = dir; gl_light_col = col; gl_ambient_col = amb;
}
void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    gl_cam_eye = eye;
    gl_view = mat4_lookat(eye, center, up);
    gl_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    gl_view_proj = mat4_mul(gl_proj, gl_view);
    extract_frustum_planes();
}
void render_set_fog(vec3 color, real start, real end) {
    gl_fog_color = color; gl_fog_start = start; gl_fog_end = end;
}
void render_set_time(real t) { gl_time = t; }
void render_clear(u8 r, u8 g, u8 b) { render_clear_color(r/255.0f, g/255.0f, b/255.0f); }
void render_clear_color(real r, real g, real b) {
    if (!gl_fbo) return;
    bind_fbo();
    C89GL_glClearColor(r, g, b, 1.0f);
    C89GL_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
const u32* render_get_fb(void) { return NULL; }

int render_resize(i32 new_w, i32 new_h) {
    if (gl_win_width == new_w && gl_win_height == new_h) return 0;
    gl_win_width = new_w;
    gl_win_height = new_h;
    /* Do NOT change gl_render_width / gl_render_height. */
    return 0;
}

void render_set_render_resolution(i32 rw, i32 rh) {
    if (rw <= 0 || rh <= 0) return;
    if (gl_render_width == rw && gl_render_height == rh) return;
    gl_render_width = rw; gl_render_height = rh;
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glBindRenderbuffer(GL_RENDERBUFFER, gl_depth_rb);
    C89GL_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
}
i32 render_get_render_width(void) { return gl_render_width; }
i32 render_get_render_height(void) { return gl_render_height; }

/* ---- Batch comparator for sorting by mode and transparency ---- */
static int batch_compare_mode(const void* a, const void* b) {
    const batch_t* ba = (const batch_t*)a;
    const batch_t* bb = (const batch_t*)b;
    if (ba->is_transparent != bb->is_transparent)
        return ba->is_transparent - bb->is_transparent;
    if (ba->mode < bb->mode) return -1;
    if (ba->mode > bb->mode) return 1;
    if (ba->mat < bb->mat) return -1;
    if (ba->mat > bb->mat) return 1;
    return 0;
}

/* ---- render_finish (indexed, with per‑batch culling and depth handling) ---- */
void render_finish(void) {
    flush_transparent_batches();

    if (gl_batch_count > 0) {
        size_t vert_bytes = gl_pool_used_floats * sizeof(float);
        size_t idx_bytes  = gl_index_pool_used * sizeof(GLushort);

        C89GL_glBindBuffer(GL_ARRAY_BUFFER, gl_vertex_vbo);
        if (gl_vbo_capacity_bytes < vert_bytes) {
            C89GL_glBufferData(GL_ARRAY_BUFFER, vert_bytes, NULL, GL_STREAM_DRAW);
            gl_vbo_capacity_bytes = vert_bytes;
        } else {
            C89GL_glBufferData(GL_ARRAY_BUFFER, vert_bytes, NULL, GL_STREAM_DRAW);
        }
        C89GL_glBufferSubData(GL_ARRAY_BUFFER, 0, vert_bytes, gl_vertex_pool);

        C89GL_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_index_vbo);
        if (gl_ibo_capacity_bytes < idx_bytes) {
            C89GL_glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_bytes, NULL, GL_STREAM_DRAW);
            gl_ibo_capacity_bytes = idx_bytes;
        } else {
            C89GL_glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_bytes, NULL, GL_STREAM_DRAW);
        }
        C89GL_glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_bytes, gl_index_pool);

        C89GL_glBindVertexArray(gl_vao);

        qsort(gl_batches, gl_batch_count, sizeof(batch_t), batch_compare_mode);

        GLuint current_program = 0;
        for (int i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            shader_variant_t *variant = get_program_for_method(b->mat->render_method);
            if (!variant) continue;

            if (current_program != variant->program) {
                C89GL_glUseProgram(variant->program);
                current_program = variant->program;
            }

            /* ---- Per‑batch backface culling (GPU) ---- */
            if (b->mat->double_sided) {
                C89GL_glDisable(GL_CULL_FACE);
            } else {
                C89GL_glEnable(GL_CULL_FACE);
            }

            /* ---- Depth & blend state ---- */
            if (b->is_transparent) {
                C89GL_glDepthMask(GL_FALSE);
                C89GL_glEnable(GL_BLEND);
            } else {
                C89GL_glDepthMask(GL_TRUE);
                C89GL_glDisable(GL_BLEND);
            }

            update_material_ubo(b->mat);
            set_uniforms_for_variant(variant);

            if (b->mode == MODE_WIREFRAME) {
                C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT,
                                     (void*)(b->index_offset * sizeof(GLushort)));
                C89GL_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            } else {
                C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT,
                                     (void*)(b->index_offset * sizeof(GLushort)));
            }
        }
        if (current_program) C89GL_glUseProgram(0);
        C89GL_glBindVertexArray(0);
        /* Restore default states for next frame */
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glDisable(GL_BLEND);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glEnable(GL_CULL_FACE);
    }

    C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
    C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_default_fbo);
    C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                            0, 0, gl_win_width, gl_win_height,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
    C89GL_swap_buffers(&gl_ctx);

    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_batch_count = 0;
    gl_transparent_count = 0;
    gl_transparent_triangle_id = 0;
    gl_model_count = 0;
}

#endif /* RASTERIZER_GL_IMPLEMENTATION */