/*
 * rasterizer_GL.h – GPU-accelerated forward renderer with clustered lighting
 *
 * Requires OpenGL 4.3+ (compute shaders, SSBOs).
 * Uses depth pre-pass + compute light culling + per-pixel shading.
 *
 * Shader files (read from disk):
 *   material.vert    – vertex shader (same for depth and color passes)
 *   material.frag    – fragment shader with #ifdef DEPTH_ONLY and
 *                      #ifdef WBOIT_PASS guards. Writes linear HDR only.
 *   post_process.frag – full-screen HDR->LDR resolve: tone map, color
 *                      grade, sRGB encode, gamma, dither, per-material
 *                      posterize mask.
 *   cluster.comp     – compute shader for light culling
 *   vbao.frag        – fragment shader for visibility-bitmask AO (half-res)
 *   vbao_blur.frag   – fragment shader for edge-aware AO blur (half-res)
 *   fullscreen.vert  – full-screen triangle
 *   particle.vert    – particle vertex shader
 *   particle.frag    – particle fragment shader (has #ifdef WBOIT_PASS)
 *   oit_composite.frag – weighted-blended OIT composite
 *   transmissive_depth.vert – vertex shader for the transmissive depth pass
 *   transmissive_depth.frag – fragment shader for the transmissive depth pass
 *                             (depth test against opaque, discard + write z)
 *   audio_occlusion.comp – compute shader for per-voice occlusion
 *   audio_reverb.comp    – compute shader for room statistics
 *   audio_portal.comp    – compute shader for per-voice portal search
 *
 * Color pipeline:
 *   Everything up to and including the two WBOIT composites and the
 *   transmissive color pass operates on linear HDR radiance. The main
 *   color buffer (gl_color_tex) and the refraction source
 *   (gl_refraction_src) are RGBA16F. Tone mapping, color grading, sRGB
 *   encoding, gamma correction, and the per-material posterize all happen
 *   in the final post-process pass, which reads the composited HDR image
 *   and writes display-referred values to the default framebuffer.
 *
 *   This ordering matters: WBOIT and alpha blending are linear, tone
 *   mapping is nonlinear, and the two do not commute. Tone mapping per
 *   material and then blending produces incorrect composites for bright
 *   or saturated content. Compositing in HDR and tone mapping once at
 *   the end is the only ordering that is physically correct.
 *
 *   The post-process pass runs at the window resolution (gl_win_width ×
 *   gl_win_height), not the internal render resolution. It is the only
 *   place in the pipeline where a display-referred operation occurs.
 *
 * Posterize mask:
 *   EFFECT_POSTERIZE is a per-material flag. The material shader writes a
 *   mask into the alpha channel of the normal output (gl_normal_tex.a):
 *   0.0 = no posterize, (levels / 16.0) = posterize with that many levels.
 *   The post-process pass reads the mask and applies posterize in display
 *   space, which is where the quantization produces even perceptual steps.
 *   Posterize is not supported on WBOIT materials because the WBOIT
 *   variants do not write the normal output; combining EFFECT_POSTERIZE
 *   with EFFECT_ALPHA is an unsupported configuration.
 *
 * Transparency: Weighted Blended Order-Independent Transparency
 * (McGuire & Bavoil 2013). Transparent fragments render into an
 * accumulation buffer and a revealage buffer in any order; a full-screen
 * composite pass resolves them onto the opaque scene. No CPU sorting is
 * required and interpenetrating geometry composites correctly.
 *
 * Ambient occlusion: Visibility Bitmask Ambient Occlusion (VBAO).
 * Reads the opaque depth buffer and a view-space normal buffer written by
 * the opaque color pass, and produces a single-channel AO image applied
 * to the ambient term in material.frag. Only opaque geometry receives AO;
 * WBOIT transparent surfaces are drawn after the VBAO passes and sample
 * the AO image only for the opaque scene behind them.
 *
 * Both VBAO passes are fragment shaders drawn as full-screen triangles,
 * not compute dispatches. The workload is a per-pixel gather with a single
 * color output, which is what the fragment stage is for.
 *
 * VBAO runs at HALF the render resolution. The raw and blurred AO textures
 * are gl_ao_width x gl_ao_height. material.frag samples the blurred one
 * with GL_LINEAR, which upsamples to full resolution for free. The
 * view-position reconstruction uses four precomputed scalars extracted
 * from the projection matrix (uProjA, uProjB, uInvProj00, uInvProj11),
 * and the slice directions are a compile-time table rotated by a single
 * per-pixel hash angle.
 *
 * VBAO UBO upload is gated on a dirty flag: the matrices it carries only
 * change when render_set_camera or render_set_render_resolution is called,
 * so the per-frame cost on an idle camera is zero. The two VBAO UBOs live
 * on binding indices 2 and 3 respectively so they never collide with the
 * material/model UBOs on 0 and 1, and the per-dispatch glBindBufferBase
 * calls are gone.
 *
 * The full-screen triangle vertex shader (fullscreen.vert) is compiled
 * once at startup and attached to all four programs that use it (WBOIT
 * composite, VBAO, VBAO blur, post-process).
 *
 * GL STATE HYGIENE:
 *   Each pass is responsible for enabling the state it depends on and
 *   restoring the state it perturbs. Pass 11 (post-process) in particular
 *   disables depth test and depth write, and MUST restore them before
 *   returning, because Pass 1 (opaque depth pre-pass) does not enable
 *   depth test itself. If Pass 11 leaves depth test disabled, the next
 *   frame's pre-pass writes garbage depth and the opaque color pass
 *   renders without depth testing, producing the wrong draw order.
 *
 * Transparency pipeline (fourteen passes):
 *   1.   Opaque depth pre-pass         -> gl_depth_tex
 *   2.   Cluster build
 *   3.   Opaque color pass            -> gl_color_tex (HDR) + gl_normal_tex
 *   3.5  VBAO (half-res)               -> gl_ao_tex
 *   3.6  VBAO bilateral blur (half-res)-> gl_ao_blurred_tex
 *   4.   Transmissive depth pass       -> gl_transmissive_depth_col
 *                                         + gl_transmissive_depth_tex
 *   5.   ALPHA_PASS_BEHIND (WBOIT)     -> gl_oit_fbo accum + reveal,
 *                                         then composite over gl_color_tex
 *   6.   Copy gl_color_tex             -> gl_refraction_src (HDR)
 *   7.   Transmissive color pass      -> gl_color_tex, writes depth
 *                                         (samples gl_refraction_src)
 *   8.   ALPHA_PASS_FRONT (WBOIT)      -> gl_oit_fbo accum + reveal,
 *                                         then composite over gl_color_tex
 *   9.   Transparent depth-only pass   -> gl_depth_tex (frontmost alpha)
 *   10.  Blit depth to low-res FBO for audio
 *   11.  Post-process                  -> default FBO (tone map, grade,
 *                                         sRGB, gamma, posterize, dither)
 *
 * All materials with EFFECT_ALPHA (window glass, smoke, holograms, oil
 * slicks, …) go through the two WBOIT passes. Materials with
 * EFFECT_TRANSMISSION (water, ice, gemstones, frosted glass, …) go through
 * the dedicated transmissive passes and sample gl_refraction_src for their
 * refracted background.
 *
 * The two alpha passes are the same geometry drawn twice with different
 * shader variants:
 *
 *   ALPHA_PASS_BEHIND — runs before the copy. Discards fragments that are
 *                       not strictly behind a transmissive surface. Its
 *                       composite is captured by gl_refraction_src and
 *                       refracted by the transmissive pass, so smoke behind
 *                       glass is visible through it.
 *
 *   ALPHA_PASS_FRONT  — runs after the transmissive pass. Depth-tested
 *                       against the combined opaque+transmissive depth
 *                       buffer, so the fragments that ALPHA_PASS_BEHIND
 *                       already drew are culled here. This pass draws
 *                       smoke in front of a transmissive surface and smoke
 *                       with no transmissive surface behind it.
 *
 * Refraction is one layer deep: a transmissive surface behind another
 * transmissive surface is not refracted.
 *
 * Sampler binding note:
 *   material.frag declares uRefractionSrc, uTransmissiveDepthTex and
 *   uAOTex with layout(binding = N). Those samplers have no addressable
 *   uniform location (glGetUniformLocation returns -1), so the C side binds
 *   the source textures to their fixed units (2, 3 and 4) unconditionally in
 *   set_uniforms_for_variant. Unit 4 receives the *blurred* AO texture;
 *   the raw gl_ao_tex is only ever read by the blur pass.
 *
 *   post_process.frag declares uColorHDR (binding = 0) and uMaskTex
 *   (binding = 1). Both are fixed-unit; the C side binds them to units 0
 *   and 1 unconditionally in the Pass 11 draw.
 *
 * Usage:
 *   #define RASTERIZER_GL_IMPLEMENTATION
 *   #define AUDIO_OCCLUSION
 *   #define AUDIO_REVERB
 *   #define AUDIO_PORTAL   (optional)
 *   #include "rasterizer_GL.h"
 *   render_init(win_w, win_h);
 *   render_set_render_resolution(512, 288);
 *   ... draw ...
 *   render_finish();
 */

#define AUDIO_OCCLUSION
#define AUDIO_REVERB
#define AUDIO_PORTAL

#ifndef RASTERIZER_GL_H
#define RASTERIZER_GL_H

#include "common.h"
#include "tags/entity.h"
#include "tags/model.h"
#include "tags/material.h"
#include "tags/particle_emitter.h"
#include "tags/light.h"
#include "window.h"

#define C89GL_IMPLEMENTATION
#include "../libs/C89FW/C89GL.h"

/* ---- Alpha pass identity ----
 * The two alpha passes are distinguished by an enum rather than an int so
 * that call sites and the shader variant cache both read symmetrically.
 * The enum value is stored directly into the shader cache key (bit 30).
 */
typedef enum {
    ALPHA_PASS_BEHIND = 0,
    ALPHA_PASS_FRONT  = 1
} alpha_pass_side;

/* ---- Audio propagation struct (matches occlusion shader output) ---- */
typedef struct audio_propagation_output {
    float occlusion;
} audio_propagation_output_t;

/* ---- Global audio stats (reverb) ---- */
typedef struct audio_global_stats {
    real avg_depth;
    real min_depth;
    real max_depth;
    real variance;
} audio_global_stats_t;

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

/* Post-process controls */
void render_set_exposure(real exposure);
void render_set_gamma(real gamma);

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

/* ---- Audio Analysis ---- */
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

/* ---- Public constants ---- */
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

/* ---- Light limits (must match shader) ---- */
#define MAX_EXTRA_DIR_LIGHTS   16
#define MAX_EXTRA_POINT_LIGHTS 64
#define MAX_EXTRA_SPOT_LIGHTS  64
#define MAX_LIGHTS             (MAX_EXTRA_DIR_LIGHTS + MAX_EXTRA_POINT_LIGHTS + MAX_EXTRA_SPOT_LIGHTS)

/* ---- Clustered constants ---- */
#define CLUSTER_TILE_SIZE       16
#define CLUSTER_DEPTH_SLICES    24
#define CLUSTER_MAX_LIGHTS_PER  64

/* ---- Audio analysis constants ---- */
#ifdef AUDIO_OCCLUSION
#define MAX_AUDIO_VOICES_GPU    8
#endif

#ifdef AUDIO_REVERB
#define MAX_REVERB_GROUPS ((64 + 7) / 8 * ((36 + 7) / 8))  /* = 8 * 5 = 40 */
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

/* ---- Helper conversion (u32 -> real) ---- */
static INLINE real u32_to_real(u32 u) {
    union { u32 i; float f; } conv;
    conv.i = u;
    return (real)conv.f;
}

/* ---- GPU light structure (matches shader) ---- */
typedef struct {
    float pos[4];
    float dir[4];
    float color[4];
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

/* Half-resolution AO target size. Recomputed whenever the render resolution
 * changes. Both the raw AO texture and the blurred AO texture use these. */
static i32 gl_ao_width  = 0;
static i32 gl_ao_height = 0;

#define MATERIAL_UBO_BINDING  0
#define MODEL_UBO_BINDING     1

/* VBAO UBO block binding indices. Distinct from material (0) and model (1)
 * so no per-dispatch rebinding is required. */
#define VBAO_UBO_BINDING       2
#define VBAO_BLUR_UBO_BINDING  3

/* ------------------------------------------------------------
   MATERIAL UBO
   ------------------------------------------------------------ */
typedef struct {
    float uMatColor[3];             float _pad0;
    float uMatTint[3];              float uMatAlpha;
    float uMatEmissiveColor[3];     float uMatEmissivePulseAmplitude;
    float uMatEmissivePulseFrequency; float uMatEmissivePulsePhase;
    float uMatTransmissionStrength; float _pad1;
    float uMatSpecularTint[3];      float uMatSpecularRoughness;
    float uMatRimColor[3];          float uMatRimExponent;
    float uMatMetallic;
    float uMatIOR;
    float uMatSubsurfaceStrength;
    float uMatClearcoatIOR;
    float uMatGoochCool[3];         float _pad2;
    float uMatGoochWarm[3];         float uMatAmbientLightFactor;
    float uMatDiffuseRoughness;     float uMatTransmissionRoughness;
    float uMatSaturation;           float uMatIridescenceStrength;
    float uMatBackGlowColor[3];     float uMatBumpWaveAmplitude;
    float uMatBumpWaveFrequency;    float uMatBumpWaveSpeed;
    float uMatBumpNoise;            float uMatFringeIntensity;
    int   uMatCelBands;             float uMatGlitchIntensity;
    int   uMatPosterizeLevels;      float _pad3;
    float uMatStrobeColor[3];       float uMatStrobeFrequency;
    float uMatStrobePhase;          float _pad4[3];
    float uClearcoatColor[3];       float uClearcoatRoughness;
    float uClearcoatStrength;       float _pad5[3];
    float uSheenColor[3];           float uSheenRoughness;
    float uSheenStrength;
    float uMatAnisotropic;
    float _pa6[2];
    float uMatTransmissionTint[3];  float _pad7;
    float uMatF82Tint[3];           float _pad8;
    float uMatSubsurfaceColor[3];   float _pad9;
} material_ubo_t;
STATIC_ASSERT(sizeof(material_ubo_t) == 352, material_ubo_t__size__wrong);

#define MAX_MODEL_MATRICES 1024

/* ---- VBAO UBO (std140 layout; matches vbao.frag's VBAOUniforms) ----
 *
 * Layout (224 bytes total, multiple of 16):
 *   offset   0..63    uInvProj
 *   offset  64..127   uProj
 *   offset 128..191   uView
 *   offset 192..199   uScreenSize   (half-res resolution)
 *   offset 200..203   uNear
 *   offset 204..207   uFar
 *   offset 208..211   uProjA        (proj[2][2])
 *   offset 212..215   uProjB        (-proj[2][3])
 *   offset 216..219   uInvProj00    (1.0 / proj[0][0])
 *   offset 220..223   uInvProj11    (1.0 / proj[1][1])
 */
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

/* ---- VBAO blur UBO (std140 layout; matches vbao_blur.frag's BlurUniforms) ----
 *
 * Layout:
 *   offset 0..7    uScreenSize (vec2, half-res)
 *   offset 8..11   uDepthThreshold (float)
 *   offset 12..15  _pad
 *   total 16 bytes.
 */
typedef struct {
    float screen_size[2];
    float depth_threshold;
    float _pad;
} vbao_blur_ubo_t;
STATIC_ASSERT(sizeof(vbao_blur_ubo_t) == 16, vbao_blur_ubo_t__size__wrong);

/* ---- Shader variant cache ----
 *
 * Cache key layout:
 *   bit 31: depth-only pass
 *   bit 30: ALPHA_PASS_FRONT (the behind pass does not set it)
 *   bits 0..29: the render_method bitmask
 *
 * The behind pass and the front pass therefore produce two distinct cached
 * programs for the same material.
 *
 * Sampler note: uRefractionSrc, uTransmissiveDepthTex and uAOTex in
 * material.frag are declared with layout(binding = N). Those samplers have
 * no addressable uniform location, so the fields below are always -1 in the
 * cached entries. They are retained for the uniform lookup calls in
 * get_program_for_method; the actual binds happen unconditionally in
 * set_uniforms_for_variant.
 */
typedef struct {
    render_method key;
    GLuint program;
    int   is_depth;
    alpha_pass_side alpha_pass;   /* only meaningful for alpha variants */
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
    GLint u_num_lights;
    GLint u_num_tiles_x;
    GLint u_num_tiles_y;
    GLint u_refraction_src;         /* always -1 with layout(binding) */
    GLint u_transmissive_depth_tex; /* always -1 with layout(binding) */
    GLint u_ao_tex;                 /* always -1 with layout(binding) */
    GLint u_alpha_pass;
    GLint u_refraction_scale;
} shader_variant_t;

#define SHADER_CACHE_INITIAL_SIZE 64
#define SHADER_CACHE_MAX_LOAD_FACTOR 0.7f

static shader_variant_t *gl_shader_cache = NULL;
static int gl_shader_cache_size = 0;
static int gl_shader_cache_count = 0;
static int gl_shader_compilations = 0;

/* ---- Shared full-screen vertex shader ----
 *
 * fullscreen.vert is used by four programs: the WBOIT composite, the
 * VBAO pass, the VBAO blur pass, and the post-process pass. Compiling it
 * once and attaching the same handle to all four saves three shader
 * compiles at startup. The handle is deleted in render_shutdown after
 * every program that uses it has been destroyed. */
static GLuint gl_fullscreen_vs = 0;

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

/* ---- Post-process parameters (defaults) ---- */
static float gl_post_exposure = 1.27f;
static float gl_post_gamma    = 1.0f;

/* ---- Global light list ---- */
static light_definition g_lights[MAX_LIGHTS];
static int g_light_count = 0;

/* ---- VAO / VBO (indexed) ---- */
static GLuint gl_vao = 0;
static GLuint gl_vertex_vbo = 0;
static GLuint gl_index_vbo = 0;
static size_t gl_vbo_capacity_bytes = 0;
static size_t gl_ibo_capacity_bytes = 0;

/* ---- Main FBO (HDR intermediate + post-process source) ----
 *
 * gl_color_tex holds the fully composited linear HDR scene at the end of
 * every frame. It is RGBA16F. The post-process pass reads it and writes
 * display-referred values to the default framebuffer.
 *
 * gl_normal_tex holds the view-space geometric normal in .rgb and the
 * per-material posterize mask in .a. It is RGBA8; the alpha channel is
 * not part of the normal data, so it is a free channel for the mask.
 *
 * gl_depth_tex is the shared depth buffer used by gl_fbo, gl_oit_fbo, and
 * the transmissive passes.
 */
static GLuint gl_fbo = 0;
static GLuint gl_color_tex = 0;
static GLuint gl_depth_tex = 0;
static GLuint gl_normal_tex = 0;
static GLint gl_default_fbo = 0;

/* ---- Low-resolution FBO for audio analysis (64x36) ---- */
static GLuint gl_fbo_low = 0;
static GLuint gl_depth_tex_low = 0;
static const int gl_low_width = 64;
static const int gl_low_height = 36;

/* ---- Transmissive / refraction resources ----
 *
 * gl_transmissive_fbo:
 *   color0 = gl_transmissive_depth_col (R32F)
 *       Frontmost transmissive gl_FragCoord.z, or 1.0 where no transmissive
 *       surface is present.
 *   depth  = gl_transmissive_depth_tex (D24)
 *       Transmissive-vs-transmissive self depth test.
 *   (gl_depth_tex is NOT attached here — the transmissive-depth shader
 *    reads it as a sampler for the opaque-occlusion test.)
 *
 * gl_refraction_src:
 *   RGBA16F copy of gl_color_tex made between the two alpha passes, after
 *   the ALPHA_PASS_BEHIND composite has run and before the transmissive
 *   color pass. Sampled by the transmissive color pass to sample the
 *   refracted background. HDR so that the refracted radiance is not
 *   pre-compressed by a tone curve that has not yet run.
 */
static GLuint gl_transmissive_fbo       = 0;
static GLuint gl_transmissive_depth_col = 0;
static GLuint gl_transmissive_depth_tex = 0;
static GLuint gl_refraction_src         = 0;
static GLuint gl_transmissive_depth_program = 0;
static GLint  gl_transmissive_depth_u_view_proj = -1;
static GLint  gl_transmissive_depth_u_opaque_depth = -1;

/* ---- Weighted Blended OIT resources ----
 *
 * gl_oit_fbo shares gl_depth_tex with gl_fbo. It has two color attachments:
 *
 *   GL_COLOR_ATTACHMENT0 -> gl_oit_accum_tex (RGBA16F)
 *     RGB: sum of (color * weight)
 *     A:   sum of (alpha * weight)
 *
 *   GL_COLOR_ATTACHMENT1 -> gl_oit_reveal_tex (R8)
 *     product of (1 - alpha); 1 = background fully visible, 0 = fully blocked
 *
 * The alpha pass renders into this FBO with per-attachment blend state and
 * no depth writes, then oit_composite_into_current_fbo() resolves the result
 * over gl_color_tex. This runs twice per frame (behind, then front) with a
 * fresh clear between.
 */
static GLuint gl_oit_fbo               = 0;
static GLuint gl_oit_accum_tex         = 0;
static GLuint gl_oit_reveal_tex        = 0;
static GLuint gl_oit_vao               = 0;
static GLuint gl_oit_composite_program = 0;
static GLint  oit_u_accum_tex          = -1;
static GLint  oit_u_reveal_tex         = -1;

/* ---- VBAO resources ----
 *
 * Two single-channel color textures and their FBOs, both at half the
 * render resolution. gl_ao_tex holds the raw VBAO output; gl_ao_blurred_tex
 * holds the bilateral-blurred version. material.frag samples
 * gl_ao_blurred_tex (bound to fixed unit 4) with GL_LINEAR, which upsamples
 * to full resolution for free. gl_ao_tex is only read by the blur pass.
 *
 * Both passes are full-screen fragment draws using gl_oit_vao, which is the
 * empty VAO used by the WBOIT composite. The vertex stage is the
 * gl_VertexID-generated triangle from fullscreen.vert.
 *
 * The UBO contents only depend on the projection matrix and the half-res
 * target size, both of which change only on render_set_camera or
 * render_set_render_resolution. A dirty flag gates the per-frame upload and
 * the CPU-side mat4_inverse.
 */
static GLuint gl_ao_tex        = 0;
static GLuint gl_ao_fbo        = 0;
static GLuint gl_ao_blurred_tex = 0;
static GLuint gl_ao_blur_fbo   = 0;
static GLuint gl_vbao_program      = 0;
static GLuint gl_vbao_ubo          = 0;
static GLuint gl_vbao_blur_program = 0;
static GLuint gl_vbao_blur_ubo     = 0;

static vbao_ubo_t      gl_vbao_ubo_cache;
static int             gl_vbao_ubo_dirty      = 1;
static vbao_blur_ubo_t gl_vbao_blur_ubo_cache;
static int             gl_vbao_blur_ubo_dirty = 1;

/* ---- Post-process resources ----
 *
 * gl_post_process_program reads the composited HDR image from gl_color_tex
 * (bound to unit 0 via layout(binding = 0)) and the posterize mask from
 * gl_normal_tex.a (bound to unit 1 via layout(binding = 1)). It writes
 * display-referred values to the default framebuffer.
 *
 * The two samplers are fixed-unit, so they have no addressable uniform
 * location. The C side binds them unconditionally in the Pass 11 draw.
 */
static GLuint gl_post_process_program = 0;
static GLint  pp_u_screen_size = -1;
static GLint  pp_u_exposure    = -1;
static GLint  pp_u_gamma       = -1;
static GLint  pp_u_time        = -1;

/* ---- Batching state ---- */
#define MAX_BATCHES         256
#define MAX_TRANSPARENT_TRIS 8192
#define MAX_VERTICES_PER_FRAME (4 * 1024 * 1024)
#define MAX_INDICES_PER_FRAME  (MAX_VERTICES_PER_FRAME * 3)
#define VERTEX_STRIDE_FLOATS 16
#define VERTEX_STRIDE_BYTES (VERTEX_STRIDE_FLOATS * sizeof(float))

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    const struct material_definition *mat;
    float depth;        /* unused by WBOIT; retained for interface stability */
    float entity_depth; /* unused by WBOIT; retained for interface stability */
    int   id;           /* unused by WBOIT; retained for interface stability */
    int   model_index;
} transparent_tri_t;

typedef struct {
    const struct material_definition *mat;
    size_t vertex_offset;
    size_t index_offset;
    int    vertex_count;
    int    index_count;
    int    is_transparent;
    int    is_refractive;
} batch_t;

static float *gl_vertex_pool = NULL;
static size_t gl_pool_capacity_floats = 0;
static size_t gl_pool_used_floats = 0;

static GLuint *gl_index_pool = NULL;
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

static GLint cluster_u_depth_tex = -1;
static GLint cluster_u_num_lights = -1;
static GLint cluster_u_tile_size = -1;
static GLint cluster_u_num_tiles_x = -1;
static GLint cluster_u_num_tiles_y = -1;
static GLint cluster_u_depth_slices = -1;
static GLint cluster_u_near = -1;
static GLint cluster_u_far = -1;

/* ================================================================
   AUDIO SSBOs & STATE (DOUBLE-BUFFERED)
   ================================================================ */
#ifdef AUDIO_OCCLUSION
static GLuint gl_audio_occlusion_program = 0;
static GLint occ_u_depth_tex = -1;
static GLint occ_u_view_proj = -1;
static GLint occ_u_inv_view_proj = -1;
static GLint occ_u_listener_pos = -1;
static GLint occ_u_num_voices = -1;
static GLuint gl_audio_voice_input_ssbo = 0;
static GLuint gl_audio_propagation_ssbo[2] = {0, 0};
static int g_audio_voice_count_gpu = 0;
static GLsync gl_audio_occlusion_fence = NULL;
#endif

#ifdef AUDIO_REVERB
static GLuint gl_audio_reverb_program = 0;
static GLint rev_u_depth_tex = -1;
static GLint rev_u_inv_view_proj = -1;
static GLint rev_u_listener_pos = -1;
static GLuint gl_audio_global_stats_ssbo[2] = {0, 0};
static GLsync gl_audio_reverb_fence = NULL;
#endif

#ifdef AUDIO_PORTAL
static GLuint gl_audio_portal_program = 0;
static GLint port_u_depth_tex = -1;
static GLint port_u_inv_view_proj = -1;
static GLint port_u_listener_pos = -1;
static GLint port_u_threshold = -1;
static GLint port_u_num_voices = -1;
static GLint port_u_view_proj = -1;
static GLuint gl_audio_portal_candidates_ssbo[2] = {0, 0};
static GLsync gl_audio_portal_fence = NULL;
#endif

static int gl_audio_stats_frame = 0;

/* ---- Particle system ---- */
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

/* Two programs, one per alpha pass side. Both compile with WBOIT_PASS. */
static GLuint g_particle_program[2] = {0, 0};
static GLint  g_particle_u_view_proj[2]           = {-1, -1};
static GLint  g_particle_u_cam_right[2]           = {-1, -1};
static GLint  g_particle_u_cam_up[2]              = {-1, -1};
static GLint  g_particle_u_transmissive_depth[2]  = {-1, -1};
static GLint  g_particle_u_screen_size[2]         = {-1, -1};

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
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c0.position.x, c0.position.y, c0.position.z));
    gl_frustum[0].d = c3.position.w + c0.position.w;
    gl_frustum[1].normal = vec3_sub(
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c0.position.x, c0.position.y, c0.position.z));
    gl_frustum[1].d = c3.position.w - c0.position.w;
    gl_frustum[2].normal = vec3_add(
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c1.position.x, c1.position.y, c1.position.z));
    gl_frustum[2].d = c3.position.w + c1.position.w;
    gl_frustum[3].normal = vec3_sub(
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c1.position.x, c1.position.y, c1.position.z));
    gl_frustum[3].d = c3.position.w - c1.position.w;
    gl_frustum[4].normal = vec3_add(
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c2.position.x, c2.position.y, c2.position.z));
    gl_frustum[4].d = c3.position.w + c2.position.w;
    gl_frustum[5].normal = vec3_sub(
        vec3_init_from_3(c3.position.x, c3.position.y, c3.position.z),
        vec3_init_from_3(c2.position.x, c2.position.y, c2.position.z));
    gl_frustum[5].d = c3.position.w - c2.position.w;

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
        fprintf(stderr, "Shader %s compilation error with defines:\n%s\n", filename, log);
        C89GL_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* ---- Generate defines for material variant ----
 *
 * is_depth         — DEPTH_ONLY variant (no fragment output).
 * alpha_pass_side  — ALPHA_PASS_BEHIND or ALPHA_PASS_FRONT. Emits exactly
 *                    one of the two ALPHA_PASS_* defines.
 *
 * EFFECT_ALPHA implies WBOIT_PASS: every alpha material writes to the
 * (accum, reveal) pair instead of a single color output.
 */
static void generate_defines(render_method key, int is_depth, alpha_pass_side side,
                             char* out, size_t out_size) {
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
    if (key & EFFECT_IRIDESCENCE)     { n = snprintf(p, remaining, "#define EFFECT_IRIDESCENCE\n"); p += n; remaining -= n; }
    if (key & EFFECT_GLITCH)          { n = snprintf(p, remaining, "#define EFFECT_GLITCH\n"); p += n; remaining -= n; }
    if (key & EFFECT_BUMP_NOISE)      { n = snprintf(p, remaining, "#define EFFECT_BUMP_NOISE\n"); p += n; remaining -= n; }
    if (key & EFFECT_FRINGE)          { n = snprintf(p, remaining, "#define EFFECT_FRINGE\n"); p += n; remaining -= n; }
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

/* ---- Shader cache hash --------------------------------------------------
 *
 * The raw cache key is a render_method bitmask — a set of low-valued bits.
 * A naive `key % size` therefore only ever uses the low bits of the key, so
 * distinct variants whose methods differ only in a high bit (e.g.
 * EFFECT_ALPHA vs EFFECT_TRANSMISSION) all land in the same bucket whenever
 * size <= 2^17. This cheap mix spreads them out so probe chains stay short.
 */
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

/* ---- Get shader variant ----
 *
 * Insertion order matters here. The resize (which reallocates the cache
 * array) must happen BEFORE we take a pointer into that array, otherwise
 * `entry` becomes a dangling pointer when the resize fires and the write
 * back through it later is a use-after-free.
 */
static shader_variant_t* get_program_for_method(render_method key,
                                                int is_depth,
                                                alpha_pass_side side) {
    shader_variant_t *entry;
    GLuint vs, fs, prog;
    int index, link_status, blockIndex, modelBlock;
    char defines[4096];
    GLint len;
    char log[512];

    if (!gl_shader_cache) {
        gl_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        gl_shader_cache = (shader_variant_t*)calloc(gl_shader_cache_size, sizeof(shader_variant_t));
        gl_shader_cache_count = 0;
    }

    /* Cache key:
     *   bit 31: depth-only pass
     *   bit 30: ALPHA_PASS_FRONT (behind pass leaves it clear)
     */
    u32 cache_key = key
                  | (is_depth ? (1u << 31) : 0)
                  | ((side == ALPHA_PASS_FRONT) ? (1u << 30) : 0);

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

    /* Grow the cache BEFORE we take an entry pointer. If we did this after
     * populating entry, the pointer would be invalidated by the realloc
     * inside shader_cache_resize and any subsequent write through it would
     * corrupt the new array. */
    if ((float)(gl_shader_cache_count + 1) / gl_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        shader_cache_resize(gl_shader_cache_size * 2);
    }

    /* Recompute the insertion index in the (possibly new) array. A second
     * lookup is cheap and guarantees we don't collide with an entry that
     * just got rehashed into our old slot. */
    index = hash_cache_key(cache_key) % gl_shader_cache_size;
    while (gl_shader_cache[index].program != 0) {
        if (gl_shader_cache[index].key == cache_key) {
            /* Someone else inserted the same key between our miss and now.
             * Discard the program we just compiled and return theirs. */
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
    entry->u_num_lights = C89GL_glGetUniformLocation(prog, "uNumLights");
    entry->u_num_tiles_x = C89GL_glGetUniformLocation(prog, "uNumTilesX");
    entry->u_num_tiles_y = C89GL_glGetUniformLocation(prog, "uNumTilesY");
    entry->u_refraction_src = C89GL_glGetUniformLocation(prog, "uRefractionSrc");
    entry->u_transmissive_depth_tex = C89GL_glGetUniformLocation(prog, "uTransmissiveDepthTex");
    entry->u_ao_tex = C89GL_glGetUniformLocation(prog, "uAOTex");
    entry->u_alpha_pass = C89GL_glGetUniformLocation(prog, "uAlphaPass");
    entry->u_refraction_scale = C89GL_glGetUniformLocation(prog, "uRefractionScale");

    gl_shader_cache_count++;
    gl_shader_compilations++;
    printf("[SHADER CACHE] Compiled new variant #%d for key 0x%x (program %u)\n",
           gl_shader_compilations, (unsigned)cache_key, prog);
    return entry;
}

/* ---- Update material UBO ---- */
static void update_material_ubo(const material_definition *mat) {
    material_ubo_t ubo;
    memset(&ubo, 0, sizeof(ubo));

    ubo.uMatColor[0] = mat->color.color.r;
    ubo.uMatColor[1] = mat->color.color.g;
    ubo.uMatColor[2] = mat->color.color.b;
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
    ubo.uMatTransmissionStrength   = mat->transmission_strength;
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
    ubo.uMatSubsurfaceStrength = mat->subsurface_strength;
    ubo.uMatClearcoatIOR = mat->clearcoat_ior;
    ubo.uMatGoochCool[0] = mat->gooch_cool.color.r;
    ubo.uMatGoochCool[1] = mat->gooch_cool.color.g;
    ubo.uMatGoochCool[2] = mat->gooch_cool.color.b;
    ubo.uMatGoochWarm[0] = mat->gooch_warm.color.r;
    ubo.uMatGoochWarm[1] = mat->gooch_warm.color.g;
    ubo.uMatGoochWarm[2] = mat->gooch_warm.color.b;
    ubo.uMatAmbientLightFactor = mat->ambient_light_factor;
    ubo.uMatDiffuseRoughness     = mat->diffuse_roughness;
    ubo.uMatTransmissionRoughness = mat->transmission_roughness;
    ubo.uMatSaturation         = mat->saturation;
    ubo.uMatIridescenceStrength = mat->iridescence_strength;
    ubo.uMatBackGlowColor[0] = mat->back_glow_color.color.r;
    ubo.uMatBackGlowColor[1] = mat->back_glow_color.color.g;
    ubo.uMatBackGlowColor[2] = mat->back_glow_color.color.b;
    ubo.uMatBumpWaveAmplitude = mat->bump_wave_amplitude;
    ubo.uMatBumpWaveFrequency = mat->bump_wave_frequency;
    ubo.uMatBumpWaveSpeed     = mat->bump_wave_speed;
    ubo.uMatBumpNoise     = mat->bump_noise;
    ubo.uMatFringeIntensity = mat->fringe_intensity;
    ubo.uMatCelBands      = mat->cel_bands;
    ubo.uMatGlitchIntensity = mat->glitch_intensity;
    ubo.uMatPosterizeLevels = mat->posterize_levels;
    ubo.uMatStrobeColor[0] = mat->strobe_color.color.r;
    ubo.uMatStrobeColor[1] = mat->strobe_color.color.g;
    ubo.uMatStrobeColor[2] = mat->strobe_color.color.b;
    ubo.uMatStrobeFrequency = mat->strobe_frequency;
    ubo.uMatStrobePhase     = mat->strobe_phase;
    ubo.uClearcoatColor[0] = mat->clearcoat_color.color.r;
    ubo.uClearcoatColor[1] = mat->clearcoat_color.color.g;
    ubo.uClearcoatColor[2] = mat->clearcoat_color.color.b;
    ubo.uClearcoatRoughness = mat->clearcoat_roughness;
    ubo.uClearcoatStrength = mat->clearcoat_strength;
    ubo.uSheenColor[0] = mat->sheen_color.color.r;
    ubo.uSheenColor[1] = mat->sheen_color.color.g;
    ubo.uSheenColor[2] = mat->sheen_color.color.b;
    ubo.uSheenRoughness = mat->sheen_roughness;
    ubo.uSheenStrength = mat->sheen_strength;
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
}

/* ---- Dispatch cluster build ---- */
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

/* ---- Dispatch VBAO ----
 *
 * Fragment-stage full-screen pass at half resolution. Reads gl_depth_tex
 * (unit 0) and gl_normal_tex (unit 1), writes the raw half-res AO into
 * gl_ao_fbo's color attachment. Must be called after the opaque color
 * pass (Pass 3) so both inputs are populated, and before the transmissive
 * passes so the AO term is available when material.frag reads it.
 *
 * The UBO is only uploaded when the camera or AO resolution has changed,
 * so the per-frame cost on an idle camera is a single FBO bind, a viewport
 * set, two texture binds, and a draw.
 *
 * State (depth test, blend, color/depth mask) is set up by the caller so
 * that the blur dispatch that follows doesn't repeat it. This function
 * leaves the FBO and viewport pointing at gl_ao_fbo.
 */
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
    /* FBO/viewport deliberately left pointing at gl_ao_fbo; the caller
     * (render_finish) restores them once after the blur dispatch. */
}

/* ---- Dispatch VBAO blur ----
 *
 * Reads the raw half-res AO image and the full-res depth, writes the
 * blurred half-res AO into gl_ao_blur_fbo. material.frag samples the
 * blurred texture (unit 4) with GL_LINEAR, which upsamples for free.
 *
 * UBO upload is gated on a dirty flag; only the AO resolution and the
 * hardcoded depth threshold feed it, and the threshold never changes.
 */
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
    /* FBO/viewport deliberately left pointing at gl_ao_blur_fbo. */
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

static void init_transmissive_depth_program(void) {
    GLuint vs = compile_shader_with_defines(GL_VERTEX_SHADER,
                                            "transmissive_depth.vert",
                                            "#version 430 core\n");
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                            "transmissive_depth.frag",
                                            "#version 430 core\n");
    if (!vs || !fs) {
        if (vs) C89GL_glDeleteShader(vs);
        if (fs) C89GL_glDeleteShader(fs);
        return;
    }
    gl_transmissive_depth_program = C89GL_glCreateProgram();
    C89GL_glAttachShader(gl_transmissive_depth_program, vs);
    C89GL_glAttachShader(gl_transmissive_depth_program, fs);
    C89GL_glLinkProgram(gl_transmissive_depth_program);
    GLint st;
    C89GL_glGetProgramiv(gl_transmissive_depth_program, GL_LINK_STATUS, &st);
    if (!st) {
        char log[512];
        C89GL_glGetProgramInfoLog(gl_transmissive_depth_program, sizeof(log), NULL, log);
        printf("Transmissive depth program link error:\n%s\n", log);
        C89GL_glDeleteProgram(gl_transmissive_depth_program);
        gl_transmissive_depth_program = 0;
    } else {
        GLint modelBlk = C89GL_glGetUniformBlockIndex(gl_transmissive_depth_program, "ModelMatrices");
        if (modelBlk != GL_INVALID_INDEX)
            C89GL_glUniformBlockBinding(gl_transmissive_depth_program, modelBlk, MODEL_UBO_BINDING);
        gl_transmissive_depth_u_view_proj = C89GL_glGetUniformLocation(gl_transmissive_depth_program, "uViewProj");
        gl_transmissive_depth_u_opaque_depth = C89GL_glGetUniformLocation(gl_transmissive_depth_program, "uOpaqueDepthTex");
    }
    C89GL_glDeleteShader(vs);
    C89GL_glDeleteShader(fs);
}

/* ---- Weighted Blended OIT init ---- */
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

    /* gl_depth_tex is shared with gl_fbo — legal, as long as both FBOs are
     * not bound at the same time. They never are. */
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, gl_depth_tex, 0);

    {
        GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        C89GL_glDrawBuffers(2, bufs);
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
            GLint st;
            C89GL_glGetProgramiv(gl_oit_composite_program, GL_LINK_STATUS, &st);
            if (!st) {
                char log[512];
                C89GL_glGetProgramInfoLog(gl_oit_composite_program,
                                          sizeof(log), NULL, log);
                printf("OIT composite link error:\n%s\n", log);
                C89GL_glDeleteProgram(gl_oit_composite_program);
                gl_oit_composite_program = 0;
            } else {
                oit_u_accum_tex  = C89GL_glGetUniformLocation(
                                       gl_oit_composite_program, "uAccumTexture");
                oit_u_reveal_tex = C89GL_glGetUniformLocation(
                                       gl_oit_composite_program, "uRevealTexture");
            }
            C89GL_glDeleteShader(fs);
        } else {
            if (fs) C89GL_glDeleteShader(fs);
            printf("ERROR: Failed to compile OIT composite shaders.\n");
        }
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ---- VBAO init ----
 *
 * Creates the raw half-res AO target (r8), its FBO, and compiles the
 * fragment-stage VBAO program from vbao.frag + the shared full-screen
 * vertex shader. The fragment shader writes to gl_ao_tex as a normal
 * color output; the vertex stage provides gl_FragCoord via the
 * gl_VertexID-generated full-screen triangle.
 *
 * The VBAO UBO is bound once to VBAO_UBO_BINDING and its storage is
 * STATIC_DRAW since the dispatch path only writes to it on camera or
 * resolution change.
 */
static void init_vbao_resources(void) {
    /* --- Raw AO target at half resolution --- */
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

    /* --- Program: shared full-screen triangle vertex + vbao.frag --- */
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

    GLuint block = C89GL_glGetUniformBlockIndex(gl_vbao_program, "VBAOUniforms");
    if (block != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(gl_vbao_program, block, VBAO_UBO_BINDING);

    C89GL_glGenBuffers(1, &gl_vbao_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(vbao_ubo_t), NULL, GL_STATIC_DRAW);
    /* Bind the UBO to its block binding index once. The per-frame dispatch
     * only calls glBufferSubData when the camera changes; it never touches
     * glBindBufferBase. */
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, VBAO_UBO_BINDING, gl_vbao_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("VBAO initialised (fragment stage, half-res %dx%d).\n",
           gl_ao_width, gl_ao_height);
}

/* ---- VBAO blur init ----
 *
 * Second fragment pass: reads the raw AO and depth, writes a bilateral-
 * blurred half-res AO image into gl_ao_blur_fbo. material.frag samples
 * this blurred texture (bound to unit 4) with GL_LINEAR.
 */
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

    GLuint block = C89GL_glGetUniformBlockIndex(gl_vbao_blur_program, "BlurUniforms");
    if (block != GL_INVALID_INDEX)
        C89GL_glUniformBlockBinding(gl_vbao_blur_program, block, VBAO_BLUR_UBO_BINDING);

    C89GL_glGenBuffers(1, &gl_vbao_blur_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, gl_vbao_blur_ubo);
    C89GL_glBufferData(GL_UNIFORM_BUFFER, sizeof(vbao_blur_ubo_t), NULL, GL_STATIC_DRAW);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, VBAO_BLUR_UBO_BINDING, gl_vbao_blur_ubo);
    C89GL_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    printf("VBAO blur initialised (fragment stage, half-res %dx%d).\n",
           gl_ao_width, gl_ao_height);
}

/* ---- Post-process init ----
 *
 * Compiles post_process.frag and attaches the shared full-screen vertex
 * shader. Two samplers are used (uColorHDR at binding 0, uMaskTex at
 * binding 1), both fixed-unit, so no uniform lookup is needed for them.
 * The scalar uniforms (screen size, exposure, gamma, time) are looked up
 * normally.
 */
static void init_post_process_resources(void) {
    GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER,
                                            "post_process.frag",
                                            "#version 430 core\n");
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

    pp_u_screen_size = C89GL_glGetUniformLocation(gl_post_process_program, "uScreenSize");
    pp_u_exposure    = C89GL_glGetUniformLocation(gl_post_process_program, "uExposure");
    pp_u_gamma       = C89GL_glGetUniformLocation(gl_post_process_program, "uGamma");
    pp_u_time        = C89GL_glGetUniformLocation(gl_post_process_program, "uTime");

    printf("Post-process initialised (HDR resolve -> sRGB).\n");
}

/* ================================================================
   AUDIO RESOURCES
   ================================================================ */
static void init_audio_resources(void) {
#ifdef AUDIO_OCCLUSION
    {
        GLuint cs_occ = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_occlusion.comp", "#version 430 core\n");
        if (cs_occ) {
            gl_audio_occlusion_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_occlusion_program, cs_occ);
            C89GL_glLinkProgram(gl_audio_occlusion_program);
            GLint status;
            C89GL_glGetProgramiv(gl_audio_occlusion_program, GL_LINK_STATUS, &status);
            if (status) {
                occ_u_depth_tex      = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uDepthTex");
                occ_u_view_proj      = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uViewProj");
                occ_u_inv_view_proj  = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uInvViewProj");
                occ_u_listener_pos   = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uListenerPos");
                occ_u_num_voices     = C89GL_glGetUniformLocation(gl_audio_occlusion_program, "uNumVoices");
            } else {
                char log[512];
                C89GL_glGetProgramInfoLog(gl_audio_occlusion_program, sizeof(log), NULL, log);
                printf("Occlusion program link error: %s\n", log);
                C89GL_glDeleteProgram(gl_audio_occlusion_program);
                gl_audio_occlusion_program = 0;
            }
            C89GL_glDeleteShader(cs_occ);
        }

        C89GL_glGenBuffers(1, &gl_audio_voice_input_ssbo);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_voice_input_ssbo);
        C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(vec3) * MAX_AUDIO_VOICES_GPU, NULL, GL_STREAM_DRAW);

        C89GL_glGenBuffers(2, gl_audio_propagation_ssbo);
        {
            int i;
            for (i = 0; i < 2; i++) {
                C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_propagation_ssbo[i]);
                C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER,
                                   sizeof(audio_propagation_output_t) * MAX_AUDIO_VOICES_GPU,
                                   NULL, GL_STREAM_READ);
            }
        }
    }
#endif

#ifdef AUDIO_REVERB
    {
        GLuint cs_rev = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_reverb.comp", "#version 430 core\n");
        if (cs_rev) {
            gl_audio_reverb_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_reverb_program, cs_rev);
            C89GL_glLinkProgram(gl_audio_reverb_program);
            GLint status;
            C89GL_glGetProgramiv(gl_audio_reverb_program, GL_LINK_STATUS, &status);
            if (status) {
                rev_u_depth_tex         = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uDepthTex");
                rev_u_inv_view_proj     = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uInvViewProj");
                rev_u_listener_pos      = C89GL_glGetUniformLocation(gl_audio_reverb_program, "uListenerPos");
            } else {
                char log[512];
                C89GL_glGetProgramInfoLog(gl_audio_reverb_program, sizeof(log), NULL, log);
                printf("Reverb program link error: %s\n", log);
                C89GL_glDeleteProgram(gl_audio_reverb_program);
                gl_audio_reverb_program = 0;
            }
            C89GL_glDeleteShader(cs_rev);
        }

        C89GL_glGenBuffers(2, gl_audio_global_stats_ssbo);
        {
            int i;
            for (i = 0; i < 2; i++) {
                C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo[i]);
                C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, REVERB_ACCUM_SIZE, NULL, GL_STREAM_READ);
            }
        }
    }
#endif

#ifdef AUDIO_PORTAL
    {
        GLuint cs_port = compile_shader_with_defines(GL_COMPUTE_SHADER, "audio_portal.comp", "#version 430 core\n");
        if (cs_port) {
            gl_audio_portal_program = C89GL_glCreateProgram();
            C89GL_glAttachShader(gl_audio_portal_program, cs_port);
            C89GL_glLinkProgram(gl_audio_portal_program);
            GLint status;
            C89GL_glGetProgramiv(gl_audio_portal_program, GL_LINK_STATUS, &status);
            if (status) {
                port_u_depth_tex        = C89GL_glGetUniformLocation(gl_audio_portal_program, "uDepthTex");
                port_u_inv_view_proj    = C89GL_glGetUniformLocation(gl_audio_portal_program, "uInvViewProj");
                port_u_listener_pos     = C89GL_glGetUniformLocation(gl_audio_portal_program, "uListenerPos");
                port_u_threshold        = C89GL_glGetUniformLocation(gl_audio_portal_program, "uPortalThreshold");
                port_u_num_voices       = C89GL_glGetUniformLocation(gl_audio_portal_program, "uNumVoices");
                port_u_view_proj        = C89GL_glGetUniformLocation(gl_audio_portal_program, "uViewProj");
            } else {
                char log[512];
                C89GL_glGetProgramInfoLog(gl_audio_portal_program, sizeof(log), NULL, log);
                printf("Portal program link error: %s\n", log);
                C89GL_glDeleteProgram(gl_audio_portal_program);
                gl_audio_portal_program = 0;
            }
            C89GL_glDeleteShader(cs_port);
        }

        C89GL_glGenBuffers(2, gl_audio_portal_candidates_ssbo);
        {
            int i;
            for (i = 0; i < 2; i++) {
                C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo[i]);
                C89GL_glBufferData(GL_SHADER_STORAGE_BUFFER, PORTAL_CANDIDATE_SIZE, NULL, GL_STREAM_READ);
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
    int write_idx = gl_audio_stats_frame & 1;

#ifdef AUDIO_REVERB
    if (gl_audio_reverb_program && gl_audio_global_stats_ssbo[0] && gl_audio_global_stats_ssbo[1]) {
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo[write_idx]);
        C89GL_glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, 0, REVERB_ACCUM_SIZE,
                                   GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex_low);
        C89GL_glUseProgram(gl_audio_reverb_program);
        C89GL_glUniform1i(rev_u_depth_tex, 0);
        mat4 inv_view_proj = mat4_inverse(gl_view_proj);
        C89GL_glUniformMatrix4fv(rev_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
        C89GL_glUniform3fv(rev_u_listener_pos, 1, (float*)&gl_cam_eye);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_audio_global_stats_ssbo[write_idx]);
        GLuint groups_x = (gl_low_width + 7) / 8;
        GLuint groups_y = (gl_low_height + 7) / 8;
        C89GL_glDispatchCompute(groups_x, groups_y, 1);
        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);

        if (gl_audio_reverb_fence) {
            C89GL_glDeleteSync(gl_audio_reverb_fence);
        }
        gl_audio_reverb_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
#endif

#ifdef AUDIO_OCCLUSION
    if (gl_audio_occlusion_program && g_audio_voice_count_gpu > 0 &&
        gl_audio_voice_input_ssbo && gl_audio_propagation_ssbo[0] && gl_audio_propagation_ssbo[1]) {
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex_low);
        C89GL_glUseProgram(gl_audio_occlusion_program);
        C89GL_glUniform1i(occ_u_depth_tex, 0);
        C89GL_glUniformMatrix4fv(occ_u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
        mat4 inv_view_proj = mat4_inverse(gl_view_proj);
        C89GL_glUniformMatrix4fv(occ_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
        C89GL_glUniform3fv(occ_u_listener_pos, 1, (float*)&gl_cam_eye);
        C89GL_glUniform1i(occ_u_num_voices, g_audio_voice_count_gpu);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_audio_voice_input_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_audio_propagation_ssbo[write_idx]);
        GLuint groups = (g_audio_voice_count_gpu + 7) / 8;
        C89GL_glDispatchCompute(groups, 1, 1);
        C89GL_glUseProgram(0);
        C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        C89GL_glActiveTexture(GL_TEXTURE0);

        if (gl_audio_occlusion_fence) {
            C89GL_glDeleteSync(gl_audio_occlusion_fence);
        }
        gl_audio_occlusion_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
#endif

    gl_audio_stats_frame++;
}

/* ---- Public: feed audio voice positions ---- */
#ifdef AUDIO_OCCLUSION
INLINE void render_set_audio_voice_data(const vec3 *positions, int count) {
    g_audio_voice_count_gpu = count;
    if (count <= 0 || !gl_audio_voice_input_ssbo) return;
    if (count > MAX_AUDIO_VOICES_GPU) count = MAX_AUDIO_VOICES_GPU;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_voice_input_ssbo);
    C89GL_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(vec3) * count, positions);
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

INLINE int render_poll_audio_propagation(audio_propagation_output_t *out, int max_voices) {
    if (!gl_audio_propagation_ssbo[0] || !gl_audio_propagation_ssbo[1]) return 0;
    if (!gl_audio_occlusion_fence) return 0;
    GLenum status = C89GL_glClientWaitSync(gl_audio_occlusion_fence, 0, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        return 0;
    }

    C89GL_glDeleteSync(gl_audio_occlusion_fence);
    gl_audio_occlusion_fence = NULL;

    int read_idx = (gl_audio_stats_frame & 1) ^ 1;
    int count = 0;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_propagation_ssbo[read_idx]);
    void *ptr = C89GL_glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                       sizeof(audio_propagation_output_t) * MAX_AUDIO_VOICES_GPU,
                                       GL_MAP_READ_BIT);
    if (ptr) {
        count = (g_audio_voice_count_gpu < max_voices) ? g_audio_voice_count_gpu : max_voices;
        memcpy(out, ptr, sizeof(audio_propagation_output_t) * count);
        C89GL_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return count;
}
#endif

#ifdef AUDIO_REVERB
INLINE int render_poll_audio_global_stats(audio_global_stats_t *stats) {
    if (!gl_audio_global_stats_ssbo[0] || !gl_audio_global_stats_ssbo[1]) return 0;
    if (!gl_audio_reverb_fence) return 0;
    GLenum status = C89GL_glClientWaitSync(gl_audio_reverb_fence, 0, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        return 0;
    }

    C89GL_glDeleteSync(gl_audio_reverb_fence);
    gl_audio_reverb_fence = NULL;

    int read_idx = (gl_audio_stats_frame & 1) ^ 1;
    int ok = 0;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_global_stats_ssbo[read_idx]);
    void *ptr = C89GL_glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                       REVERB_ACCUM_SIZE,
                                       GL_MAP_READ_BIT);
    if (ptr) {
        reverb_group_accum_t *groups = (reverb_group_accum_t*)ptr;
        float total_sum = 0.0f, total_sumsq = 0.0f;
        u32 total_count = 0;
        u32 total_min_bits = 0xFFFFFFFF;
        u32 total_max_bits = 0;
        int i;
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
            stats->avg_depth = (real)(total_sum / total_count);
            stats->min_depth = u32_to_real(total_min_bits);
            stats->max_depth = u32_to_real(total_max_bits);
            real mean = stats->avg_depth;
            stats->variance = (real)(total_sumsq / total_count) - mean * mean;
        } else {
            stats->avg_depth = (real)5.0f;
            stats->min_depth = (real)0.5f;
            stats->max_depth = (real)10.0f;
            stats->variance = (real)1.0f;
        }
        ok = 1;
        C89GL_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return ok;
}
#endif

#ifdef AUDIO_PORTAL
INLINE void render_trigger_portal_search(void) {
    if (!gl_audio_portal_program) return;
    if (!gl_audio_portal_candidates_ssbo[0] || !gl_audio_portal_candidates_ssbo[1]) return;
    if (gl_audio_portal_fence) return;

    int write_idx = gl_audio_stats_frame & 1;

    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo[write_idx]);
    portal_candidate_t *reset_ptr = (portal_candidate_t*)C89GL_glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0, PORTAL_CANDIDATE_SIZE,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (reset_ptr) {
        for (int i = 0; i < MAX_AUDIO_VOICES_GPU; i++) {
            reset_ptr[i].dist = 1e10f;
            reset_ptr[i].pos_x = 0.0f;
            reset_ptr[i].pos_y = 0.0f;
            reset_ptr[i].pos_z = 0.0f;
        }
        C89GL_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    C89GL_glActiveTexture(GL_TEXTURE0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex_low);
    C89GL_glUseProgram(gl_audio_portal_program);

    C89GL_glUniform1i(port_u_depth_tex, 0);
    C89GL_glUniform1f(port_u_threshold, 0.5f);

    mat4 inv_view_proj = mat4_inverse(gl_view_proj);
    C89GL_glUniformMatrix4fv(port_u_inv_view_proj, 1, GL_TRUE, (float*)&inv_view_proj);
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

    C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gl_audio_portal_candidates_ssbo[write_idx]);

    GLuint groups = (g_audio_voice_count_gpu + 7) / 8;
    if (groups == 0) groups = 1;
    C89GL_glDispatchCompute(groups, 1, 1);

    C89GL_glUseProgram(0);
    C89GL_glActiveTexture(GL_TEXTURE0);

    gl_audio_portal_fence = C89GL_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

INLINE int render_poll_audio_portal(vec3 *portal_positions, float *portal_distances, int *portal_active_flags, int max_voices) {
    if (!gl_audio_portal_candidates_ssbo[0] || !gl_audio_portal_candidates_ssbo[1]) return 0;
    if (!gl_audio_portal_fence) return 0;

    GLenum status = C89GL_glClientWaitSync(gl_audio_portal_fence, 0, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) return 0;

    C89GL_glDeleteSync(gl_audio_portal_fence);
    gl_audio_portal_fence = NULL;

    int read_idx = (gl_audio_stats_frame & 1) ^ 1;
    int count = 0;
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_audio_portal_candidates_ssbo[read_idx]);
    void *ptr = C89GL_glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, PORTAL_CANDIDATE_SIZE, GL_MAP_READ_BIT);
    if (ptr) {
        portal_candidate_t *cands = (portal_candidate_t*)ptr;
        int num_to_read = (g_audio_voice_count_gpu < max_voices) ? g_audio_voice_count_gpu : max_voices;
        for (int i = 0; i < num_to_read; i++) {
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
        C89GL_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    C89GL_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return count;
}
#endif

/* ---- Public API: Light management ---- */
INLINE void render_clear_lights(void) {
    g_light_count = 0;
}

INLINE void render_set_light_at_index(int index, const light_definition *def) {
    if (index < 0 || index >= MAX_LIGHTS) return;
    g_lights[index] = *def;
    if (index + 1 > g_light_count) g_light_count = index + 1;
}

/* ---- Public: post-process controls ---- */
INLINE void render_set_exposure(real exposure) { gl_post_exposure = (float)exposure; }
INLINE void render_set_gamma(real gamma)       { gl_post_gamma    = (float)gamma; }

/* ---- Set uniforms for a shader variant ----
 *
 * side: ALPHA_PASS_BEHIND or ALPHA_PASS_FRONT. Ignored for opaque and
 * depth-only variants. Written into the uAlphaPass uniform so the shader
 * can branch on the pass identity at runtime if it ever needs to.
 *
 * uRefractionSrc, uTransmissiveDepthTex and uAOTex are declared in
 * material.frag with layout(binding = N). The compiler resolves them to
 * fixed texture units at link time, so glGetUniformLocation returns -1 for
 * them and the usual != -1 guard would silently skip the bind. To avoid
 * that, the source textures are bound to their fixed units (2, 3, 4)
 * unconditionally here. Unit 4 receives the *blurred* half-res AO texture;
 * material.frag samples it with GL_LINEAR, which upsamples to full-res.
 */
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
        if (variant->u_depth_tex != -1) {
            C89GL_glActiveTexture(GL_TEXTURE1);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
            C89GL_glUniform1i(variant->u_depth_tex, 1);
        }
        if (variant->u_screen_size != -1)
            C89GL_glUniform2f(variant->u_screen_size, (float)gl_render_width, (float)gl_render_height);
        if (variant->u_num_lights != -1)
            C89GL_glUniform1i(variant->u_num_lights, g_light_count);
        if (variant->u_num_tiles_x != -1)
            C89GL_glUniform1i(variant->u_num_tiles_x, gl_num_tiles_x);
        if (variant->u_num_tiles_y != -1)
            C89GL_glUniform1i(variant->u_num_tiles_y, gl_num_tiles_y);

        /* Fixed-unit sampler binds. The shader uses
         *   layout(binding = 2) uniform sampler2D uRefractionSrc;
         *   #ifdef ALPHA_PASS_BEHIND
         *   layout(binding = 3) uniform sampler2D uTransmissiveDepthTex;
         *   #endif
         *   layout(binding = 4) uniform sampler2D uAOTex;
         * so glGetUniformLocation for all of them returns -1 and the guards
         * above never fire. Bind unconditionally on every non-depth variant. */
        C89GL_glActiveTexture(GL_TEXTURE2);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);

        C89GL_glActiveTexture(GL_TEXTURE3);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_col);

        C89GL_glActiveTexture(GL_TEXTURE4);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_blurred_tex);

        C89GL_glActiveTexture(GL_TEXTURE0);

        if (variant->u_alpha_pass != -1)
            C89GL_glUniform1i(variant->u_alpha_pass, (int)side);
        if (variant->u_refraction_scale != -1)
            C89GL_glUniform1f(variant->u_refraction_scale, 0.1f);

        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_light_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_cluster_ssbo);
        C89GL_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl_cluster_offset_ssbo);
    }

    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MATERIAL_UBO_BINDING, gl_material_ubo);
    C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MODEL_UBO_BINDING, gl_model_ubo);
}

/* ---- Transparent sort comparator ----
 *
 * WBOIT is order-independent, so the only reason to sort is to group
 * consecutive triangles by material and reduce state changes during the
 * transparent pass. The depth fields on transparent_tri_t are no longer
 * read; they are retained for now to keep the diff small.
 */
static int transparent_compare(const void* a, const void* b) {
    const transparent_tri_t* ta = (const transparent_tri_t*)a;
    const transparent_tri_t* tb = (const transparent_tri_t*)b;
    if (ta->mat < tb->mat) return -1;
    if (ta->mat > tb->mat) return  1;
    return 0;
}

/* ---- Vertex packer macro ---- */
#define PACK_V(v, n, l, lfn, lc, mi) \
    *(ptr++) = (v).position.x; *(ptr++) = (v).position.y; *(ptr++) = (v).position.z; \
    *(ptr++) = (n).position.x; *(ptr++) = (n).position.y; *(ptr++) = (n).position.z; \
    *(ptr++) = (l).position.x; *(ptr++) = (l).position.y; *(ptr++) = (l).position.z; \
    *(ptr++) = (float)(mi); \
    *(ptr++) = (lfn).position.x; *(ptr++) = (lfn).position.y; *(ptr++) = (lfn).position.z; \
    *(ptr++) = (lc).position.x; *(ptr++) = (lc).position.y; *(ptr++) = (lc).position.z;

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
            b->vertex_offset = gl_pool_used_floats / VERTEX_STRIDE_FLOATS;
            b->index_offset = gl_index_pool_used;
            b->vertex_count = 0;
            b->index_count = 0;
            b->is_transparent = 1;
            b->is_refractive = 0;

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
                    GLuint *new_idx = (GLuint*)realloc(gl_index_pool, new_idx_cap * sizeof(GLuint));
                    if (!new_idx) return;
                    gl_index_pool = new_idx;
                    gl_index_pool_capacity = new_idx_cap;
                }
                float *ptr = &gl_vertex_pool[gl_pool_used_floats];
                vec3 localFaceNormal = vec3_normalize(vec3_cross(vec3_sub(t->v1, t->v0), vec3_sub(t->v2, t->v0)));
                vec3 localCentroid = vec3_div_scalar(vec3_add(vec3_add(t->v0, t->v1), t->v2), 3.0f);
                PACK_V(t->v0, t->n0, t->v0, localFaceNormal, localCentroid, t->model_index);
                PACK_V(t->v1, t->n1, t->v1, localFaceNormal, localCentroid, t->model_index);
                PACK_V(t->v2, t->n2, t->v2, localFaceNormal, localCentroid, t->model_index);
                GLuint base = (GLuint)(gl_pool_used_floats / VERTEX_STRIDE_FLOATS);
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

/* ---- draw_triangle_indexed ----
 *
 * Routes a triangle into one of three buckets:
 *   - EFFECT_ALPHA               -> transparent queue (WBOIT passes)
 *   - EFFECT_TRANSMISSION        -> transmissive batch
 *   - everything else            -> opaque batch
 */
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
        /* depth is unused by WBOIT; retained for interface stability. */
        t->depth = entity_depth;
        return;
    }

    /* Opaque or transmissive — grouped by material pointer */
    int batch_idx = -1;
    for (int i = 0; i < gl_batch_count; i++) {
        if (gl_batches[i].mat == mat && !gl_batches[i].is_transparent) {
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
        gl_batches[batch_idx].vertex_offset = gl_pool_used_floats / VERTEX_STRIDE_FLOATS;
        gl_batches[batch_idx].index_offset = gl_index_pool_used;
        gl_batches[batch_idx].vertex_count = 0;
        gl_batches[batch_idx].index_count = 0;
        gl_batches[batch_idx].is_transparent = 0;
        gl_batches[batch_idx].is_refractive = (mat->render_method & EFFECT_TRANSMISSION) ? 1 : 0;
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
        GLuint *new_idx = (GLuint*)realloc(gl_index_pool, new_idx_cap * sizeof(GLuint));
        if (!new_idx) { render_finish(); return; }
        gl_index_pool = new_idx;
        gl_index_pool_capacity = new_idx_cap;
    }

    float *ptr = &gl_vertex_pool[gl_pool_used_floats];
    PACK_V(local_v0, local_n0, local_v0, localFaceNormal, localCentroid, model_index);
    PACK_V(local_v1, local_n1, local_v1, localFaceNormal, localCentroid, model_index);
    PACK_V(local_v2, local_n2, local_v2, localFaceNormal, localCentroid, model_index);

    GLuint base = (GLuint)(gl_pool_used_floats / VERTEX_STRIDE_FLOATS);
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
            static material_definition fallback = DEFAULT_MATERIAL_WATER;
            mat = &fallback;
        }

        model_vertex *verts = (model_vertex*)prim->vertices.address;
        u32 *indices = (u32*)prim->indices.address;
        u32 tri_count = prim->indices.count / 3;
        for (t = 0; t < tri_count; ++t) {
            u32 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
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

/* ---- Legacy CPU-transform path (kept for compatibility) ---- */
static void draw_triangle_internal_legacy(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat,
    float entity_depth) { /* empty */ }

/* ================================================================
   PUBLIC API IMPLEMENTATION
   ================================================================ */

INLINE int render_init(i32 window_width, i32 window_height) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (gl_vao) return 1;

    gl_win_width = window_width;
    gl_win_height = window_height;
    gl_render_width = window_width;
    gl_render_height = window_height;
    gl_ao_width  = (gl_render_width  + 1) / 2;
    gl_ao_height = (gl_render_height + 1) / 2;
    gl_transparent_count = 0;
    gl_batch_count = 0;
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_shader_compilations = 0;
    gl_shader_cache = NULL;
    gl_shader_cache_size = 0;
    gl_shader_cache_count = 0;
    /* Force the first VBAO upload on the first frame. */
    gl_vbao_ubo_dirty      = 1;
    gl_vbao_blur_ubo_dirty = 1;

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

    /* ---- Main render FBO ----
     *
     * gl_color_tex is RGBA16F: it holds the composited linear HDR scene
     * that the post-process pass reads. gl_normal_tex is RGBA8: .rgb is
     * the view-space normal, .a is the per-material posterize mask. */
    C89GL_glGenFramebuffers(1, &gl_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

    C89GL_glGenTextures(1, &gl_color_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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

    {
        GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        C89GL_glDrawBuffers(2, bufs);
    }

    C89GL_glGenTextures(1, &gl_depth_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);

    /* ---- Low-resolution FBO for audio analysis (64x36) ---- */
    C89GL_glGenFramebuffers(1, &gl_fbo_low);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo_low);

    C89GL_glGenTextures(1, &gl_depth_tex_low);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex_low);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_low_width, gl_low_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex_low, 0);

    C89GL_glDrawBuffer(GL_NONE);
    C89GL_glReadBuffer(GL_NONE);

    GLenum fbo_status_low = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbo_status_low != GL_FRAMEBUFFER_COMPLETE) {
        printf("Low-res FBO incomplete! status=0x%x\n", fbo_status_low);
    }

    /* ---- Transmissive depth FBO ---- */
    C89GL_glGenFramebuffers(1, &gl_transmissive_fbo);
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_transmissive_fbo);

    C89GL_glGenTextures(1, &gl_transmissive_depth_col);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_col);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gl_render_width, gl_render_height, 0, GL_RED, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_transmissive_depth_col, 0);

    C89GL_glGenTextures(1, &gl_transmissive_depth_tex);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_transmissive_depth_tex, 0);

    {
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        C89GL_glDrawBuffers(1, bufs);
    }
    {
        GLenum s = C89GL_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (s != GL_FRAMEBUFFER_COMPLETE)
            printf("Transmissive FBO incomplete! status=0x%x\n", s);
    }

    /* ---- Refraction source (HDR) ---- */
    C89GL_glGenTextures(1, &gl_refraction_src);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    C89GL_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);

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

    /* Compile the full-screen triangle vertex shader once; all four
     * full-screen programs (WBOIT composite, VBAO, VBAO blur, post-process)
     * share it. */
    gl_fullscreen_vs = compile_shader_with_defines(GL_VERTEX_SHADER,
                                                   "fullscreen.vert",
                                                   "#version 430 core\n");
    if (!gl_fullscreen_vs) {
        printf("ERROR: Failed to compile shared full-screen vertex shader.\n");
        return 0;
    }

    init_cluster_resources();
    init_audio_resources();
    init_transmissive_depth_program();
    init_wboit_resources();
    init_vbao_resources();
    init_vbao_blur_resources();
    init_post_process_resources();

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
    if (gl_depth_tex) { C89GL_glDeleteTextures(1, &gl_depth_tex); gl_depth_tex = 0; }
    if (gl_fbo_low) { C89GL_glDeleteFramebuffers(1, &gl_fbo_low); gl_fbo_low = 0; }
    if (gl_depth_tex_low) { C89GL_glDeleteTextures(1, &gl_depth_tex_low); gl_depth_tex_low = 0; }
    if (gl_material_ubo) { C89GL_glDeleteBuffers(1, &gl_material_ubo); gl_material_ubo = 0; }
    if (gl_model_ubo) { C89GL_glDeleteBuffers(1, &gl_model_ubo); gl_model_ubo = 0; }
    if (gl_light_ssbo) { C89GL_glDeleteBuffers(1, &gl_light_ssbo); gl_light_ssbo = 0; }
    if (gl_cluster_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_ssbo); gl_cluster_ssbo = 0; }
    if (gl_cluster_offset_ssbo) { C89GL_glDeleteBuffers(1, &gl_cluster_offset_ssbo); gl_cluster_offset_ssbo = 0; }
    if (gl_cluster_program) { C89GL_glDeleteProgram(gl_cluster_program); gl_cluster_program = 0; }

    /* VBAO cleanup */
    if (gl_vbao_program) { C89GL_glDeleteProgram(gl_vbao_program); gl_vbao_program = 0; }
    if (gl_vbao_ubo)     { C89GL_glDeleteBuffers(1, &gl_vbao_ubo);  gl_vbao_ubo = 0; }
    if (gl_ao_tex)       { C89GL_glDeleteTextures(1, &gl_ao_tex);   gl_ao_tex = 0; }
    if (gl_ao_fbo)       { C89GL_glDeleteFramebuffers(1, &gl_ao_fbo); gl_ao_fbo = 0; }

    if (gl_vbao_blur_program) { C89GL_glDeleteProgram(gl_vbao_blur_program); gl_vbao_blur_program = 0; }
    if (gl_vbao_blur_ubo)     { C89GL_glDeleteBuffers(1, &gl_vbao_blur_ubo); gl_vbao_blur_ubo = 0; }
    if (gl_ao_blurred_tex)    { C89GL_glDeleteTextures(1, &gl_ao_blurred_tex); gl_ao_blurred_tex = 0; }
    if (gl_ao_blur_fbo)       { C89GL_glDeleteFramebuffers(1, &gl_ao_blur_fbo); gl_ao_blur_fbo = 0; }

    /* WBOIT cleanup */
    if (gl_oit_composite_program) { C89GL_glDeleteProgram(gl_oit_composite_program); gl_oit_composite_program = 0; }
    if (gl_oit_vao)         { C89GL_glDeleteVertexArrays(1, &gl_oit_vao); gl_oit_vao = 0; }
    if (gl_oit_reveal_tex)  { C89GL_glDeleteTextures(1, &gl_oit_reveal_tex); gl_oit_reveal_tex = 0; }
    if (gl_oit_accum_tex)   { C89GL_glDeleteTextures(1, &gl_oit_accum_tex); gl_oit_accum_tex = 0; }
    if (gl_oit_fbo)         { C89GL_glDeleteFramebuffers(1, &gl_oit_fbo); gl_oit_fbo = 0; }

    /* Post-process cleanup */
    if (gl_post_process_program) { C89GL_glDeleteProgram(gl_post_process_program); gl_post_process_program = 0; }

    /* Transmissive / refraction cleanup */
    if (gl_transmissive_depth_program) { C89GL_glDeleteProgram(gl_transmissive_depth_program); gl_transmissive_depth_program = 0; }
    if (gl_transmissive_fbo) { C89GL_glDeleteFramebuffers(1, &gl_transmissive_fbo); gl_transmissive_fbo = 0; }
    if (gl_transmissive_depth_col) { C89GL_glDeleteTextures(1, &gl_transmissive_depth_col); gl_transmissive_depth_col = 0; }
    if (gl_transmissive_depth_tex) { C89GL_glDeleteTextures(1, &gl_transmissive_depth_tex); gl_transmissive_depth_tex = 0; }
    if (gl_refraction_src) { C89GL_glDeleteTextures(1, &gl_refraction_src); gl_refraction_src = 0; }

    /* Shared full-screen vertex shader — safe to delete now that every
     * program that referenced it has been destroyed. */
    if (gl_fullscreen_vs) { C89GL_glDeleteShader(gl_fullscreen_vs); gl_fullscreen_vs = 0; }

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
    {
        int j;
        for (j = 0; j < 2; j++) {
            if (gl_audio_propagation_ssbo[j]) {
                C89GL_glDeleteBuffers(1, &gl_audio_propagation_ssbo[j]);
                gl_audio_propagation_ssbo[j] = 0;
            }
        }
    }
    if (gl_audio_occlusion_fence) C89GL_glDeleteSync(gl_audio_occlusion_fence);
#endif
#ifdef AUDIO_REVERB
    if (gl_audio_reverb_program) C89GL_glDeleteProgram(gl_audio_reverb_program);
    {
        int j;
        for (j = 0; j < 2; j++) {
            if (gl_audio_global_stats_ssbo[j]) {
                C89GL_glDeleteBuffers(1, &gl_audio_global_stats_ssbo[j]);
                gl_audio_global_stats_ssbo[j] = 0;
            }
        }
    }
    if (gl_audio_reverb_fence) C89GL_glDeleteSync(gl_audio_reverb_fence);
#endif
#ifdef AUDIO_PORTAL
    if (gl_audio_portal_program) C89GL_glDeleteProgram(gl_audio_portal_program);
    {
        int j;
        for (j = 0; j < 2; j++) {
            if (gl_audio_portal_candidates_ssbo[j]) {
                C89GL_glDeleteBuffers(1, &gl_audio_portal_candidates_ssbo[j]);
                gl_audio_portal_candidates_ssbo[j] = 0;
            }
        }
    }
    if (gl_audio_portal_fence) C89GL_glDeleteSync(gl_audio_portal_fence);
#endif

    if (gl_ctx.initialized) C89GL_destroy_context(&gl_ctx);
    gl_transparent_count = 0;
    gl_batch_count = 0;
    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    render_particle_system_shutdown();
}

INLINE void render_draw_entities(struct entity_definition **entities, int count) {
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

INLINE void render_draw_entity(const struct entity_definition *ent) {
    if (!ent) return;
    struct entity_definition *ents[1] = { (struct entity_definition*)ent };
    render_draw_entities(ents, 1);
}

INLINE void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    gl_light_dir = dir; gl_light_col = col; gl_ambient_col = amb;
}

INLINE void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    gl_cam_eye = eye;
    gl_view = mat4_lookat(eye, center, up);
    gl_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    gl_view_proj = mat4_mul(gl_proj, gl_view);
    extract_frustum_planes();
    gl_near = 0.05f;
    gl_far = 1000.0f;
    /* The VBAO UBO carries the projection matrix and its four derived
     * scalars; both have just changed, so request a re-upload on the next
     * dispatch. */
    gl_vbao_ubo_dirty = 1;
}

INLINE void render_set_fog(vec3 color, real start, real end) {
    gl_fog_color = color; gl_fog_start = start; gl_fog_end = end;
}
INLINE void render_set_time(real t) { gl_time = t; }
INLINE void render_clear(u8 r, u8 g, u8 b) { render_clear_color(r/255.0f, g/255.0f, b/255.0f); }
INLINE void render_clear_color(real r, real g, real b) {
    if (!gl_fbo) return;
    bind_fbo();
    C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    C89GL_glDepthMask(GL_TRUE);

    GLfloat color[4] = { r, g, b, 1.0f };
    GLfloat zero[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
    C89GL_glClearBufferfv(GL_COLOR, 0, color);
    C89GL_glClearBufferfv(GL_COLOR, 1, zero);   /* normal + posterize mask */
    C89GL_glClear(GL_DEPTH_BUFFER_BIT);
}
INLINE const u32* render_get_fb(void) { return NULL; }

INLINE int render_resize(i32 new_w, i32 new_h) {
    if (gl_win_width == new_w && gl_win_height == new_h) return 0;
    gl_win_width = new_w;
    gl_win_height = new_h;
    return 0;
}

INLINE void render_set_render_resolution(i32 rw, i32 rh) {
    if (rw <= 0 || rh <= 0) return;
    if (gl_render_width == rw && gl_render_height == rh) return;
    gl_render_width = rw; gl_render_height = rh;
    gl_ao_width  = (gl_render_width  + 1) / 2;
    gl_ao_height = (gl_render_height + 1) / 2;
    /* The half-res AO size is baked into both UBOs; force a re-upload. */
    gl_vbao_ubo_dirty      = 1;
    gl_vbao_blur_ubo_dirty = 1;

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_normal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_render_width, gl_render_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl_normal_tex, 0);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_depth_tex, 0);
    {
        GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        C89GL_glDrawBuffers(2, bufs);
    }

    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_transmissive_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_col);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gl_render_width, gl_render_height, 0, GL_RED, GL_FLOAT, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, gl_render_width, gl_render_height, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);

    C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl_render_width, gl_render_height, 0, GL_RGBA, GL_FLOAT, NULL);

    /* WBOIT targets resize */
    C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_accum_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                       gl_render_width, gl_render_height, 0,
                       GL_RGBA, GL_FLOAT, NULL);
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_oit_reveal_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_render_width, gl_render_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);
    C89GL_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, gl_depth_tex, 0);

    /* VBAO raw target resize (half-res) */
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);

    /* VBAO blurred target resize (half-res) */
    C89GL_glBindTexture(GL_TEXTURE_2D, gl_ao_blurred_tex);
    C89GL_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                       gl_ao_width, gl_ao_height, 0,
                       GL_RED, GL_UNSIGNED_BYTE, NULL);

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

INLINE i32 render_get_render_width(void) { return gl_render_width; }
INLINE i32 render_get_render_height(void) { return gl_render_height; }

/* ---- Batch comparator ----
 * Order: opaque -> transmissive -> transparent.
 * Within each group: by material pointer.
 */
static int batch_compare_mode(const void* a, const void* b) {
    const batch_t* ba = (const batch_t*)a;
    const batch_t* bb = (const batch_t*)b;
    if (ba->is_transparent != bb->is_transparent)
        return ba->is_transparent - bb->is_transparent;
    if (ba->is_refractive != bb->is_refractive)
        return ba->is_refractive - bb->is_refractive;
    if (ba->mat->render_method < bb->mat->render_method) return -1;
    if (ba->mat->render_method > bb->mat->render_method) return  1;
    if (ba->mat < bb->mat) return -1;
    if (ba->mat > bb->mat) return  1;
    return 0;
}

static void render_particle_system_draw_internal(void);
static void render_particle_system_draw_wboit(alpha_pass_side side);

/* Composite the WBOIT accumulation targets over the currently-bound FBO
 * (expected to be gl_fbo) using standard alpha blending. */
static void oit_composite_into_current_fbo(void) {
    if (!gl_oit_composite_program) return;

    C89GL_glDisable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_FALSE);
    C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
    C89GL_glBindVertexArray(gl_vao);

    C89GL_glUseProgram(0);

    // Restore single-target blend func on the draw FBO. Per-attachment
    // state (glBlendFunci) persists per-FBO in GL 4.3+, but the global
    // glBlendFunc() call above does not touch the per-attachment entries
    // set on gl_oit_fbo, so re-install them explicitly next time we bind
    // gl_oit_fbo. Doing it here documents the dependency and keeps the
    // two alpha passes symmetric.
    C89GL_glBlendFunci(0, GL_ONE, GL_ONE);
    C89GL_glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

    C89GL_glEnable(GL_DEPTH_TEST);
    C89GL_glDepthMask(GL_TRUE);
}

/* ---- render_finish (fourteen-pass pipeline with VBAO + WBOIT + post) ---- */
INLINE void render_finish(void) {
    GLuint current_program = 0;
    int current_cull = 1;
    int i;

    flush_transparent_batches();
    qsort(gl_batches, gl_batch_count, sizeof(batch_t), batch_compare_mode);

    dispatch_audio_compute();

    if (gl_batch_count > 0) {
        size_t vert_bytes = gl_pool_used_floats * sizeof(float);
        size_t idx_bytes  = gl_index_pool_used * sizeof(GLuint);

        C89GL_glBindBuffer(GL_ARRAY_BUFFER, gl_vertex_vbo);
        C89GL_glBufferData(GL_ARRAY_BUFFER, vert_bytes, NULL, GL_STREAM_DRAW);
        C89GL_glBufferSubData(GL_ARRAY_BUFFER, 0, vert_bytes, gl_vertex_pool);

        C89GL_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_index_vbo);
        C89GL_glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_bytes, NULL, GL_STREAM_DRAW);
        C89GL_glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_bytes, gl_index_pool);

        /* ============================================================
           Pass 1: Opaque depth pre-pass

           Self-contained state setup. Depth test is enabled explicitly
           here rather than assumed, because Pass 11 (post-process) disables
           it at the end of the previous frame and restores it — but this
           pass must not depend on that restore having happened. If depth
           test is not enabled here, the pre-pass writes garbage depth
           (last-fragment-wins instead of closest-fragment-wins) and the
           following color pass renders with depth testing off, producing
           the wrong draw order.

           GL_BLEND is disabled because the pre-pass writes no color and
           leaving blend state from a previous pass would only matter if
           blending were somehow enabled here, which it should never be.
           ============================================================ */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glBindVertexArray(gl_vao);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glDepthFunc(GL_LESS);
        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (b->is_transparent || b->is_refractive) continue;
            shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 1, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 1, ALPHA_PASS_FRONT);
            }
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                 (void*)(b->index_offset * sizeof(GLuint)));
        }
        if (current_program) C89GL_glUseProgram(0);

        /* ============================================================
           Pass 2: Cluster build
           ============================================================ */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        dispatch_cluster_build();

        C89GL_glDepthFunc(GL_LEQUAL);

        /* ============================================================
           Pass 3: Opaque color pass
           Writes linear HDR color to gl_color_tex and view-space normal
           to gl_normal_tex.rgb, plus the posterize mask in gl_normal_tex.a.
           ============================================================ */
        {
            GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            C89GL_glDrawBuffers(2, bufs);
        }
        C89GL_glBindVertexArray(gl_vao);
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunc(GL_ONE, GL_ZERO);
        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (b->is_transparent || b->is_refractive) continue;
            shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 0, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
            }
            update_material_ubo(b->mat);
            int want_cull = b->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                 (void*)(b->index_offset * sizeof(GLuint)));
        }
        if (current_program) C89GL_glUseProgram(0);

        /* ============================================================
           Pass 3.5 + 3.6: VBAO and VBAO bilateral blur (half-res)
           Both passes share the same disabled-depth / disabled-blend /
           full-color-mask / no-depth-write state. The two dispatches
           each bind their own FBO, viewport, textures and program, and
           the common state is restored once here afterwards.
           ============================================================ */
        C89GL_glDisable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        C89GL_glDepthMask(GL_FALSE);

        dispatch_vbao();
        dispatch_vbao_blur();

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glBindVertexArray(gl_vao);

        /* Restore single-attachment draw buffer for downstream passes that
         * bind gl_fbo. The transmissive color pass (Pass 7) and the
         * transparent depth-only pass (Pass 9) both target gl_fbo with a
         * single color attachment. */
        {
            GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
            C89GL_glDrawBuffers(1, bufs);
        }

        /* ============================================================
           Pass 4: Transmissive depth pass
           ============================================================ */
        int have_transmissive = 0;
        for (i = 0; i < gl_batch_count; i++)
            if (gl_batches[i].is_refractive) { have_transmissive = 1; break; }

        /* Always clear the transmissive depth color to 1.0 so the
         * alpha-behind shader can trust "1.0 means no transmissive here". */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_transmissive_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        {
            GLfloat clr[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            C89GL_glClearBufferfv(GL_COLOR, 0, clr);
            C89GL_glClear(GL_DEPTH_BUFFER_BIT);
        }
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

        if (have_transmissive && gl_transmissive_depth_program) {
            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_transmissive_fbo);
            C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

            C89GL_glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
            C89GL_glDepthMask(GL_FALSE);
            C89GL_glDepthFunc(GL_LESS);
            C89GL_glEnable(GL_DEPTH_TEST);
            C89GL_glDisable(GL_BLEND);

            C89GL_glActiveTexture(GL_TEXTURE0);
            C89GL_glBindTexture(GL_TEXTURE_2D, gl_depth_tex);

            C89GL_glUseProgram(gl_transmissive_depth_program);
            C89GL_glUniformMatrix4fv(gl_transmissive_depth_u_view_proj, 1, GL_TRUE, (float*)&gl_view_proj);
            C89GL_glUniform1i(gl_transmissive_depth_u_opaque_depth, 0);
            C89GL_glBindBufferBase(GL_UNIFORM_BUFFER, MODEL_UBO_BINDING, gl_model_ubo);

            C89GL_glBindVertexArray(gl_vao);
            current_cull = 1;
            C89GL_glEnable(GL_CULL_FACE);
            for (i = 0; i < gl_batch_count; i++) {
                batch_t *b = &gl_batches[i];
                if (!b->is_refractive) continue;
                C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                     (void*)(b->index_offset * sizeof(GLuint)));
            }
            C89GL_glUseProgram(0);
            C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            C89GL_glActiveTexture(GL_TEXTURE0);
        }

        /* ============================================================
           Pass 5: ALPHA_PASS_BEHIND — WBOIT accumulate + composite
           ============================================================ */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);
        C89GL_glBindVertexArray(gl_vao);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

        {
            GLfloat clear_accum[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
            GLfloat clear_reveal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            C89GL_glClearBufferfv(GL_COLOR, 0, clear_accum);
            C89GL_glClearBufferfv(GL_COLOR, 1, clear_reveal);
        }

        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunci(0, GL_ONE, GL_ONE);
        C89GL_glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthMask(GL_FALSE);
        C89GL_glDepthFunc(GL_LEQUAL);

        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (!b->is_transparent) continue;
            shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 0, ALPHA_PASS_BEHIND);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 0, ALPHA_PASS_BEHIND);
            }
            update_material_ubo(b->mat);
            int want_cull = b->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                 (void*)(b->index_offset * sizeof(GLuint)));
        }
        if (current_program) C89GL_glUseProgram(0);

        render_particle_system_draw_wboit(ALPHA_PASS_BEHIND);

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        oit_composite_into_current_fbo();

        /* ============================================================
           Pass 6: Copy gl_color_tex -> gl_refraction_src (HDR)
           ============================================================ */
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_refraction_src);
        C89GL_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, gl_render_width, gl_render_height);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

        /* ============================================================
           Pass 7: Transmissive color pass
           ============================================================ */
        if (have_transmissive) {
            C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
            C89GL_glBindVertexArray(gl_vao);
            C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
            C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            C89GL_glDepthMask(GL_TRUE);
            C89GL_glDepthFunc(GL_LEQUAL);
            C89GL_glEnable(GL_DEPTH_TEST);
            C89GL_glEnable(GL_BLEND);
            C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            current_program = 0;
            for (i = 0; i < gl_batch_count; i++) {
                batch_t *b = &gl_batches[i];
                if (!b->is_refractive) continue;
                shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 0, ALPHA_PASS_FRONT);
                if (!v) continue;
                if (current_program != v->program) {
                    C89GL_glUseProgram(v->program);
                    current_program = v->program;
                    set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
                }
                update_material_ubo(b->mat);
                int want_cull = b->mat->double_sided ? 0 : 1;
                if (current_cull != want_cull) {
                    if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                    else           C89GL_glDisable(GL_CULL_FACE);
                    current_cull = want_cull;
                }
                C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                     (void*)(b->index_offset * sizeof(GLuint)));
            }
            if (current_program) C89GL_glUseProgram(0);
        }

        /* ============================================================
           Pass 8: ALPHA_PASS_FRONT — WBOIT accumulate + composite
           ============================================================ */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_oit_fbo);
        C89GL_glBindVertexArray(gl_vao);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);

        {
            GLfloat clear_accum[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
            GLfloat clear_reveal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            C89GL_glClearBufferfv(GL_COLOR, 0, clear_accum);
            C89GL_glClearBufferfv(GL_COLOR, 1, clear_reveal);
        }

        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunci(0, GL_ONE, GL_ONE);
        C89GL_glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthMask(GL_FALSE);
        C89GL_glDepthFunc(GL_LESS);

        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (!b->is_transparent) continue;
            shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 0, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 0, ALPHA_PASS_FRONT);
            }
            update_material_ubo(b->mat);
            int want_cull = b->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                 (void*)(b->index_offset * sizeof(GLuint)));
        }
        if (current_program) C89GL_glUseProgram(0);

        render_particle_system_draw_wboit(ALPHA_PASS_FRONT);

        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        oit_composite_into_current_fbo();

        C89GL_glBindVertexArray(gl_vao);

        /* ============================================================
           Pass 9: Transparent depth-only pass
           ============================================================ */
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
        C89GL_glViewport(0, 0, gl_render_width, gl_render_height);
        C89GL_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glDepthFunc(GL_LESS);
        C89GL_glEnable(GL_DEPTH_TEST);

        current_program = 0;
        for (i = 0; i < gl_batch_count; i++) {
            batch_t *b = &gl_batches[i];
            if (!b->is_transparent) continue;
            shader_variant_t *v = get_program_for_method((render_method)b->mat->render_method, 1, ALPHA_PASS_FRONT);
            if (!v) continue;
            if (current_program != v->program) {
                C89GL_glUseProgram(v->program);
                current_program = v->program;
                set_uniforms_for_variant(v, 1, ALPHA_PASS_FRONT);
            }
            int want_cull = b->mat->double_sided ? 0 : 1;
            if (current_cull != want_cull) {
                if (want_cull) C89GL_glEnable(GL_CULL_FACE);
                else           C89GL_glDisable(GL_CULL_FACE);
                current_cull = want_cull;
            }
            C89GL_glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_INT,
                                 (void*)(b->index_offset * sizeof(GLuint)));
        }
        if (current_program) C89GL_glUseProgram(0);
        C89GL_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        /* ============================================================
           Pass 10: Blit combined depth to low-res FBO for audio
           ============================================================ */
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_fbo_low);
        C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                0, 0, gl_low_width, gl_low_height,
                                GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    } else {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    }

    /* ============================================================
       Pass 11: Post-process to default FBO
       ============================================================
       Reads the composited linear HDR scene from gl_color_tex, tone maps,
       color grades, encodes to sRGB, applies the user gamma, applies the
       per-material posterize mask from gl_normal_tex.a, and dithers.

       This is the only pass in the entire pipeline that produces
       display-referred values. Every upstream pass operates on linear
       HDR so that the nonlinear tone curve is applied exactly once,
       after all linear blending has already happened.

       State restoration: this pass disables depth test and depth write
       so that the full-screen triangle is not rejected by the depth
       buffer left over from earlier passes. Both must be restored at
       the end because Pass 1 of the next frame does not enable depth
       test itself — it assumes the state is already correct from
       render_init and the previous frame's cleanup. See the top-of-file
       GL STATE HYGIENE note.
       ============================================================ */
    if (gl_post_process_program) {
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
        C89GL_glViewport(0, 0, gl_win_width, gl_win_height);

        C89GL_glDisable(GL_DEPTH_TEST);
        C89GL_glDisable(GL_BLEND);
        C89GL_glDepthMask(GL_FALSE);

        C89GL_glUseProgram(gl_post_process_program);

        /* Both samplers use layout(binding = N), so they are fixed-unit
         * and have no addressable uniform location. Bind unconditionally. */
        C89GL_glActiveTexture(GL_TEXTURE0);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_color_tex);

        if (pp_u_screen_size != -1)
            C89GL_glUniform2f(pp_u_screen_size, (float)gl_render_width, (float)gl_render_height);
        if (pp_u_exposure != -1)
            C89GL_glUniform1f(pp_u_exposure, gl_post_exposure);
        if (pp_u_gamma != -1)
            C89GL_glUniform1f(pp_u_gamma, gl_post_gamma);
        if (pp_u_time != -1)
            C89GL_glUniform1f(pp_u_time, (float)gl_time);

        C89GL_glBindVertexArray(gl_oit_vao);
        C89GL_glDrawArrays(GL_TRIANGLES, 0, 3);
        C89GL_glBindVertexArray(0);

        C89GL_glUseProgram(0);
        C89GL_glActiveTexture(GL_TEXTURE0);

        /* Restore pipeline state perturbed by this pass. The depth test
         * and depth write in particular MUST be restored: Pass 1 of the
         * next frame (opaque depth pre-pass) does not enable depth test
         * itself, so if we leave it disabled the pre-pass writes garbage
         * depth and the following opaque color pass renders without
         * depth testing, producing the wrong draw order. */
        C89GL_glEnable(GL_DEPTH_TEST);
        C89GL_glDepthMask(GL_TRUE);
        C89GL_glEnable(GL_BLEND);
        C89GL_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        /* Fallback: raw blit, no post-process. Only useful for debugging
         * when the post-process program failed to compile — the output
         * will look wrong (linear HDR values interpreted as display). */
        C89GL_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        C89GL_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_default_fbo);
        C89GL_glBlitFramebuffer(0, 0, gl_render_width, gl_render_height,
                                0, 0, gl_win_width, gl_win_height,
                                GL_COLOR_BUFFER_BIT, GL_NEAREST);
        C89GL_glBindFramebuffer(GL_FRAMEBUFFER, gl_default_fbo);
    }

    C89GL_swap_buffers(&gl_ctx);

    gl_pool_used_floats = 0;
    gl_index_pool_used = 0;
    gl_batch_count = 0;
    gl_transparent_count = 0;
    gl_transparent_triangle_id = 0;
    gl_model_count = 0;
}

/* ========================================================================
   Particle system
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

INLINE void render_particle_system_init(int max_particles) {
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

    const char* defines_for_side[2] = {
        "#version 330 core\n#define ALPHA_PASS_BEHIND 1\n#define WBOIT_PASS 1\n",
        "#version 330 core\n#define ALPHA_PASS_FRONT 1\n#define WBOIT_PASS 1\n"
    };

    for (int side = 0; side < 2; side++) {
        GLuint vs = compile_shader_with_defines(GL_VERTEX_SHADER, "particle.vert", defines_for_side[side]);
        GLuint fs = compile_shader_with_defines(GL_FRAGMENT_SHADER, "particle.frag", defines_for_side[side]);
        if (!vs || !fs) {
            if (vs) C89GL_glDeleteShader(vs);
            if (fs) C89GL_glDeleteShader(fs);
            fprintf(stderr, "Failed to compile particle shaders for side %d\n", side);
            render_particle_system_shutdown();
            return;
        }
        g_particle_program[side] = C89GL_glCreateProgram();
        C89GL_glAttachShader(g_particle_program[side], vs);
        C89GL_glAttachShader(g_particle_program[side], fs);
        C89GL_glLinkProgram(g_particle_program[side]);
        GLint status;
        C89GL_glGetProgramiv(g_particle_program[side], GL_LINK_STATUS, &status);
        if (!status) {
            char log[512];
            C89GL_glGetProgramInfoLog(g_particle_program[side], sizeof(log), NULL, log);
            printf("Particle program link error (side %d):\n%s\n", side, log);
            C89GL_glDeleteProgram(g_particle_program[side]);
            g_particle_program[side] = 0;
            C89GL_glDeleteShader(vs);
            C89GL_glDeleteShader(fs);
            render_particle_system_shutdown();
            return;
        }
        C89GL_glDeleteShader(vs);
        C89GL_glDeleteShader(fs);

        g_particle_u_view_proj[side]           = C89GL_glGetUniformLocation(g_particle_program[side], "uViewProj");
        g_particle_u_cam_right[side]           = C89GL_glGetUniformLocation(g_particle_program[side], "uCamRight");
        g_particle_u_cam_up[side]              = C89GL_glGetUniformLocation(g_particle_program[side], "uCamUp");
        g_particle_u_transmissive_depth[side]  = C89GL_glGetUniformLocation(g_particle_program[side], "uTransmissiveDepthTex");
        g_particle_u_screen_size[side]         = C89GL_glGetUniformLocation(g_particle_program[side], "uScreenSize");
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
    for (int i = 0; i < 2; i++) {
        if (g_particle_program[i]) C89GL_glDeleteProgram(g_particle_program[i]);
        g_particle_program[i] = 0;
    }
    if (g_particle_vbo) C89GL_glDeleteBuffers(1, &g_particle_vbo);
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

INLINE void render_particle_system_set_camera(const mat4 *view_proj, vec3 cam_right, vec3 cam_up) {
    g_particle_view_proj = *view_proj;
    g_particle_cam_right = cam_right;
    g_particle_cam_up = cam_up;
    g_particle_cam_valid = 1;
}

/* -------------------------------------------------------------------------
 * Particle solid (non-WBOIT) draw
 *
 * Retained for interface stability. In the current pipeline particles are
 * drawn via render_particle_system_draw_wboit() inside the WBOIT passes, so
 * this function has no callers. It remains useful as a fallback for any
 * future non-WBOIT path or for isolating particle behaviour during debugging.
 * ------------------------------------------------------------------------- */
static void render_particle_system_draw_internal(void) {
    if (g_particle_count == 0 || !g_particle_program[ALPHA_PASS_FRONT]) return;

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

    C89GL_glUseProgram(g_particle_program[ALPHA_PASS_FRONT]);
    C89GL_glUniformMatrix4fv(g_particle_u_view_proj[ALPHA_PASS_FRONT], 1, GL_TRUE, (float*)&g_particle_view_proj);

    float aspect = (gl_render_width > 0 && gl_render_height > 0) ? ((float)gl_render_width / (float)gl_render_height) : 1.0f;
    vec3 cam_right_scaled = vec3_mul_scalar(g_particle_cam_right, 1.0f / aspect);
    C89GL_glUniform3fv(g_particle_u_cam_right[ALPHA_PASS_FRONT], 1, (float*)&cam_right_scaled);
    C89GL_glUniform3fv(g_particle_u_cam_up[ALPHA_PASS_FRONT], 1, (float*)&g_particle_cam_up);

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

/* -------------------------------------------------------------------------
 * Particle WBOIT draw
 *
 * Draws the particle instances into the currently-bound WBOIT FBO with the
 * WBOIT variant of the fragment shader. Assumes the caller has already set
 * up the OIT FBO, cleared its attachments, and installed the per-attachment
 * blend state (ONE, ONE / ZERO, ONE_MINUS_SRC_ALPHA). Inherits GL_LEQUAL
 * against the opaque depth buffer and does not write depth, matching the
 * transparent material pass.
 * ------------------------------------------------------------------------- */
static void render_particle_system_draw_wboit(alpha_pass_side side) {
    if (g_particle_count == 0) return;
    if (!g_particle_program[side]) return;

    C89GL_glBindBuffer(GL_ARRAY_BUFFER, g_particle_vbo);
    C89GL_glBufferData(GL_ARRAY_BUFFER,
                       g_particle_count * sizeof(particle_instance_t),
                       g_particles, GL_STREAM_DRAW);

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

    C89GL_glUseProgram(g_particle_program[side]);
    C89GL_glUniformMatrix4fv(g_particle_u_view_proj[side], 1, GL_TRUE,
                             (float*)&g_particle_view_proj);

    float aspect = (gl_render_width > 0 && gl_render_height > 0)
                 ? ((float)gl_render_width / (float)gl_render_height)
                 : 1.0f;
    vec3 cam_right_scaled = vec3_mul_scalar(g_particle_cam_right, 1.0f / aspect);
    C89GL_glUniform3fv(g_particle_u_cam_right[side], 1, (float*)&cam_right_scaled);
    C89GL_glUniform3fv(g_particle_u_cam_up[side], 1, (float*)&g_particle_cam_up);

    if (side == ALPHA_PASS_BEHIND) {
        C89GL_glActiveTexture(GL_TEXTURE3);
        C89GL_glBindTexture(GL_TEXTURE_2D, gl_transmissive_depth_col);
        C89GL_glUniform1i(g_particle_u_transmissive_depth[side], 3);
        C89GL_glUniform2f(g_particle_u_screen_size[side],
                          (float)gl_render_width, (float)gl_render_height);
        C89GL_glActiveTexture(GL_TEXTURE0);
    }

    GLint prev_vao = 0;
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