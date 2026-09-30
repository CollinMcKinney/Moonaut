/*
 * rasterizer_GL.h – GPU-accelerated forward renderer with clustered lighting
 *
 * Requires OpenGL 4.3+ (compute shaders, SSBOs).
 * Uses depth pre-pass + compute light culling + per-pixel shading.
 *
 * Shader files (read from disk):
 *   material.vert    – vertex shader (same for depth and color passes)
 *   material.frag    – fragment shader with #ifdef DEPTH_ONLY and
 *                      #ifdef WBOIT_PASS guards
 *   sky.frag         – procedural sky; compiled twice, once for cube faces
 *                      (feeding the environment cube) and once
 *                      (SKYBOX_MODE) for the main-pass visible sky
 *   post_process.frag – HDR->LDR resolve
 *   cluster.comp     – compute shader for light culling
 *   vbao.frag, vbao_blur.frag – ambient occlusion
 *   fullscreen.vert, particle.vert, particle.frag – particles
 *   oit_composite.frag – weighted-blended OIT composite
 *   audio_*.comp     – audio compute shaders
 *
 * Geometry submission (per-primitive GPU cache):
 *   Every model_primitive is uploaded to its own VAO/VBO/IBO the first
 *   time it is drawn, in the 16-float layout the shader expects. Once
 *   uploaded it is never touched again. Per frame, each (entity,
 *   primitive) pair emits one draw_call_t; draw calls are sorted for
 *   state-change batching and drawn with per-primitive VAOs. The model
 *   index is a per-draw vertex attrib constant (glVertexAttrib1f on
 *   attribute 3), so no per-vertex model index is stored.
 *
 *   This replaces the previous design, where every frame repacked every
 *   triangle into a global CPU vertex pool and uploaded it. For a level
 *   the size of Sponza that was ~50 MB of CPU writes and PCIe traffic
 *   per frame.
 *
 *   Consequence: static geometry is assumed. If a model is unloaded
 *   and reloaded, the cache key (the model_primitive pointer) becomes
 *   stale. Future work: an explicit gpu_primitive_invalidate_model()
 *   called from the tag unload path.
 *
 * Environment cube:
 *   gl_sky_cube is a single RGBA16F cubemap, 256^2 per face, mipmapped.
 *   It is filled each frame by sky.frag (cube-face mode) via
 *   render_sky_cube_pass(). material.frag samples it through
 *   sample_env_map() for both diffuse irradiance and specular reflection.
 *
 *   Because it contains sky only — no scene geometry — reflections are
 *   reflections of the sky. A baked environment probe system will replace
 *   this later; the pipeline is structured so that swapping the source of
 *   gl_sky_cube is a single-pass change.
 *
 * Depth cube:
 *   gl_depth_cube is a 256^2-per-face depth cubemap filled each frame
 *   by render_depth_cube_pass(). It contains scene geometry from the
 *   camera's point of view, rendered with DEPTH_ONLY variants. Its
 *   current consumer is the audio compute shaders (occlusion, reverb,
 *   portal), which sample it to determine sound propagation and
 *   occlusion; nothing in the material or sky pipelines references it.
 *   It is a general-purpose scene depth cube — future realtime
 *   environment-mapping work may sample the same texture.
 *
 *   The depth-cube pass draws from the sorted draw call list, so it
 *   sees every acoustically solid surface (opaque, alpha-blended, and
 *   transmissive geometry). None of those materials discard fragments
 *   in the DEPTH_ONLY variant, so a single bare-key program is used.
 *
 * Audio compute fence contract:
 *   The three audio compute dispatches (occlusion, reverb, portal) are
 *   asynchronous. Each queues a dispatch and immediately records a
 *   GLsync fence. The dispatch is gated on the previous fence having
 *   been cleared, so the SSBO the CPU reads from is never being written
 *   to concurrently.
 *
 *   THE GAME MUST CALL EACH POLL FUNCTION (render_poll_audio_propagation,
 *   render_poll_audio_global_stats, render_poll_audio_portal) ONCE PER
 *   FRAME, regardless of whether it needs the result. The poll is what
 *   clears the fence. If the game skips the poll, the fence stays set,
 *   the next frame's dispatch is skipped, and the SSBO serves stale
 *   data until the game finally polls. This was the cause of an
 *   observed "audio stuck for a few seconds" bug.
 *
 *   As a safety net, if a fence survives more than
 *   AUDIO_FENCE_MAX_AGE_FRAMES frames without being cleared by a poll,
 *   dispatch_audio_compute force-clears it and lets the next dispatch
 *   proceed. This risks a benign race with a stuck GPU read, which is
 *   preferable to a permanent freeze.
 *
 * Visible sky:
 *   After the opaque depth pre-pass, render_sky_pass draws the same
 *   procedural sky as a full-screen triangle at z = 1 with depth test
 *   LEQUAL and depth write off.
 *
 * Ambient scale:
 *   uSkyAmbientScale multiplies the *diffuse* ambient term only. It does
 *   not affect reflections.
 *
 * GL STATE HYGIENE:
 *   The sky-cube pass, depth-cube pass, sky pass, and post-process pass
 *   save and restore depth state, blend state, draw buffers, FBO, and
 *   viewport.
 *
 * Usage:
 *   #define RASTERIZER_GL_IMPLEMENTATION
 *   #include "rasterizer_GL.h"
 *   render_init(win_w, win_h);
 *   render_set_resolution_scale(0.75f);              // optional, default 80%
 *   render_set_sky(zenith, horizon, ground, 2.0f);      // optional
 *   render_set_clouds(cloud_color, 0.55f);              // optional
 *   render_set_sky_ambient_scale(0.5f);                 // optional
 *   ... draw ...
 *   render_finish();
 *
 * Resolution handling:
 *   The internal render resolution is derived from the window size multiplied
 *   by gl_resolution_scale (default 0.8f). render_resize() recomputes the
 *   render resolution from the new window size and the current scale.
 *   render_set_render_resolution() overrides the internal resolution
 *   directly. render_set_resolution_scale() changes the scale and recomputes
 *   from the window size. Scales above 1.0 supersample: the AA pass is
 *   skipped and the resolve downsamples to the window resolution.
 */

#define AUDIO_OCCLUSION
#define AUDIO_REVERB
#define AUDIO_PORTAL

#ifndef RASTERIZER_GL_H
#define RASTERIZER_GL_H

#include "common.h"
#include "tags/entity.h"
#include "tags/model.h"
#include "tags/animation.h"
#include "animation.h"
#include "tags/material.h"
#include "tags/particle_emitter.h"
#include "tags/light.h"
#include "window.h"

#define C89GL_IMPLEMENTATION
#include "../libs/C89FW/C89GL.h"

typedef enum {
    ALPHA_PASS_BEHIND = 0,
    ALPHA_PASS_FRONT  = 1
} alpha_pass_side;

typedef struct audio_propagation_output {
    float occlusion;
} audio_propagation_output_t;

typedef struct audio_global_stats {
    real avg_depth;
    real min_depth;
    real max_depth;
    real variance;
} audio_global_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

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
void render_draw_entities(struct entity_definition **entities, int count, const i32 *entity_handles);
void render_finish(void);
const u32* render_get_fb(void);
int render_resize(i32 new_w, i32 new_h);
void render_set_render_resolution(i32 render_width, i32 render_height);
void render_set_resolution_scale(float scale);  /* 0.01..2.0, 2.0 = SSAA, default 0.8 */
void render_set_anti_aliasing(int enabled);   /* 0 = blit, 1 = FXAA/SSAA, default 1 */
void render_set_vbao(int enabled);             /* 0 = skip AO passes, default 1 */
void render_set_dither(int enabled);           /* 0 = skip present dither, default 1 */
int  render_set_vsync(int enabled);           /* 1 = sync to refresh; 0 = present free */
void render_set_sky_cube(int enabled);        /* IBL cube pass */
void render_set_skybox(int enabled);          /* main-pass skybox */
i32 render_get_render_width(void);
i32 render_get_render_height(void);
static INLINE u8 color_to_u8(real x);

void render_set_exposure(real exposure);
void render_set_gamma(real gamma);

/* Bloom. intensity is the contribution of the glow relative to the tonemapped
 * scene, threshold the scene-referred luminance that starts to bloom (after
 * exposure), and knee the width of the soft ramp around it — larger knee fades
 * highlights in over a wider band instead of switching them on at a hard edge. */
void render_set_bloom(int enabled, real intensity, real threshold, real knee);
void render_set_bloom_radius(real radius);

void render_particle_system_init(int max_particles);
void render_particle_system_shutdown(void);
void render_particle_system_set_emitter(const struct particle_emitter_definition *def);
void render_particle_system_update(float dt);
void render_particle_system_set_camera(const mat4 *view_proj, vec3 cam_right, vec3 cam_up);
void render_particle_system_emit_burst(int count);
void render_precompile_material(u32 method);

void render_clear_lights(void);
void render_set_light_at_index(int index, const struct light_definition *def);

/* ---- Environment cube ---- */
void render_set_env_probe_box(vec3 boxMin, vec3 boxMax);
void render_set_env_cube_enabled(int enabled);

/* ---- Procedural sky ---- */
void render_set_sky(vec3 zenith, vec3 horizon, vec3 ground, real exponent);
void render_set_clouds(vec3 color, real coverage);
void render_set_sky_ambient_scale(real scale);

/* ---- Audio compute polls ---- */
#ifdef AUDIO_OCCLUSION
void render_set_audio_voice_data(const vec3 *positions, int count);
int  render_poll_audio_propagation(audio_propagation_output_t *out, int max_voices);
#endif

#ifdef AUDIO_REVERB
int  render_poll_audio_global_stats(audio_global_stats_t *stats);
#endif

#ifdef AUDIO_PORTAL
void render_trigger_portal_search(void);
int  render_poll_audio_portal(vec3 *portal_positions, float *portal_distances, int *portal_active_flags, int max_voices);
#endif

#define RENDER_MAX_LIGHTS (MAX_EXTRA_DIR_LIGHTS + MAX_EXTRA_POINT_LIGHTS + MAX_EXTRA_SPOT_LIGHTS)

#ifdef __cplusplus
}
#endif

#endif /* RASTERIZER_GL_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#define RASTERIZER_GL_IMPLEMENTATION
#ifdef RASTERIZER_GL_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MAX_EXTRA_DIR_LIGHTS   16
#define MAX_EXTRA_POINT_LIGHTS 64
#define MAX_EXTRA_SPOT_LIGHTS  64
/* One definition of the light budget. This was spelled out twice — once here as
 * MAX_LIGHTS and once in the header above as RENDER_MAX_LIGHTS — with the two
 * kept in sync only by hand. They are the same number; RENDER_MAX_LIGHTS is the
 * public-facing name bounds checks use, so this is now an alias of it rather
 * than a second copy of the same arithmetic. */
#define MAX_LIGHTS             RENDER_MAX_LIGHTS

#define CLUSTER_TILE_SIZE       16
#define CLUSTER_DEPTH_SLICES    24
#define CLUSTER_MAX_LIGHTS_PER  64
/* Tiles per workgroup in cluster.comp. Injected as a define so the shader's
 * local_size and the dispatch group count below come from one value. */
#define CLUSTER_WORKGROUP_SIZE  8

/* 1 = post_process.frag shows the raw HDR scene buffer (magenta for NaN),
 * skipping tone map and the display-referred chain. Diagnostic for tracking
 * down where a black region originates; 0 = normal output. */
#define POST_PROCESS_DEBUG_SOURCE 0

/* Per-pass GPU timing with GL_TIME_ELAPSED, reported as rolling averages.
 * Compile-time opt-in. Reading results is non-blocking: each frame's queries
 * live in their own ring slot and are only read once the GPU has retired them,
 * a few frames later, so an enabled timer costs one glBeginQuery/glEndQuery
 * pair per pass rather than a pipeline stall. */
//#define GL_PASS_TIMING 1

#ifdef GL_PASS_TIMING
/* Both are defined next to render_finish; render_init reaches them first. */
static void init_pass_timing(void);
static void shutdown_pass_timing(void);
#endif

/* Reads the cluster offset buffer back and reports how the light lists are
 * distributed: occupancy, average and peak lights per cluster, clusters that
 * overflowed their 64-light budget, and the per-slice averages used to check
 * that logarithmic slicing is not wasting cluster capacity. Compile-time
 * opt-in; the readback is a full pipeline stall, so it is a measurement tool,
 * not something to leave enabled. */
//#define CLUSTER_DIAG

/* Two independent cube resolutions.
 *
 * PROBE_SIZE sizes the environment cube, which is the IBL source: it is
 * mipmapped and sampled for both diffuse irradiance and specular reflection, so
 * its resolution is a visible quality setting. 256 keeps sharp reflections.
 *
 * PROBE_DEPTH_SIZE sizes the depth cube read by the audio compute shaders.
 * That one is sampled with a single texture() per voice to answer "what is the
 * first surface along this direction" — a yes/no-ish occlusion and distance
 * query with no spatial detail to preserve. 128 costs nothing perceptible there
 * and cuts the pass's rasterised area by 4x.
 *
 * These were one constant. Sharing them meant lowering the audio probe also
 * lowered reflection quality, which is not a trade anyone should have to make
 * to save half a millisecond. */
#define PROBE_SIZE        256
#define PROBE_DEPTH_SIZE  128
#define PROBE_MIP_LEVELS  9
#define PROBE_FACE_COUNT  6

#define AUDIO_FENCE_MAX_AGE_FRAMES 10

#define ENV_CUBE_UNIT             5

#ifdef AUDIO_OCCLUSION
#define MAX_AUDIO_VOICES_GPU    8
typedef struct audio_voice_input {
    vec3 world_pos;
    float _pad;
} audio_voice_input_t;
#endif

#ifdef AUDIO_REVERB
#define MAX_REVERB_GROUPS ((64 + 7) / 8 * ((36 + 7) / 8))
#define REVERB_ACCUM_SIZE (MAX_REVERB_GROUPS * sizeof(reverb_group_accum_t))
typedef struct reverb_group_accum {
    float sum;
    float sumsq;
    u32   count;
    u32   min_bits;
    u32   max_bits;
} reverb_group_accum_t;
#endif

#ifdef AUDIO_PORTAL
typedef struct portal_candidate {
    float dist;
    float pos_x;
    float pos_y;
    float pos_z;
} portal_candidate_t;
#define PORTAL_CANDIDATE_SIZE (MAX_AUDIO_VOICES_GPU * sizeof(portal_candidate_t))
#endif

static INLINE real u32_to_real(u32 u) {
    union { u32 i; float f; } conv;
    conv.i = u;
    return (real)conv.f;
}

typedef struct {
    float pos[4];
    float dir[4];
    float color[4];
    float range;
    float inner_cos;
    float outer_cos;
    float falloff;
} gpu_light_t;

static C89GL_Context gl_ctx;
static i32 gl_win_width  = 0;
static i32 gl_win_height = 0;
static i32 gl_render_width  = 0;
static i32 gl_render_height = 0;
/* Render scale. 0.8, not 1.0 — the header comments used to claim 1.0 in two
 * places and 75% in a third, none of which matched. */
static real gl_resolution_scale = 0.8f;
static i32  gl_anti_aliasing_enabled = 1;
static i32  gl_vbao_enabled = 1;
static i32  gl_dither_enabled = 1;
static i32  gl_opaque_depth_prepass = 1;
static i32  gl_vsync_enabled = 0;
static i32  gl_sky_cube_enabled = 1;
static i32  gl_skybox_enabled = 1;

static i32 gl_ao_width  = 0;
static i32 gl_ao_height = 0;

#define MATERIAL_UBO_BINDING  0
#define MODEL_UBO_BINDING     1
#define VBAO_UBO_BINDING       2
#define VBAO_BLUR_UBO_BINDING  3

typedef struct {
    float  uMatAlbedo[3];               float uMatAlpha;                    // 16 bytes
    float  uMatTint[3];                 float uMatSpecularRoughness;        // 16 bytes
    float  uMatSpecularTint[3];         float uMatMetallic;                 // 16 bytes
    float  uMatF82Tint[3];              float uMatIOR;                      // 16 bytes

    float  uMatClearcoatColor[3];       float uMatClearcoat;                // 16 bytes
    float  uMatTransmissionTint[3];     float uMatTransmission;             // 16 bytes
    float  uMatSubsurfaceColor[3];      float uMatSubsurface;               // 16 bytes
    float  uMatSheenColor[3];           float uMatSheen;                    // 16 bytes

    float uMatSheenRoughness;
    float uMatDiffuseRoughness;
    float uMatTransmissionRoughness;
    float uMatClearcoatRoughness;                                          // 16 bytes

    float uMatAmbient;
    float uMatClearcoatIOR;
    float uMatThinFilm;
    float uMatThinFilmIOR;                                                  // 16 bytes

    float uMatAnisotropic;
    float uMatDiffraction;
    float uMatEmissivePulseFrequency;
    float uMatEmissivePulsePhase;                                           // 16 bytes

    float  uMatEmissiveColor[3];        float uMatEmissivePulseAmplitude;   // 16 bytes
    float  uMatRimColor[3];             float uMatRimExponent;              // 16 bytes
    float  uMatBackGlowColor[3];        float uMatStrobeFrequency;          // 16 bytes
    float  uMatStrobeColor[3];          float uMatStrobePhase;              // 16 bytes
    float  uMatGoochCool[3];            float uMatSaturation;               // 16 bytes
    float  uMatGoochWarm[3];            float uMatBumpWaveAmplitude;        // 16 bytes

    float uMatBumpWaveFrequency;
    float uMatBumpWaveSpeed;
    float uMatBumpNoise;
    float uMatGlitch;                                                       // 16 bytes

    int   uMatCelBands;
    int   uMatPosterizeLevels;
    float _pad296;
    float _pad300;                                                          // 16 bytes
} material_ubo_t;
STATIC_ASSERT(sizeof(material_ubo_t) == 304, material_ubo_t__size__wrong);

#define MAX_MODEL_MATRICES 1024

typedef struct {
    float inv_proj[16];
    float proj[16];
    float view[16];
    float screen_size[2];
    float near_plane;
    float far_plane;
    float proj_a;
    float proj_b;
    float inv_proj_00;
    float inv_proj_11;
} vbao_ubo_t;
STATIC_ASSERT(sizeof(vbao_ubo_t) == 224, vbao_ubo_t__size__wrong);

typedef struct {
    float screen_size[2];
    float depth_threshold;
    float _pad;
} vbao_blur_ubo_t;
STATIC_ASSERT(sizeof(vbao_blur_ubo_t) == 16, vbao_blur_ubo_t__size__wrong);

typedef struct {
    render_method key;
    GLuint program;
    int   is_depth;
    alpha_pass_side alpha_pass;
    int   hit_logged;
    GLint u_view_proj;
    GLint u_view;
    GLint u_light_dir;
    GLint u_light_col;
    GLint u_ambient_col;
    GLint u_cam_eye;
    GLint u_time;
    GLint u_fog_color;
    GLint u_fog_start;
    GLint u_fog_end;
    GLint u_depth_tex;
    GLint u_screen_size;
    GLint u_num_tiles_x;
    GLint u_num_tiles_y;
    GLint u_cluster_near;
    GLint u_cluster_far;
    GLint u_refraction_src;
    GLint u_ao_tex;
    GLint u_alpha_pass;
    GLint u_refraction_scale;
    GLint u_env_cube;
    GLint u_env_cube_max_mip;
    GLint u_sky_zenith;
    GLint u_sky_horizon;
    GLint u_sky_ground;
    GLint u_sky_exponent;
    GLint u_cloud_color;
    GLint u_cloud_coverage;
    GLint u_sky_ambient_scale;
    GLint u_joint_offset;
    GLint u_node_transform;
} shader_variant_t;

#define SHADER_CACHE_INITIAL_SIZE 64
#define SHADER_CACHE_MAX_LOAD_FACTOR 0.7f

static shader_variant_t *gl_shader_cache = NULL;
static int gl_shader_cache_size = 0;
static int gl_shader_cache_count = 0;
static int gl_shader_compilations = 0;

static GLuint gl_fullscreen_vs = 0;

static GLuint gl_material_ubo = 0;
static GLuint gl_model_ubo = 0;

static mat4 gl_view, gl_proj, gl_view_proj;
static vec3 gl_cam_eye;
static float gl_near = 0.05f, gl_far = 1000.0f;

static vec3 gl_light_dir;
static vec3 gl_light_col;
static vec3 gl_ambient_col;
static vec3 gl_fog_color;
static real gl_fog_start;
static real gl_fog_end;
static real gl_time;

static float gl_post_exposure = 1.27f;
static float gl_post_gamma    = 1.0f;

static light_definition g_lights[MAX_LIGHTS];
static int g_light_count = 0;

/* ---- Fallback VAO/VBO/IBO and CPU pools ----
 *
 * The per-primitive GPU cache (below) handles everything that comes
 * from a model tag. These remain for draw_triangle_shaded() and any
 * other procedural submission path that still feeds the CPU pools.
 * Nothing in render_finish consumes them any more. */
static GLuint gl_vao = 0;
static GLuint gl_vertex_vbo = 0;
static GLuint gl_index_vbo = 0;
static size_t gl_vbo_capacity_bytes = 0;
static size_t gl_ibo_capacity_bytes = 0;

static float *gl_vertex_pool = NULL;
static size_t gl_pool_capacity_floats = 0;
static size_t gl_pool_used_floats = 0;
static GLuint *gl_index_pool = NULL;
static size_t gl_index_pool_capacity = 0;
static size_t gl_index_pool_used = 0;

static GLuint gl_fbo = 0;
static GLuint gl_color_tex = 0;
static GLuint gl_depth_tex = 0;
static GLuint gl_normal_tex = 0;
/* Emissive-only colour, written as COLOR_ATTACHMENT2 and used as the bloom
 * source instead of gl_color_tex. Shared between the main scene FBO and
 * gl_oit_fbo, so the WBOIT pass adds particle and transparent-material glow
 * directly instead of staging it for the composite to resolve. See the bloom
 * section for why the chain cannot simply threshold the main colour buffer. */
static GLuint gl_emissive_tex = 0;
static GLint gl_default_fbo = 0;

/* Snapshot of the opaque scene, sampled by the transmissive pass so glass can
 * refract what is behind it. */
static GLuint gl_refraction_src         = 0;

static GLuint gl_oit_fbo               = 0;
static GLuint gl_oit_accum_tex         = 0;
static GLuint gl_oit_reveal_tex        = 0;
static GLuint gl_oit_vao               = 0;
static GLuint gl_oit_composite_program = 0;
static GLint  oit_u_accum_tex          = -1;
static GLint  oit_u_reveal_tex         = -1;

static GLuint gl_ao_tex        = 0;
static GLuint gl_ao_fbo        = 0;
static GLuint gl_ao_blurred_tex = 0;
static GLuint gl_ao_blur_fbo   = 0;
/* 1x1 R8 texel of 1.0, bound to uAOTex when VBAO is off. material.frag always
 * samples uAOTex, so "off" has to read as fully unoccluded rather than skip
 * the sample; binding white keeps the shaders untouched and the flag free. */
static GLuint gl_ao_white_tex  = 0;
static GLuint gl_vbao_program      = 0;
static GLuint gl_vbao_ubo          = 0;
static GLuint gl_vbao_blur_program = 0;
static GLuint gl_vbao_blur_ubo     = 0;

static vbao_ubo_t      gl_vbao_ubo_cache;
static int             gl_vbao_ubo_dirty      = 1;
static vbao_blur_ubo_t gl_vbao_blur_ubo_cache;
static int             gl_vbao_blur_ubo_dirty = 1;

static GLuint gl_post_process_program = 0;
static GLint  pp_u_screen_size = -1;
static GLint  pp_u_exposure    = -1;
static GLint  pp_u_gamma       = -1;
static GLint  pp_u_bloom_tex   = -1;
static GLint  pp_u_bloom_intensity = -1;

/* ---- Bloom ----
 *
 * A mip chain of progressively halved render targets, built from the HDR scene
 * before tonemapping and read back by post_process.frag. The width of the glow
 * comes from the number of levels, not from a wide kernel: level 0 is half the
 * render resolution and each subsequent level halves again, so six levels
 * resolve a blur an order of magnitude wider than a single full-resolution
 * separable pass could afford.
 *
 * Run order each frame:
 *   prefilter  scene  -> level 0                (threshold, half res)
 *   downsample level N -> level N+1            (13-tap, N = 0 .. count-2)
 *   upsample   level N+1 -> level N            (3x3 tent, additive blend)
 *
 * The upsample pass writes into the level it is adding to, not into a separate
 * target, which is why the level-N framebuffer has to be both readable and
 * writable across passes with no aliasing hazard: a level is never sampled by
 * the same pass that writes it. */
#define BLOOM_MIP_COUNT 6

static GLuint gl_bloom_prefilter_program = 0;
static GLuint gl_bloom_downsample_program = 0;
static GLuint gl_bloom_upsample_program   = 0;
static GLuint gl_bloom_fbo[BLOOM_MIP_COUNT];
static GLuint gl_bloom_tex[BLOOM_MIP_COUNT];
static i32    gl_bloom_width[BLOOM_MIP_COUNT];
static i32    gl_bloom_height[BLOOM_MIP_COUNT];

static GLint bl_pf_u_source   = -1;
static GLint bl_pf_u_texel    = -1;
static GLint bl_pf_u_dsts     = -1;
static GLint bl_pf_u_threshold = -1;
static GLint bl_pf_u_knee     = -1;
static GLint bl_pf_u_clamp    = -1;
static GLint bl_pf_u_exposure = -1;
static GLint bl_ds_u_source   = -1;
static GLint bl_ds_u_texel    = -1;
static GLint bl_ds_u_dsts     = -1;
static GLint bl_us_u_source   = -1;
static GLint bl_us_u_texel    = -1;
static GLint bl_us_u_dsts     = -1;
static GLint bl_us_u_radius   = -1;

/* Artistic controls. Defaults are the values that read as "Halo 3" against the
 * default material set: a low threshold so ordinary lit surfaces contribute,
 * and an intensity low enough that the glow frames a highlight rather than
 * washing the frame. All three are reachable at runtime.
 *
 * The threshold is in exposure-multiplied units, where 1.0 is display white
 * before the tone curve. Sitting it above 1.0 restricts bloom to genuine
 * super-blooms and reads as almost nothing on a normally lit scene, so it is
 * set well below white here and the intensity is what keeps the result from
 * veiling. */
static int   gl_bloom_enabled   = 1;
static float gl_bloom_intensity = 1.0f;
static float gl_bloom_threshold = 0.0f;
static float gl_bloom_knee      = 0.45f;
static float gl_bloom_radius    = 1.5f;

/* ---- Anti-aliasing / resolve (after post-process) ----
 * Every mode converges on gl_present_tex, a window-resolution surface that
 * dither.frag then presents:
 *   AA off          post-process -------\
 *   AA, scale <=1   post-process -> FXAA -> gl_present_tex (upscale blit)
 *   AA, scale >1    post-process -> SSAA -> gl_present_tex
 * gl_post_fxaa_tex holds the post-process output, gl_aa_tex the FXAA output;
 * both are internal resolution. */
static GLuint gl_fxaa_program    = 0;
static GLuint gl_post_fxaa_fbo    = 0;
static GLuint gl_post_fxaa_tex    = 0;
static GLint  aa_u_screen_texture = -1;
static GLint  aa_u_resolution      = -1;

static GLuint gl_aa_fbo    = 0;
static GLuint gl_aa_tex    = 0;

/* Window-resolution surface the dither pass presents from */
static GLuint gl_present_fbo    = 0;
static GLuint gl_present_tex    = 0;
static GLuint gl_dither_program = 0;
static GLint  dt_u_screen_texture = -1;
static GLint  dt_u_time            = -1;

/* Supersampling resolve (used instead of FXAA when scaling up) */
static GLuint gl_ssaa_program = 0;
static GLint  ss_u_screen_texture = -1;
static GLint  ss_u_src_size  = -1;
static GLint  ss_u_dst_size  = -1;

/* ---- Environment cube (IBL source) ---- */
static GLuint gl_sky_cube     = 0;
static GLuint gl_sky_cube_fbo = 0;

/* ---- Depth cube (scene depth from camera POV; audio-only consumer).
 *      Depth-only texture and FBO; geometry comes from the draw call
 *      list, so no dedicated pool. ---- */
static GLuint gl_depth_cube     = 0;
static GLuint gl_depth_cube_fbo = 0;

static int  gl_env_cube_enabled = 1;
static vec3 gl_env_box_min;
static vec3 gl_env_box_max;
static int  gl_env_box_set = 0;

/* ---- Procedural sky (cube faces) ---- */
static GLuint gl_sky_program      = 0;
static GLint  sky_u_face_index    = -1;
static GLint  sky_u_probe_size    = -1;
static GLint  sky_u_time          = -1;
static GLint  sky_u_sky_zenith    = -1;
static GLint  sky_u_sky_horizon   = -1;
static GLint  sky_u_ground_color  = -1;
static GLint  sky_u_cloud_color   = -1;
static GLint  sky_u_sky_exponent  = -1;
static GLint  sky_u_cloud_coverage = -1;

/* ---- Procedural sky (visible skybox) ---- */
static GLuint gl_skybox_program = 0;
static GLint  skybox_u_inv_view_proj  = -1;
static GLint  skybox_u_cam_eye        = -1;
static GLint  skybox_u_screen_size    = -1;
static GLint  skybox_u_time           = -1;
static GLint  skybox_u_sky_zenith     = -1;
static GLint  skybox_u_sky_horizon    = -1;
static GLint  skybox_u_sky_ground     = -1;
static GLint  skybox_u_cloud_color    = -1;
static GLint  skybox_u_sky_exponent   = -1;
static GLint  skybox_u_cloud_coverage = -1;

static vec3  gl_sky_zenith   = {0.10f, 0.20f, 0.45f};
static vec3  gl_sky_horizon  = {0.55f, 0.65f, 0.80f};
static vec3  gl_sky_ground   = {0.12f, 0.11f, 0.10f};
static vec3  gl_sky_cloud    = {0.92f, 0.94f, 0.98f};
static float gl_sky_exponent    = 2.0f;
static float gl_sky_cloud_cover = 0.55f;
static float gl_sky_ambient_scale = 0.5f;

/* Floats per vertex: position(3), normal(3), position again(3), unused(1),
 * face normal(3), centroid(3), bone indices(4), bone weights(4).
 * Attributes 0-5 are the original layout; 6 and 7 carry the skinning data
 * added for GPU skinning. */
#define VERTEX_STRIDE_FLOATS 24
#define VERTEX_STRIDE_BYTES (VERTEX_STRIDE_FLOATS * sizeof(float))

/* Joint palette capacity, in matrices. The palette lives in a shader storage
 * buffer rather than a uniform block: 4096 mat4 is 256KB, well past
 * GL_MAX_UNIFORM_BLOCK_SIZE (64KB on desktop, 16KB guaranteed), while
 * GL_MAX_SHADER_STORAGE_BLOCK_SIZE is orders of magnitude larger. This also
 * matches how the light and cluster data is already bound. Each animated
 * entity reserves a contiguous slice; its draw calls carry the base slot. */
#define MAX_JOINT_SLOTS 4096
#define JOINT_SSBO_BINDING 3
static GLuint gl_joint_ubo = 0;
static mat4   gl_joint_matrices[MAX_JOINT_SLOTS];
static int    gl_joint_slot_count = 0;

/* ---- Per-primitive GPU cache ----
 *
 * One entry per (model, primitive) pair. Keyed by the model_primitive
 * pointer; the entry is created on first draw and never modified. See
 * the header note about model reloading. */
typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint ibo;
    u32    index_count;
} gpu_primitive_t;

#define MAX_GPU_PRIMITIVES 4096
static gpu_primitive_t         gl_gpu_prims[MAX_GPU_PRIMITIVES];
static const model_primitive  *gl_gpu_prim_keys[MAX_GPU_PRIMITIVES];
static int                     gl_gpu_prim_count = 0;

/* ---- Per-frame draw call ----
 *
 * Emitted per (entity, primitive). Sorted by transparency class, then
 * material state, so the draw loops skip redundant program binds and
 * material UBO uploads. */
typedef struct {
    const material_definition *mat;
    const gpu_primitive_t     *prim;
    int  model_index;
    int  is_transparent;
    int  is_refractive;
    /* First joint matrix for this draw in the shared joint palette, or -1
     * when the primitive is not skinned. */
    int  joint_offset;
    /* The mesh node's world transform, carried per draw because it is not
     * baked into the vertices of a skinned primitive. Identity otherwise. */
    mat4 node_transform;
} draw_call_t;

#define MAX_DRAW_CALLS 8192
static draw_call_t gl_draw_calls[MAX_DRAW_CALLS];
static int         gl_draw_call_count = 0;

static mat4 gl_model_matrices[MAX_MODEL_MATRICES];
static int gl_model_count = 0;

static GLuint gl_light_ssbo = 0;
static GLuint gl_cluster_ssbo = 0;
static GLuint gl_cluster_offset_ssbo = 0;
static GLuint gl_cluster_program = 0;
static int gl_num_tiles_x = 0;
static int gl_num_tiles_y = 0;
static int gl_num_clusters = 0;

static GLint cluster_u_depth_tex = -1;
static GLint cluster_u_num_lights = -1;
static GLint cluster_u_tile_size = -1;
static GLint cluster_u_num_tiles_x = -1;
static GLint cluster_u_num_tiles_y = -1;
static GLint cluster_u_depth_slices = -1;
static GLint cluster_u_near = -1;
static GLint cluster_u_far = -1;
static GLint cluster_u_view = -1;
static GLint cluster_u_inv_view_proj = -1;

#ifdef AUDIO_OCCLUSION
static GLuint gl_audio_occlusion_program = 0;
static GLint occ_u_view_proj = -1;
static GLint occ_u_inv_view_proj = -1;
static GLint occ_u_listener_pos = -1;
static GLint occ_u_num_voices = -1;
static GLint occ_u_probe_depth_cube = -1;
static GLint occ_u_probe_near_far   = -1;
static GLuint gl_audio_voice_input_ssbo = 0;
static int   g_audio_voice_count_gpu = 0;
static audio_voice_input_t gl_audio_voice_inputs[MAX_AUDIO_VOICES_GPU];

static GLuint gl_audio_propagation_ssbo = 0;
static GLsync gl_audio_propagation_fence = NULL;
static int    gl_audio_propagation_fence_age = 0;

static vec3 gl_audio_last_voice_positions[MAX_AUDIO_VOICES_GPU];
static int  gl_audio_last_voice_count = 0;
#endif

#ifdef AUDIO_REVERB
static GLuint gl_audio_reverb_program = 0;
static GLint rev_u_inv_view_proj = -1;
static GLint rev_u_listener_pos = -1;
static GLint rev_u_probe_depth_cube = -1;
static GLint rev_u_probe_near_far   = -1;
static GLuint gl_audio_global_stats_ssbo = 0;
static GLsync gl_audio_reverb_fence = NULL;
static int    gl_audio_reverb_fence_age = 0;
#endif

#ifdef AUDIO_PORTAL
static GLuint gl_audio_portal_program = 0;
static GLint port_u_inv_view_proj = -1;
static GLint port_u_listener_pos = -1;
static GLint port_u_threshold = -1;
static GLint port_u_num_voices = -1;
static GLint port_u_view_proj = -1;
static GLint port_u_probe_depth_cube = -1;
static GLint port_u_probe_near_far   = -1;
static GLuint gl_audio_portal_candidates_ssbo = 0;
static GLsync gl_audio_portal_fence = NULL;
static int    gl_audio_portal_fence_age = 0;

static portal_candidate_t gl_audio_portal_reset_template[MAX_AUDIO_VOICES_GPU];
#endif

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
/* Bytes currently allocated in g_particle_vbo. Sized once and topped up with
 * glBufferSubData, rather than re-specified every frame. */
static size_t g_particle_vbo_capacity = 0;

/* One program, not one per alpha-pass side. The BEHIND variant existed only to
 * split transparency around transmissive geometry, and there is no longer a
 * BEHIND pass for it to draw into, so alpha_pass_side no longer reaches the
 * particle system at all. These were two-wide arrays indexed by the side enum;
 * they are now scalars, and the draw function takes no side argument. */
static GLuint g_particle_program  = 0;
static GLint  g_particle_u_view_proj  = -1;
static GLint  g_particle_u_cam_right = -1;
static GLint  g_particle_u_cam_up    = -1;

static mat4 g_particle_view_proj;
static vec3 g_particle_cam_right;
static vec3 g_particle_cam_up;
static int  g_particle_cam_valid = 0;

static mat4 entity_model_matrix(const entity_definition *ent) {
    mat4 R = quat_to_mat4(ent->orientation);
    mat4 T = mat4_translation(ent->position);
    return mat4_mul(T, R);
}

typedef struct {
    struct entity_definition *ent;
    float depth;
    int model_index;
    int joint_offset;   /* base slot in the shared joint palette, or -1 */
    i32  handle;        /* entity tag handle, or -1 for a runtime entity */
} entity_sort_t;

/* Scratch for render_draw_entities' front-to-back sort. Owned here rather than
 * malloc'd per call; grows to the largest entity count seen and is released in
 * render_shutdown. */
static entity_sort_t *g_entity_sort_buf = NULL;
static int g_entity_sort_cap = 0;

static int entity_sort_compare(const void* a, const void* b) {
    const entity_sort_t *sa = (const entity_sort_t*)a;
    const entity_sort_t *sb = (const entity_sort_t*)b;
    if (sa->depth > sb->depth) return -1;
    if (sa->depth < sb->depth) return  1;
    if (sa->model_index < sb->model_index) return -1;
    if (sa->model_index > sb->model_index) return  1;
    return 0;
}

static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

static mat4 cube_face_view(vec3 eye, int face) {
    static const vec3 dirs[6] = {
        { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        { 0.0f, 1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f}
    };
    static const vec3 ups[6] = {
        {0.0f,-1.0f, 0.0f}, {0.0f,-1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f,-1.0f},
        {0.0f,-1.0f, 0.0f}, {0.0f,-1.0f, 0.0f}
    };
    vec3 center = vec3_add(eye, dirs[face]);
    return mat4_lookat(eye, center, ups[face]);
}

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
        fprintf(stderr, "Shader %s compilation error with defines:\n%s\n", filename, log);
        C89GL_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void generate_defines(render_method key, int is_depth, alpha_pass_side side,
                             char* out, size_t out_size) {
    char* p = out;
    size_t remaining = out_size;
    int n;

    n = snprintf(p, remaining, "#version 430 core\n");
    p += n; remaining -= n;

    n = snprintf(p, remaining, "#define USE_MODEL_UBO 1\n");
    p += n; remaining -= n;

    if (gl_env_cube_enabled) {
        n = snprintf(p, remaining, "#define USE_ENV_CUBE 1\n");
        p += n; remaining -= n;
    }

    if (is_depth) {
        n = snprintf(p, remaining, "#define DEPTH_ONLY 1\n");
        p += n; remaining -= n;
    }

    if (side == ALPHA_PASS_BEHIND) {
        n = snprintf(p, remaining, "#define ALPHA_PASS_BEHIND 1\n");
        p += n; remaining -= n;
    } else {
        n = snprintf(p, remaining, "#define ALPHA_PASS_FRONT 1\n");
        p += n; remaining -= n;
    }

    if (key & EFFECT_BUMP_WAVE)       { n = snprintf(p, remaining, "#define EFFECT_BUMP_WAVE\n"); p += n; remaining -= n; }
    if (key & EFFECT_DIFFUSE_WRAP)    { n = snprintf(p, remaining, "#define EFFECT_DIFFUSE_WRAP\n"); p += n; remaining -= n; }
    if (key & EFFECT_CEL_SHADING)     { n = snprintf(p, remaining, "#define EFFECT_CEL_SHADING\n"); p += n; remaining -= n; }
    if (key & EFFECT_GOOCH)           { n = snprintf(p, remaining, "#define EFFECT_GOOCH\n"); p += n; remaining -= n; }
    if (key & EFFECT_BACK_GLOW)       { n = snprintf(p, remaining, "#define EFFECT_BACK_GLOW\n"); p += n; remaining -= n; }
    if (key & EFFECT_RIM)             { n = snprintf(p, remaining, "#define EFFECT_RIM\n"); p += n; remaining -= n; }
    if (key & EFFECT_EMISSIVE)        { n = snprintf(p, remaining, "#define EFFECT_EMISSIVE\n"); p += n; remaining -= n; }
    if (key & EFFECT_EMISSIVE_PULSE)  { n = snprintf(p, remaining, "#define EFFECT_EMISSIVE_PULSE\n"); p += n; remaining -= n; }
    if (key & EFFECT_STROBE)          { n = snprintf(p, remaining, "#define EFFECT_STROBE\n"); p += n; remaining -= n; }
    if (key & EFFECT_SATURATION)      { n = snprintf(p, remaining, "#define EFFECT_SATURATION\n"); p += n; remaining -= n; }
    if (key & EFFECT_THIN_FILM)       { n = snprintf(p, remaining, "#define EFFECT_THIN_FILM\n"); p += n; remaining -= n; }
    if (key & EFFECT_GLITCH)          { n = snprintf(p, remaining, "#define EFFECT_GLITCH\n"); p += n; remaining -= n; }
    if (key & EFFECT_BUMP_NOISE)      { n = snprintf(p, remaining, "#define EFFECT_BUMP_NOISE\n"); p += n; remaining -= n; }
    if (key & EFFECT_DIFFRACTION)     { n = snprintf(p, remaining, "#define EFFECT_DIFFRACTION\n"); p += n; remaining -= n; }
    if (key & EFFECT_POSTERIZE)       { n = snprintf(p, remaining, "#define EFFECT_POSTERIZE\n"); p += n; remaining -= n; }
    if (key & EFFECT_FOG)             { n = snprintf(p, remaining, "#define EFFECT_FOG\n"); p += n; remaining -= n; }
    if (key & EFFECT_ALPHA) {
        n = snprintf(p, remaining, "#define EFFECT_ALPHA\n"); p += n; remaining -= n;
        n = snprintf(p, remaining, "#define WBOIT_PASS 1\n"); p += n; remaining -= n;
    }
    if (key & EFFECT_CLEARCOAT)       { n = snprintf(p, remaining, "#define EFFECT_CLEARCOAT\n"); p += n; remaining -= n; }
    if (key & EFFECT_SHEEN)           { n = snprintf(p, remaining, "#define EFFECT_SHEEN\n"); p += n; remaining -= n; }
    if (key & EFFECT_ANISOTROPIC)     { n = snprintf(p, remaining, "#define EFFECT_ANISOTROPIC\n"); p += n; remaining -= n; }
    if (key & EFFECT_SUBSURFACE)      { n = snprintf(p, remaining, "#define EFFECT_SUBSURFACE\n"); p += n; remaining -= n; }
    if (key & EFFECT_TRANSMISSION)    { n = snprintf(p, remaining, "#define EFFECT_TRANSMISSION\n"); p += n; remaining -= n; }
}

static u32 hash_cache_key(u32 k) {
    k ^= k >> 16;
    k *= 0x7feb352d;
    k ^= k >> 15;
    k *= 0x846ca68b;
    k ^= k >> 16;
    return k;
}

static void shader_cache_resize(int new_size) {
    shader_variant_t *old_cache = gl_shader_cache;
    int old_size = gl_shader_cache_size;
    int i;
    printf("[SHADER CACHE] Resize: %d -> %d\n", old_size, new_size);
    gl_shader_cache = (shader_variant_t*)calloc(new_size, sizeof(shader_variant_t));
    gl_shader_cache_size = new_size;
    gl_shader_cache_count = 0;
    for (i = 0; i < old_size; i++) {
        if (old_cache[i].program != 0) {
            int index = hash_cache_key((u32)old_cache[i].key) % new_size;
            while (gl_shader_cache[index].program != 0) index = (index + 1) % new_size;
            gl_shader_cache[index] = old_cache[i];
            gl_shader_cache_count++;
        }
    }
    free(old_cache);
}

/* Compile and link a variant for cache_key, then insert it into the cache.
 *
 * Split out from get_program_for_method so the 4KB defines buffer and 512B info
 * log live in this frame only. The lookup runs once per draw call per pass every
 * frame and hits the cache essentially always after warmup, so carrying those
 * buffers on the hot path cost a ~4.6KB stack frame per call for nothing. */
static shader_variant_t* shader_cache_compile(u32 cache_key, render_method key,
                                              int is_depth, alpha_pass_side side) {
    shader_variant_t *entry;
    GLuint vs, fs, prog;
    int index, link_status, blockIndex, modelBlock;
    char defines[4096];
    GLint len;
    char log[512];

    /* get_program_for_method initialises the cache before probing, and only
     * reaches here on a miss, so the table is already allocated. */
    if (!gl_shader_cache) return NULL;

    printf("[SHADER CACHE] Miss for key 0x%x (depth=%d alpha_pass=%s) - compiling new variant...\n",
           (unsigned)cache_key, is_depth,
           side == ALPHA_PASS_BEHIND ? "BEHIND" : "FRONT");
    generate_defines(key, is_depth, side, defines, sizeof(defines));

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

    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);

    if ((float)(gl_shader_cache_count + 1) / gl_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        shader_cache_resize(gl_shader_cache_size * 2);
    }

    index = hash_cache_key(cache_key) % gl_shader_cache_size;
    while (gl_shader_cache[index].program != 0) {
        if (gl_shader_cache[index].key == cache_key) {
            C89GL_glDeleteProgram(prog);
            return &gl_shader_cache[index];
        }
        index = (index + 1) % gl_shader_cache_size;
    }

    entry = &gl_shader_cache[index];
    entry->key = (render_method)cache_key;
    entry->program = prog;
    entry->is_depth = is_depth;
    entry->alpha_pass = side;
    entry->hit_logged = 0;
    entry->u_view_proj = C89GL_glGetUniformLocation(prog, "uViewProj");
    entry->u_view = C89GL_glGetUniformLocation(prog, "uView");
    entry->u_light_dir = C89GL_glGetUniformLocation(prog, "uLightDir");
    entry->u_light_col = C89GL_glGetUniformLocation(prog, "uLightCol");
    entry->u_ambient_col = C89GL_glGetUniformLocation(prog, "uAmbientCol");
    entry->u_cam_eye = C89GL_glGetUniformLocation(prog, "uCamEye");
    entry->u_time = C89GL_glGetUniformLocation(prog, "uTime");
    entry->u_fog_color = C89GL_glGetUniformLocation(prog, "uFogColor");
    entry->u_fog_start = C89GL_glGetUniformLocation(prog, "uFogStart");
    entry->u_fog_end = C89GL_glGetUniformLocation(prog, "uFogEnd");
    entry->u_depth_tex = C89GL_glGetUniformLocation(prog, "uDepthTex");
    entry->u_screen_size = C89GL_glGetUniformLocation(prog, "uScreenSize");
    entry->u_num_tiles_x = C89GL_glGetUniformLocation(prog, "uNumTilesX");
    entry->u_num_tiles_y = C89GL_glGetUniformLocation(prog, "uNumTilesY");
    entry->u_cluster_near = C89GL_glGetUniformLocation(prog, "uClusterNear");
    entry->u_cluster_far = C89GL_glGetUniformLocation(prog, "uClusterFar");
    entry->u_refraction_src = C89GL_glGetUniformLocation(prog, "uRefractionSrc");
    entry->u_ao_tex = C89GL_glGetUniformLocation(prog, "uAOTex");
    entry->u_alpha_pass = C89GL_glGetUniformLocation(prog, "uAlphaPass");
    entry->u_refraction_scale = C89GL_glGetUniformLocation(prog, "uRefractionScale");
    entry->u_env_cube = C89GL_glGetUniformLocation(prog, "uEnvCube");
    entry->u_env_cube_max_mip = C89GL_glGetUniformLocation(prog, "uEnvCubeMaxMip");
    entry->u_sky_zenith = C89GL_glGetUniformLocation(prog, "uSkyZenith");
    entry->u_sky_horizon = C89GL_glGetUniformLocation(prog, "uSkyHorizon");
    entry->u_sky_ground = C89GL_glGetUniformLocation(prog, "uSkyGround");
    entry->u_sky_exponent = C89GL_glGetUniformLocation(prog, "uSkyExponent");
    entry->u_cloud_color = C89GL_glGetUniformLocation(prog, "uCloudColor");
    entry->u_cloud_coverage = C89GL_glGetUniformLocation(prog, "uCloudCoverage");
    entry->u_sky_ambient_scale = C89GL_glGetUniformLocation(prog, "uSkyAmbientScale");
    entry->u_joint_offset = C89GL_glGetUniformLocation(prog, "uJointOffset");
    entry->u_node_transform = C89GL_glGetUniformLocation(prog, "uNodeTransform");

    gl_shader_cache_count++;
    gl_shader_compilations++;
    printf("[SHADER CACHE] Compiled new variant #%d for key 0x%x (program %u)\n",
           gl_shader_compilations, (unsigned)cache_key, prog);
    return entry;
}

/* Hot path. Probes the cache and compiles only on a miss.
 *
 * This is called once per draw call in every geometry pass, so it stays small:
 * the 4KB defines buffer and the info log belong to shader_cache_compile, not
 * here, because a cache hit must not pay for a frame big enough to hold them. */
static shader_variant_t* get_program_for_method(render_method key,
                                                int is_depth,
                                                alpha_pass_side side) {
    u32 cache_key = key
                  | (is_depth ? (1u << 31) : 0)
                  | ((side == ALPHA_PASS_FRONT) ? (1u << 30) : 0);
    int index;

    if (!gl_shader_cache) {
        gl_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        gl_shader_cache = (shader_variant_t*)calloc(gl_shader_cache_size, sizeof(shader_variant_t));
        gl_shader_cache_count = 0;
    }

    index = hash_cache_key(cache_key) % gl_shader_cache_size;
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

    return shader_cache_compile(cache_key, key, is_depth, side);
}

void render_precompile_material(u32 method) {
    render_method key = (render_method)method;

    /* gl_vao is the existing "render_init has run" sentinel. A material
     * registered before the context exists still compiles on first use; warming
     * the cache here would either touch a dead context or leave programs behind
     * that render_shutdown never sees. */
    if (!gl_vao) return;

    /* Same three-way classification the draw-call builder applies, because the
     * set of passes a material is ever submitted to follows from it. */
    int is_transparent = (key & EFFECT_ALPHA) != 0;
    int is_refractive  = !is_transparent && (key & EFFECT_TRANSMISSION) != 0;

    if (is_transparent) {
        /* The single WBOIT pass. There is no depth variant for transparency: the
         * transparent depth prepass that used to consume one has been removed,
         * so compiling it here would only add an unused program per material. */
        get_program_for_method(key, 0, ALPHA_PASS_FRONT);
    } else if (is_refractive) {
        /* The transmissive colour pass. Refractive geometry is skipped by the
         * opaque prepass and colour passes. */
        get_program_for_method(key, 0, ALPHA_PASS_FRONT);
    } else {
        /* Opaque depth prepass and opaque colour. */
        get_program_for_method(key, 1, ALPHA_PASS_FRONT);
        get_program_for_method(key, 0, ALPHA_PASS_FRONT);
    }
}

static void update_material_ubo(const material_definition *mat) {
    material_ubo_t ubo;
    memset(&ubo, 0, sizeof(ubo));

    ubo.uMatAlbedo[0] = mat->albedo.color.r;
    ubo.uMatAlbedo[1] = mat->albedo.color.g;
    ubo.uMatAlbedo[2] = mat->albedo.color.b;
    ubo.uMatTint[0] = mat->tint.color.r;
    ubo.uMatTint[1] = mat->tint.color.g;
    ubo.uMatTint[2] = mat->tint.color.b;
    ubo.uMatAlpha = mat->alpha;
    ubo.uMatEmissiveColor[0] = mat->emissive_color.color.r;
    ubo.uMatEmissiveColor[1] = mat->emissive_color.color.g;
    ubo.uMatEmissiveColor[2] = mat->emissive_color.color.b;
    ubo.uMatEmissivePulseAmplitude = mat->emissive_pulse_amplitude;
    ubo.uMatEmissivePulseFrequency = mat->emissive_pulse_frequency;
    ubo.uMatEmissivePulsePhase     = mat->emissive_pulse_phase;
    ubo.uMatTransmission   = mat->transmission;
    ubo.uMatSpecularTint[0] = mat->specular_tint.color.r;
    ubo.uMatSpecularTint[1] = mat->specular_tint.color.g;
    ubo.uMatSpecularTint[2] = mat->specular_tint.color.b;
    ubo.uMatSpecularRoughness = mat->specular_roughness;
    ubo.uMatRimColor[0] = mat->rim_color.color.r;
    ubo.uMatRimColor[1] = mat->rim_color.color.g;
    ubo.uMatRimColor[2] = mat->rim_color.color.b;
    ubo.uMatRimExponent = mat->rim_exponent;
    ubo.uMatMetallic = mat->metallic;
    ubo.uMatIOR      = mat->ior;
    ubo.uMatSubsurface = mat->subsurface;
    ubo.uMatClearcoatIOR = mat->clearcoat_ior;
    ubo.uMatGoochCool[0] = mat->gooch_cool.color.r;
    ubo.uMatGoochCool[1] = mat->gooch_cool.color.g;
    ubo.uMatGoochCool[2] = mat->gooch_cool.color.b;
    ubo.uMatGoochWarm[0] = mat->gooch_warm.color.r;
    ubo.uMatGoochWarm[1] = mat->gooch_warm.color.g;
    ubo.uMatGoochWarm[2] = mat->gooch_warm.color.b;
    ubo.uMatAmbient = mat->ambient;
    ubo.uMatDiffuseRoughness     = mat->diffuse_roughness;
    ubo.uMatTransmissionRoughness = mat->transmission_roughness;
    ubo.uMatSaturation         = mat->saturation;
    ubo.uMatThinFilm = mat->thin_film;
    ubo.uMatThinFilmIOR = mat->thin_film_ior;
    ubo.uMatBackGlowColor[0] = mat->back_glow_color.color.r;
    ubo.uMatBackGlowColor[1] = mat->back_glow_color.color.g;
    ubo.uMatBackGlowColor[2] = mat->back_glow_color.color.b;
    ubo.uMatBumpWaveAmplitude = mat->bump_wave_amplitude;
    ubo.uMatBumpWaveFrequency = mat->bump_wave_frequency;
    ubo.uMatBumpWaveSpeed     = mat->bump_wave_speed;
    ubo.uMatBumpNoise     = mat->bump_noise;
    ubo.uMatDiffraction = mat->diffraction;
    ubo.uMatCelBands      = mat->cel_bands;
    ubo.uMatGlitch = mat->glitch;
    ubo.uMatPosterizeLevels = mat->posterize_levels;
    ubo.uMatStrobeColor[0] = mat->strobe_color.color.r;
    ubo.uMatStrobeColor[1] = mat->strobe_color.color.g;
    ubo.uMatStrobeColor[2] = mat->strobe_color.color.b;
    ubo.uMatStrobeFrequency = mat->strobe_frequency;
    ubo.uMatStrobePhase     = mat->strobe_phase;
    ubo.uMatClearcoatColor[0] = mat->clearcoat_color.color.r;
    ubo.uMatClearcoatColor[1] = mat->clearcoat_color.color.g;
    ubo.uMatClearcoatColor[2] = mat->clearcoat_color.color.b;
    ubo.uMatClearcoatRoughness = mat->clearcoat_roughness;
    ubo.uMatClearcoat = mat->clearcoat;
    ubo.uMatSheenColor[0] = mat->sheen_color.color.r;
    ubo.uMatSheenColor[1] = mat->sheen_color.color.g;
    ubo.uMatSheenColor[2] = mat->sheen_color.color.b;
    ubo.uMatSheenRoughness = mat->sheen_roughness;
    ubo.uMatSheen = mat->sheen;
    ubo.uMatAnisotropic = mat->anisotropic;
    ubo.uMatTransmissionTint[0] = mat->transmission_tint.color.r;
    ubo.uMatTransmissionTint[1] = mat->transmission_tint.color.g;
    ubo.uMatTransmissionTint[2] = mat->transmission_tint.color.b;
    ubo.uMatF82Tint[0] = mat->f82_tint.color.r;
    ubo.uMatF82Tint[1] = mat->f82_tint.color.g;
    ubo.uMatF82Tint[2] = mat->f82_tint.color.b;
    ubo.uMatSubsurfaceColor[0] = mat->subsurface_color.color.r;
    ubo.uMatSubsurfaceColor[1] = mat->subsurface_color.color.g;
    ubo.uMatSubsurfaceColor[2] = mat->subsurface_color.color.b;

    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_material_ubo);
    C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(material_ubo_t), &ubo);
}

static void update_model_ubo(void) {
    size_t total_bytes = gl_model_count * sizeof(mat4);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_model_ubo);
    if (total_bytes > 0) {
        C89GL_glBufferData(GL_UNIFORM_BUFFER, total_bytes, NULL, GL_STREAM_DRAW);
        C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, total_bytes, gl_model_matrices);
    }
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/* Reserve `count` consecutive slots in the shared joint palette and return
 * the first slot index, or -1 when the palette is full. Allocation is a
 * bump pointer reset once per frame by render_draw_entities. */
static int joint_palette_alloc(int count) {
    int base;
    if (count <= 0) return -1;
    if (gl_joint_slot_count + count > MAX_JOINT_SLOTS) return -1;
    base = gl_joint_slot_count;
    gl_joint_slot_count += count;
    return base;
}

static void update_joint_ubo(void) {
    size_t total_bytes = (size_t)gl_joint_slot_count * sizeof(mat4);
    if (total_bytes == 0) return;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_joint_ubo);
    C89GL_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, total_bytes, gl_joint_matrices);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/* GLSL reads a mat4 out of a buffer as four column vectors, so a row-major
 * matrix copied in verbatim is interpreted transposed -- every skinned vertex
 * would be blended against the inverse of its joint transform. Copy through an
 * explicit transpose instead, which is what the GL_TRUE uniform uploads
 * elsewhere in this file already do for the same reason. */
static void joint_palette_store(int slot, const mat4 *src) {
    int r, c;
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c)
            gl_joint_matrices[slot].data[c * 4 + r] = src->data[r * 4 + c];
}

/* Number of lights actually written by the last upload_lights_to_ssbo() call.
 * That upload compacts out disabled lights, so it can be lower than
 * g_light_count. The cluster build must iterate this many, not g_light_count,
 * or it reads past the uploaded range and writes garbage indices into the
 * light lists. */
static int g_gpu_light_count = 0;

static void upload_lights_to_ssbo(void) {
    /* File scope, not a local. This is 144 lights x 64 bytes, about 9.2KB, and
     * as a local it was reserved and refilled on the stack every single frame
     * before being copied straight into the SSBO. It is private scratch space
     * that no caller ever sees, so it has no reason to live in a frame that
     * happens to need it. */
    static gpu_light_t gpu_lights[MAX_LIGHTS];
    int count = 0;
    int i;
    for (i = 0; i < g_light_count && count < MAX_LIGHTS; i++) {
        if (!g_lights[i].enabled) continue;
        gpu_lights[count].pos[0] = g_lights[i].position.position.x;
        gpu_lights[count].pos[1] = g_lights[i].position.position.y;
        gpu_lights[count].pos[2] = g_lights[i].position.position.z;
        gpu_lights[count].pos[3] = (float)g_lights[i].type;
        gpu_lights[count].dir[0] = g_lights[i].direction.rotation.i;
        gpu_lights[count].dir[1] = g_lights[i].direction.rotation.j;
        gpu_lights[count].dir[2] = g_lights[i].direction.rotation.k;
        gpu_lights[count].dir[3] = 0.0f;
        gpu_lights[count].color[0] = g_lights[i].color.color.r;
        gpu_lights[count].color[1] = g_lights[i].color.color.g;
        gpu_lights[count].color[2] = g_lights[i].color.color.b;
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
    g_gpu_light_count = count;
}

#ifdef CLUSTER_DIAG
static void cluster_diag_report(void);
#endif

static void dispatch_cluster_build(void) {
    if (!gl_cluster_program) return;

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);

    C89GL_glUseProgram(gl_cluster_program);
    C89GL_glUniform1i(cluster_u_depth_tex, 0);
    C89GL_glUniform1i(cluster_u_num_lights, g_gpu_light_count);
    C89GL_glUniform1i(cluster_u_tile_size, CLUSTER_TILE_SIZE);
    C89GL_glUniform1i(cluster_u_num_tiles_x, gl_num_tiles_x);
    C89GL_glUniform1i(cluster_u_num_tiles_y, gl_num_tiles_y);
    C89GL_glUniform1i(cluster_u_depth_slices, CLUSTER_DEPTH_SLICES);
    C89GL_glUniform1f(cluster_u_near, gl_near);
    C89GL_glUniform1f(cluster_u_far, gl_far);

    /* The culling is done in view space against each tile's frustum bounds,
     * so the view matrix and the inverse view-projection are both needed:
     * the first puts light centres in the space the AABB lives in, the
     * second unprojects the tile's corners to build that AABB. */
    if (cluster_u_view != -1)
        C89GL_glUniformMatrix4fv(cluster_u_view, 1, GL_TRUE, (float*)&gl_view);
    if (cluster_u_inv_view_proj != -1) {
        mat4 inv_view_proj = mat4_inverse(gl_view_proj);
        C89GL_glUniformMatrix4fv(cluster_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
    }

    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_light_ssbo);
    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_cluster_ssbo);
    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_cluster_offset_ssbo);

    {
        /* Must match local_size in cluster.comp. Dispatching fewer groups than
         * the tile grid needs leaves the untouched tiles holding whatever the
         * offset buffer had last frame. */
        GLuint groups_x = (GLuint)((gl_num_tiles_x + CLUSTER_WORKGROUP_SIZE - 1) / CLUSTER_WORKGROUP_SIZE);
        GLuint groups_y = (GLuint)((gl_num_tiles_y + CLUSTER_WORKGROUP_SIZE - 1) / CLUSTER_WORKGROUP_SIZE);
        /* A group that does not cover the whole grid leaves those tiles
         * unwritten, which shows up as lighting confined to one screen
         * corner. Cheap to assert at init, so log the coverage once. */
        if (groups_x * CLUSTER_WORKGROUP_SIZE < (GLuint)gl_num_tiles_x ||
            groups_y * CLUSTER_WORKGROUP_SIZE < (GLuint)gl_num_tiles_y) {
            printf("ERROR: cluster dispatch under-covers tile grid "
                   "(%ux%u tiles, %ux%u workgroups of %dx%d)\n",
                   gl_num_tiles_x, gl_num_tiles_y,
                   groups_x, groups_y, CLUSTER_WORKGROUP_SIZE, CLUSTER_WORKGROUP_SIZE);
        }
        C89GL_glDispatchCompute(groups_x, groups_y, 1);
    }

    C89GL_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    C89GL_glUseProgram(0);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    C89GL_glActiveTexture(GL_TEXTURE0);

#ifdef CLUSTER_DIAG
    cluster_diag_report();
#endif
}

#ifdef CLUSTER_DIAG
/* Reports how well the cluster build is culling. The interesting numbers:
 *
 *   avg        average lights per *non-empty* cluster. Compared against
 *              g_gpu_light_count this is the culling effectiveness: 3 instead
 *              of 13 means the build is doing its job, 13 means the light
 *              lists are no cheaper than looping every light.
 *   full       clusters that hit CLUSTER_MAX_LIGHTS_PER. Every light dropped
 *              at that point is a light a fragment cannot find, so this must
 *              be 0 in any frame that looks correct.
 *   slices     average lights per slice, near slice first. Logarithmic
 *              slicing concentrates slices near the camera, so a front slice
 *              far busier than the rest means the range is being spent
 *              where there is little geometry, and deep slices overflow
 *              before they are used.
 */
static void cluster_diag_report(void) {
    static unsigned int *counts = NULL;
    static int counts_cap = 0;
    static int frame = 0;
    long long total = 0;
    int occupied = 0;
    int peak = 0;
    int full = 0;
    int i;
    int s;

    if (!C89GL_glGetBufferSubData || !gl_cluster_offset_ssbo || gl_num_clusters <= 0) return;

    frame++;
    if (frame > 6 && (frame % 60) != 0) return;

    if (counts_cap < gl_num_clusters) {
        unsigned int *grown = (unsigned int*)realloc(counts, (size_t)gl_num_clusters * sizeof(unsigned int));
        if (!grown) return;
        counts = grown;
        counts_cap = gl_num_clusters;
    }

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_offset_ssbo);
    C89GL_glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                             (long long)gl_num_clusters * (long long)sizeof(unsigned int),
                             counts);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (i = 0; i < gl_num_clusters; i++) {
        int c = (int)counts[i];
        if (c <= 0) continue;
        occupied++;
        total += c;
        if (c > peak) peak = c;
        if (c >= CLUSTER_MAX_LIGHTS_PER) full++;
    }

    printf("[cluster] f=%d lights=%d clusters=%d occupied=%d (%.1f%%) "
           "avg=%.2f peak=%d full=%d\n",
           frame, g_gpu_light_count, gl_num_clusters, occupied,
           100.0 * (double)occupied / (double)gl_num_clusters,
           occupied ? (double)total / (double)occupied : 0.0, peak, full);
    if (g_gpu_light_count > 0 && occupied > 0) {
        printf("[cluster] culling=%.1f%% of scene lights per lit cluster\n",
               100.0 * (1.0 - ((double)total / (double)occupied) / (double)g_gpu_light_count));
    }
    printf("[cluster] slices:");
    for (s = 0; s < CLUSTER_DEPTH_SLICES; s++) {
        long long sum = 0;
        int n = 0;
        for (i = s; i < gl_num_clusters; i += CLUSTER_DEPTH_SLICES) { sum += (long long)counts[i]; n++; }
        printf(" %.1f", n ? (double)sum / (double)n : 0.0);
    }
    printf("\n");
    fflush(stdout);
}
#endif

static void dispatch_vbao(void) {
    if (!gl_vbao_program || !gl_vbao_ubo || !gl_ao_fbo) return;

    if (gl_vbao_ubo_dirty) {
        mat4 inv_proj = mat4_inverse(gl_proj);
        memcpy(gl_vbao_ubo_cache.inv_proj, &inv_proj, sizeof(float) * 16);
        memcpy(gl_vbao_ubo_cache.proj,     &gl_proj,  sizeof(float) * 16);
        memcpy(gl_vbao_ubo_cache.view,     &gl_view,  sizeof(float) * 16);
        gl_vbao_ubo_cache.screen_size[0] = (float)gl_ao_width;
        gl_vbao_ubo_cache.screen_size[1] = (float)gl_ao_height;
        gl_vbao_ubo_cache.near_plane     = gl_near;
        gl_vbao_ubo_cache.far_plane      = gl_far;
        gl_vbao_ubo_cache.proj_a         =  gl_proj.columns[2].position.z;
        gl_vbao_ubo_cache.proj_b         = -gl_proj.columns[2].position.w;
        gl_vbao_ubo_cache.inv_proj_00    =  1.0f / gl_proj.columns[0].position.x;
        gl_vbao_ubo_cache.inv_proj_11    =  1.0f / gl_proj.columns[1].position.y;

        C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_ubo);
        C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(vbao_ubo_t),
                              &gl_vbao_ubo_cache);
        C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
        gl_vbao_ubo_dirty = 0;
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_ao_fbo);
    C89GL_glViewport(0, 0, gl_ao_width, gl_ao_height);

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glActiveTexture(GL_TEXTURE1);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_normal_tex);
    C89GL_glActiveTexture(GL_TEXTURE0);

    C89GL_glUseProgram(gl_vbao_program);
    C89GL_glBindVertexArray(gl_oit_vao);
    C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    C89GL_glUseProgram(0);
}

static void dispatch_vbao_blur(void) {
    if (!gl_vbao_blur_program || !gl_vbao_blur_ubo || !gl_ao_blur_fbo) return;

    if (gl_vbao_blur_ubo_dirty) {
        gl_vbao_blur_ubo_cache.screen_size[0]  = (float)gl_ao_width;
        gl_vbao_blur_ubo_cache.screen_size[1]  = (float)gl_ao_height;
        gl_vbao_blur_ubo_cache.depth_threshold = 0.0005f;
        gl_vbao_blur_ubo_cache._pad            = 0.0f;

        C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_blur_ubo);
        C89GL_glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(vbao_blur_ubo_t),
                              &gl_vbao_blur_ubo_cache);
        C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);
        gl_vbao_blur_ubo_dirty = 0;
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_ao_blur_fbo);
    C89GL_glViewport(0, 0, gl_ao_width, gl_ao_height);

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_tex);
    C89GL_glActiveTexture(GL_TEXTURE1);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glActiveTexture(GL_TEXTURE0);

    C89GL_glUseProgram(gl_vbao_blur_program);
    C89GL_glBindVertexArray(gl_oit_vao);
    C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    C89GL_glUseProgram(0);
}

static void init_cluster_resources(void) {
    {
        char defines[64];
        snprintf(defines, sizeof(defines),
                 "#version 430 core\n#define CLUSTER_WORKGROUP_SIZE %d\n",
                 CLUSTER_WORKGROUP_SIZE);
        GLuint cs = compile_shader_with_defines(GL_COMPUTE_SHADER, "cluster.comp", defines);
        if (!cs) {
            printf("ERROR: Failed to compile cluster compute shader.\n");
            return;
        }
        gl_cluster_program = C89GL_glCreateProgram();
        C89GL_glAttachShader(gl_cluster_program, cs);
        C89GL_glLinkProgram(gl_cluster_program);
        {
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
        }
        C89GL_glDeleteShader(cs);
    }

    cluster_u_depth_tex = C89GL_glGetUniformLocation(gl_cluster_program, "uDepthTex");
    cluster_u_num_lights = C89GL_glGetUniformLocation(gl_cluster_program, "uNumLights");
    cluster_u_tile_size = C89GL_glGetUniformLocation(gl_cluster_program, "uTileSize");
    cluster_u_num_tiles_x = C89GL_glGetUniformLocation(gl_cluster_program, "uNumTilesX");
    cluster_u_num_tiles_y = C89GL_glGetUniformLocation(gl_cluster_program, "uNumTilesY");
    cluster_u_depth_slices = C89GL_glGetUniformLocation(gl_cluster_program, "uDepthSlices");
    cluster_u_near = C89GL_glGetUniformLocation(gl_cluster_program, "uNear");
    cluster_u_far = C89GL_glGetUniformLocation(gl_cluster_program, "uFar");
    cluster_u_view = C89GL_glGetUniformLocation(gl_cluster_program, "uView");
    cluster_u_inv_view_proj = C89GL_glGetUniformLocation(gl_cluster_program, "uInvViewProj");

    C89GL_glGenBuffers(1, &gl_light_ssbo);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_light_ssbo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_LIGHTS * sizeof(gpu_light_t), NULL, GL_DYNAMIC_DRAW);

    gl_num_tiles_x = (gl_render_width + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_tiles_y = (gl_render_height + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_clusters = gl_num_tiles_x * gl_num_tiles_y * CLUSTER_DEPTH_SLICES;

    {
        size_t cluster_list_size = gl_num_clusters * CLUSTER_MAX_LIGHTS_PER * sizeof(GLuint);
        C89GL_glGenBuffers(1, &gl_cluster_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, cluster_list_size, NULL, GL_DYNAMIC_DRAW);
    }

    {
        size_t offset_size = gl_num_clusters * sizeof(GLuint);
        C89GL_glGenBuffers(1, &gl_cluster_offset_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_offset_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, offset_size, NULL, GL_DYNAMIC_DRAW);
        C89GL_glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                                GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    }

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    printf("Clustered rendering initialised: tiles=%dx%d, clusters=%d\n",
           gl_num_tiles_x, gl_num_tiles_y, gl_num_clusters);
}

static void init_env_cube_resources(void) {
    int f;

    /* Environment cube: color only, mipmapped, used for IBL. */
    C89GL_glGenTextures(1, &gl_sky_cube);
    C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_sky_cube);
    for (f = 0; f < PROBE_FACE_COUNT; f++) {
        C89GL_glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                           GL_RGBA16F, PROBE_SIZE, PROBE_SIZE, 0,
                           GL_RGBA, GL_FLOAT, NULL);
    }
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    C89GL_glGenFramebuffers(1, &gl_sky_cube_fbo);

    /* Depth cube: depth only, no mipmaps. General-purpose scene depth
     * from the camera POV; currently consumed by the audio compute
     * shaders. Attached to a depth-only FBO (draw buffer NONE).
     *
     * Deliberately not PROBE_SIZE: the consumers are per-voice single-tap
     * occlusion and distance lookups, so the extra resolution the IBL cube
     * needs buys nothing here. */
    C89GL_glGenTextures(1, &gl_depth_cube);
    C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_depth_cube);
    for (f = 0; f < PROBE_FACE_COUNT; f++) {
        C89GL_glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                           GL_DEPTH_COMPONENT24, PROBE_DEPTH_SIZE, PROBE_DEPTH_SIZE, 0,
                           GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    }
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    C89GL_glGenFramebuffers(1, &gl_depth_cube_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_depth_cube_fbo);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                                 gl_depth_cube, 0);
    C89GL_glDrawBuffer(GL_NONE);
    C89GL_glReadBuffer(GL_NONE);
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("Depth cube FBO incomplete! status=0x%x\n", s);
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    C89GL_glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    /* Sky program, cube-face mode. */
    {
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                                "sky.frag",
                                                "#version 430 core\n");
        if (gl_fullscreen_vs && fs) {
            gl_sky_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_sky_program, gl_fullscreen_vs);
            C89GL_glAttachShader(gl_sky_program, fs);
            C89GL_glLinkProgram(gl_sky_program);
            {
                GLint st;
                C89GL_glGetProgramiv(gl_sky_program, GL_LINK_STATUS, &st);
                if (!st) {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_sky_program, sizeof(log), NULL, log);
                    printf("Sky program link error:\n%s\n", log);
                    C89GL_glDeleteProgram(gl_sky_program);
                    gl_sky_program = 0;
                } else {
                    sky_u_face_index    = C89GL_glGetUniformLocation(gl_sky_program, "uFaceIndex");
                    sky_u_probe_size    = C89GL_glGetUniformLocation(gl_sky_program, "uProbeSize");
                    sky_u_time          = C89GL_glGetUniformLocation(gl_sky_program, "uTime");
                    sky_u_sky_zenith    = C89GL_glGetUniformLocation(gl_sky_program, "uSkyZenith");
                    sky_u_sky_horizon   = C89GL_glGetUniformLocation(gl_sky_program, "uSkyHorizon");
                    sky_u_ground_color  = C89GL_glGetUniformLocation(gl_sky_program, "uGroundColor");
                    sky_u_cloud_color   = C89GL_glGetUniformLocation(gl_sky_program, "uCloudColor");
                    sky_u_sky_exponent  = C89GL_glGetUniformLocation(gl_sky_program, "uSkyExponent");
                    sky_u_cloud_coverage = C89GL_glGetUniformLocation(gl_sky_program, "uCloudCoverage");
                    printf("Sky program initialised (cube-face mode).\n");
                }
            }
            C89GL_glDeleteShader(fs);
        } else {
            if (fs) C89GL_glDeleteShader(fs);
            printf("WARNING: sky.frag failed to compile; env cube will fall back to a flat clear.\n");
        }
    }

    /* Sky program, skybox variant. */
    {
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                                "sky.frag",
                                                "#version 430 core\n#define SKYBOX_MODE 1\n");
        if (gl_fullscreen_vs && fs) {
            gl_skybox_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_skybox_program, gl_fullscreen_vs);
            C89GL_glAttachShader(gl_skybox_program, fs);
            C89GL_glLinkProgram(gl_skybox_program);
            {
                GLint st;
                C89GL_glGetProgramiv(gl_skybox_program, GL_LINK_STATUS, &st);
                if (!st) {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_skybox_program, sizeof(log), NULL, log);
                    printf("Skybox program link error:\n%s\n", log);
                    C89GL_glDeleteProgram(gl_skybox_program);
                    gl_skybox_program = 0;
                } else {
                    skybox_u_inv_view_proj  = C89GL_glGetUniformLocation(gl_skybox_program, "uInvViewProj");
                    skybox_u_cam_eye        = C89GL_glGetUniformLocation(gl_skybox_program, "uCamEye");
                    skybox_u_screen_size    = C89GL_glGetUniformLocation(gl_skybox_program, "uScreenSize");
                    skybox_u_time           = C89GL_glGetUniformLocation(gl_skybox_program, "uTime");
                    skybox_u_sky_zenith     = C89GL_glGetUniformLocation(gl_skybox_program, "uSkyZenith");
                    skybox_u_sky_horizon    = C89GL_glGetUniformLocation(gl_skybox_program, "uSkyHorizon");
                    skybox_u_sky_ground     = C89GL_glGetUniformLocation(gl_skybox_program, "uSkyGround");
                    skybox_u_cloud_color    = C89GL_glGetUniformLocation(gl_skybox_program, "uCloudColor");
                    skybox_u_sky_exponent   = C89GL_glGetUniformLocation(gl_skybox_program, "uSkyExponent");
                    skybox_u_cloud_coverage = C89GL_glGetUniformLocation(gl_skybox_program, "uCloudCoverage");
                    printf("Sky program initialised (skybox mode).\n");
                }
            }
            C89GL_glDeleteShader(fs);
        } else {
            if (fs) C89GL_glDeleteShader(fs);
            printf("WARNING: sky.frag SKYBOX_MODE failed to compile; will fall back to clear color.\n");
        }
    }

    gl_env_box_min = vec3_init_from_3(-1000.0f, -1000.0f, -1000.0f);
    gl_env_box_max = vec3_init_from_3( 1000.0f,  1000.0f,  1000.0f);
    gl_env_box_set = 0;

    /* Both sizes, because they are no longer the same number and a single
     * figure here would hide which cube was actually built. */
    printf("Environment cube initialised (cubemap %d^2), "
           "audio depth cube (cubemap %d^2).\n",
           PROBE_SIZE, PROBE_DEPTH_SIZE);
}

static void init_wboit_resources(void) {
    C89GL_glGenFramebuffers(1, &gl_oit_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);

    C89GL_glGenTextures(1, &gl_oit_accum_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_accum_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                       gl_render_width, gl_render_height, 0,
                       GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, gl_oit_accum_tex, 0);

    C89GL_glGenTextures(1, &gl_oit_reveal_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_reveal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_render_width, gl_render_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                 GL_TEXTURE_2D, gl_oit_reveal_tex, 0);

    /* The real bloom source, not a staging copy. gl_emissive_tex is already
     * COLOR_ATTACHMENT2 of the main scene FBO and a texture may be attached to
     * more than one FBO at a time, so the WBOIT pass accumulates glow straight
     * into the attachment the bloom chain will read.
     *
     * Sharing it is what makes this pass the cheap option at high particle
     * counts. A separate OIT emissive target would need a read and a resolve in
     * the composite; a second particle pass would repeat the whole vertex and
     * raster load. Neither cost is avoidable here, and at millions of particles
     * the second pass would roughly double the dominant work in the frame.
     *
     * The consequence is that accumulation is additive and order-dependent, so
     * overlapping particles sum their glow rather than resolving to a weighted
     * average. That is the physically sensible answer for light, and it is the
     * only one available now that nothing downstream re-weights the value.
     *
     * This attachment is deliberately NOT cleared in the WBOIT pass. It already
     * holds the sky's sun and moon and any emissive material written earlier in
     * the frame; clearing it here would erase that glow. */
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                 GL_TEXTURE_2D, gl_emissive_tex, 0);

    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, gl_depth_tex, 0);

    {
        GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        C89GL_glDrawBuffers(3, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("WBOIT FBO incomplete! status=0x%x\n", s);
    }

    C89GL_glGenVertexArrays(1, &gl_oit_vao);

    {
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                                "oit_composite.frag",
                                                "#version 430 core\n");
        if (gl_fullscreen_vs && fs) {
            gl_oit_composite_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_oit_composite_program, gl_fullscreen_vs);
            C89GL_glAttachShader(gl_oit_composite_program, fs);
            C89GL_glLinkProgram(gl_oit_composite_program);
            {
                GLint st;
                C89GL_glGetProgramiv(gl_oit_composite_program, GL_LINK_STATUS, &st);
                if (!st) {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_oit_composite_program, sizeof(log), NULL, log);
                    printf("OIT composite link error:\n%s\n", log);
                    C89GL_glDeleteProgram(gl_oit_composite_program);
                    gl_oit_composite_program = 0;
                } else {
                    oit_u_accum_tex  = C89GL_glGetUniformLocation(gl_oit_composite_program, "uAccumTexture");
                    oit_u_reveal_tex = C89GL_glGetUniformLocation(gl_oit_composite_program, "uRevealTexture");
                }
            }
            C89GL_glDeleteShader(fs);
        } else {
            if (fs) C89GL_glDeleteShader(fs);
            printf("ERROR: Failed to compile OIT composite shaders.\n");
        }
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void init_vbao_resources(void) {
    /* Fully-unoccluded stand-in for when the pass is skipped. */
    {
        unsigned char white = 255;
        C89GL_glGenTextures(1, &gl_ao_white_tex);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_white_tex);
        C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0,
                           GL_RED, GL_UNSIGNED_BYTE, &white);
        C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    C89GL_glGenTextures(1, &gl_ao_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    C89GL_glGenFramebuffers(1, &gl_ao_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_ao_fbo);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, gl_ao_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("VBAO FBO incomplete! status=0x%x\n", s);
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    {
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                                "vbao.frag",
                                                "#version 430 core\n");
        if (!gl_fullscreen_vs || !fs) {
            if (fs) C89GL_glDeleteShader(fs);
            fprintf(stderr, "ERROR: Failed to compile vbao program\n");
            return;
        }
        gl_vbao_program = C89GL_glCreateProgram();
        C89GL_glAttachShader(gl_vbao_program, gl_fullscreen_vs);
        C89GL_glAttachShader(gl_vbao_program, fs);
        C89GL_glLinkProgram(gl_vbao_program);
        C89GL_glDeleteShader(fs);
    }

    {
        GLint status;
        C89GL_glGetProgramiv(gl_vbao_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_vbao_program, sizeof(log), NULL, log);
            printf("VBAO program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_vbao_program);
            gl_vbao_program = 0;
            return;
        }
    }

    {
        GLuint block = C89GL_glGetUniformBlockIndex(gl_vbao_program, "VBAOUniforms");
        if (block != GL_INVALID_INDEX)
            C89GL_glUniformBlockBinding(gl_vbao_program, block, VBAO_UBO_BINDING);
    }

    C89GL_glGenBuffers(1, &gl_vbao_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(vbao_ubo_t), NULL, GL_STATIC_DRAW);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, VBAO_UBO_BINDING, gl_vbao_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("VBAO initialised (fragment stage, half-res %dx%d).\n",
           gl_ao_width, gl_ao_height);
}

static void init_vbao_blur_resources(void) {
    C89GL_glGenTextures(1, &gl_ao_blurred_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_blurred_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    C89GL_glGenFramebuffers(1, &gl_ao_blur_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_ao_blur_fbo);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, gl_ao_blurred_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("VBAO blur FBO incomplete! status=0x%x\n", s);
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    {
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                                "vbao_blur.frag",
                                                "#version 430 core\n");
        if (!gl_fullscreen_vs || !fs) {
            if (fs) C89GL_glDeleteShader(fs);
            fprintf(stderr, "ERROR: Failed to compile vbao_blur program\n");
            return;
        }
        gl_vbao_blur_program = C89GL_glCreateProgram();
        C89GL_glAttachShader(gl_vbao_blur_program, gl_fullscreen_vs);
        C89GL_glAttachShader(gl_vbao_blur_program, fs);
        C89GL_glLinkProgram(gl_vbao_blur_program);
        C89GL_glDeleteShader(fs);
    }

    {
        GLint status;
        C89GL_glGetProgramiv(gl_vbao_blur_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_vbao_blur_program, sizeof(log), NULL, log);
            printf("VBAO blur program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_vbao_blur_program);
            gl_vbao_blur_program = 0;
            return;
        }
    }

    {
        GLuint block = C89GL_glGetUniformBlockIndex(gl_vbao_blur_program, "BlurUniforms");
        if (block != GL_INVALID_INDEX)
            C89GL_glUniformBlockBinding(gl_vbao_blur_program, block, VBAO_BLUR_UBO_BINDING);
    }

    C89GL_glGenBuffers(1, &gl_vbao_blur_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_blur_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(vbao_blur_ubo_t), NULL, GL_STATIC_DRAW);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, VBAO_BLUR_UBO_BINDING, gl_vbao_blur_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("VBAO blur initialised (fragment stage, half-res %dx%d).\n",
           gl_ao_width, gl_ao_height);
}

static void init_post_process_resources(void) {
    char defines[64];
    snprintf(defines, sizeof(defines),
             "#version 430 core\n#define PP_DEBUG_SOURCE %d\n",
             POST_PROCESS_DEBUG_SOURCE);
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                             "post_process.frag",
                                             defines);
    if (!gl_fullscreen_vs || !fs) {
        if (fs) C89GL_glDeleteShader(fs);
        fprintf(stderr, "ERROR: Failed to compile post_process program\n");
        return;
    }
    gl_post_process_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_post_process_program, gl_fullscreen_vs);
    C89GL_glAttachShader(gl_post_process_program, fs);
    C89GL_glLinkProgram(gl_post_process_program);
    C89GL_glDeleteShader(fs);

    {
        GLint status;
        C89GL_glGetProgramiv(gl_post_process_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_post_process_program, sizeof(log), NULL, log);
            printf("post_process program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_post_process_program);
            gl_post_process_program = 0;
            return;
        }
    }

    pp_u_screen_size = C89GL_glGetUniformLocation(gl_post_process_program, "uScreenSize");
    pp_u_exposure    = C89GL_glGetUniformLocation(gl_post_process_program, "uExposure");
    pp_u_gamma       = C89GL_glGetUniformLocation(gl_post_process_program, "uGamma");
    pp_u_bloom_tex   = C89GL_glGetUniformLocation(gl_post_process_program, "uBloomTex");
    pp_u_bloom_intensity =
        C89GL_glGetUniformLocation(gl_post_process_program, "uBloomIntensity");

    printf("Post-process initialised (HDR resolve -> sRGB).\n");
}

/* Compiles one full-screen pass from a fragment shader file. Returns 0 on any
 * failure so a single bad shader cannot leave a half-built program bound. */
static GLuint compile_fullscreen_program(const char *frag_file, const char *label) {
    GLuint fs, prog;
    GLint status;

    if (!gl_fullscreen_vs) {
        fprintf(stderr, "ERROR: %s: no shared full-screen vertex shader\n", label);
        return 0;
    }

    fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, frag_file, "#version 430 core\n");
    if (!fs) {
        fprintf(stderr, "ERROR: Failed to compile %s (%s)\n", label, frag_file);
        return 0;
    }

    prog = C89GL_glCreateProgram();
    C89GL_glAttachShader(prog, gl_fullscreen_vs);
    C89GL_glAttachShader(prog, fs);
    C89GL_glLinkProgram(prog);
    C89GL_glDeleteShader(fs);

    C89GL_glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        C89GL_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("%s link error:\n%s\n", label, log);
        C89GL_glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void init_bloom_resources(void) {
    gl_bloom_prefilter_program =
        compile_fullscreen_program("bloom_prefilter.frag", "bloom prefilter");
    gl_bloom_downsample_program =
        compile_fullscreen_program("bloom_downsample.frag", "bloom downsample");
    gl_bloom_upsample_program =
        compile_fullscreen_program("bloom_upsample.frag", "bloom upsample");

    if (gl_bloom_prefilter_program)
        bl_pf_u_source    = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uSourceTex");
    if (gl_bloom_prefilter_program)
        bl_pf_u_texel     = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uTexelSize");
    if (gl_bloom_prefilter_program)
        bl_pf_u_dsts      = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uDstSize");
    if (gl_bloom_prefilter_program)
        bl_pf_u_threshold = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uThreshold");
    if (gl_bloom_prefilter_program)
        bl_pf_u_knee      = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uKnee");
    if (gl_bloom_prefilter_program)
        bl_pf_u_clamp     = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uClamp");
    if (gl_bloom_prefilter_program)
        bl_pf_u_exposure  = C89GL_glGetUniformLocation(gl_bloom_prefilter_program, "uExposure");
    if (gl_bloom_downsample_program)
        bl_ds_u_source    = C89GL_glGetUniformLocation(gl_bloom_downsample_program, "uSourceTex");
    if (gl_bloom_downsample_program)
        bl_ds_u_texel     = C89GL_glGetUniformLocation(gl_bloom_downsample_program, "uTexelSize");
    if (gl_bloom_downsample_program)
        bl_ds_u_dsts      = C89GL_glGetUniformLocation(gl_bloom_downsample_program, "uDstSize");
    if (gl_bloom_upsample_program)
        bl_us_u_source    = C89GL_glGetUniformLocation(gl_bloom_upsample_program, "uSourceTex");
    if (gl_bloom_upsample_program)
        bl_us_u_texel     = C89GL_glGetUniformLocation(gl_bloom_upsample_program, "uTexelSize");
    if (gl_bloom_upsample_program)
        bl_us_u_dsts      = C89GL_glGetUniformLocation(gl_bloom_upsample_program, "uDstSize");
    if (gl_bloom_upsample_program)
        bl_us_u_radius    = C89GL_glGetUniformLocation(gl_bloom_upsample_program, "uRadius");

    if (gl_bloom_prefilter_program && gl_bloom_downsample_program && gl_bloom_upsample_program)
        printf("Bloom initialised (%d mip levels, threshold %.2f knee %.2f).\n",
               BLOOM_MIP_COUNT, (double)gl_bloom_threshold, (double)gl_bloom_knee);
}

static void init_fxaa_resources(void) {
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                            "fxaa.frag",
                                            "#version 430 core\n");
    if (!gl_fullscreen_vs || !fs) {
        if (fs) C89GL_glDeleteShader(fs);
        fprintf(stderr, "ERROR: Failed to compile anti_aliasing program\n");
        return;
    }
    gl_fxaa_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_fxaa_program, gl_fullscreen_vs);
    C89GL_glAttachShader(gl_fxaa_program, fs);
    C89GL_glLinkProgram(gl_fxaa_program);
    C89GL_glDeleteShader(fs);

    {
        GLint status;
        C89GL_glGetProgramiv(gl_fxaa_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_fxaa_program, sizeof(log), NULL, log);
            printf("AA program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_fxaa_program);
            gl_fxaa_program = 0;
            return;
        }
    }

    aa_u_screen_texture = C89GL_glGetUniformLocation(gl_fxaa_program, "screenTexture");
    aa_u_resolution      = C89GL_glGetUniformLocation(gl_fxaa_program, "resolution");

    printf("Anti-aliasing (FXAA) initialised.\n");
}

/* ---- Final dither/present pass ----
 * Owns the pre-present target as well, so it is initialised after the AA
 * targets exist. */
static void init_dither_resources(void) {
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                            "dither.frag",
                                            "#version 430 core\n");
    if (!gl_fullscreen_vs || !fs) {
        if (fs) C89GL_glDeleteShader(fs);
        fprintf(stderr, "ERROR: Failed to compile dither program\n");
        return;
    }
    gl_dither_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_dither_program, gl_fullscreen_vs);
    C89GL_glAttachShader(gl_dither_program, fs);
    C89GL_glLinkProgram(gl_dither_program);
    C89GL_glDeleteShader(fs);

    {
        GLint status;
        C89GL_glGetProgramiv(gl_dither_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_dither_program, sizeof(log), NULL, log);
            printf("Dither program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_dither_program);
            gl_dither_program = 0;
            return;
        }
    }

    dt_u_screen_texture = C89GL_glGetUniformLocation(gl_dither_program, "screenTexture");
    dt_u_time           = C89GL_glGetUniformLocation(gl_dither_program, "uTime");

    /* Window-resolution pre-present surface, sampled 1:1 by the dither pass,
     * so GL_NEAREST keeps it a straight copy with no extra softening. */
    C89GL_glGenFramebuffers(1, &gl_present_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_present_fbo);
    C89GL_glGenTextures(1, &gl_present_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_present_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_win_width, gl_win_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_present_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("Present FBO incomplete! status=0x%x\n", s);
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    printf("Dither/present pass initialised.\n");
}

/* ---- Supersampling resolve, used instead of FXAA + blit when scaling up ---- */
static void init_ssaa_resources(void) {
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                            "ssaa.frag",
                                            "#version 430 core\n");
    if (!gl_fullscreen_vs || !fs) {
        if (fs) C89GL_glDeleteShader(fs);
        fprintf(stderr, "ERROR: Failed to compile supersample resolve program\n");
        return;
    }
    gl_ssaa_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_ssaa_program, gl_fullscreen_vs);
    C89GL_glAttachShader(gl_ssaa_program, fs);
    C89GL_glLinkProgram(gl_ssaa_program);
    C89GL_glDeleteShader(fs);

    {
        GLint status;
        C89GL_glGetProgramiv(gl_ssaa_program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(gl_ssaa_program, sizeof(log), NULL, log);
            printf("SSAA program link error:\n%s\n", log);
            C89GL_glDeleteProgram(gl_ssaa_program);
            gl_ssaa_program = 0;
            return;
        }
    }

    ss_u_screen_texture = C89GL_glGetUniformLocation(gl_ssaa_program, "screenTexture");
    ss_u_src_size       = C89GL_glGetUniformLocation(gl_ssaa_program, "uSrcSize");
    ss_u_dst_size       = C89GL_glGetUniformLocation(gl_ssaa_program, "uDstSize");

    printf("Supersampling resolve initialised.\n");
}

static void init_audio_resources(void) {
#ifdef AUDIO_OCCLUSION
    {
        GLuint cs_occ = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_occlusion.comp", "#version 430 core\n");
        if (cs_occ) {
            gl_audio_occlusion_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_occlusion_program, cs_occ);
            C89GL_glLinkProgram(gl_audio_occlusion_program);
            {
                GLint status;
                C89GL_glGetProgramiv(gl_audio_occlusion_program, GL_LINK_STATUS, &status);
                if (status) {
                    occ_u_view_proj = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uViewProj");
                    occ_u_inv_view_proj  = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uInvViewProj");
                    occ_u_listener_pos   = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uListenerPos");
                    occ_u_num_voices     = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uNumVoices");
                    occ_u_probe_depth_cube = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uProbeDepthCube");
                    occ_u_probe_near_far   = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uProbeNearFar");
                } else {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_audio_occlusion_program, sizeof(log), NULL, log);
                    printf("Occlusion program link error: %s\n", log);
                    C89GL_glDeleteProgram(gl_audio_occlusion_program);
                    gl_audio_occlusion_program = 0;
                }
            }
            C89GL_glDeleteShader(cs_occ);
        }

        C89GL_glGenBuffers(1, &gl_audio_voice_input_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_voice_input_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER,
                   sizeof(gl_audio_voice_inputs), NULL, GL_STREAM_DRAW);

        C89GL_glGenBuffers(1, &gl_audio_propagation_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_propagation_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER,
                           sizeof(audio_propagation_output_t) * MAX_AUDIO_VOICES_GPU,
                           NULL, GL_STREAM_READ);
        gl_audio_propagation_fence = NULL;
        gl_audio_propagation_fence_age = 0;

        gl_audio_last_voice_count = 0;
        memset(gl_audio_last_voice_positions, 0, sizeof(gl_audio_last_voice_positions));
    }
#endif

#ifdef AUDIO_REVERB
    {
        GLuint cs_rev = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_reverb.comp", "#version 430 core\n");
        if (cs_rev) {
            gl_audio_reverb_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_reverb_program, cs_rev);
            C89GL_glLinkProgram(gl_audio_reverb_program);
            {
                GLint status;
                C89GL_glGetProgramiv(gl_audio_reverb_program, GL_LINK_STATUS, &status);
                if (status) {
                    rev_u_inv_view_proj = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uInvViewProj");
                    rev_u_listener_pos      = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uListenerPos");
                    rev_u_probe_depth_cube  = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uProbeDepthCube");
                    rev_u_probe_near_far    = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uProbeNearFar");
                } else {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_audio_reverb_program, sizeof(log), NULL, log);
                    printf("Reverb program link error: %s\n", log);
                    C89GL_glDeleteProgram(gl_audio_reverb_program);
                    gl_audio_reverb_program = 0;
                }
            }
            C89GL_glDeleteShader(cs_rev);
        }

        C89GL_glGenBuffers(1, &gl_audio_global_stats_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, REVERB_ACCUM_SIZE, NULL, GL_STREAM_READ);
        gl_audio_reverb_fence = NULL;
        gl_audio_reverb_fence_age = 0;
    }
#endif

#ifdef AUDIO_PORTAL
    {
        GLuint cs_port = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_portal.comp", "#version 430 core\n");
        if (cs_port) {
            gl_audio_portal_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_portal_program, cs_port);
            C89GL_glLinkProgram(gl_audio_portal_program);
            {
                GLint status;
                C89GL_glGetProgramiv(gl_audio_portal_program, GL_LINK_STATUS, &status);
                if (status) {
                    port_u_inv_view_proj = C89GL_glGetUniformLocation(gl_audio_portal_program, "uInvViewProj");
                    port_u_listener_pos     = C89GL_glGetUniformLocation(gl_audio_portal_program, "uListenerPos");
                    port_u_threshold        = C89GL_glGetUniformLocation(gl_audio_portal_program, "uPortalThreshold");
                    port_u_num_voices       = C89GL_glGetUniformLocation(gl_audio_portal_program, "uNumVoices");
                    port_u_view_proj        = C89GL_glGetUniformLocation(gl_audio_portal_program, "uViewProj");
                    port_u_probe_depth_cube = C89GL_glGetUniformLocation(gl_audio_portal_program, "uProbeDepthCube");
                    port_u_probe_near_far   = C89GL_glGetUniformLocation(gl_audio_portal_program, "uProbeNearFar");
                } else {
                    char log[512];
                    C89GL_glGetProgramInfoLog(gl_audio_portal_program, sizeof(log), NULL, log);
                    printf("Portal program link error: %s\n", log);
                    C89GL_glDeleteProgram(gl_audio_portal_program);
                    gl_audio_portal_program = 0;
                }
            }
            C89GL_glDeleteShader(cs_port);
        }

        C89GL_glGenBuffers(1, &gl_audio_portal_candidates_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, PORTAL_CANDIDATE_SIZE, NULL, GL_STREAM_READ);
        gl_audio_portal_fence = NULL;
        gl_audio_portal_fence_age = 0;

        {
            int i;
            for (i = 0; i < MAX_AUDIO_VOICES_GPU; i++) {
                gl_audio_portal_reset_template[i].dist = 1e10f;
                gl_audio_portal_reset_template[i].pos_x = 0.0f;
                gl_audio_portal_reset_template[i].pos_y = 0.0f;
                gl_audio_portal_reset_template[i].pos_z = 0.0f;
            }
        }
    }
#endif
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    printf("Audio resources initialised (occlusion:%s, reverb:%s, portal:%s).\n",
           #ifdef AUDIO_OCCLUSION
           "yes"
           #else
           "no"
           #endif
           ,
           #ifdef AUDIO_REVERB
           "yes"
           #else
           "no"
           #endif
           ,
           #ifdef AUDIO_PORTAL
           "yes"
           #else
           "no"
           #endif
          );
}

static void dispatch_audio_compute(void) {
    float probe_nf[2] = { gl_near, gl_far };

#ifdef AUDIO_REVERB
    if (gl_audio_reverb_fence) {
        gl_audio_reverb_fence_age++;
        if (gl_audio_reverb_fence_age > AUDIO_FENCE_MAX_AGE_FRAMES) {
            C89GL_glDeleteSync(gl_audio_reverb_fence);
            gl_audio_reverb_fence = NULL;
            gl_audio_reverb_fence_age = 0;
        }
    }
#endif
#ifdef AUDIO_OCCLUSION
    if (gl_audio_propagation_fence) {
        gl_audio_propagation_fence_age++;
        if (gl_audio_propagation_fence_age > AUDIO_FENCE_MAX_AGE_FRAMES) {
            C89GL_glDeleteSync(gl_audio_propagation_fence);
            gl_audio_propagation_fence = NULL;
            gl_audio_propagation_fence_age = 0;
        }
    }
#endif
#ifdef AUDIO_PORTAL
    if (gl_audio_portal_fence) {
        gl_audio_portal_fence_age++;
        if (gl_audio_portal_fence_age > AUDIO_FENCE_MAX_AGE_FRAMES) {
            C89GL_glDeleteSync(gl_audio_portal_fence);
            gl_audio_portal_fence = NULL;
            gl_audio_portal_fence_age = 0;
        }
    }
#endif

#ifdef AUDIO_REVERB
    if (gl_audio_reverb_program && gl_audio_global_stats_ssbo && !gl_audio_reverb_fence) {
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo);
        C89GL_glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, 0, REVERB_ACCUM_SIZE,
                                   GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        /* Reverb reads the depth cubemap only. There is no screen-space depth
         * input; audio occlusion moved to the cube and the old 2D binding left
         * behind. */
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_depth_cube);
        C89GL_glActiveTexture(GL_TEXTURE0);

        C89GL_glUseProgram(gl_audio_reverb_program);
        C89GL_glUniform1i(rev_u_probe_depth_cube, 0);
        C89GL_glUniform2fv(rev_u_probe_near_far, 1, probe_nf);
        {
            mat4 inv_view_proj = mat4_inverse(gl_view_proj);
            C89GL_glUniformMatrix4fv(rev_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
        }
        C89GL_glUniform3fv(rev_u_listener_pos, 1, (float*)&gl_cam_eye);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_audio_global_stats_ssbo);
        {
            C89GL_glDispatchCompute(1, 1, 1);
        }
        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);

        gl_audio_reverb_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl_audio_reverb_fence_age = 0;
    }
#endif

#ifdef AUDIO_OCCLUSION
    if (gl_audio_occlusion_program && g_audio_voice_count_gpu > 0 &&
        gl_audio_voice_input_ssbo && gl_audio_propagation_ssbo &&
        !gl_audio_propagation_fence) {

        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_depth_cube);
        C89GL_glActiveTexture(GL_TEXTURE0);

        C89GL_glUseProgram(gl_audio_occlusion_program);
        C89GL_glUniform1i(occ_u_probe_depth_cube, 0);
        C89GL_glUniform2fv(occ_u_probe_near_far, 1, probe_nf);
        C89GL_glUniformMatrix4fv(occ_u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
        {
            mat4 inv_view_proj = mat4_inverse(gl_view_proj);
            C89GL_glUniformMatrix4fv(occ_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
        }
        C89GL_glUniform3fv(occ_u_listener_pos, 1, (float*)&gl_cam_eye);
        C89GL_glUniform1i(occ_u_num_voices, g_audio_voice_count_gpu);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_audio_voice_input_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_audio_propagation_ssbo);
        {
            GLuint groups = (g_audio_voice_count_gpu + 7) / 8;
            C89GL_glDispatchCompute(groups, 1, 1);
        }
        C89GL_glUseProgram(0);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        C89GL_glActiveTexture(GL_TEXTURE0);

        gl_audio_propagation_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl_audio_propagation_fence_age = 0;
    }
#endif
}

#ifdef AUDIO_OCCLUSION
INLINE void render_set_audio_voice_data(const vec3 *positions, int count) {
    if (count < 0) count = 0;
    if (count > MAX_AUDIO_VOICES_GPU) count = MAX_AUDIO_VOICES_GPU;
    g_audio_voice_count_gpu = count;
    if (count == 0 || !gl_audio_voice_input_ssbo) return;

    if (count == gl_audio_last_voice_count &&
        memcmp(positions, gl_audio_last_voice_positions, sizeof(vec3) * count) == 0) {
        return;
    }
    memcpy(gl_audio_last_voice_positions, positions, sizeof(vec3) * count);
    gl_audio_last_voice_count = count;

    {
        int i;
        for (i = 0; i < count; i++) {
            gl_audio_voice_inputs[i].world_pos = positions[i];
            gl_audio_voice_inputs[i]._pad = 0.0f;
        }
    }

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_voice_input_ssbo);
    C89GL_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                          sizeof(audio_voice_input_t) * count,
                          gl_audio_voice_inputs);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

INLINE int render_poll_audio_propagation(audio_propagation_output_t *out, int max_voices) {
    int count;
    GLenum status;
    if (!gl_audio_propagation_ssbo || !gl_audio_propagation_fence) return 0;

    status = C89GL_glClientWaitSync(gl_audio_propagation_fence,
                                    GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        return 0;
    }

    C89GL_glDeleteSync(gl_audio_propagation_fence);
    gl_audio_propagation_fence = NULL;
    gl_audio_propagation_fence_age = 0;

    count = (g_audio_voice_count_gpu < max_voices) ? g_audio_voice_count_gpu : max_voices;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_propagation_ssbo);
    C89GL_glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                             sizeof(audio_propagation_output_t) * count, out);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return count;
}
#endif

#ifdef AUDIO_REVERB
INLINE int render_poll_audio_global_stats(audio_global_stats_t *stats) {
    GLenum status;
    reverb_group_accum_t groups[MAX_REVERB_GROUPS];
    float total_sum = 0.0f, total_sumsq = 0.0f;
    u32 total_count = 0;
    u32 total_min_bits = 0xFFFFFFFF;
    u32 total_max_bits = 0;
    int i;

    if (!gl_audio_global_stats_ssbo || !gl_audio_reverb_fence) return 0;

    status = C89GL_glClientWaitSync(gl_audio_reverb_fence,
                                    GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        return 0;
    }

    C89GL_glDeleteSync(gl_audio_reverb_fence);
    gl_audio_reverb_fence = NULL;
    gl_audio_reverb_fence_age = 0;

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo);
    C89GL_glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(groups), groups);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (i = 0; i < MAX_REVERB_GROUPS; i++) {
        if (groups[i].count > 0) {
            total_sum += groups[i].sum;
            total_sumsq += groups[i].sumsq;
            total_count += groups[i].count;
            if (groups[i].min_bits < total_min_bits) total_min_bits = groups[i].min_bits;
            if (groups[i].max_bits > total_max_bits) total_max_bits = groups[i].max_bits;
        }
    }
    if (total_count > 0) {
        real mean;
        stats->avg_depth = (real)(total_sum / total_count);
        stats->min_depth = u32_to_real(total_min_bits);
        stats->max_depth = u32_to_real(total_max_bits);
        mean = stats->avg_depth;
        stats->variance = (real)(total_sumsq / total_count) - mean * mean;
    } else {
        stats->avg_depth = (real)5.0f;
        stats->min_depth = (real)0.5f;
        stats->max_depth = (real)10.0f;
        stats->variance = (real)1.0f;
    }
    return 1;
}
#endif

#ifdef AUDIO_PORTAL
INLINE void render_trigger_portal_search(void) {
    if (!gl_audio_portal_program) return;
    if (!gl_audio_portal_candidates_ssbo) return;
    if (gl_audio_portal_fence) return;

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo);
    C89GL_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                          sizeof(gl_audio_portal_reset_template),
                          gl_audio_portal_reset_template);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_depth_cube);
    C89GL_glActiveTexture(GL_TEXTURE0);

    C89GL_glUseProgram(gl_audio_portal_program);

    C89GL_glUniform1i(port_u_probe_depth_cube, 0);
    {
        float probe_nf[2];
        probe_nf[0] = gl_near;
        probe_nf[1] = gl_far;
        C89GL_glUniform2fv(port_u_probe_near_far, 1, probe_nf);
    }
    C89GL_glUniform1f(port_u_threshold, 0.5f);

    {
        mat4 inv_view_proj = mat4_inverse(gl_view_proj);
        C89GL_glUniformMatrix4fv(port_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
    }
    C89GL_glUniform3fv(port_u_listener_pos, 1, (float*)&gl_cam_eye);

#ifdef AUDIO_OCCLUSION
    if (gl_audio_voice_input_ssbo && g_audio_voice_count_gpu > 0) {
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_audio_voice_input_ssbo);
        C89GL_glUniform1i(port_u_num_voices, g_audio_voice_count_gpu);
        C89GL_glUniformMatrix4fv(port_u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
    } else {
        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);
        return;
    }
#else
    C89GL_glUseProgram(0);
    C89GL_glActiveTexture(GL_TEXTURE0);
    return;
#endif

    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gl_audio_portal_candidates_ssbo);

    {
        GLuint groups = (g_audio_voice_count_gpu > 0) ? (GLuint)g_audio_voice_count_gpu : 1;
        C89GL_glDispatchCompute(groups, 1, 1);
    }

    C89GL_glUseProgram(0);
    C89GL_glActiveTexture(GL_TEXTURE0);

    gl_audio_portal_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl_audio_portal_fence_age = 0;
}

INLINE int render_poll_audio_portal(vec3 *portal_positions, float *portal_distances, int *portal_active_flags, int max_voices) {
    GLenum status;
    portal_candidate_t cands[MAX_AUDIO_VOICES_GPU];
    int count = 0;
    int num_to_read;
    int i;

    if (!gl_audio_portal_candidates_ssbo || !gl_audio_portal_fence) return 0;

    status = C89GL_glClientWaitSync(gl_audio_portal_fence,
                                    GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) return 0;

    C89GL_glDeleteSync(gl_audio_portal_fence);
    gl_audio_portal_fence = NULL;
    gl_audio_portal_fence_age = 0;

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo);
    C89GL_glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(cands), cands);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    num_to_read = (g_audio_voice_count_gpu < max_voices) ? g_audio_voice_count_gpu : max_voices;
    for (i = 0; i < num_to_read; i++) {
        if (cands[i].dist < 1e9f) {
            portal_positions[i] = vec3_init_from_3(cands[i].pos_x, cands[i].pos_y, cands[i].pos_z);
            portal_distances[i] = cands[i].dist;
            portal_active_flags[i] = 1;
            count++;
        } else {
            portal_active_flags[i] = 0;
            portal_distances[i] = 0.0f;
        }
    }
    return count;
}
#endif

INLINE void render_clear_lights(void) {
    g_light_count = 0;
}

INLINE void render_set_light_at_index(int index, const light_definition *def) {
    if (index < 0 || index >= MAX_LIGHTS) return;
    g_lights[index] = *def;
    if (index + 1 > g_light_count) g_light_count = index + 1;
}

INLINE void render_set_exposure(real exposure) { gl_post_exposure = (float)exposure; }
INLINE void render_set_gamma(real gamma)       { gl_post_gamma    = (float)gamma; }

INLINE void render_set_bloom(int enabled, real intensity, real threshold, real knee) {
    gl_bloom_enabled   = enabled;
    gl_bloom_intensity = (float)intensity;
    gl_bloom_threshold = (float)threshold;
    gl_bloom_knee      = (float)knee;
}
INLINE void render_set_bloom_radius(real radius) { gl_bloom_radius = (float)radius; }

INLINE void render_set_env_probe_box(vec3 boxMin, vec3 boxMax) {
    gl_env_box_min = boxMin;
    gl_env_box_max = boxMax;
    gl_env_box_set = 1;
}

INLINE void render_set_env_cube_enabled(int enabled) {
    if (enabled != gl_env_cube_enabled) {
        int i;
        gl_env_cube_enabled = enabled;
        if (gl_shader_cache) {
            for (i = 0; i < gl_shader_cache_size; i++) {
                if (gl_shader_cache[i].program)
                    C89GL_glDeleteProgram(gl_shader_cache[i].program);
            }
            free(gl_shader_cache);
            gl_shader_cache = NULL;
            gl_shader_cache_size = 0;
            gl_shader_cache_count = 0;
        }
    }
}

/* EFFECTIVELY A NO-OP TODAY.
 *
 * These values are stored, but sky.frag does not declare matching uniforms, so
 * the sky you see is the compile-time SKY_* constants in the shader. The
 * per-frame uploads that used to be issued here and in render_sky_cube_pass /
 * render_sky_pass were removed because every one of them targeted location -1
 * and the driver discarded them.
 *
 * To make this API real: declare the six uniforms in sky.frag (both the
 * cube-face and skybox variants compile the same source, so one edit covers
 * both), restore the uploads, and drop the SKY_* #defines in favour of uniform
 * defaults. Note the C-side defaults here differ from the shader constants, so
 * this is a visible change that should be made on purpose. */
INLINE void render_set_sky(vec3 zenith, vec3 horizon, vec3 ground, real exponent) {
    gl_sky_zenith   = zenith;
    gl_sky_horizon  = horizon;
    gl_sky_ground   = ground;
    gl_sky_exponent = (float)exponent;
}

INLINE void render_set_clouds(vec3 color, real coverage) {
    gl_sky_cloud       = color;
    gl_sky_cloud_cover = (float)coverage;
}

INLINE void render_set_sky_ambient_scale(real scale) {
    gl_sky_ambient_scale = (float)scale;
}

static void set_uniforms_for_variant(shader_variant_t* variant, int is_depth_pass,
                                     alpha_pass_side side) {
    if (!variant) return;
    if (variant->u_view_proj != -1)
        C89GL_glUniformMatrix4fv(variant->u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
    if (variant->u_view != -1)
        C89GL_glUniformMatrix4fv(variant->u_view, 1, GL_TRUE, (float*)&gl_view);
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

    if (!is_depth_pass) {
        if (variant->u_sky_zenith != -1)
            C89GL_glUniform3fv(variant->u_sky_zenith, 1, (float*)&gl_sky_zenith);
        if (variant->u_sky_horizon != -1)
            C89GL_glUniform3fv(variant->u_sky_horizon, 1, (float*)&gl_sky_horizon);
        if (variant->u_sky_ground != -1)
            C89GL_glUniform3fv(variant->u_sky_ground, 1, (float*)&gl_sky_ground);
        if (variant->u_sky_exponent != -1)
            C89GL_glUniform1f(variant->u_sky_exponent, gl_sky_exponent);
        if (variant->u_cloud_color != -1)
            C89GL_glUniform3fv(variant->u_cloud_color, 1, (float*)&gl_sky_cloud);
        if (variant->u_cloud_coverage != -1)
            C89GL_glUniform1f(variant->u_cloud_coverage, gl_sky_cloud_cover);
        if (variant->u_sky_ambient_scale != -1)
            C89GL_glUniform1f(variant->u_sky_ambient_scale, gl_sky_ambient_scale);

        if (variant->u_depth_tex != -1) {
            C89GL_glActiveTexture(GL_TEXTURE1);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
            C89GL_glUniform1i(variant->u_depth_tex, 1);
        }
        if (variant->u_screen_size != -1)
            C89GL_glUniform2f(variant->u_screen_size, (float)gl_render_width, (float)gl_render_height);
        if (variant->u_num_tiles_x != -1)
            C89GL_glUniform1i(variant->u_num_tiles_x, gl_num_tiles_x);
        if (variant->u_num_tiles_y != -1)
            C89GL_glUniform1i(variant->u_num_tiles_y, gl_num_tiles_y);
        /* Depth bounds used to linearize and bin this fragment's depth. Must
         * match the values the cluster build was dispatched with. */
        if (variant->u_cluster_near != -1)
            C89GL_glUniform1f(variant->u_cluster_near, gl_near);
        if (variant->u_cluster_far != -1)
            C89GL_glUniform1f(variant->u_cluster_far, gl_far);

        C89GL_glActiveTexture(GL_TEXTURE2);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);

        C89GL_glActiveTexture(GL_TEXTURE4);
        C89GL_glBindTexture(GL_TEXTURE_2D,
                            gl_vbao_enabled ? gl_ao_blurred_tex : gl_ao_white_tex);

        if (variant->u_alpha_pass != -1)
            C89GL_glUniform1i(variant->u_alpha_pass, (int)side);

        if (variant->u_refraction_scale != -1)
            C89GL_glUniform1f(variant->u_refraction_scale, 0.1f);

        C89GL_glActiveTexture(GL_TEXTURE0 + ENV_CUBE_UNIT);
        C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_sky_cube);
        if (variant->u_env_cube_max_mip != -1)
            C89GL_glUniform1f(variant->u_env_cube_max_mip,
                              (float)(PROBE_MIP_LEVELS - 1));

        C89GL_glActiveTexture(GL_TEXTURE0);

        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_light_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_cluster_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_cluster_offset_ssbo);
    }

    /* The joint palette is read by the VERTEX shader, which the depth prepass
     * shares with the colour pass, so it must be bound on every path -- not
     * just the non-depth one. Leaving it unbound made the prepass read zeros,
     * collapse `skin` to the identity fallback in material.vert, and write
     * depth for the bind pose while colour drew the animated pose. Fragments
     * behind the stale bind-pose depth were then rejected, punching
     * model-shaped holes that fell through to the sky. */
    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, JOINT_SSBO_BINDING, gl_joint_ubo);

    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MATERIAL_UBO_BINDING, gl_material_ubo);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MODEL_UBO_BINDING, gl_model_ubo);
}

/* ================================================================
   Per-primitive GPU cache
   ================================================================ */

/* Pack one vertex into the 24-float GPU layout the shader expects.
 * The layout is position, normal, position again (attr2), unused float
 * (attr3 - set per draw as a vertex attrib constant), face normal,
 * centroid, then the four bone indices (attr6) and weights (attr7).
 * Bone weights arrive as 0-255 and are stored pre-divided by 255 so the
 * shader can use them directly. */
static void gpu_pack_vertex(float **pp,
                            vec3 position, vec3 normal,
                            vec3 face_normal, vec3 centroid,
                            const u16 bone_idx[4], const u8 bone_wgt[4])
{
    float *p = *pp;
    p[0]  = position.position.x; p[1]  = position.position.y; p[2]  = position.position.z;
    p[3]  = normal.position.x;   p[4]  = normal.position.y;   p[5]  = normal.position.z;
    p[6]  = position.position.x; p[7]  = position.position.y; p[8]  = position.position.z;
    p[9]  = 0.0f;
    p[10] = face_normal.position.x; p[11] = face_normal.position.y; p[12] = face_normal.position.z;
    p[13] = centroid.position.x;    p[14] = centroid.position.y;    p[15] = centroid.position.z;
    p[16] = (float)bone_idx[0]; p[17] = (float)bone_idx[1];
    p[18] = (float)bone_idx[2]; p[19] = (float)bone_idx[3];
    p[20] = (float)bone_wgt[0] * (1.0f / 255.0f);
    p[21] = (float)bone_wgt[1] * (1.0f / 255.0f);
    p[22] = (float)bone_wgt[2] * (1.0f / 255.0f);
    p[23] = (float)bone_wgt[3] * (1.0f / 255.0f);
    *pp = p + VERTEX_STRIDE_FLOATS;
}

/* Find or create the GPU primitive for a model_primitive. Returns NULL
 * if the cache is full or an allocation fails. The lookup is linear but
 * the table is small (hundreds at most) and repeated lookups hit the
 * cache on the first match. */
static gpu_primitive_t*
gpu_primitive_get_or_upload(const model_definition *mod, u32 prim_idx)
{
    model_primitive *prim;
    material_definition *mat;
    gpu_primitive_t *gp;
    float  *verts_stage;
    u32    *inds_stage;
    u32     i, tri, tri_count;
    model_vertex *src_verts;
    u32    *src_idx;
    int     is_transparent;

    prim = TAG_BLOCK_GET_ELEMENT(&mod->primitives, prim_idx, model_primitive);
    if (!prim || !prim->vertices.address || !prim->indices.address) return NULL;
    if (prim->indices.count == 0 || (prim->indices.count % 3) != 0) return NULL;

    for (i = 0; i < (u32)gl_gpu_prim_count; i++) {
        if (gl_gpu_prim_keys[i] == prim) return &gl_gpu_prims[i];
    }

    if (gl_gpu_prim_count >= MAX_GPU_PRIMITIVES) return NULL;

    /* Preserve the original behavior: face normal + centroid were zero
     * for opaque and computed for transparent. */
    mat = NULL;
    if (prim->material_index >= 0 && mod->materials.address) {
        tag_reference *refs = (tag_reference*)mod->materials.address;
        i32 mat_handle = refs[prim->material_index].handle;
        if (mat_handle >= 0)
            mat = (material_definition*)tag_get(mat_handle, TAG_material);
    }
    is_transparent = (mat && (mat->render_method & EFFECT_ALPHA)) ? 1 : 0;

    src_verts = (model_vertex*)prim->vertices.address;
    src_idx   = (u32*)prim->indices.address;
    tri_count = prim->indices.count / 3;

    verts_stage = (float*)malloc((size_t)prim->indices.count * VERTEX_STRIDE_FLOATS * sizeof(float));
    inds_stage  = (u32*)malloc((size_t)prim->indices.count * sizeof(u32));
    if (!verts_stage || !inds_stage) { free(verts_stage); free(inds_stage); return NULL; }

    {
        float *p = verts_stage;
        for (tri = 0; tri < tri_count; tri++) {
            u32 i0 = src_idx[tri*3+0], i1 = src_idx[tri*3+1], i2 = src_idx[tri*3+2];
            vec3 v0 = src_verts[i0].position, v1 = src_verts[i1].position, v2 = src_verts[i2].position;
            vec3 n0 = src_verts[i0].normal,   n1 = src_verts[i1].normal,   n2 = src_verts[i2].normal;
            u16 b0[4] = { src_verts[i0].bone_index0, src_verts[i0].bone_index1,
                          src_verts[i0].bone_index2, src_verts[i0].bone_index3 };
            u16 b1[4] = { src_verts[i1].bone_index0, src_verts[i1].bone_index1,
                          src_verts[i1].bone_index2, src_verts[i1].bone_index3 };
            u16 b2[4] = { src_verts[i2].bone_index0, src_verts[i2].bone_index1,
                          src_verts[i2].bone_index2, src_verts[i2].bone_index3 };
            u8  w0[4] = { src_verts[i0].bone_weight0, src_verts[i0].bone_weight1,
                          src_verts[i0].bone_weight2, src_verts[i0].bone_weight3 };
            u8  w1[4] = { src_verts[i1].bone_weight0, src_verts[i1].bone_weight1,
                          src_verts[i1].bone_weight2, src_verts[i1].bone_weight3 };
            u8  w2[4] = { src_verts[i2].bone_weight0, src_verts[i2].bone_weight1,
                          src_verts[i2].bone_weight2, src_verts[i2].bone_weight3 };
            vec3 lfn, lc;
            if (is_transparent) {
                lfn = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
                lc  = vec3_div_scalar(vec3_add(vec3_add(v0, v1), v2), 3.0f);
            } else {
                lfn = vec3_init_from_3(0.0f, 0.0f, 0.0f);
                lc  = vec3_init_from_3(0.0f, 0.0f, 0.0f);
            }
            gpu_pack_vertex(&p, v0, n0, lfn, lc, b0, w0);
            gpu_pack_vertex(&p, v1, n1, lfn, lc, b1, w1);
            gpu_pack_vertex(&p, v2, n2, lfn, lc, b2, w2);
            inds_stage[tri*3+0] = tri*3 + 0;
            inds_stage[tri*3+1] = tri*3 + 1;
            inds_stage[tri*3+2] = tri*3 + 2;
        }
    }

    gp = &gl_gpu_prims[gl_gpu_prim_count];
    gl_gpu_prim_keys[gl_gpu_prim_count] = prim;
    gl_gpu_prim_count++;

    C89GL_glGenVertexArrays(1, &gp->vao);
    C89GL_glBindVertexArray(gp->vao);

    C89GL_glGenBuffers(1, &gp->vbo);
    C89GL_glBindBuffer(GL_ARRAY_BUFFER, gp->vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER,
                       (GLsizeiptr)((size_t)prim->indices.count * VERTEX_STRIDE_BYTES),
                       verts_stage, GL_STATIC_DRAW);

    C89GL_glGenBuffers(1, &gp->ibo);
    C89GL_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gp->ibo);
    C89GL_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                       (GLsizeiptr)((size_t)prim->indices.count * sizeof(u32)),
                       inds_stage, GL_STATIC_DRAW);

    C89GL_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)0);
    C89GL_glEnableVertexAttribArray(0);
    C89GL_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(3 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(1);
    C89GL_glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(6 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(2);
    /* Attribute 3 stays DISABLED. Its constant value is set per draw
     * call with glVertexAttrib1f(3, model_index). */
    C89GL_glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(10 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(4);
    C89GL_glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(13 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(5);
    /* Attributes 6/7 carry the four joint indices and their weights. They are
     * enabled for every primitive; an unskinned primitive packs zero weights,
     * so the blend collapses to identity without needing a shader branch. */
    C89GL_glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(16 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(6);
    C89GL_glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, VERTEX_STRIDE_BYTES, (void*)(20 * sizeof(float)));
    C89GL_glEnableVertexAttribArray(7);

    gp->index_count = prim->indices.count;

    C89GL_glBindVertexArray(0);
    free(verts_stage);
    free(inds_stage);
    return gp;
}

/* ================================================================
   Scene submission
   ================================================================ */

static int draw_call_compare(const void* a, const void* b) {
    const draw_call_t *da = (const draw_call_t*)a;
    const draw_call_t *db = (const draw_call_t*)b;
    if (da->is_transparent != db->is_transparent)
        return da->is_transparent - db->is_transparent;
    if (da->is_refractive != db->is_refractive)
        return da->is_refractive - db->is_refractive;
    if (da->mat->render_method < db->mat->render_method) return -1;
    if (da->mat->render_method > db->mat->render_method) return  1;
    if (da->mat < db->mat) return -1;
    if (da->mat > db->mat) return  1;
    return 0;
}

static void bind_fbo(void) {
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
}

/* Per-draw state that is not part of the vertex data: the entity's model
 * matrix index, the joint palette base, and the mesh node transform. Every
 * draw loop calls this instead of setting attribute 3 by hand, so the
 * skinning uniforms cannot drift out of sync between passes. */
static void set_draw_uniforms(const shader_variant_t *v, const draw_call_t *dc) {
    C89GL_glVertexAttrib1f(3, (float)dc->model_index);
    if (v->u_joint_offset != -1)
        C89GL_glUniform1i(v->u_joint_offset, dc->joint_offset);
    if (v->u_node_transform != -1) {
        static const float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        if (dc->joint_offset >= 0)
            C89GL_glUniformMatrix4fv(v->u_node_transform, 1, GL_TRUE, (const float*)&dc->node_transform);
        else
            C89GL_glUniformMatrix4fv(v->u_node_transform, 1, GL_FALSE, identity);
    }
}

static void draw_entity_with_model_index(const struct entity_definition *ent, int model_index, int joint_offset)
{
    model_definition *mod;
    u32 p;

    if (!ent || ent->model.handle < 0) return;
    mod = (model_definition*)tag_get(ent->model.handle, TAG_model);
    if (!mod) return;

    for (p = 0; p < mod->primitives.count; ++p) {
        model_primitive *prim = TAG_BLOCK_GET_ELEMENT(&mod->primitives, p, model_primitive);
        material_definition *mat = NULL;
        gpu_primitive_t *gp;
        draw_call_t *dc;

        if (!prim || prim->vertices.count == 0 || prim->indices.count == 0) continue;

        if (prim->material_index >= 0 && mod->materials.address) {
            tag_reference *refs = (tag_reference*)mod->materials.address;
            i32 mat_handle = refs[prim->material_index].handle;
            if (mat_handle >= 0)
                mat = (material_definition*)tag_get(mat_handle, TAG_material);
        }
        if (!mat) {
            static material_definition fallback = DEFAULT_MATERIAL_WATER;
            mat = &fallback;
        }

        gp = gpu_primitive_get_or_upload(mod, p);
        if (!gp) continue;

        if (gl_draw_call_count >= MAX_DRAW_CALLS) {
            render_finish();
            return;
        }
        dc = &gl_draw_calls[gl_draw_call_count++];
        dc->mat = mat;
        dc->prim = gp;
        dc->model_index = model_index;
        /* Only a skinned primitive both needs a palette and carries a node
         * transform that was not baked into its vertices. */
        dc->joint_offset = prim->is_skinned ? joint_offset : -1;
        if (prim->is_skinned)
            dc->node_transform = prim->node_transform;
        else
            memset(&dc->node_transform, 0, sizeof(dc->node_transform));
        if (mat->render_method & EFFECT_ALPHA) {
            dc->is_transparent = 1;
            dc->is_refractive = 0;
        } else if (mat->render_method & EFFECT_TRANSMISSION) {
            dc->is_transparent = 0;
            dc->is_refractive = 1;
        } else {
            dc->is_transparent = 0;
            dc->is_refractive = 0;
        }
    }
}

static void resize_render_targets(void);

INLINE int render_init(i32 window_width, i32 window_height) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (gl_vao) return 1;

    gl_win_width = window_width;
    gl_win_height = window_height;
    gl_render_width  = (i32)(window_width  * gl_resolution_scale);
    gl_render_height = (i32)(window_height * gl_resolution_scale);
    gl_ao_width  = (gl_render_width  + 1) / 2;
    gl_ao_height = (gl_render_height + 1) / 2;
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_draw_call_count = 0;
    gl_gpu_prim_count = 0;
    gl_shader_compilations = 0;
    gl_shader_cache = NULL;
    gl_shader_cache_size = 0;
    gl_shader_cache_count = 0;
    gl_vbao_ubo_dirty      = 1;
    gl_vbao_blur_ubo_dirty = 1;

    gl_sky_zenith        = vec3_init_from_3(0.10f, 0.20f, 0.45f);
    gl_sky_horizon       = vec3_init_from_3(0.55f, 0.65f, 0.80f);
    gl_sky_ground        = vec3_init_from_3(0.12f, 0.11f, 0.10f);
    gl_sky_cloud         = vec3_init_from_3(0.92f, 0.94f, 0.98f);
    gl_sky_exponent      = 2.0f;
    gl_sky_cloud_cover   = 0.55f;
    gl_sky_ambient_scale = 0.5f;

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

    {
        GLint back_encoding = 0;
        GLint front_encoding = 0;
        GLint fb_binding = -1;
        C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb_binding);
        C89GL_glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &back_encoding);
        C89GL_glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_FRONT_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &front_encoding);
        printf("default FBO binding at init: %d\n", fb_binding);
        printf("default FBO BACK_LEFT encoding: %s\n",
               (back_encoding == GL_SRGB) ? "GL_SRGB" : "GL_LINEAR");
        printf("default FBO FRONT_LEFT encoding: %s\n",
               (front_encoding == GL_SRGB) ? "GL_SRGB" : "GL_LINEAR");
    }

    /* Fallback VAO/VBO/IBO used by draw_triangle_shaded and other
     * procedural submission paths. Model geometry uses per-primitive
     * VAOs and does not touch these. */
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

    C89GL_glGenBuffers(1, &gl_joint_ubo);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_joint_ubo);
    C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_JOINT_SLOTS * sizeof(mat4), NULL, GL_STREAM_DRAW);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl_default_fbo = 0;
    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &gl_default_fbo);

    C89GL_glGenFramebuffers(1, &gl_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

    C89GL_glGenTextures(1, &gl_color_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    /* The post-process resolve reads this 1:1 at internal resolution, so
     * filtering is a no-op there; GL_LINEAR keeps a non-integer internal
     * resolution from snapping when the resolve viewport and this differ by
     * a fraction of a texel. */
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_color_tex, 0);

    C89GL_glGenTextures(1, &gl_normal_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_normal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl_normal_tex, 0);

    /* Emissive-only bloom source. Same format and filtering as the colour
     * attachment because the bloom prefilter samples it with the same taps it
     * would use on the scene, and the values it holds are scene-referred HDR
     * radiance. Only fragments shaded with an EFFECT_EMISSIVE material write
     * anything here; everything else writes zero, so the bloom chain sees a
     * frame whose lit surfaces are black and whose emissive surfaces are
     * intact. That is what makes the glow selective rather than global. */
    C89GL_glGenTextures(1, &gl_emissive_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_emissive_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gl_emissive_tex, 0);

    {
        GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        C89GL_glDrawBuffers(3, bufs);
    }

    C89GL_glGenTextures(1, &gl_depth_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);

    C89GL_glGenTextures(1, &gl_refraction_src);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* ---- FXAA intermediate FBO (internal resolution, for the post-process -> AA chain) ----
     * The post-process resolve runs at internal resolution and so does the AA
     * pass, so this is the 1:1 input FXAA filters. */
    C89GL_glGenFramebuffers(1, &gl_post_fxaa_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_post_fxaa_fbo);
    C89GL_glGenTextures(1, &gl_post_fxaa_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_post_fxaa_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_post_fxaa_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("AA FBO incomplete! status=0x%x\n", s);
    }

    /* ---- AA output FBO (internal resolution, source for the resolve blit) ---- */
    C89GL_glGenFramebuffers(1, &gl_aa_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_aa_fbo);
    C89GL_glGenTextures(1, &gl_aa_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_aa_tex);
    /* Display-referred, so 8 bits per channel is enough; the dither in the
     * AA pass cleans up the final quantization. */
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_aa_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("AA output FBO incomplete! status=0x%x\n", s);
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    {
        GLenum fbo_status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
            printf("FBO incomplete! status=0x%x\n", fbo_status);
            return 0;
        }
    }
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    C89GL_glEnable(GL_CULL_FACE);
    C89GL_glFrontFace(GL_CCW);

    gl_fullscreen_vs = compile_shader_with_defines(GL_VERTEX_SHADER,
                                                   "fullscreen.vert",
                                                   "#version 430 core\n");
    if (!gl_fullscreen_vs) {
        printf("ERROR: Failed to compile shared full-screen vertex shader.\n");
        return 0;
    }

    init_cluster_resources();
#ifdef GL_PASS_TIMING
    init_pass_timing();
#endif
    init_env_cube_resources();
    init_audio_resources();
    init_wboit_resources();
    init_vbao_resources();
    init_vbao_blur_resources();
    init_post_process_resources();
    init_bloom_resources();
    init_fxaa_resources();
    init_ssaa_resources();
    init_dither_resources();

    /* render_init above creates its own targets inline, so the bloom mip chain
     * — which lives in resize_render_targets, alongside everything else that
     * depends only on the render resolution — is still unallocated here. Run it
     * once to size the chain to the initial resolution. It is idempotent: it
     * re-specifies storage on the names already created above and creates only
     * the ones that do not exist yet. */
    resize_render_targets();

    /* Off by default. Enabling vsync did not remove the intermittent flicker,
     * and it costs most of the frame rate here (1560 -> 240 FPS on a 240Hz
     * display), so it stays opt-in through render_set_vsync(). */

    printf("render_init returning 1 (success)\n");
    return 1;
}

INLINE void render_shutdown(void) {
    int i;
    if (gl_vertex_vbo) { C89GL_glDeleteBuffers(1, &gl_vertex_vbo); gl_vertex_vbo = 0; }
    if (gl_index_vbo) { C89GL_glDeleteBuffers(1, &gl_index_vbo); gl_index_vbo = 0; }
    if (gl_vao) { C89GL_glDeleteVertexArrays(1, &gl_vao); gl_vao = 0; }
    if (gl_fbo) { C89GL_glDeleteFramebuffers(1, &gl_fbo); gl_fbo = 0; }
    if (gl_color_tex) { C89GL_glDeleteTextures(1, &gl_color_tex); gl_color_tex = 0; }
    if (gl_normal_tex) { C89GL_glDeleteTextures(1, &gl_normal_tex); gl_normal_tex = 0; }
    if (gl_emissive_tex) { C89GL_glDeleteTextures(1, &gl_emissive_tex); gl_emissive_tex = 0; }
    if (gl_depth_tex) { C89GL_glDeleteTextures(1, &gl_depth_tex); gl_depth_tex = 0; }
    if (gl_material_ubo) { C89GL_glDeleteBuffers(1, &gl_material_ubo); gl_material_ubo = 0; }
    if (gl_model_ubo) { C89GL_glDeleteBuffers(1, &gl_model_ubo); gl_model_ubo = 0; }
    if (gl_joint_ubo) { C89GL_glDeleteBuffers(1, &gl_joint_ubo); gl_joint_ubo = 0; }
    if (gl_light_ssbo) { C89GL_glDeleteBuffers(1, &gl_light_ssbo); gl_light_ssbo = 0; }
    if (gl_cluster_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_ssbo); gl_cluster_ssbo = 0; }
    if (gl_cluster_offset_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_offset_ssbo); gl_cluster_offset_ssbo = 0; }
    if (gl_cluster_program) { C89GL_glDeleteProgram(gl_cluster_program); gl_cluster_program = 0; }
#ifdef GL_PASS_TIMING
    shutdown_pass_timing();
#endif
    if (gl_vbao_program) { C89GL_glDeleteProgram(gl_vbao_program); gl_vbao_program = 0; }
    if (gl_vbao_ubo)     { C89GL_glDeleteBuffers(1, &gl_vbao_ubo);  gl_vbao_ubo = 0; }
    if (gl_ao_white_tex)  { C89GL_glDeleteTextures(1, &gl_ao_white_tex); gl_ao_white_tex = 0; }
    if (gl_ao_tex)       { C89GL_glDeleteTextures(1, &gl_ao_tex);   gl_ao_tex = 0; }
    if (gl_ao_fbo)       { C89GL_glDeleteFramebuffers(1, &gl_ao_fbo); gl_ao_fbo = 0; }

    if (gl_vbao_blur_program) { C89GL_glDeleteProgram(gl_vbao_blur_program); gl_vbao_blur_program = 0; }
    if (gl_vbao_blur_ubo)     { C89GL_glDeleteBuffers(1, &gl_vbao_blur_ubo); gl_vbao_blur_ubo = 0; }
    if (gl_ao_blurred_tex)    { C89GL_glDeleteTextures(1, &gl_ao_blurred_tex); gl_ao_blurred_tex = 0; }
    if (gl_ao_blur_fbo)       { C89GL_glDeleteFramebuffers(1, &gl_ao_blur_fbo); gl_ao_blur_fbo = 0; }

    if (gl_oit_composite_program) { C89GL_glDeleteProgram(gl_oit_composite_program); gl_oit_composite_program = 0; }
    if (gl_oit_vao)         { C89GL_glDeleteVertexArrays(1, &gl_oit_vao); gl_oit_vao = 0; }
    if (gl_oit_reveal_tex)  { C89GL_glDeleteTextures(1, &gl_oit_reveal_tex); gl_oit_reveal_tex = 0; }
    if (gl_oit_accum_tex)   { C89GL_glDeleteTextures(1, &gl_oit_accum_tex); gl_oit_accum_tex = 0; }
    if (gl_oit_fbo)         { C89GL_glDeleteFramebuffers(1, &gl_oit_fbo); gl_oit_fbo = 0; }

    if (gl_post_process_program) { C89GL_glDeleteProgram(gl_post_process_program); gl_post_process_program = 0; }

    if (gl_bloom_prefilter_program) { C89GL_glDeleteProgram(gl_bloom_prefilter_program); gl_bloom_prefilter_program = 0; }
    if (gl_bloom_downsample_program) { C89GL_glDeleteProgram(gl_bloom_downsample_program); gl_bloom_downsample_program = 0; }
    if (gl_bloom_upsample_program)   { C89GL_glDeleteProgram(gl_bloom_upsample_program);   gl_bloom_upsample_program = 0; }
    for (i = 0; i < BLOOM_MIP_COUNT; i++) {
        if (gl_bloom_tex[i]) { C89GL_glDeleteTextures(1, &gl_bloom_tex[i]); gl_bloom_tex[i] = 0; }
        if (gl_bloom_fbo[i]) { C89GL_glDeleteFramebuffers(1, &gl_bloom_fbo[i]); gl_bloom_fbo[i] = 0; }
    }

    if (gl_fxaa_program) { C89GL_glDeleteProgram(gl_fxaa_program); gl_fxaa_program = 0; }
    if (gl_ssaa_program) { C89GL_glDeleteProgram(gl_ssaa_program); gl_ssaa_program = 0; }
    if (gl_dither_program) { C89GL_glDeleteProgram(gl_dither_program); gl_dither_program = 0; }
    if (gl_present_fbo) { C89GL_glDeleteFramebuffers(1, &gl_present_fbo); gl_present_fbo = 0; }
    if (gl_present_tex) { C89GL_glDeleteTextures(1, &gl_present_tex); gl_present_tex = 0; }
    if (gl_aa_fbo) { C89GL_glDeleteFramebuffers(1, &gl_aa_fbo); gl_aa_fbo = 0; }
    if (gl_aa_tex) { C89GL_glDeleteTextures(1, &gl_aa_tex); gl_aa_tex = 0; }
    if (gl_post_fxaa_fbo) { C89GL_glDeleteFramebuffers(1, &gl_post_fxaa_fbo); gl_post_fxaa_fbo = 0; }
    if (gl_post_fxaa_tex) { C89GL_glDeleteTextures(1, &gl_post_fxaa_tex); gl_post_fxaa_tex = 0; }

    if (gl_refraction_src) { C89GL_glDeleteTextures(1, &gl_refraction_src); gl_refraction_src = 0; }

    /* Environment cube */
    if (gl_sky_cube)     { C89GL_glDeleteTextures(1, &gl_sky_cube); gl_sky_cube = 0; }
    if (gl_sky_cube_fbo) { C89GL_glDeleteFramebuffers(1, &gl_sky_cube_fbo); gl_sky_cube_fbo = 0; }

    /* Depth cube (no dedicated geometry pool any more) */
    if (gl_depth_cube)     { C89GL_glDeleteTextures(1, &gl_depth_cube); gl_depth_cube = 0; }
    if (gl_depth_cube_fbo) { C89GL_glDeleteFramebuffers(1, &gl_depth_cube_fbo); gl_depth_cube_fbo = 0; }

    if (gl_sky_program)     { C89GL_glDeleteProgram(gl_sky_program); gl_sky_program = 0; }
    if (gl_skybox_program)  { C89GL_glDeleteProgram(gl_skybox_program); gl_skybox_program = 0; }

    if (gl_fullscreen_vs) { C89GL_glDeleteShader(gl_fullscreen_vs); gl_fullscreen_vs = 0; }

    /* Per-primitive GPU cache */
    for (i = 0; i < gl_gpu_prim_count; i++) {
        if (gl_gpu_prims[i].vbo) C89GL_glDeleteBuffers(1, &gl_gpu_prims[i].vbo);
        if (gl_gpu_prims[i].ibo) C89GL_glDeleteBuffers(1, &gl_gpu_prims[i].ibo);
        if (gl_gpu_prims[i].vao) C89GL_glDeleteVertexArrays(1, &gl_gpu_prims[i].vao);
    }
    gl_gpu_prim_count = 0;

    if (gl_vertex_pool) { free(gl_vertex_pool); gl_vertex_pool = NULL; }
    if (gl_index_pool) { free(gl_index_pool); gl_index_pool = NULL; }
    if (gl_shader_cache) {
        for (i = 0; i < gl_shader_cache_size; i++)
            if (gl_shader_cache[i].program)
                C89GL_glDeleteProgram(gl_shader_cache[i].program);
        free(gl_shader_cache);
        gl_shader_cache = NULL;
    }

#ifdef AUDIO_OCCLUSION
    if (gl_audio_occlusion_program) C89GL_glDeleteProgram(gl_audio_occlusion_program);
    if (gl_audio_voice_input_ssbo) C89GL_glDeleteBuffers(1, &gl_audio_voice_input_ssbo);
    if (gl_audio_propagation_ssbo) C89GL_glDeleteBuffers(1, &gl_audio_propagation_ssbo);
    if (gl_audio_propagation_fence) {
        C89GL_glDeleteSync(gl_audio_propagation_fence);
        gl_audio_propagation_fence = NULL;
    }
#endif
#ifdef AUDIO_REVERB
    if (gl_audio_reverb_program) C89GL_glDeleteProgram(gl_audio_reverb_program);
    if (gl_audio_global_stats_ssbo) C89GL_glDeleteBuffers(1, &gl_audio_global_stats_ssbo);
    if (gl_audio_reverb_fence) {
        C89GL_glDeleteSync(gl_audio_reverb_fence);
        gl_audio_reverb_fence = NULL;
    }
#endif
#ifdef AUDIO_PORTAL
    if (gl_audio_portal_program) C89GL_glDeleteProgram(gl_audio_portal_program);
    if (gl_audio_portal_candidates_ssbo) C89GL_glDeleteBuffers(1, &gl_audio_portal_candidates_ssbo);
    if (gl_audio_portal_fence) {
        C89GL_glDeleteSync(gl_audio_portal_fence);
        gl_audio_portal_fence = NULL;
    }
#endif

    /* Particle resources must go before the context that owns their names.
     * render_particle_system_shutdown issues glDeleteProgram/glDeleteBuffers/
     * glDeleteVertexArrays, so running it after C89GL_destroy_context made those
     * calls hit a dead context and leaked all three objects. */
    render_particle_system_shutdown();

    if (g_entity_sort_buf) {
        free(g_entity_sort_buf);
        g_entity_sort_buf = NULL;
        g_entity_sort_cap = 0;
    }

    if (gl_ctx.initialized) C89GL_destroy_context(&gl_ctx);
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_draw_call_count = 0;
}

INLINE void render_draw_entities(struct entity_definition **entities, int count,
                                 const i32 *entity_handles) {
    int i, valid_count;
    entity_sort_t *sorted;
    if (!entities || count <= 0) return;

    gl_model_count = 0;
    gl_joint_slot_count = 0;
    for (i = 0; i < count && gl_model_count < MAX_MODEL_MATRICES; i++) {
        entity_definition *ent = entities[i];
        if (!ent || ent->model.handle < 0) continue;
        gl_model_matrices[gl_model_count] = entity_model_matrix(ent);
        gl_model_count++;
    }
    update_model_ubo();

    /* Grown on demand, not malloc'd per frame. This runs every frame, and via
     * render_draw_entity it runs once per entity per frame, so a 16-byte
     * malloc/free pair was being issued per entity just to hold a sort key. */
    if (g_entity_sort_cap < count) {
        entity_sort_t *grown = (entity_sort_t*)realloc(g_entity_sort_buf,
                                                       (size_t)count * sizeof(entity_sort_t));
        if (!grown) return;
        g_entity_sort_buf = grown;
        g_entity_sort_cap = count;
    }
    sorted = g_entity_sort_buf;

    valid_count = 0;
    for (i = 0; i < count; i++) {
        vec4 c;
        if (!entities[i] || entities[i]->model.handle < 0) continue;
        /* Must match the cap in the loop above. model_index is assigned from
         * valid_count and indexes ModelMatrices on the GPU, so letting it run
         * past MAX_MODEL_MATRICES is an out-of-bounds read in the vertex
         * shader. Both loops apply the same filter, so they stay in step. */
        if (valid_count >= MAX_MODEL_MATRICES) break;
        sorted[valid_count].ent = entities[i];
        sorted[valid_count].model_index = valid_count;
        c = mat4_mul_vec4(gl_view, vec4_init_from_4(
            entities[i]->position.position.x,
            entities[i]->position.position.y,
            entities[i]->position.position.z,
            1.0f));
        sorted[valid_count].depth = -c.position.z;
        sorted[valid_count].joint_offset = -1;
        sorted[valid_count].handle =
            (entity_handles && i < count) ? entity_handles[i] : -1;
        valid_count++;
    }
    qsort(sorted, valid_count, sizeof(entity_sort_t), entity_sort_compare);

    /* Evaluate one pose per animated entity and copy the resulting palettes
     * into the shared joint buffer. The copy matters: the scratch holds a
     * single pose, so every entity's matrices must be lifted out before the
     * next one is evaluated. */
    for (i = 0; i < valid_count; i++) {
        entity_definition *ent = sorted[i].ent;
        const model_definition *mod;
        const animation_definition *adef;
        const animation_clip *clips;
        animation_player *player;
        const animation_clip *clip;
        u32 joint_count, j;
        int base;

        if (ent->animation.handle < 0) continue;
        if (sorted[i].handle < 0) continue;   /* runtime entity: no playback state */
        mod = (const model_definition*)tag_get(ent->model.handle, TAG_model);
        adef = (const animation_definition*)tag_get(ent->animation.handle, TAG_animation);
        if (!mod || !adef || !adef->clips.address || adef->clips.count == 0u) continue;

        player = animation_player_for(sorted[i].handle);
        if (!player || player->entity != sorted[i].handle) continue;
        if (player->clip < 0 || (u32)player->clip >= adef->clips.count) player->clip = 0;

        clips = (const animation_clip*)adef->clips.address;
        clip = &clips[player->clip];
        if (mod->skeleton.count == 0u) continue;

        joint_count = animation_eval_pose(mod, clip, player->time);
        if (joint_count == 0u) continue;

        base = joint_palette_alloc((int)joint_count);
        if (base < 0) continue;   /* palette exhausted this frame */

        for (j = 0; j < joint_count; ++j)
            joint_palette_store(base + j, &g_anim_scratch.palette[j]);

        sorted[i].joint_offset = base;
    }
    update_joint_ubo();

    for (i = 0; i < valid_count; i++) {
        draw_entity_with_model_index(sorted[i].ent, sorted[i].model_index,
                                     sorted[i].joint_offset);
    }
}

INLINE void render_draw_entity(const struct entity_definition *ent) {
    if (!ent) return;
    {
        struct entity_definition *ents[1] = { (struct entity_definition*)ent };
        i32 handles[1] = { -1 };
        render_draw_entities(ents, 1, handles);
    }
}

INLINE void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    gl_light_dir = dir; gl_light_col = col; gl_ambient_col = amb;
}

INLINE void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    gl_cam_eye = eye;
    gl_view = mat4_lookat(eye, center, up);
    /* One source of truth for the clip planes. These same numbers drive the
     * cluster build's logarithmic slice mapping, the linearisation in
     * material.frag, the audio compute dispatch and the VBAO UBO, all of which
     * read gl_near/gl_far. The projection used to carry its own literals, so
     * changing one side silently desynchronised log-depth slicing from the
     * matrix it was supposed to match. */
    gl_near = 0.05f;
    gl_far  = 1000.0f;
    gl_proj = mat4_perspective(fov, aspect, gl_near, gl_far);
    gl_view_proj = mat4_mul(gl_proj, gl_view);
    gl_vbao_ubo_dirty = 1;
}

INLINE void render_set_fog(vec3 color, real start, real end) {
    gl_fog_color = color; gl_fog_start = start; gl_fog_end = end;
}
INLINE void render_set_time(real t) { gl_time = t; }
INLINE void render_clear(u8 r, u8 g, u8 b) { render_clear_color(r/255.0f, g/255.0f, b/255.0f); }
INLINE void render_clear_color(real r, real g, real b) {
    GLfloat color[4];
    GLfloat zero[4];
    if (!gl_fbo) return;
    bind_fbo();
    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    C89GL_glDepthMask(GL_TRUE);

    color[0] = r; color[1] = g; color[2] = b; color[3] = 1.0f;
    zero[0] = zero[1] = zero[2] = zero[3] = 0.0f;
    C89GL_glClearBufferfv(GL_COLOR, GL_COLOR_ATTACHMENT0, color);
    C89GL_glClearBufferfv(GL_COLOR, GL_COLOR_ATTACHMENT1, zero);
    /* Emissive buffer has to be cleared every frame like any other attachment.
     * Nothing writes it for a non-emissive surface, so a frame that skipped
     * this would leave last frame's emitters glowing through whatever
     * replaced them — a trail that follows the object instead of the frame. */
    C89GL_glClearBufferfv(GL_COLOR, GL_COLOR_ATTACHMENT2, zero);
    C89GL_glClear(GL_DEPTH_BUFFER_BIT);
}
INLINE const u32* render_get_fb(void) { return NULL; }

static void resize_render_targets(void) {
    gl_ao_width  = (gl_render_width  + 1) / 2;
    gl_ao_height = (gl_render_height + 1) / 2;
    gl_vbao_ubo_dirty      = 1;
    gl_vbao_blur_ubo_dirty = 1;

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_normal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl_normal_tex, 0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_emissive_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gl_emissive_tex, 0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);
    {
        GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        C89GL_glDrawBuffers(3, bufs);
    }
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);

    /* Rescale AA intermediate texture to internal resolution */
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_post_fxaa_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_post_fxaa_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_post_fxaa_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_aa_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_aa_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_aa_tex, 0);
    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }

    /* The pre-present surface follows the window size, not the internal one */
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_present_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_present_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_win_width, gl_win_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_accum_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                       gl_render_width, gl_render_height, 0,
                       GL_RGBA, GL_FLOAT, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_reveal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_render_width, gl_render_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);
    /* Re-attached even though gl_emissive_tex keeps its name across a resize:
     * the storage above is reallocated, and the WBOIT pass needs the attachment
     * to point at the current level 0 explicitly rather than rely on the driver
     * to have followed the reallocation. */
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                 GL_TEXTURE_2D, gl_emissive_tex, 0);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, gl_depth_tex, 0);

    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);

    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_blurred_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);

    /* Bloom mip chain. Level 0 is half the render resolution and each level
     * halves again, so the sizes are derived from the level index rather than
     * stored. Clamped at 1 so a very small render target cannot drive a level
     * to zero and produce an incomplete framebuffer. */
    {
        int mip, w = (gl_render_width  + 1) / 2;
        int h = (gl_render_height + 1) / 2;
        for (mip = 0; mip < BLOOM_MIP_COUNT; mip++) {
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            gl_bloom_width[mip]  = w;
            gl_bloom_height[mip] = h;

            if (!gl_bloom_tex[mip]) C89GL_glGenTextures(1, &gl_bloom_tex[mip]);
            if (!gl_bloom_fbo[mip]) C89GL_glGenFramebuffers(1, &gl_bloom_fbo[mip]);

            C89GL_glBindTexture(GL_TEXTURE_2D, gl_bloom_tex[mip]);
            /* RGBA16F to match gl_color_tex: the chain is a plain average, so
             * there is no reason to narrow the format, and the prefilter writes
             * the scene's full range. */
            C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
            C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_bloom_fbo[mip]);
            C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, gl_bloom_tex[mip], 0);
            {
                GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
                C89GL_glDrawBuffers(1, bufs);
            }
            {
                GLenum status = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE)
                    printf("Bloom mip %d framebuffer incomplete! status=0x%x\n", mip, status);
            }

            w = (w + 1) / 2;
            h = (h + 1) / 2;
        }
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

    gl_num_tiles_x = (gl_render_width + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_tiles_y = (gl_render_height + CLUSTER_TILE_SIZE - 1) / CLUSTER_TILE_SIZE;
    gl_num_clusters = gl_num_tiles_x * gl_num_tiles_y * CLUSTER_DEPTH_SLICES;

    {
        size_t list_size = gl_num_clusters * CLUSTER_MAX_LIGHTS_PER * sizeof(GLuint);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, list_size, NULL, GL_DYNAMIC_DRAW);
    }
    {
        size_t off_size = gl_num_clusters * sizeof(GLuint);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_cluster_offset_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, off_size, NULL, GL_DYNAMIC_DRAW);
        C89GL_glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                                GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

INLINE int render_resize(i32 new_w, i32 new_h) {
    if (gl_win_width == new_w && gl_win_height == new_h) return 0;
    gl_win_width  = new_w;
    gl_win_height = new_h;

    gl_render_width  = (i32)(new_w * gl_resolution_scale);
    gl_render_height = (i32)(new_h * gl_resolution_scale);
    if (gl_render_width <= 0 || gl_render_height <= 0) return 0;

    resize_render_targets();
    return 0;
}

INLINE void render_set_render_resolution(i32 rw, i32 rh) {
    if (rw <= 0 || rh <= 0) return;
    if (gl_render_width == rw && gl_render_height == rh) return;
    gl_render_width  = rw;
    gl_render_height = rh;
    resize_render_targets();
}

INLINE void render_set_resolution_scale(float scale) {
    if (scale < 0.01f) scale = 0.01f;
    /* Above 1.0 is supersampling: the AA pass is skipped and the resolve
     * downsamples the internal resolution to the window. */
    if (scale > 2.0f)  scale = 2.0f;
    gl_resolution_scale = scale;

    gl_render_width  = (i32)(gl_win_width  * gl_resolution_scale);
    gl_render_height = (i32)(gl_win_height * gl_resolution_scale);
    if (gl_render_width <= 0 || gl_render_height <= 0) return;

    resize_render_targets();
}

INLINE i32 render_get_render_width(void) { return gl_render_width; }
INLINE i32 render_get_render_height(void) { return gl_render_height; }

INLINE void render_set_anti_aliasing(int enabled) {
    gl_anti_aliasing_enabled = enabled ? 1 : 0;
}

INLINE void render_set_vbao(int enabled) {
    gl_vbao_enabled = enabled ? 1 : 0;
}

INLINE void render_set_dither(int enabled) {
    gl_dither_enabled = enabled ? 1 : 0;
}

/* Returns 1 if the driver accepted the request, 0 if vsync could not be
 * enabled (extension absent or rejected), in which case presentation is
 * unchanged. */
INLINE int render_set_vsync(int enabled) {
    int ok;
    gl_vsync_enabled = enabled ? 1 : 0;
    ok = C89GL_set_swap_interval(&gl_ctx, gl_vsync_enabled ? 1 : 0);
    if (enabled && !ok)
        printf("WARNING: vsync requested but the driver refused it; "
               "presenting unsynchronised.\n");
    return ok;
}

INLINE void render_set_sky_cube(int enabled) { gl_sky_cube_enabled = enabled ? 1 : 0; }
INLINE void render_set_skybox(int enabled)  { gl_skybox_enabled  = enabled ? 1 : 0; }

/* The particle system has exactly one draw path: the WBOIT pass below. An
 * earlier straight-alpha path existed alongside it and was never reached, which
 * left the appearance that particles were tied to some other condition. */
static void render_particle_system_draw_wboit(void);

/* ---- Environment cube pass ----
 *
 * Renders sky.frag into all six faces of gl_sky_cube, then mipmaps.
 * This cube is the sole IBL source for material.frag: reflections and
 * diffuse irradiance both come from it. It contains sky only; scene
 * geometry is not drawn into it.
 */
static void render_sky_cube_pass(void) {
    int face;
    GLint prev_fbo = 0;
    GLint prev_vp[4] = {0,0,0,0};

    if (!gl_sky_program) return;
    if (!gl_sky_cube_enabled) return;

    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    C89GL_glGetIntegerv(GL_VIEWPORT, prev_vp);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_sky_cube_fbo);
    C89GL_glViewport(0, 0, PROBE_SIZE, PROBE_SIZE);
    C89GL_glDisable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_FALSE);
    C89GL_glDisable(GL_BLEND);
    C89GL_glDisable(GL_CULL_FACE);

    C89GL_glUseProgram(gl_sky_program);
    if (sky_u_probe_size != -1)
        C89GL_glUniform1f(sky_u_probe_size, (float)PROBE_SIZE);
    if (sky_u_time != -1)
        C89GL_glUniform1f(sky_u_time, (float)gl_time);
    /* NOT UPLOADED: sky_u_sky_zenith / sky_u_sky_horizon / sky_u_ground_color /
     * sky_u_sky_exponent / sky_u_cloud_color / sky_u_cloud_coverage.
     *
     * sky.frag declares only uTime, uInvViewProj, uCamEye, uScreenSize,
     * uFaceIndex and uProbeSize. Every one of these six locations is -1, so
     * these uploads were silently discarded by the driver on every frame of
     * every sky cube and skybox pass. They are left unwritten rather than
     * deleted because render_set_sky()/render_set_clouds() are public API and
     * the C-side defaults differ from the shader's compile-time constants, so
     * wiring them up is a visual change that needs to be a deliberate act.
     * See the comment on render_set_sky(). */

    C89GL_glBindVertexArray(gl_oit_vao);
    for (face = 0; face < PROBE_FACE_COUNT; face++) {
        C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                     gl_sky_cube, 0);
        if (sky_u_face_index != -1)
            C89GL_glUniform1i(sky_u_face_index, face);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    C89GL_glUseProgram(0);

    C89GL_glBindTexture(GL_TEXTURE_CUBE_MAP, gl_sky_cube);
    C89GL_glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    C89GL_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_TRUE);
    C89GL_glEnable(GL_BLEND);
    /* Cull face was disabled above and the block restored everything else; the
     * sky pass is over a fullscreen triangle so culling never mattered here,
     * but the leak did reach render_depth_cube_pass. */
    C89GL_glEnable(GL_CULL_FACE);
}

/* ---- Depth-cube pass ----
 *
 * Renders scene geometry from the camera's POV into gl_depth_cube, one
 * cube face at a time, using the per-primitive GPU cache. Must run after
 * the draw call list has been sorted and before the main pass resets
 * state.
 *
 * Uses a single bare-key DEPTH_ONLY program; no depth-cube material
 * uses a fragment-discarding effect, so per-material variants are
 * unnecessary.
 */
static void render_depth_cube_pass(void) {
    int face, i;
    GLint prev_fbo = 0;
    GLint prev_vp[4] = {0,0,0,0};
    mat4 proj;
    shader_variant_t *depth_only;

    if (gl_draw_call_count == 0) return;

    C89GL_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    C89GL_glGetIntegerv(GL_VIEWPORT, prev_vp);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_depth_cube_fbo);
    C89GL_glViewport(0, 0, PROBE_DEPTH_SIZE, PROBE_DEPTH_SIZE);
    C89GL_glDisable(GL_BLEND);
    C89GL_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, gl_near, gl_far);

    /* One program for the whole pass, resolved once. The key is the bare
     * DEPTH_ONLY variant regardless of material, so looking it up per draw call
     * re-hashed the same key draw_count * PROBE_FACE_COUNT times a frame and
     * carried a 4KB defines buffer on the stack for each one. */
    depth_only = get_program_for_method((render_method)0, 1, ALPHA_PASS_FRONT);
    if (!depth_only) {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
        C89GL_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        return;
    }

    for (face = 0; face < PROBE_FACE_COUNT; face++) {
        mat4 view, vp;
        int current_cull = 1;
        shader_variant_t *cur_variant = NULL;

        C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                     GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                     gl_depth_cube, 0);
        /* Before the clear, not after: glClear obeys the depth writemask, so on
         * the first face the mask is still whatever the previous pass left. */
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glClear(GL_DEPTH_BUFFER_BIT);

        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthFunc(GL_LESS);
        C89GL_glEnable(GL_CULL_FACE);

        view = cube_face_view(gl_cam_eye, face);
        vp = mat4_mul(proj, view);

        for (i = 0; i < gl_draw_call_count; i++) {
            draw_call_t *dc = &gl_draw_calls[i];
            shader_variant_t *v = depth_only;
            int want_cull;

            if (!v) continue;

            if (cur_variant != v) {
                C89GL_glUseProgram(v->program);
                cur_variant = v;
                set_uniforms_for_variant(v, 1, ALPHA_PASS_FRONT);
                if (v->u_view_proj != -1)
                    C89GL_glUniformMatrix4fv(v->u_view_proj, 1, GL_TRUE, (float*)&vp);
                if (v->u_view != -1)
                    C89GL_glUniformMatrix4fv(v->u_view, 1, GL_TRUE, (float*)&view);
            }

            /* No update_material_ubo() here. The DEPTH_ONLY program links
             * MaterialUniforms (the block sits outside the #ifdef in
             * material.frag, so the binding must exist) but its main() is empty
             * and reads none of it. Re-uploading 304 bytes per face, six times
             * a frame, bought nothing. */
            want_cull = dc->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }

            C89GL_glBindVertexArray(dc->prim->vao);
            set_draw_uniforms(v, dc);
            C89GL_glDrawElements(GL_TRIANGLES, (GLsizei)dc->prim->index_count,
                                 GL_UNSIGNED_INT, (void*)0);
        }
        C89GL_glUseProgram(0);
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    C89GL_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_TRUE);
    C89GL_glDepthFunc(GL_LESS);
    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---- Main-pass skybox ---- */
static void render_sky_pass(void) {
    /* Attachment 2 is included so sky.frag's outEmissive is actually written.
     * With only attachment 0 selected the driver discards location 2, and the
     * sky region keeps whatever the frame-start clear left there. */
    GLenum sky_buf[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    GLenum all_bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    mat4 inv_vp;

    if (!gl_skybox_program) return;
    if (!gl_skybox_enabled) return;

    C89GL_glDrawBuffers(3, sky_buf);

    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    C89GL_glDepthMask(GL_FALSE);
    C89GL_glDisable(GL_BLEND);

    inv_vp = mat4_inverse(gl_view_proj);

    C89GL_glUseProgram(gl_skybox_program);
    if (skybox_u_inv_view_proj != -1)
        C89GL_glUniformMatrix4fv(skybox_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_vp);
    if (skybox_u_cam_eye != -1)
        C89GL_glUniform3fv(skybox_u_cam_eye, 1, (float*)&gl_cam_eye);
    if (skybox_u_screen_size != -1)
        C89GL_glUniform2f(skybox_u_screen_size, (float)gl_render_width, (float)gl_render_height);
    if (skybox_u_time != -1)
        C89GL_glUniform1f(skybox_u_time, (float)gl_time);
    /* The six sky colour/exponent/cloud uniforms are not uploaded here for the
     * same reason as in render_sky_cube_pass: sky.frag does not declare them. */

    C89GL_glBindVertexArray(gl_oit_vao);
    C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    C89GL_glUseProgram(0);

    C89GL_glDrawBuffers(3, all_bufs);

    C89GL_glDepthMask(GL_TRUE);
    C89GL_glEnable(GL_BLEND);
}

static void oit_composite_into_current_fbo(void) {
    if (!gl_oit_composite_program) return;

    C89GL_glDisable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_FALSE);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* The composite writes the resolved colour, so it must not inherit a
     * depth-only mask from whichever pass ran before it. */
    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    C89GL_glUseProgram(gl_oit_composite_program);

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_accum_tex);
    if (oit_u_accum_tex != -1) C89GL_glUniform1i(oit_u_accum_tex, 0);

    C89GL_glActiveTexture(GL_TEXTURE1);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_reveal_tex);
    if (oit_u_reveal_tex != -1) C89GL_glUniform1i(oit_u_reveal_tex, 1);

    C89GL_glActiveTexture(GL_TEXTURE0);

    C89GL_glBindVertexArray(gl_oit_vao);
    C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    /* Unbind, matching every other fullscreen pass. Leaving the fallback
     * model VAO bound makes it the target of any later glVertexAttrib* call
     * and of any draw that assumes the default VAO. */
    C89GL_glBindVertexArray(0);

    C89GL_glUseProgram(0);

    C89GL_glBlendFunci(0, GL_ONE, GL_ONE);
    C89GL_glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_TRUE);
}

#ifdef GL_PASS_TIMING

/* One GL_TIME_ELAPSED query per (pass, frame slot). Each frame writes only the
 * passes it actually ran, recorded in a per-slot bitmask, so a pass that is
 * skipped this frame is not read back as a stale result. */
#define PASS_TIMING_SLOTS        4
#define PASS_TIMING_REPORT_EVERY 120

typedef enum {
    PT_AUDIO_COMPUTE = 0,
    PT_SKY_CUBE,
    PT_DEPTH_CUBE,
    PT_OPAQUE_DEPTH,
    PT_CLUSTER,
    PT_SKY,
    PT_OPAQUE_COLOR,
    PT_VBAO,
    PT_REFRACTION_COPY,
    PT_TRANSMISSIVE_COLOR,
    PT_WBOIT_FRONT,
    PT_OIT_COMPOSITE_FRONT,
    PT_BLOOM_PREFILTER,
    PT_BLOOM_DOWN,
    PT_BLOOM_UP,
    PT_POST_PROCESS,
    PT_AA,
    PT_DITHER,
    PT_PASS_COUNT
} gpu_pass_t;

static const char* const gl_pass_names[PT_PASS_COUNT] = {
    "audio_compute", "sky_cube", "depth_cube", "opaque_depth", "cluster",
    "sky", "opaque_color", "vbao",
    "refraction_copy", "transmissive_color", "wboit_front", "oit_composite_front",
    "bloom_prefilter", "bloom_down", "bloom_up", "post_process", "aa", "dither"
};

/* The passes that only exist to service transparent and refractive geometry,
 * plus the refraction copy that feeds them. Summed separately in the report
 * because that is the number the WBOIT path is actually costing. */
static const gpu_pass_t gl_transparency_passes[] = {
    PT_REFRACTION_COPY, PT_TRANSMISSIVE_COLOR,
    PT_WBOIT_FRONT, PT_OIT_COMPOSITE_FRONT
};

static GLuint       gl_pass_queries[PT_PASS_COUNT * PASS_TIMING_SLOTS];
static unsigned int gl_pass_slot_used[PASS_TIMING_SLOTS];
static int          gl_pass_frame = 0;
static int          gl_pass_active = -1;
static double       gl_pass_accum[PT_PASS_COUNT];
static int          gl_pass_accum_frames = 0;
static int          gl_pass_timing_ready = 0;

/* GL_TIME_ELAPSED cannot be active alongside another GL_TIME_ELAPSED query, so
 * a pass that fails to begin must not leave the previous one open. */
static void pass_begin(gpu_pass_t pass) {
    if (!gl_pass_timing_ready || gl_pass_active >= 0) return;
    C89GL_glBeginQuery(GL_TIME_ELAPSED,
                       gl_pass_queries[(int)pass * PASS_TIMING_SLOTS +
                                       (gl_pass_frame % PASS_TIMING_SLOTS)]);
    gl_pass_slot_used[gl_pass_frame % PASS_TIMING_SLOTS] |= 1u << (int)pass;
    gl_pass_active = (int)pass;
}

static void pass_end(void) {
    if (!gl_pass_timing_ready || gl_pass_active < 0) return;
    C89GL_glEndQuery(GL_TIME_ELAPSED);
    gl_pass_active = -1;
}

static void pass_frame_begin(void) {
    if (!gl_pass_timing_ready) return;
    gl_pass_slot_used[gl_pass_frame % PASS_TIMING_SLOTS] = 0u;
}

/* Reads the slot that is about to be recycled, which holds a frame the GPU
 * retired a few frames ago. Polling GL_QUERY_RESULT_AVAIL never blocks; if
 * the GPU has not caught up the frame is dropped rather than waited on. */
static void pass_frame_end(void) {
    int slot;
    unsigned int used;
    int all_ready = 1;
    int p;

    if (!gl_pass_timing_ready) return;

    slot = (gl_pass_frame + 1) % PASS_TIMING_SLOTS;
    if (gl_pass_frame + 1 < PASS_TIMING_SLOTS) { gl_pass_frame++; return; }

    used = gl_pass_slot_used[slot];
    for (p = 0; p < PT_PASS_COUNT; p++) {
        /* glGetQueryObjectuiv returns void; availability comes back through the
         * pointer. Polling it never blocks, so a frame the GPU has not retired
         * yet is dropped rather than waited on. */
        GLuint avail = 0;
        if (!(used & (1u << p))) continue;
        C89GL_glGetQueryObjectuiv(gl_pass_queries[p * PASS_TIMING_SLOTS + slot],
                                  GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) { all_ready = 0; break; }
    }
    gl_pass_frame++;

    if (!all_ready) return;

    for (p = 0; p < PT_PASS_COUNT; p++) {
        GLuint ns = 0;
        if (!(used & (1u << p))) continue;
        C89GL_glGetQueryObjectuiv(gl_pass_queries[p * PASS_TIMING_SLOTS + slot],
                                  GL_QUERY_RESULT, &ns);
        gl_pass_accum[p] += (double)ns * 1e-6;
    }
    gl_pass_accum_frames++;

    if (gl_pass_accum_frames < PASS_TIMING_REPORT_EVERY) return;
    {
        double inv = 1.0 / (double)gl_pass_accum_frames;
        double total = 0.0;
        size_t i;
        printf("---- GPU pass timing (avg of %d frames, ms) ----\n",
               gl_pass_accum_frames);
        for (p = 0; p < PT_PASS_COUNT; p++) {
            double ms = gl_pass_accum[p] * inv;
            total += ms;
            printf("  %-22s %8.3f\n", gl_pass_names[p], ms);
        }
        printf("  %-22s %8.3f\n", "TOTAL", total);

        for (p = 0; p < PT_PASS_COUNT; p++) gl_pass_accum[p] = 0.0;
        gl_pass_accum_frames = 0;
        fflush(stdout);
    }
}

static void init_pass_timing(void) {
    int i;
    for (i = 0; i < PT_PASS_COUNT * PASS_TIMING_SLOTS; i++) gl_pass_queries[i] = 0;
    for (i = 0; i < PASS_TIMING_SLOTS; i++) gl_pass_slot_used[i] = 0u;
    for (i = 0; i < PT_PASS_COUNT; i++) gl_pass_accum[i] = 0.0;
    gl_pass_frame = 0;
    gl_pass_active = -1;
    gl_pass_accum_frames = 0;

    if (!C89GL_glGenQueries || !C89GL_glBeginQuery || !C89GL_glEndQuery ||
        !C89GL_glGetQueryObjectuiv) {
        printf("GPU pass timing unavailable: timer query entry points missing.\n");
        return;
    }
    C89GL_glGenQueries(PT_PASS_COUNT * PASS_TIMING_SLOTS, gl_pass_queries);
    gl_pass_timing_ready = 1;
    printf("GPU pass timing enabled (%d passes).\n", (int)PT_PASS_COUNT);
}

static void shutdown_pass_timing(void) {
    if (gl_pass_timing_ready && C89GL_glDeleteQueries) {
        C89GL_glDeleteQueries(PT_PASS_COUNT * PASS_TIMING_SLOTS, gl_pass_queries);
    }
    gl_pass_timing_ready = 0;
}

#else
/* The enum and the query plumbing vanish with the define; the call sites stay
 * in the pass sequence so enabling timing cannot silently move a boundary. The
 * macro arguments are discarded unevaluated, so the PT_* names need not exist. */
#define pass_begin(p)         ((void)0)
#define pass_end()            ((void)0)
#define pass_frame_begin()    ((void)0)
#define pass_frame_end()      ((void)0)
#define init_pass_timing()    ((void)0)
#define shutdown_pass_timing() ((void)0)
#endif

/* =============================================================================
 * Bloom
 * =============================================================================
 *
 * Builds the mip chain from the finished HDR scene and leaves the result in
 * gl_bloom_tex[0], which post_process.frag reads.
 *
 * The source is gl_emissive_tex, not gl_color_tex. material.frag writes its
 * COLOR_ATTACHMENT2 only for fragments shaded with an EFFECT_EMISSIVE
 * material, so the chain sees a frame in which every non-emissive surface is
 * black. Thresholding the main colour buffer could not express this: a surface
 * that is merely lit is exactly what a brightness threshold selects, so the
 * glow would still cover the whole viewport and there would be no per-material
 * control over what blooms. With the emissive buffer, the EFFECT_EMISSIVE flag
 * is the switch, and the threshold is left to shape how hard a flagged surface
 * glows.
 *
 * Written by the opaque colour pass, the sky pass, and the WBOIT pass, which
 * shares this same texture as COLOR_ATTACHMENT2 of gl_oit_fbo rather than
 * resolving through the composite. EFFECT_EMISSIVE is the opt-in for material
 * geometry; particles write their colour directly. Anything that does not opt in
 * writes black, so the chain sees a frame whose lit surfaces are black and whose
 * emitters are intact.
 *
 * Runs after every pass that writes gl_emissive_tex, so the glow is ordered
 * against the last of the transparency passes and cannot itself be left
 * uncomposited.
 *
 * Every level is written by one pass and read by another, so there is no
 * read-write aliasing even though the upsample stage adds into the same target
 * it read the smaller level from. Defined after the pass-timing block above
 * because the pass_begin/pass_end macros are only in scope from there.
 */
static void run_bloom_chain(void) {
    int mip;

    if (!gl_bloom_enabled || gl_bloom_intensity <= 0.0f) return;
    if (!gl_bloom_prefilter_program || !gl_bloom_downsample_program ||
        !gl_bloom_upsample_program) return;
    if (!gl_bloom_tex[0]) return;

    C89GL_glDisable(GL_DEPTH_TEST);
    C89GL_glDisable(GL_BLEND);
    C89GL_glDepthMask(GL_FALSE);
    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* ---- Prefilter: scene -> level 0, at half resolution ----
     * The source is gl_color_tex while the draw target is gl_bloom_fbo[0], so
     * the draw FBO is bound explicitly rather than left over from the pass
     * above. */
    pass_begin(PT_BLOOM_PREFILTER);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_bloom_fbo[0]);
    C89GL_glViewport(0, 0, gl_bloom_width[0], gl_bloom_height[0]);
    C89GL_glUseProgram(gl_bloom_prefilter_program);
    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_emissive_tex);
    if (bl_pf_u_source    != -1) C89GL_glUniform1i(bl_pf_u_source, 0);
    if (bl_pf_u_texel     != -1) C89GL_glUniform2f(bl_pf_u_texel,
                                                    1.0f / (float)gl_render_width,
                                                    1.0f / (float)gl_render_height);
    if (bl_pf_u_dsts      != -1) C89GL_glUniform2f(bl_pf_u_dsts,
                                                    (float)gl_bloom_width[0],
                                                    (float)gl_bloom_height[0]);
    if (bl_pf_u_threshold != -1) C89GL_glUniform1f(bl_pf_u_threshold, gl_bloom_threshold);
    if (bl_pf_u_knee      != -1) C89GL_glUniform1f(bl_pf_u_knee,
                                                    gl_bloom_knee > 1e-4f ? gl_bloom_knee : 1e-4f);
    if (bl_pf_u_clamp     != -1) C89GL_glUniform1f(bl_pf_u_clamp, 8.0f);
    if (bl_pf_u_exposure  != -1) C89GL_glUniform1f(bl_pf_u_exposure, gl_post_exposure);
    C89GL_glBindVertexArray(gl_oit_vao);
    C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    C89GL_glBindVertexArray(0);
    C89GL_glUseProgram(0);
    pass_end();

    /* ---- Downsample: level N -> level N+1 ----
     * One timing entry covers the whole chain. The levels are a fraction of a
     * full-resolution pass between them, and splitting the report per level
     * would add five columns that are never individually acted on. */
    pass_begin(PT_BLOOM_DOWN);
    C89GL_glUseProgram(gl_bloom_downsample_program);
    for (mip = 0; mip < BLOOM_MIP_COUNT - 1; mip++) {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_bloom_fbo[mip + 1]);
        C89GL_glViewport(0, 0, gl_bloom_width[mip + 1], gl_bloom_height[mip + 1]);
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_bloom_tex[mip]);
        if (bl_ds_u_source != -1) C89GL_glUniform1i(bl_ds_u_source, 0);
        if (bl_ds_u_texel  != -1) C89GL_glUniform2f(bl_ds_u_texel,
                                                    1.0f / (float)gl_bloom_width[mip],
                                                    1.0f / (float)gl_bloom_height[mip]);
        if (bl_ds_u_dsts   != -1) C89GL_glUniform2f(bl_ds_u_dsts,
                                                    (float)gl_bloom_width[mip + 1],
                                                    (float)gl_bloom_height[mip + 1]);
        C89GL_glBindVertexArray(gl_oit_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    C89GL_glBindVertexArray(0);
    C89GL_glUseProgram(0);
    pass_end();

    /* ---- Upsample: level N+1 -> level N, additive ----
     * Additive blending is what lets each level contribute one octave of the
     * blur, instead of one level having to resolve the whole kernel. */
    pass_begin(PT_BLOOM_UP);
    C89GL_glEnable(GL_BLEND);
    C89GL_glBlendFunc(GL_ONE, GL_ONE);
    C89GL_glUseProgram(gl_bloom_upsample_program);
    for (mip = BLOOM_MIP_COUNT - 1; mip > 0; mip--) {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_bloom_fbo[mip - 1]);
        C89GL_glViewport(0, 0, gl_bloom_width[mip - 1], gl_bloom_height[mip - 1]);
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_bloom_tex[mip]);
        if (bl_us_u_source != -1) C89GL_glUniform1i(bl_us_u_source, 0);
        if (bl_us_u_texel  != -1) C89GL_glUniform2f(bl_us_u_texel,
                                                    1.0f / (float)gl_bloom_width[mip],
                                                    1.0f / (float)gl_bloom_height[mip]);
        if (bl_us_u_dsts   != -1) C89GL_glUniform2f(bl_us_u_dsts,
                                                    (float)gl_bloom_width[mip - 1],
                                                    (float)gl_bloom_height[mip - 1]);
        if (bl_us_u_radius != -1) C89GL_glUniform1f(bl_us_u_radius, gl_bloom_radius);
        C89GL_glBindVertexArray(gl_oit_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    C89GL_glBindVertexArray(0);
    C89GL_glUseProgram(0);
    C89GL_glBlendFunc(GL_ONE, GL_ZERO);
    C89GL_glDisable(GL_BLEND);
    C89GL_glActiveTexture(GL_TEXTURE0);
    pass_end();
}

INLINE void render_finish(void) {
    GLuint current_program = 0;
    int current_cull = 1;
    /* Shared across the four draw loops below. They run sequentially over the
     * same frame and the material UBO is global state, so it must be re-asserted
     * at the start of each loop but may be carried within one. NULL forces the
     * first draw call in each loop to upload. */
    const struct material_definition *cur_mat = NULL;
    int i;

    upload_lights_to_ssbo();

#ifdef GL_PASS_TIMING
    pass_frame_begin();
#endif

    /* Audio compute: reads the *previous* frame's depth cube. */
    pass_begin(PT_AUDIO_COMPUTE);
    dispatch_audio_compute();
    pass_end();

    /* 1. Environment cube. Sky-only; IBL source for material.frag. */
    pass_begin(PT_SKY_CUBE);
    render_sky_cube_pass();
    pass_end();

    /* 2. Sort draw calls once, now that all entities have been
     *    submitted. From here on the list is frozen for the frame. */
    if (gl_draw_call_count > 0) {
        qsort(gl_draw_calls, gl_draw_call_count, sizeof(draw_call_t), draw_call_compare);
    }

    /* 3. Depth cube. Scene depth from camera POV; audio-only consumer.
     *    Draws from the sorted draw call list, so it must run after the
     *    sort above.
     *
     *    Wrapped in the timing query like every other pass. It used to report a
     *    flat 0.000 forever: the enum entry and name string existed, so the pass
     *    printed a row, but with no query opened the slot bit was never set and
     *    nothing was ever accumulated. */
    pass_begin(PT_DEPTH_CUBE);
    render_depth_cube_pass();
    pass_end();

    if (gl_draw_call_count > 0) {
        int have_transparent = 0;
        int have_transmissive = 0;
        int have_wboit;

        /* Per-frame census of the two geometry classes the tail of the frame
         * branches on. Every OIT stage costs something even with nothing to
         * draw: two full-screen attachment clears, a fullscreen composite, and
         * a full-resolution framebuffer copy. Gating them on an actual count
         * is what keeps an opaque-only scene from paying for the transparency
         * pipeline.
         *
         * Particles count as transparent work. They are accumulated by the same
         * two WBOIT passes and nothing else draws them, so leaving them out of
         * the census means a scene whose only transparency is the particle
         * system skips both passes and renders no particles at all. */
        for (i = 0; i < gl_draw_call_count; i++) {
            draw_call_t *dc = &gl_draw_calls[i];
            if (dc->is_transparent) {
                have_transparent = 1;
            } else if (dc->is_refractive) {
                have_transmissive = 1;
            }
        }
        have_wboit = have_transparent || g_particle_count > 0;

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

        /* ---- Opaque depth prepass ---- */
        pass_begin(PT_OPAQUE_DEPTH);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glDepthFunc(GL_LESS);
        /* The prepass must rasterise exactly the triangle set the colour
         * pass will rasterise. Forcing GL_CULL_FACE on for every opaque draw
         * made it disagree with the colour pass for double-sided materials:
         * the prepass wrote depth for front faces only, while colour drew
         * both. Fragments whose depth lost to a face the colour pass never
         * drew were then rejected, leaving model-shaped holes that fell
         * through to the sky. Mirror the colour pass's cull choice exactly,
         * including the seed, which render_depth_cube_pass can leave stale. */
        C89GL_glEnable(GL_CULL_FACE);
        current_cull = 1;
        current_program = 0;
        for (i = 0; gl_opaque_depth_prepass && i < gl_draw_call_count; i++) {
            draw_call_t *dc = &gl_draw_calls[i];
            shader_variant_t *v;
            int want_cull;
            if (dc->is_transparent || dc->is_refractive) continue;
            v = get_program_for_method((render_method)dc->mat->render_method, 1, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 1, ALPHA_PASS_FRONT);
            }
            want_cull = dc->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glBindVertexArray(dc->prim->vao);
            set_draw_uniforms(v, dc);
            C89GL_glDrawElements(GL_TRIANGLES, (GLsizei)dc->prim->index_count,
                                 GL_UNSIGNED_INT, (void*)0);
        }
        if (current_program) C89GL_glUseProgram(0);
        pass_end();

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        pass_begin(PT_CLUSTER);
        dispatch_cluster_build();
        pass_end();

        C89GL_glDepthFunc(GL_LEQUAL);

        pass_begin(PT_SKY);
        render_sky_pass();
        pass_end();

        /* ---- Main opaque color pass ---- */
        pass_begin(PT_OPAQUE_COLOR);
        {
            GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
            C89GL_glDrawBuffers(3, bufs);
        }
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunc(GL_ONE, GL_ZERO);
        current_program = 0;
        current_cull = 1;
        cur_mat = NULL;
        for (i = 0; i < gl_draw_call_count; i++) {
            draw_call_t *dc = &gl_draw_calls[i];
            shader_variant_t *v;
            int want_cull;
            if (dc->is_transparent || dc->is_refractive) continue;
            v = get_program_for_method((render_method)dc->mat->render_method, 0, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
            }
            /* Draw calls are sorted by render_method then by material pointer
             * (draw_call_compare), so equal materials are adjacent and one
             * upload per material run replaces one per draw call. This is the
             * same guard render_depth_cube_pass already uses. */
            if (dc->mat != cur_mat) {
                update_material_ubo(dc->mat);
                cur_mat = dc->mat;
            }
            want_cull = dc->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glBindVertexArray(dc->prim->vao);
            set_draw_uniforms(v, dc);
            C89GL_glDrawElements(GL_TRIANGLES, (GLsizei)dc->prim->index_count,
                                 GL_UNSIGNED_INT, (void*)0);
        }
        if (current_program) C89GL_glUseProgram(0);
        pass_end();

        C89GL_glDisable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        C89GL_glDepthMask(GL_FALSE);

        if (gl_vbao_enabled) {
            pass_begin(PT_VBAO);
            dispatch_vbao();
            dispatch_vbao_blur();
            pass_end();
        }

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        C89GL_glEnable(GL_DEPTH_TEST);

        {
            GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
            C89GL_glDrawBuffers(1, bufs);
        }

        /* The refraction source exists only to be sampled by the transmissive
         * pass, so the full-framebuffer read and write is only worth paying for
         * when there is transmissive geometry to sample it.
         *
         * Taken after the opaque pass and before the transmissive pass, so it
         * holds the scene the glass is meant to refract. Transparent geometry
         * drawn later composites over the glass rather than being refracted by
         * it, which is the accepted cost of a single WBOIT pass. */
        if (have_transmissive) {
            C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
            C89GL_glActiveTexture(GL_TEXTURE0);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
            pass_begin(PT_REFRACTION_COPY);
            C89GL_glReadBuffer(GL_COLOR_ATTACHMENT0);
            C89GL_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, gl_render_width, gl_render_height);
            pass_end();
            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        }

        /* ---- Transmissive color pass ---- */
        {
            if (have_transmissive) {
                pass_begin(PT_TRANSMISSIVE_COLOR);
                C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
                C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
                C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                C89GL_glDepthMask(GL_TRUE);
                C89GL_glDepthFunc(GL_LEQUAL);
                C89GL_glEnable(GL_DEPTH_TEST);
                C89GL_glEnable(GL_BLEND);
                C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                current_program = 0;
                current_cull = 1;
                cur_mat = NULL;
                for (i = 0; i < gl_draw_call_count; i++) {
                    draw_call_t *dc = &gl_draw_calls[i];
                    shader_variant_t *v;
                    int want_cull;
                    if (!dc->is_refractive) continue;
                    v = get_program_for_method((render_method)dc->mat->render_method, 0, ALPHA_PASS_FRONT);
                    if (!v) continue;
                    if (current_program != v->program) {
                        C89GL_glUseProgram(v->program);
                        current_program = v->program;
                        set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
                    }
                    if (dc->mat != cur_mat) {
                        update_material_ubo(dc->mat);
                        cur_mat = dc->mat;
                    }
                    want_cull = dc->mat->double_sided ? 0 : 1;
                    if (current_cull != want_cull) {
                        if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                        else           C89GL_glDisable(GL_CULL_FACE);
                        current_cull = want_cull;
                    }
                    C89GL_glBindVertexArray(dc->prim->vao);
                    set_draw_uniforms(v, dc);
                    C89GL_glDrawElements(GL_TRIANGLES, (GLsizei)dc->prim->index_count,
                                         GL_UNSIGNED_INT, (void*)0);
                }
                if (current_program) C89GL_glUseProgram(0);
                pass_end();
            }
        }

        /* ---- WBOIT pass ----
         *
         * The single transparency pass, run after the transmissive pass so
         * transparent geometry composites over glass by blending rather than
         * discarding it.
         *
         * The price of collapsing the old BEHIND/FRONT pair into one pass is
         * that glass no longer refracts transparent geometry. The transmissive
         * pass writes depth, so anything behind it is depth-rejected here and
         * simply occluded. That is the intended limitation, not a bug: material
         * the player is meant to see through belongs in this pass, and
         * transmissive is reserved for surfaces where what sits behind them
         * does not need to be resolved.
         *
         * Culling is per material, exactly as in the opaque pass: a closed
         * double-sided volume contributes its front and back face once each.
         * The face mode is never inverted, because scene winding is not
         * consistent enough for GL_FRONT to mean "the far side" everywhere.
         *
         * Gated on have_wboit, not on transparent geometry alone: with no
         * transmissive surface this pass is the only one that runs, and it is
         * also the only thing that draws the particle system. */
        if (have_wboit) {
            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);
            C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
            pass_begin(PT_WBOIT_FRONT);

            {
                GLfloat clear_accum[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
                GLfloat clear_reveal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                C89GL_glClearBufferfv(GL_COLOR, 0, clear_accum);
                C89GL_glClearBufferfv(GL_COLOR, 1, clear_reveal);
                /* Index 2 is the shared bloom source and is intentionally not
                 * cleared. It already carries the sky's sun and moon plus any
                 * emissive material written before this pass; clearing it would
                 * wipe glow the opaque and sky passes already committed. */
            }

            C89GL_glEnable(GL_BLEND);
            C89GL_glBlendFunci(0, GL_ONE, GL_ONE);
            C89GL_glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
            /* Additive so overlapping particles sum their glow instead of the
             * last one winning. Light accumulates; this is the one attachment in
             * the pass that is not a weighted average, and nothing downstream
             * re-weights it, so the shaders must not apply the WBOIT weight to
             * the emissive output. Premultiplied alpha is the expected form. */
            C89GL_glBlendFunci(2, GL_ONE, GL_ONE);
            C89GL_glEnable(GL_DEPTH_TEST);
            C89GL_glDepthMask(GL_FALSE);
            C89GL_glDepthFunc(GL_LESS);
            C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            C89GL_glEnable(GL_CULL_FACE);
            C89GL_glCullFace(GL_BACK);
            current_cull = 1;

            current_program = 0;
            cur_mat = NULL;
            for (i = 0; i < gl_draw_call_count; i++) {
                draw_call_t *dc = &gl_draw_calls[i];
                shader_variant_t *v;
                int want_cull;
                if (!dc->is_transparent) continue;
                v = get_program_for_method((render_method)dc->mat->render_method, 0, ALPHA_PASS_FRONT);
                if (!v) continue;
                if (current_program != v->program) {
                    C89GL_glUseProgram(v->program);
                    current_program = v->program;
                    set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
                }
                if (dc->mat != cur_mat) {
                    update_material_ubo(dc->mat);
                    cur_mat = dc->mat;
                }
                want_cull = dc->mat->double_sided ? 0 : 1;
                if (current_cull != want_cull) {
                    if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                    else           C89GL_glDisable(GL_CULL_FACE);
                    current_cull = want_cull;
                }
                C89GL_glBindVertexArray(dc->prim->vao);
                set_draw_uniforms(v, dc);
                C89GL_glDrawElements(GL_TRIANGLES, (GLsizei)dc->prim->index_count,
                                     GL_UNSIGNED_INT, (void*)0);
            }
            if (current_program) C89GL_glUseProgram(0);

            /* Particles are camera-facing billboards with no back faces. The
             * shader's glass partition assigns each one to exactly one of the
             * two passes, so drawing them in both here is not a double count.
             * The loop above may have left culling off for a double-sided
             * material, so restore the state the billboard quads expect. */
            if (current_cull != 1) {
                C89GL_glEnable(GL_CULL_FACE);
                current_cull = 1;
            }
            render_particle_system_draw_wboit();
            pass_end();

            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
            C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
            pass_begin(PT_OIT_COMPOSITE_FRONT);
            oit_composite_into_current_fbo();
            pass_end();
        }

    } else {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthFunc(GL_LEQUAL);
        C89GL_glDepthMask(GL_FALSE);
        C89GL_glDisable(GL_BLEND);
        render_sky_pass();
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glEnable(GL_BLEND);
    }

    /* ---- Bloom ----
     * After everything that writes gl_color_tex, including the transparent depth
     * work and the sky-only path above, and before the resolve that consumes it. */
    run_bloom_chain();

    if (gl_post_process_program) {
        pass_begin(PT_POST_PROCESS);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_post_fxaa_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

        C89GL_glDisable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glDepthMask(GL_FALSE);

        C89GL_glUseProgram(gl_post_process_program);

        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);

        C89GL_glActiveTexture(GL_TEXTURE1);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_bloom_enabled ? gl_bloom_tex[0] : gl_ao_white_tex);
        if (pp_u_bloom_tex != -1)
            C89GL_glUniform1i(pp_u_bloom_tex, 1);
        if (pp_u_bloom_intensity != -1)
            C89GL_glUniform1f(pp_u_bloom_intensity,
                              gl_bloom_enabled ? gl_bloom_intensity : 0.0f);

        C89GL_glActiveTexture(GL_TEXTURE0);

        if (pp_u_screen_size != -1)
            C89GL_glUniform2f(pp_u_screen_size, (float)gl_render_width, (float)gl_render_height);
        if (pp_u_exposure != -1)
            C89GL_glUniform1f(pp_u_exposure, gl_post_exposure);
        if (pp_u_gamma != -1)
            C89GL_glUniform1f(pp_u_gamma, gl_post_gamma);

        C89GL_glBindVertexArray(gl_oit_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
        C89GL_glBindVertexArray(0);

        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);
        pass_end();

        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        /* No post-process program: carry the raw scene color through at
         * internal resolution. The AA pass still resolves to the window. */
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_post_fxaa_fbo);
        C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                0, 0, gl_render_width, gl_render_height,
                                GL_COLOR_BUFFER_BIT, GL_NEAREST);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_post_fxaa_fbo);
    }

    /* ---- Anti-aliasing, into the pre-present surface ----
     * Every branch ends up in gl_present_tex at window resolution; dither.frag
     * presents from there. Three regimes:
     *   AA off          the post-process output, upscaled
     *   AA, scale <=1.0 FXAA at internal resolution (1:1 with the image it
     *                    filters), then a GL_LINEAR blit, which is the
     *                    upscale
     *   AA, scale > 1.0 supersampling: FXAA is skipped, since the extra
     *                    samples already resolved the edges and FXAA would
     *                    only soften them. ssaa.frag downsamples properly. */
    if (!gl_anti_aliasing_enabled) {
        pass_begin(PT_AA);
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_post_fxaa_fbo);
        C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_present_fbo);
        C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                0, 0, gl_win_width, gl_win_height,
                                GL_COLOR_BUFFER_BIT, GL_LINEAR);
        pass_end();
    } else if (gl_resolution_scale > 1.0f) {
        if (gl_ssaa_program) {
            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_present_fbo);
            C89GL_glViewport(0, 0, gl_win_width, gl_win_height);

            C89GL_glDisable(GL_DEPTH_TEST);
            C89GL_glDisable(GL_BLEND);
            C89GL_glDepthMask(GL_FALSE);

            C89GL_glUseProgram(gl_ssaa_program);

            C89GL_glActiveTexture(GL_TEXTURE0);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_post_fxaa_tex);

            if (ss_u_screen_texture != -1)
                C89GL_glUniform1i(ss_u_screen_texture, 0);
            if (ss_u_src_size != -1)
                C89GL_glUniform2f(ss_u_src_size, (float)gl_render_width, (float)gl_render_height);
            if (ss_u_dst_size != -1)
                C89GL_glUniform2f(ss_u_dst_size, (float)gl_win_width, (float)gl_win_height);

            C89GL_glBindVertexArray(gl_oit_vao);
            C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
            C89GL_glBindVertexArray(0);

            C89GL_glUseProgram(0);
            C89GL_glActiveTexture(GL_TEXTURE0);
        } else {
            /* No resolve program: fall back to a plain blit. */
            C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_post_fxaa_fbo);
            C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_present_fbo);
            C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                    0, 0, gl_win_width, gl_win_height,
                                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
    } else {
        /* ---- Anti-aliasing pass (FXAA) ----
         * Runs at internal resolution, 1:1 with the image it filters: every
         * tap offset in fxaa.frag is a texel step, so the texel grid and the
         * output pixel grid have to match. */
        pass_begin(PT_AA);
        {
            GLuint aa_src_fbo = gl_post_fxaa_fbo;
            /* At 1:1 there is no upscale, so the resolve blit would be a
             * pixel-for-pixel copy. Skip it and let FXAA write straight into
             * the pre-present surface; the viewport is the only thing that
             * differs. */
            i32 aa_direct = (gl_render_width  == gl_win_width &&
                             gl_render_height == gl_win_height);

            if (gl_fxaa_program) {
                C89GL_glBindFramebuffer(GL_FRAMEBUFFER,
                                        aa_direct ? gl_present_fbo : gl_aa_fbo);
                C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

                C89GL_glDisable(GL_DEPTH_TEST);
                C89GL_glDisable(GL_BLEND);
                C89GL_glDepthMask(GL_FALSE);

                C89GL_glUseProgram(gl_fxaa_program);

                C89GL_glActiveTexture(GL_TEXTURE0);
                C89GL_glBindTexture(GL_TEXTURE_2D, gl_post_fxaa_tex);

                if (aa_u_screen_texture != -1)
                    C89GL_glUniform1i(aa_u_screen_texture, 0);
                if (aa_u_resolution != -1)
                    C89GL_glUniform2f(aa_u_resolution, (float)gl_render_width, (float)gl_render_height);

                C89GL_glBindVertexArray(gl_oit_vao);
                C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
                C89GL_glBindVertexArray(0);

                C89GL_glUseProgram(0);
                C89GL_glActiveTexture(GL_TEXTURE0);

                aa_src_fbo = aa_direct ? gl_present_fbo : gl_aa_fbo;
            }

            /* ---- Resolve internal resolution to the window ----
             * GL_LINEAR gives the bilinear upscale; at a resolution scale of
             * 1.0 this is 1:1. */
            if (!aa_direct) {
                C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, aa_src_fbo);
                C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_present_fbo);
                C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                        0, 0, gl_win_width, gl_win_height,
                                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
            }
        }
        pass_end();
    }

    /* ---- Dither / present ----
     * Last pass before the backbuffer, and the reason it is separate: dither
     * only works applied after every filter. */
    if (gl_dither_program && gl_dither_enabled) {
        pass_begin(PT_DITHER);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
        C89GL_glViewport(0, 0, gl_win_width, gl_win_height);

        C89GL_glDisable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glDepthMask(GL_FALSE);

        C89GL_glUseProgram(gl_dither_program);

        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_present_tex);

        if (dt_u_screen_texture != -1)
            C89GL_glUniform1i(dt_u_screen_texture, 0);
        if (dt_u_time != -1)
            C89GL_glUniform1f(dt_u_time, (float)gl_time);

        C89GL_glBindVertexArray(gl_oit_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
        C89GL_glBindVertexArray(0);

        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);
        pass_end();
    } else {
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_present_fbo);
        C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_default_fbo);
        C89GL_glBlitFramebuffer(0, 0, gl_win_width, gl_win_height,
                                0, 0, gl_win_width, gl_win_height,
                                GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    pass_frame_end();
    C89GL_swap_buffers(&gl_ctx);

    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_draw_call_count = 0;
    gl_model_count = 0;
}

/* ========================================================================
   Particle system
   ======================================================================== */

static INLINE float rand_float(float min, float max) {
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}
static INLINE vec3 rand_vec3(float min, float max) {
    vec3 v;
    v.position.x = rand_float(min, max);
    v.position.y = rand_float(min, max);
    v.position.z = rand_float(min, max);
    return v;
}
static INLINE vec3 rand_sphere(float radius) {
    vec3 v;
    do { v = rand_vec3(-1.0f, 1.0f); } while (vec3_magnitude(v) > 1.0f);
    return vec3_mul_scalar(v, radius);
}

static void spawn_particle(void) {
    particle_instance_t *p;
    vec3 *vel;
    float *life;
    float *max_life;
    vec3 dir;

    if (g_particle_count >= g_particle_capacity) return;
    p = &g_particles[g_particle_count];
    vel = &g_particle_velocities[g_particle_count];
    life = &g_particle_lifetimes[g_particle_count];
    max_life = &g_particle_max_lifetimes[g_particle_count];

    p->center = vec3_add(g_emitter_pos, rand_sphere(0.1f));

    dir = rand_vec3(-1.0f, 1.0f);
    dir = vec3_normalize(dir);
    *vel = vec3_mul_scalar(dir, g_emitter_speed * rand_float(0.5f, 1.5f));

    *max_life = rand_float(g_emitter_lifetime * 0.8f, g_emitter_lifetime * 1.2f);
    *life = *max_life;

    p->size = rand_float(g_emitter_size * 0.8f, g_emitter_size * 1.2f);

    p->color = vec4_init_from_4(g_emitter_color.position.x, g_emitter_color.position.y,
                                g_emitter_color.position.z, g_emitter_alpha);

    g_particle_count++;
}

INLINE void render_particle_system_init(int max_particles) {
    const char* defines = "#version 330 core\n#define ALPHA_PASS_FRONT 1\n#define WBOIT_PASS 1\n";
    GLuint vs, fs;

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

    {
        vs = compile_shader_with_defines(GL_VERTEX_SHADER, "particle.vert", defines);
        fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, "particle.frag", defines);
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
        {
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
        }
        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);

        g_particle_u_view_proj  = C89GL_glGetUniformLocation(g_particle_program, "uViewProj");
        g_particle_u_cam_right  = C89GL_glGetUniformLocation(g_particle_program, "uCamRight");
        g_particle_u_cam_up     = C89GL_glGetUniformLocation(g_particle_program, "uCamUp");
        /* No uScreenSize: neither particle.vert nor particle.frag declares it,
         * so the lookup returned -1 and the per-frame glUniform2f was a no-op
         * against a non-existent uniform. The billboard corner maths in the
         * vertex shader is resolution-independent, which is why it never needed
         * one. */
    }

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
}

INLINE void render_particle_system_shutdown(void) {
    if (g_particles) { free(g_particles); g_particles = NULL; }
    if (g_particle_velocities) { free(g_particle_velocities); g_particle_velocities = NULL; }
    if (g_particle_lifetimes) { free(g_particle_lifetimes); g_particle_lifetimes = NULL; }
    if (g_particle_max_lifetimes) { free(g_particle_max_lifetimes); g_particle_max_lifetimes = NULL; }
    if (g_particle_program) C89GL_glDeleteProgram(g_particle_program);
    g_particle_program = 0;
    if (g_particle_vbo) C89GL_glDeleteBuffers(1, &g_particle_vbo);
    g_particle_vbo_capacity = 0;
    if (g_particle_vao) C89GL_glDeleteVertexArrays(1, &g_particle_vao);
    g_particle_count = 0;
    g_particle_capacity = 0;
}

INLINE void render_particle_system_set_emitter(const struct particle_emitter_definition *def) {
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

INLINE void render_particle_system_update(float dt) {
    int i;
    if (!g_particles) return;

    if (g_emitter_loop) {
        float interval = 1.0f / g_emission_rate;
        g_emission_timer += dt;
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

        {
            vec3 grav = vec3_init_from_3(0.0f, g_emitter_gravity, 0.0f);
            float frac;
            g_particle_velocities[i] = vec3_add(g_particle_velocities[i], vec3_mul_scalar(grav, dt));
            g_particles[i].center = vec3_add(g_particles[i].center, vec3_mul_scalar(g_particle_velocities[i], dt));

            frac = 1.0f - (g_particle_lifetimes[i] / g_particle_max_lifetimes[i]);
            g_particles[i].color.color.a = g_emitter_alpha * (1.0f - frac);
        }

        ++i;
    }
}

INLINE void render_particle_system_set_camera(const mat4 *view_proj, vec3 cam_right, vec3 cam_up) {
    g_particle_view_proj = *view_proj;
    g_particle_cam_right = cam_right;
    g_particle_cam_up = cam_up;
    g_particle_cam_valid = 1;
}

static void render_particle_system_draw_wboit(void) {
    float aspect;
    vec3 cam_right_scaled;
    GLint prev_vao = 0;

    if (g_particle_count == 0) return;
    if (!g_particle_program) return;

    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_particle_vbo);
    /* glBufferData re-allocates and copies the whole store every frame, which
     * at a large particle count means a full reallocation plus upload per
     * frame. The buffer is the one thing here that never changes size within a
     * run, so grow it only when the instance count outgrows it and otherwise
     * push just the live prefix. */
    {
        size_t bytes = (size_t)g_particle_count * sizeof(particle_instance_t);
        if (bytes > g_particle_vbo_capacity) {
            g_particle_vbo_capacity = bytes;
            C89GL_glBufferData(GL_ARRAY_BUFFER, g_particle_vbo_capacity,
                               NULL, GL_STREAM_DRAW);
        }
        if (bytes > 0)
            C89GL_glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, g_particles);
    }

    if (!g_particle_cam_valid) {
        g_particle_view_proj = gl_view_proj;
        g_particle_cam_right = vec3_init_from_3(gl_view.transpose[0][0],
                                                gl_view.transpose[0][1],
                                                gl_view.transpose[0][2]);
        g_particle_cam_up    = vec3_init_from_3(gl_view.transpose[1][0],
                                                gl_view.transpose[1][1],
                                                gl_view.transpose[1][2]);
        g_particle_cam_valid = 1;
    }

    C89GL_glUseProgram(g_particle_program);
    C89GL_glUniformMatrix4fv(g_particle_u_view_proj, 1, GL_TRUE,
                             (float*)&g_particle_view_proj);

    aspect = (gl_render_width > 0 && gl_render_height > 0)
           ? ((float)gl_render_width / (float)gl_render_height)
           : 1.0f;
    cam_right_scaled = vec3_mul_scalar(g_particle_cam_right, 1.0f / aspect);
    C89GL_glUniform3fv(g_particle_u_cam_right, 1, (float*)&cam_right_scaled);
    C89GL_glUniform3fv(g_particle_u_cam_up, 1, (float*)&g_particle_cam_up);

    C89GL_glActiveTexture(GL_TEXTURE0);

    C89GL_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);

    C89GL_glBindVertexArray(g_particle_vao);
    C89GL_glDrawArraysInstanced(GL_TRIANGLES, 0, 6, g_particle_count);
    C89GL_glBindVertexArray((GLuint)prev_vao);

    C89GL_glUseProgram(0);
}

INLINE void render_particle_system_emit_burst(int count) {
    int i;
    if (!g_particles) return;
    for (i = 0; i < count; ++i) {
        spawn_particle();
    }
}

#endif /* RASTERIZER_GL_IMPLEMENTATION */
