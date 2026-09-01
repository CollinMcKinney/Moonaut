/*
 * rasterizer_GL.h – GPU‑accelerated forward renderer with clustered lighting
 *
 * Requires OpenGL 4.3+ (compute shaders, SSBOs).
 * Uses depth pre‑pass + compute light culling + per‑pixel shading.
 *
 * Shader files (read from disk):
 *   material.vert    – vertex shader (same for depth and colour passes)
 *   material.frag    – fragment shader with #ifdef DEPTH_ONLY guard
 *   cluster.comp     – compute shader for light culling
 *   particle.vert    – particle vertex shader
 *   particle.frag    – particle fragment shader
 *
 * Usage:
 *   #define RASTERIZER_GL_IMPLEMENTATION
 *   #include "rasterizer_GL.h"
 *   render_init(win_w, win_h);
 *   render_set_render_resolution(512, 288);
 *   ... draw ...
 *   render_finish();  // does depth pre‑pass, compute, colour pass, upscale & swap
 */

#ifndef RASTERIZER_GL_H
#define RASTERIZER_GL_H

#include "common.h"
#include "tags/entity.h"
#include "tags/model.h"
#include "tags/material.h"
#include "tags/particle_emitter.h"
#include "tags/light.h"

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
void draw_triangle_shaded( vec3 v0, vec3 v1, vec3 v2,
                           vec3 n0, vec3 n1, vec3 n2,
                           vec3 l0, vec3 l1, vec3 l2,
                           const struct material_definition *mat );
void render_draw_entity(const struct entity_definition *ent);
void render_draw_entities(struct entity_definition **entities, int count);
void render_finish(void);
const u32* render_get_fb(void);
int render_resize(i32 new_w, i32 new_h);
void render_set_render_resolution(i32 render_width, i32 render_height);
i32 render_get_render_width(void);
i32 render_get_render_height(void);
static INLINE u8 color_to_u8(real x);

/* Particle system */
void render_particle_system_init(int max_particles);
void render_particle_system_shutdown(void);
void render_particle_system_set_emitter(const struct particle_emitter_definition *def);
void render_particle_system_update(float dt);
void render_particle_system_set_camera(const mat4 *view_proj, vec3 cam_right, vec3 cam_up);
void render_particle_system_emit_burst(int count);

/* Lights */
void render_clear_lights(void);
void render_set_light_at_index(int index, const struct light_definition *def);

/* ---- Public constants ---- */
#define RENDER_MAX_LIGHTS (MAX_EXTRA_DIR_LIGHTS + MAX_EXTRA_POINT_LIGHTS + MAX_EXTRA_SPOT_LIGHTS)

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
#include <math.h>

/* ---- Light limits (must match shader) ---- */
#define MAX_EXTRA_DIR_LIGHTS   16
#define MAX_EXTRA_POINT_LIGHTS 64
#define MAX_EXTRA_SPOT_LIGHTS  64
#define MAX_LIGHTS             (MAX_EXTRA_DIR_LIGHTS + MAX_EXTRA_POINT_LIGHTS + MAX_EXTRA_SPOT_LIGHTS)

/* ---- Clustered constants ---- */
#define CLUSTER_TILE_SIZE       16
#define CLUSTER_DEPTH_SLICES    24
#define CLUSTER_MAX_LIGHTS_PER  64

/* ---- GPU light structure (matches shader) ---- */
typedef struct {
    float pos[4];      // .xyz = position, .w = type (0=dir,1=point,2=spot)
    float dir[4];      // .xyz = direction (normalised)
    float color[4];    // .rgb = colour, .w = intensity scale (unused)
    float range;
    float inner_cos;
    float outer_cos;
    float falloff;
} gpu_light_t;

/* ---- Internal state ---- */
static C89GL_Context gl_ctx;
static i32 gl_win_width  = 0;
static i32 gl_win_height = 0;
static i32 gl_render_width  = 0;
static i32 gl_render_height = 0;

#define MATERIAL_UBO_BINDING  0
#define MODEL_UBO_BINDING     1

/* ------------------------------------------------------------
   MATERIAL UBO – updated with anisotropy and adjusted padding
   ------------------------------------------------------------ */
typedef struct {
    float uMatColor[3];             float _pad0;
    float uMatTint[3];              float uMatAlpha;
    float uMatEmissiveColor[3];     float uMatEmissivePulseAmplitude;
    float uMatEmissivePulseFrequency; float uMatEmissivePulsePhase;
    float uMatTransmissionStrength; float _pad1;
    float uMatSpecularTint[3];      float uMatSurfaceRoughness;
    float uMatRimColor[3];          float uMatRimExponent;
    float uMatMetallic;
    float uMatIor;
    float uMatSubsurfaceStrength;
    float uMatFresnelExponent;
    float uMatGoochCool[3];         float _pad2;
    float uMatGoochWarm[3];         float uMatAmbientLightFactor;
    float uMatOrenNayarSigma;       float uMatMinnaertK;
    float uMatSaturation;           float uMatIridescenceStrength;
    float uMatBackGlowColor[3];     float uMatBumpAmplitude;
    float uMatBumpFrequency;        float uMatBumpSpeed;
    float uMatRoughness;            float uMatFringeIntensity;
    int   uMatCelBands;             float uMatGlitchIntensity;
    int   uMatPosterizeLevels;      float _pad3;
    float uMatStrobeColor[3];       float uMatStrobeFrequency;
    float uMatStrobePhase;          float _pad4[3];
    float uClearcoatColor[3];       float uClearcoatRoughness;
    float uClearcoatStrength;       float _pad5[3];
    float uSheenColor[3];           float uSheenExponent;
    float uSheenStrength;
    float uMatAnisotropic;          // new anisotropy uniform
    float uMatTransmissionTint[3];  float _pad6;
} material_ubo_t;

#define MAX_MODEL_MATRICES 1024

/* ---- Shader variant cache ---- */
typedef struct {
    render_method key;
    GLuint program;
    int   is_depth;              // 1 for depth‑only variant
    int   hit_logged;
    GLint u_view_proj;
    GLint u_light_dir;
    GLint u_light_col;
    GLint u_ambient_col;
    GLint u_cam_eye;
    GLint u_time;
    GLint u_fog_color;
    GLint u_fog_start;
    GLint u_fog_end;
    GLint u_gouraud_blend;
    /* Uniforms for clustered */
    GLint u_depth_tex;
    GLint u_screen_size;
    GLint u_num_lights;
    GLint u_num_tiles_x;
    GLint u_num_tiles_y;
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

/* ---- Frustum culling ---- */
#define FRUSTUM_PLANES 6
typedef struct {
    vec3 normal;
    real d;
} frustum_plane_t;
static frustum_plane_t gl_frustum[FRUSTUM_PLANES];
static mat4 gl_view, gl_proj, gl_view_proj;
static vec3 gl_cam_eye;
static float gl_near = 0.05f, gl_far = 1000.0f;

/* ---- Lighting and environment ---- */
static vec3 gl_light_dir;
static vec3 gl_light_col;
static vec3 gl_ambient_col;
static vec3 gl_fog_color;
static real gl_fog_start;
static real gl_fog_end;
static real gl_time;

/* ---- Global light list ---- */
static light_definition g_lights[MAX_LIGHTS];
static int g_light_count = 0;

/* ---- VAO / VBO (indexed) ---- */
static GLuint gl_vao = 0;
static GLuint gl_vertex_vbo = 0;
static GLuint gl_index_vbo = 0;
static size_t gl_vbo_capacity_bytes = 0;
static size_t gl_ibo_capacity_bytes = 0;

/* ---- FBO (upscaling) ---- */
static GLuint gl_fbo = 0;
static GLuint gl_color_tex = 0;
static GLuint gl_depth_tex = 0;      // depth texture (sampled in compute)
static GLint gl_default_fbo = 0;

/* ---- Batching state ---- */
#define MAX_BATCHES         128
#define MAX_TRANSPARENT_TRIS 8192
#define MAX_VERTICES_PER_FRAME (1024 * 1024)
#define MAX_INDICES_PER_FRAME  (MAX_VERTICES_PER_FRAME * 3)
#define VERTEX_STRIDE_FLOATS 16
#define VERTEX_STRIDE_BYTES (VERTEX_STRIDE_FLOATS * sizeof(float))

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    const struct material_definition *mat;
    float depth;
    float entity_depth;
    int   id;
    int   model_index;
} transparent_tri_t;

typedef struct {
    const struct material_definition *mat;
    size_t vertex_offset;
    size_t index_offset;
    int    vertex_count;
    int    index_count;
    int    mode;
    int    is_transparent;
} batch_t;

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

static mat4 gl_model_matrices[MAX_MODEL_MATRICES];
static int gl_model_count = 0;

/* ---- Cluster SSBOs ---- */
static GLuint gl_light_ssbo = 0;
static GLuint gl_cluster_ssbo = 0;
static GLuint gl_cluster_offset_ssbo = 0;
static GLuint gl_cluster_program = 0;
static int gl_num_tiles_x = 0;
static int gl_num_tiles_y = 0;
static int gl_num_clusters = 0;

/* ---- Uniform locations for cluster compute ---- */
static GLint cluster_u_depth_tex = -1;
static GLint cluster_u_num_lights = -1;
static GLint cluster_u_tile_size = -1;
static GLint cluster_u_num_tiles_x = -1;
static GLint cluster_u_num_tiles_y = -1;
static GLint cluster_u_depth_slices = -1;
static GLint cluster_u_near = -1;
static GLint cluster_u_far = -1;

/* ---- Particle system (unchanged) ---- */
typedef struct {
    vec3 center;
    vec4 color;
    float size;
} particle_instance_t;

static particle_instance_t *g_particles = NULL;
static int g_particle_count = 0;
static int g_particle_capacity = 0;

static vec3  g_emitter_pos;
static vec3  g_emitter_color;
static float g_emitter_alpha;
static float g_emitter_size;
static float g_emitter_lifetime;
static float g_emitter_speed;
static float g_emitter_spread;
static float g_emitter_gravity;
static int   g_emitter_loop;
static float g_emission_rate;
static float g_emission_timer;
static int   g_burst_done;

static vec3  *g_particle_velocities = NULL;
static float *g_particle_lifetimes = NULL;
static float *g_particle_max_lifetimes = NULL;

static GLuint g_particle_vao = 0;
static GLuint g_particle_vbo = 0;
static GLuint g_particle_program = 0;
static GLint  g_particle_u_view_proj = -1;
static GLint  g_particle_u_cam_right = -1;
static GLint  g_particle_u_cam_up = -1;

static mat4 g_particle_view_proj;
static vec3 g_particle_cam_right;
static vec3 g_particle_cam_up;
static int  g_particle_cam_valid = 0;

/* ---------------------------------------------------------------------------
   Helper functions
   --------------------------------------------------------------------------- */
static mat4 entity_model_matrix(const entity_definition *ent) {
    mat4 R = quat_to_mat4(ent->orientation);
    mat4 T = mat4_translation(ent->position);
    return mat4_mul(T, R);
}

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

static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

/* ---------------------------------------------------------------------------
   Frustum culling
   --------------------------------------------------------------------------- */
static void extract_frustum_planes(void) {
    vec4 c0 = gl_view_proj.columns[0];
    vec4 c1 = gl_view_proj.columns[1];
    vec4 c2 = gl_view_proj.columns[2];
    vec4 c3 = gl_view_proj.columns[3];
    int i;
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
    int i;
    for (i = 0; i < FRUSTUM_PLANES; i++) {
        int o0 = (vec3_dot(gl_frustum[i].normal, v0) + gl_frustum[i].d) < 0.0f;
        int o1 = (vec3_dot(gl_frustum[i].normal, v1) + gl_frustum[i].d) < 0.0f;
        int o2 = (vec3_dot(gl_frustum[i].normal, v2) + gl_frustum[i].d) < 0.0f;
        if (o0 && o1 && o2) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
   File reading / shader compilation
   --------------------------------------------------------------------------- */
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

/* ---- Generate defines for material variant (mode + effects) ---- */
static void generate_defines(render_method key, int is_depth, char* out, size_t out_size) {
    char* p = out;
    size_t remaining = out_size;
    int n;

    n = snprintf(p, remaining, "#version 430 core\n");
    p += n; remaining -= n;

    n = snprintf(p, remaining, "#define USE_MODEL_UBO 1\n");
    p += n; remaining -= n;

    if (is_depth) {
        n = snprintf(p, remaining, "#define DEPTH_ONLY 1\n");
        p += n; remaining -= n;
    }

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
    if (effects & EFFECT_CLEARCOAT)       { n = snprintf(p, remaining, "#define EFFECT_CLEARCOAT\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SHEEN)           { n = snprintf(p, remaining, "#define EFFECT_SHEEN\n"); p += n; remaining -= n; }
    if (effects & EFFECT_ANISOTROPIC)     { n = snprintf(p, remaining, "#define EFFECT_ANISOTROPIC\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SUBSURFACE)      { n = snprintf(p, remaining, "#define EFFECT_SUBSURFACE\n"); p += n; remaining -= n; }
    if (effects & EFFECT_TRANSMISSION)    { n = snprintf(p, remaining, "#define EFFECT_TRANSMISSION\n"); p += n; remaining -= n; }
}

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

/* ---- Get shader variant ---- */
static shader_variant_t* get_program_for_method(render_method key, int is_depth) {
    shader_variant_t *entry;
    GLuint vs, fs, prog;
    int index, link_status, blockIndex, modelBlock;
    char defines[4096];
    char name[32];
    GLint len;
    char log[512];
    int i;   // declared at top for C89

    if (!gl_shader_cache) {
        gl_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        gl_shader_cache = (shader_variant_t*)calloc(gl_shader_cache_size, sizeof(shader_variant_t));
        gl_shader_cache_count = 0;
    }

    render_method cache_key = key | (is_depth ? (1u << 31) : 0);

    index = (unsigned)cache_key % gl_shader_cache_size;
    while (gl_shader_cache[index].program != 0) {
        if (gl_shader_cache[index].key == cache_key) {
            if (!gl_shader_cache[index].hit_logged) {
                printf("[SHADER CACHE] Hit for key 0x%x (program %u)\n", (unsigned)cache_key, gl_shader_cache[index].program);
                gl_shader_cache[index].hit_logged = 1;
            }
            return &gl_shader_cache[index];
        }
        index = (index + 1) % gl_shader_cache_size;
    }

    printf("[SHADER CACHE] Miss for key 0x%x - compiling new variant...\n", (unsigned)cache_key);
    generate_defines(key, is_depth, defines, sizeof(defines));

    vs = compile_shader_with_defines(GL_VERTEX_SHADER, "material.vert", defines);
    fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, "material.frag", defines);
    if (!vs || !fs) {
        if (vs) C89GL_glDeleteShader(vs);
        if (fs) C89GL_glDeleteShader(fs);
        printf("[SHADER CACHE] ERROR: Failed to compile shaders for key 0x%x\n", (unsigned)cache_key);
        return NULL;
    }

    prog = C89GL_glCreateProgram();
    C89GL_glAttachShader(prog, vs);
    C89GL_glAttachShader(prog, fs);
    C89GL_glLinkProgram(prog);
    C89GL_glGetProgramiv(prog, GL_LINK_STATUS, &link_status);
    if (!link_status) {
        C89GL_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("[SHADER CACHE] ERROR: Program link failed for key 0x%x:\n%s\n", (unsigned)cache_key, log);
        C89GL_glDeleteProgram(prog);
        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);
        return NULL;
    }
    C89GL_glGetProgramInfoLog(prog, sizeof(log), &len, log);
    if (len > 0) printf("Program info log (warnings):\n%s\n", log);

    blockIndex = C89GL_glGetUniformBlockIndex(prog, "MaterialUniforms");
    if (blockIndex != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(prog, blockIndex, MATERIAL_UBO_BINDING);
    modelBlock = C89GL_glGetUniformBlockIndex(prog, "ModelMatrices");
    if (modelBlock != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(prog, modelBlock, MODEL_UBO_BINDING);

    entry = &gl_shader_cache[index];
    entry->key = cache_key;
    entry->program = prog;
    entry->is_depth = is_depth;
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
    entry->u_gouraud_blend = C89GL_glGetUniformLocation(prog, "uGouraudBlend");
    entry->u_depth_tex = C89GL_glGetUniformLocation(prog, "uDepthTex");
    entry->u_screen_size = C89GL_glGetUniformLocation(prog, "uScreenSize");
    entry->u_num_lights = C89GL_glGetUniformLocation(prog, "uNumLights");
    entry->u_num_tiles_x = C89GL_glGetUniformLocation(prog, "uNumTilesX");
    entry->u_num_tiles_y = C89GL_glGetUniformLocation(prog, "uNumTilesY");

    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);

    if ((float)(gl_shader_cache_count + 1) / gl_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        shader_cache_resize(gl_shader_cache_size * 2);
        index = (unsigned)cache_key % gl_shader_cache_size;
        while (gl_shader_cache[index].program != 0) index = (index + 1) % gl_shader_cache_size;
    }
    gl_shader_cache[index] = *entry;
    gl_shader_cache_count++;
    gl_shader_compilations++;
    printf("[SHADER CACHE] Compiled new variant #%d for key 0x%x (program %u)\n",
           gl_shader_compilations, (unsigned)cache_key, prog);
    return &gl_shader_cache[index];
}

/* ---- Update material UBO (now includes anisotropic) ---- */
static void update_material_ubo(const material_definition *mat) {
    material_ubo_t ubo;
    memset(&ubo, 0, sizeof(ubo));

    /* existing fields */
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
    ubo.uMatTransmissionStrength   = mat->transmission_strength;
    ubo.uMatSpecularTint[0] = mat->specular_tint.position.x;
    ubo.uMatSpecularTint[1] = mat->specular_tint.position.y;
    ubo.uMatSpecularTint[2] = mat->specular_tint.position.z;
    ubo.uMatSurfaceRoughness = mat->surface_roughness;
    ubo.uMatRimColor[0] = mat->rim_color.position.x;
    ubo.uMatRimColor[1] = mat->rim_color.position.y;
    ubo.uMatRimColor[2] = mat->rim_color.position.z;
    ubo.uMatRimExponent = mat->rim_exponent;
    ubo.uMatMetallic = mat->metallic;
    ubo.uMatIor      = mat->ior;
    ubo.uMatSubsurfaceStrength = mat->subsurface_strength;
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
    ubo.uClearcoatColor[0] = mat->clearcoat_color.position.x;
    ubo.uClearcoatColor[1] = mat->clearcoat_color.position.y;
    ubo.uClearcoatColor[2] = mat->clearcoat_color.position.z;
    ubo.uClearcoatRoughness = mat->clearcoat_roughness;
    ubo.uClearcoatStrength = mat->clearcoat_strength;
    ubo.uSheenColor[0] = mat->sheen_color.position.x;
    ubo.uSheenColor[1] = mat->sheen_color.position.y;
    ubo.uSheenColor[2] = mat->sheen_color.position.z;
    ubo.uSheenExponent = mat->sheen_exponent;
    ubo.uSheenStrength = mat->sheen_strength;

    /* NEW: copy anisotropic value */
    ubo.uMatAnisotropic = mat->anisotropic;
    ubo.uMatTransmissionTint[0] = mat->transmission_tint.color.r;
    ubo.uMatTransmissionTint[1] = mat->transmission_tint.color.g;
    ubo.uMatTransmissionTint[2] = mat->transmission_tint.color.b;

    /* _pad6[2] is left zero (was 3 floats, now 2) */

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

/* ---- Upload lights to SSBO ---- */
static void upload_lights_to_ssbo(void) {
    gpu_light_t gpu_lights[MAX_LIGHTS];
    int count = 0;
    int i;
    for (i = 0; i < g_light_count && count < MAX_LIGHTS; i++) {
        if (!g_lights[i].enabled) continue;
        gpu_lights[count].pos[0] = g_lights[i].position.position.x;
        gpu_lights[count].pos[1] = g_lights[i].position.position.y;
        gpu_lights[count].pos[2] = g_lights[i].position.position.z;
        gpu_lights[count].pos[3] = (float)g_lights[i].type;
        gpu_lights[count].dir[0] = g_lights[i].direction.position.x;
        gpu_lights[count].dir[1] = g_lights[i].direction.position.y;
        gpu_lights[count].dir[2] = g_lights[i].direction.position.z;
        gpu_lights[count].dir[3] = 0.0f;
        gpu_lights[count].color[0] = g_lights[i].color.position.x;
        gpu_lights[count].color[1] = g_lights[i].color.position.y;
        gpu_lights[count].color[2] = g_lights[i].color.position.z;
        gpu_lights[count].color[3] = 1.0f;
        gpu_lights[count].range = g_lights[i].range;
        gpu_lights[count].inner_cos = (float)cos(g_lights[i].spot_inner_angle);
        gpu_lights[count].outer_cos = (float)cos(g_lights[i].spot_outer_angle);
        gpu_lights[count].falloff = g_lights[i].spot_falloff;
        count++;
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_light_ssbo);
    C89GL_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(gpu_light_t), gpu_lights);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/* ---- Dispatch compute shader ---- */
static void dispatch_cluster_build(void) {
    if (!gl_cluster_program) return;
    upload_lights_to_ssbo();

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);

    C89GL_glUseProgram(gl_cluster_program);
    C89GL_glUniform1i(cluster_u_depth_tex, 0);
    C89GL_glUniform1i(cluster_u_num_lights, g_light_count);
    C89GL_glUniform1i(cluster_u_tile_size, CLUSTER_TILE_SIZE);
    C89GL_glUniform1i(cluster_u_num_tiles_x, gl_num_tiles_x);
    C89GL_glUniform1i(cluster_u_num_tiles_y, gl_num_tiles_y);
    C89GL_glUniform1i(cluster_u_depth_slices, CLUSTER_DEPTH_SLICES);
    C89GL_glUniform1f(cluster_u_near, gl_near);
    C89GL_glUniform1f(cluster_u_far, gl_far);

    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_light_ssbo);
    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_cluster_ssbo);
    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_cluster_offset_ssbo);

    GLuint groups_x = (gl_num_tiles_x + 15) / 16;
    GLuint groups_y = (gl_num_tiles_y + 15) / 16;
    C89GL_glDispatchCompute(groups_x, groups_y, 1);

    C89GL_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    C89GL_glUseProgram(0);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    C89GL_glActiveTexture(GL_TEXTURE0);
}

/* ---- Init cluster resources ---- */
static void init_cluster_resources(void) {
    GLuint cs = compile_shader_with_defines(GL_COMPUTE_SHADER, "cluster.comp", "#version 430 core\n");
    if (!cs) {
        printf("ERROR: Failed to compile cluster compute shader.\n");
        return;
    }
    gl_cluster_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_cluster_program, cs);
    C89GL_glLinkProgram(gl_cluster_program);
    GLint link_status;
    C89GL_glGetProgramiv(gl_cluster_program, GL_LINK_STATUS, &link_status);
    if (!link_status) {
        char log[512];
        C89GL_glGetProgramInfoLog(gl_cluster_program, sizeof(log), NULL, log);
        printf("Cluster program link error:\n%s\n", log);
        C89GL_glDeleteProgram(gl_cluster_program);
        gl_cluster_program = 0;
        C89GL_glDeleteShader(cs);
        return;
    }
    C89GL_glDeleteShader(cs);

    cluster_u_depth_tex = C89GL_glGetUniformLocation(gl_cluster_program, "uDepthTex");
    cluster_u_num_lights = C89GL_glGetUniformLocation(gl_cluster_program, "uNumLights");
    cluster_u_tile_size = C89GL_glGetUniformLocation(gl_cluster_program, "uTileSize");
    cluster_u_num_tiles_x = C89GL_glGetUniformLocation(gl_cluster_program, "uNumTilesX");
    cluster_u_num_tiles_y = C89GL_glGetUniformLocation(gl_cluster_program, "uNumTilesY");
    cluster_u_depth_slices = C89GL_glGetUniformLocation(gl_cluster_program, "uDepthSlices");
    cluster_u_near = C89GL_glGetUniformLocation(gl_cluster_program, "uNear");
    cluster_u_far = C89GL_glGetUniformLocation(gl_cluster_program, "uFar");

    C89GL_glGenBuffers(1, &gl_light_ssbo);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_light_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_LIGHTS * sizeof(gpu_light_t), NULL, GL_DYNAMIC_DRAW);

    gl_num_tiles_x = (gl_render_width + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_tiles_y = (gl_render_height + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_clusters = gl_num_tiles_x * gl_num_tiles_y * CLUSTER_DEPTH_SLICES;

    size_t cluster_list_size = gl_num_clusters * CLUSTER_MAX_LIGHTS_PER * sizeof(GLuint);
    C89GL_glGenBuffers(1, &gl_cluster_ssbo);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, cluster_list_size, NULL, GL_DYNAMIC_DRAW);

    size_t offset_size = gl_num_clusters * sizeof(GLuint);
    C89GL_glGenBuffers(1, &gl_cluster_offset_ssbo);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_offset_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, offset_size, NULL, GL_DYNAMIC_DRAW);

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    printf("Clustered rendering initialised: tiles=%dx%d, clusters=%d\n",
           gl_num_tiles_x, gl_num_tiles_y, gl_num_clusters);
}

/* ---- Public API: Light management ---- */
void render_clear_lights(void) {
    g_light_count = 0;
}

void render_set_light_at_index(int index, const light_definition *def) {
    if (index < 0 || index >= MAX_LIGHTS) return;
    g_lights[index] = *def;
    if (index + 1 > g_light_count) g_light_count = index + 1;
}

/* ---- Set uniforms for a shader variant ---- */
static void set_uniforms_for_variant(shader_variant_t* variant, int is_depth_pass) {
    if (!variant) return;
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
    if (variant->u_gouraud_blend != -1)
        C89GL_glUniform1f(variant->u_gouraud_blend, 0.0f);

    if (!is_depth_pass) {
        if (variant->u_depth_tex != -1) {
            C89GL_glActiveTexture(GL_TEXTURE1);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
            C89GL_glUniform1i(variant->u_depth_tex, 1);
            C89GL_glActiveTexture(GL_TEXTURE0);
        }
        if (variant->u_screen_size != -1)
            C89GL_glUniform2f(variant->u_screen_size, (float)gl_render_width, (float)gl_render_height);
        if (variant->u_num_lights != -1)
            C89GL_glUniform1i(variant->u_num_lights, g_light_count);
        if (variant->u_num_tiles_x != -1)
            C89GL_glUniform1i(variant->u_num_tiles_x, gl_num_tiles_x);
        if (variant->u_num_tiles_y != -1)
            C89GL_glUniform1i(variant->u_num_tiles_y, gl_num_tiles_y);

        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_light_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_cluster_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_cluster_offset_ssbo);
    }

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

/* ---- Flush transparent batches ---- */
static void flush_transparent_batches(void) {
    int i, j;
    if (gl_transparent_count == 0) return;
    qsort(gl_transparent_tris, gl_transparent_count, sizeof(transparent_tri_t), transparent_compare);

    i = 0;
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

            for (j = start; j < i; j++) {
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
                vec3 localFaceNormal = vec3_normalize(vec3_cross(vec3_sub(t->v1, t->v0), vec3_sub(t->v2, t->v0)));
                vec3 localCentroid = vec3_div_scalar(vec3_add(vec3_add(t->v0, t->v1), t->v2), 3.0f);
                #define PACK_V(v, n, l, lfn, lc, mi) \
                    *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
                    *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
                    *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
                    *(ptr++) = (float)(mi); \
                    *(ptr++) = (lfn).position.x; *(ptr++) = (lfn).position.y; *(ptr++) = (lfn).position.z; \
                    *(ptr++) = (lc).position.x; *(ptr++) = (lc).position.y; *(ptr++) = (lc).position.z;
                PACK_V(t->v0, t->n0, t->v0, localFaceNormal, localCentroid, t->model_index);
                PACK_V(t->v1, t->n1, t->v1, localFaceNormal, localCentroid, t->model_index);
                PACK_V(t->v2, t->n2, t->v2, localFaceNormal, localCentroid, t->model_index);
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

    if (triangle_outside_frustum(world_v0, world_v1, world_v2)) return;

    vec3 localFaceNormal = {0,0,0}, localCentroid = {0,0,0};
    u32 mode = mat->render_method & 0x7;
    if (mode == MODE_FLAT || mode == MODE_WIREFRAME) {
        localFaceNormal = vec3_normalize(vec3_cross(vec3_sub(local_v1, local_v0), vec3_sub(local_v2, local_v0)));
        localCentroid = vec3_div_scalar(vec3_add(vec3_add(local_v0, local_v1), local_v2), 3.0f);
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
    int i;
    for (i = 0; i < gl_batch_count; i++) {
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
    #define PACK_V(v, n, l, lfn, lc, mi) \
        *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
        *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
        *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
        *(ptr++) = (float)(mi); \
        *(ptr++) = (lfn).position.x; *(ptr++) = (lfn).position.y; *(ptr++) = (lfn).position.z; \
        *(ptr++) = (lc).position.x; *(ptr++) = (lc).position.y; *(ptr++) = (lc).position.z;
    PACK_V(local_v0, local_n0, local_v0, localFaceNormal, localCentroid, model_index);
    PACK_V(local_v1, local_n1, local_v1, localFaceNormal, localCentroid, model_index);
    PACK_V(local_v2, local_n2, local_v2, localFaceNormal, localCentroid, model_index);
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

    u32 p, t;
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
            static material_definition fallback = DEFAULT_MATERIAL_PLASTIC;
            mat = &fallback;
        }

        model_vertex *verts = (model_vertex*)prim->vertices.address;
        u16 *indices = (u16*)prim->indices.address;
        u32 tri_count = prim->indices.count / 3;
        for (t = 0; t < tri_count; ++t) {
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
    float entity_depth) { /* empty */ }

/* ================================================================
   PUBLIC API IMPLEMENTATION
   ================================================================ */

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

    C89GL_glGenBuffers(1, &gl_material_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_material_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(material_ubo_t), NULL, GL_DYNAMIC_DRAW);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    C89GL_glGenBuffers(1, &gl_model_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_model_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, MAX_MODEL_MATRICES * sizeof(mat4), NULL, GL_STREAM_DRAW);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

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

    C89GL_glGenTextures(1, &gl_depth_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);

    GLenum fbo_status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO incomplete! status=0x%x\n", fbo_status);
        return 0;
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    C89GL_glEnable(GL_CULL_FACE);
    C89GL_glFrontFace(GL_CCW);

    init_cluster_resources();

    printf("render_init returning 1 (success)\n");
    return 1;
}

void render_shutdown(void) {
    int i;
    if (gl_vertex_vbo) { C89GL_glDeleteBuffers(1, &gl_vertex_vbo); gl_vertex_vbo = 0; }
    if (gl_index_vbo) { C89GL_glDeleteBuffers(1, &gl_index_vbo); gl_index_vbo = 0; }
    if (gl_vao) { C89GL_glDeleteVertexArrays(1, &gl_vao); gl_vao = 0; }
    if (gl_fbo) { C89GL_glDeleteFramebuffers(1, &gl_fbo); gl_fbo = 0; }
    if (gl_color_tex) { C89GL_glDeleteTextures(1, &gl_color_tex); gl_color_tex = 0; }
    if (gl_depth_tex) { C89GL_glDeleteTextures(1, &gl_depth_tex); gl_depth_tex = 0; }
    if (gl_material_ubo) { C89GL_glDeleteBuffers(1, &gl_material_ubo); gl_material_ubo = 0; }
    if (gl_model_ubo) { C89GL_glDeleteBuffers(1, &gl_model_ubo); gl_model_ubo = 0; }
    if (gl_light_ssbo) { C89GL_glDeleteBuffers(1, &gl_light_ssbo); gl_light_ssbo = 0; }
    if (gl_cluster_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_ssbo); gl_cluster_ssbo = 0; }
    if (gl_cluster_offset_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_offset_ssbo); gl_cluster_offset_ssbo = 0; }
    if (gl_cluster_program) { C89GL_glDeleteProgram(gl_cluster_program); gl_cluster_program = 0; }
    if (gl_vertex_pool) { free(gl_vertex_pool); gl_vertex_pool = NULL; }
    if (gl_index_pool) { free(gl_index_pool); gl_index_pool = NULL; }
    if (gl_shader_cache) {
        for (i = 0; i < gl_shader_cache_size; i++)
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
    render_particle_system_shutdown();
}

void render_draw_entities(struct entity_definition **entities, int count) {
    int i, valid_count;
    if (!entities || count <= 0) return;

    gl_model_count = 0;
    for (i = 0; i < count && gl_model_count < MAX_MODEL_MATRICES; i++) {
        entity_definition *ent = entities[i];
        if (!ent || ent->model.handle < 0) continue;
        gl_model_matrices[gl_model_count] = entity_model_matrix(ent);
        gl_model_count++;
    }
    update_model_ubo();

    entity_sort_t *sorted = (entity_sort_t*)malloc(count * sizeof(entity_sort_t));
    if (!sorted) return;
    valid_count = 0;
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

void render_draw_entity(const struct entity_definition *ent) {
    if (!ent) return;
    struct entity_definition *ents[1] = { (struct entity_definition*)ent };
    render_draw_entities(ents, 1);
}

/* ---- Other API functions ---- */
void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    gl_light_dir = dir; gl_light_col = col; gl_ambient_col = amb;
}

void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    gl_cam_eye = eye;
    gl_view = mat4_lookat(eye, center, up);
    gl_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    gl_view_proj = mat4_mul(gl_proj, gl_view);
    extract_frustum_planes();
    gl_near = 0.05f;
    gl_far = 1000.0f;
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
    return 0;
}

void render_set_render_resolution(i32 rw, i32 rh) {
    if (rw <= 0 || rh <= 0) return;
    if (gl_render_width == rw && gl_render_height == rh) return;
    gl_render_width = rw; gl_render_height = rh;
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
    gl_num_tiles_x = (gl_render_width + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_tiles_y = (gl_render_height + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_clusters = gl_num_tiles_x * gl_num_tiles_y * CLUSTER_DEPTH_SLICES;
    size_t list_size = gl_num_clusters * CLUSTER_MAX_LIGHTS_PER * sizeof(GLuint);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, list_size, NULL, GL_DYNAMIC_DRAW);
    size_t off_size = gl_num_clusters * sizeof(GLuint);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_offset_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, off_size, NULL, GL_DYNAMIC_DRAW);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
i32 render_get_render_width(void) { return gl_render_width; }
i32 render_get_render_height(void) { return gl_render_height; }

/* ---- Batch comparator ---- */
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

static void render_particle_system_draw_internal(void);

/* ---- render_finish ---- */
void render_finish(void) {
    int i;
    GLuint current_program = 0;

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

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glBindVertexArray(gl_vao);

        // ---- Depth pre‑pass ----
        C89GL_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        C89GL_glDepthMask(GL_TRUE);
        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (b->is_transparent) continue;
            shader_variant_t *variant = get_program_for_method(b->mat->render_method, 1);
            if (!variant) continue;
            if (current_program != variant->program) {
                C89GL_glUseProgram(variant->program);
                current_program = variant->program;
            }
            update_material_ubo(b->mat);
            set_uniforms_for_variant(variant, 1);
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT,
                                 (void*)(b->index_offset * sizeof(GLushort)));
        }
        if (current_program) C89GL_glUseProgram(0);

        // ---- Compute cluster lists ----
        dispatch_cluster_build();

        C89GL_glDepthFunc(GL_LEQUAL);

        // ---- Colour pass (opaque) ----
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        C89GL_glDepthMask(GL_TRUE);
        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (b->is_transparent) continue;
            shader_variant_t *variant = get_program_for_method(b->mat->render_method, 0);
            if (!variant) continue;
            if (current_program != variant->program) {
                C89GL_glUseProgram(variant->program);
                current_program = variant->program;
            }
            update_material_ubo(b->mat);
            set_uniforms_for_variant(variant, 0);
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT,
                                 (void*)(b->index_offset * sizeof(GLushort)));
        }
        if (current_program) C89GL_glUseProgram(0);

        // ---- Transparent pass ----
        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        C89GL_glDepthMask(GL_FALSE);
        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (!b->is_transparent) continue;
            shader_variant_t *variant = get_program_for_method(b->mat->render_method, 0);
            if (!variant) continue;
            if (current_program != variant->program) {
                C89GL_glUseProgram(variant->program);
                current_program = variant->program;
            }
            update_material_ubo(b->mat);
            set_uniforms_for_variant(variant, 0);
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT,
                                 (void*)(b->index_offset * sizeof(GLushort)));
        }
        if (current_program) C89GL_glUseProgram(0);
        C89GL_glDisable(GL_BLEND);
        C89GL_glDepthMask(GL_TRUE);

        C89GL_glBindVertexArray(0);
        render_particle_system_draw_internal();
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

/* ========================================================================
   Particle system (unchanged)
   ======================================================================== */

static INLINE float rand_float(float min, float max) {
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}
static INLINE vec3 rand_vec3(float min, float max) {
    vec3 v = {rand_float(min, max), rand_float(min, max), rand_float(min, max)};
    return v;
}
static INLINE vec3 rand_sphere(float radius) {
    vec3 v;
    do { v = rand_vec3(-1.0f, 1.0f); } while (vec3_magnitude(v) > 1.0f);
    return vec3_mul_scalar(v, radius);
}

static void spawn_particle(void) {
    int i;
    if (g_particle_count >= g_particle_capacity) return;
    particle_instance_t *p = &g_particles[g_particle_count];
    vec3 *vel = &g_particle_velocities[g_particle_count];
    float *life = &g_particle_lifetimes[g_particle_count];
    float *max_life = &g_particle_max_lifetimes[g_particle_count];

    p->center = vec3_add(g_emitter_pos, rand_sphere(0.1f));

    vec3 dir = rand_vec3(-1.0f, 1.0f);
    dir = vec3_normalize(dir);
    *vel = vec3_mul_scalar(dir, g_emitter_speed * rand_float(0.5f, 1.5f));

    *max_life = rand_float(g_emitter_lifetime * 0.8f, g_emitter_lifetime * 1.2f);
    *life = *max_life;

    p->size = rand_float(g_emitter_size * 0.8f, g_emitter_size * 1.2f);

    p->color = vec4_init_from_4(g_emitter_color.position.x, g_emitter_color.position.y,
                                g_emitter_color.position.z, g_emitter_alpha);

    g_particle_count++;
}

void render_particle_system_init(int max_particles) {
    if (g_particles) return;
    g_particle_capacity = max_particles > 0 ? max_particles : 4096;
    g_particles = (particle_instance_t*)malloc(g_particle_capacity * sizeof(particle_instance_t));
    g_particle_velocities = (vec3*)malloc(g_particle_capacity * sizeof(vec3));
    g_particle_lifetimes = (float*)malloc(g_particle_capacity * sizeof(float));
    g_particle_max_lifetimes = (float*)malloc(g_particle_capacity * sizeof(float));
    if (!g_particles || !g_particle_velocities || !g_particle_lifetimes || !g_particle_max_lifetimes) {
        fprintf(stderr, "Failed to allocate particle memory\n");
        render_particle_system_shutdown();
        return;
    }
    g_particle_count = 0;
    g_emission_timer = 0.0f;
    g_burst_done = 0;

    GLuint vs = compile_shader_with_defines(GL_VERTEX_SHADER, "particle.vert", "#version 330 core\n");
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, "particle.frag", "#version 330 core\n");
    if (!vs || !fs) {
        if (vs) C89GL_glDeleteShader(vs);
        if (fs) C89GL_glDeleteShader(fs);
        fprintf(stderr, "Failed to compile particle shaders\n");
        render_particle_system_shutdown();
        return;
    }
    g_particle_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(g_particle_program, vs);
    C89GL_glAttachShader(g_particle_program, fs);
    C89GL_glLinkProgram(g_particle_program);
    GLint status;
    C89GL_glGetProgramiv(g_particle_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        C89GL_glGetProgramInfoLog(g_particle_program, sizeof(log), NULL, log);
        printf("Particle program link error:\n%s\n", log);
        C89GL_glDeleteProgram(g_particle_program);
        g_particle_program = 0;
        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);
        render_particle_system_shutdown();
        return;
    }
    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);

    C89GL_glGenVertexArrays(1, &g_particle_vao);
    C89GL_glGenBuffers(1, &g_particle_vbo);

    C89GL_glBindVertexArray(g_particle_vao);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_particle_vbo);
    C89GL_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                sizeof(particle_instance_t), (void*)offsetof(particle_instance_t, center));
    C89GL_glEnableVertexAttribArray(0);
    C89GL_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                                sizeof(particle_instance_t), (void*)offsetof(particle_instance_t, color));
    C89GL_glEnableVertexAttribArray(1);
    C89GL_glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE,
                                sizeof(particle_instance_t), (void*)offsetof(particle_instance_t, size));
    C89GL_glEnableVertexAttribArray(2);
    C89GL_glVertexAttribDivisor(0, 1);
    C89GL_glVertexAttribDivisor(1, 1);
    C89GL_glVertexAttribDivisor(2, 1);
    C89GL_glBindVertexArray(0);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, 0);

    g_particle_u_view_proj = C89GL_glGetUniformLocation(g_particle_program, "uViewProj");
    g_particle_u_cam_right = C89GL_glGetUniformLocation(g_particle_program, "uCamRight");
    g_particle_u_cam_up = C89GL_glGetUniformLocation(g_particle_program, "uCamUp");
}

void render_particle_system_shutdown(void) {
    if (g_particles) { free(g_particles); g_particles = NULL; }
    if (g_particle_velocities) { free(g_particle_velocities); g_particle_velocities = NULL; }
    if (g_particle_lifetimes) { free(g_particle_lifetimes); g_particle_lifetimes = NULL; }
    if (g_particle_max_lifetimes) { free(g_particle_max_lifetimes); g_particle_max_lifetimes = NULL; }
    if (g_particle_program) C89GL_glDeleteProgram(g_particle_program);
    if (g_particle_vbo) C89GL_glDeleteBuffers(1, &g_particle_vbo);
    if (g_particle_vao) C89GL_glDeleteVertexArrays(1, &g_particle_vao);
    g_particle_count = 0;
    g_particle_capacity = 0;
    g_particle_program = 0;
}

void render_particle_system_set_emitter(const struct particle_emitter_definition *def) {
    if (!def) return;
    g_emitter_pos       = def->position;
    g_emitter_color     = def->color;
    g_emitter_alpha     = def->alpha;
    g_emitter_size      = def->size;
    g_emitter_lifetime  = def->lifetime;
    g_emitter_speed     = def->speed;
    g_emitter_spread    = def->spread;
    g_emitter_gravity   = def->gravity;
    g_emitter_loop      = def->loop;
    g_emission_rate     = def->emission_rate > 0.0f ? def->emission_rate : 30.0f;
    g_emission_timer    = 0.0f;
    g_burst_done        = 0;
}

void render_particle_system_update(float dt) {
    int i;
    if (!g_particles) return;

    if (g_emitter_loop) {
        g_emission_timer += dt;
        float interval = 1.0f / g_emission_rate;
        while (g_emission_timer >= interval) {
            spawn_particle();
            g_emission_timer -= interval;
        }
    } else {
        if (!g_burst_done) {
            int burst = (int)(g_emission_rate * 0.5f);
            for (i = 0; i < burst; ++i) spawn_particle();
            g_burst_done = 1;
        }
    }

    i = 0;
    while (i < g_particle_count) {
        float *life = &g_particle_lifetimes[i];
        *life -= dt;
        if (*life <= 0.0f) {
            g_particles[i] = g_particles[g_particle_count-1];
            g_particle_velocities[i] = g_particle_velocities[g_particle_count-1];
            g_particle_lifetimes[i] = g_particle_lifetimes[g_particle_count-1];
            g_particle_max_lifetimes[i] = g_particle_max_lifetimes[g_particle_count-1];
            g_particle_count--;
            continue;
        }

        vec3 grav = vec3_init_from_3(0.0f, g_emitter_gravity, 0.0f);
        g_particle_velocities[i] = vec3_add(g_particle_velocities[i], vec3_mul_scalar(grav, dt));
        g_particles[i].center = vec3_add(g_particles[i].center, vec3_mul_scalar(g_particle_velocities[i], dt));

        float frac = 1.0f - (g_particle_lifetimes[i] / g_particle_max_lifetimes[i]);
        g_particles[i].color.color.a = g_emitter_alpha * (1.0f - frac);

        ++i;
    }
}

void render_particle_system_set_camera(const mat4 *view_proj, vec3 cam_right, vec3 cam_up) {
    g_particle_view_proj = *view_proj;
    g_particle_cam_right = cam_right;
    g_particle_cam_up = cam_up;
    g_particle_cam_valid = 1;
}

static void render_particle_system_draw_internal(void) {
    if (g_particle_count == 0 || !g_particle_program) return;

    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_particle_vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER,
                       g_particle_count * sizeof(particle_instance_t),
                       g_particles, GL_STREAM_DRAW);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glDepthFunc(GL_LESS);
    C89GL_glDepthMask(GL_FALSE);

    if (!g_particle_cam_valid) {
        g_particle_view_proj = gl_view_proj;
        g_particle_cam_right = vec3_init_from_3(gl_view.transpose[0][0], gl_view.transpose[0][1], gl_view.transpose[0][2]);
        g_particle_cam_up    = vec3_init_from_3(gl_view.transpose[1][0], gl_view.transpose[1][1], gl_view.transpose[1][2]);
        g_particle_cam_valid = 1;
    }

    C89GL_glUseProgram(g_particle_program);
    C89GL_glUniformMatrix4fv(g_particle_u_view_proj, 1, GL_TRUE, (float*)&g_particle_view_proj);

    float aspect = (gl_render_width > 0 && gl_render_height > 0) ? ((float)gl_render_width / (float)gl_render_height) : 1.0f;
    vec3 cam_right_scaled = vec3_mul_scalar(g_particle_cam_right, 1.0f / aspect);
    C89GL_glUniform3fv(g_particle_u_cam_right, 1, (float*)&cam_right_scaled);
    C89GL_glUniform3fv(g_particle_u_cam_up, 1, (float*)&g_particle_cam_up);

    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    C89GL_glBindVertexArray(g_particle_vao);
    C89GL_glDrawArraysInstanced(GL_TRIANGLES, 0, 6, g_particle_count);
    C89GL_glBindVertexArray(0);

    C89GL_glDepthMask(GL_TRUE);
    C89GL_glDisable(GL_BLEND);
    C89GL_glUseProgram(0);
    C89GL_glEnable(GL_DEPTH_TEST);
}

void render_particle_system_emit_burst(int count) {
    int i;
    if (!g_particles) return;
    for (i = 0; i < count; ++i) {
        spawn_particle();
    }
}

#endif /* RASTERIZER_GL_IMPLEMENTATION */