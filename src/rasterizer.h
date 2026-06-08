/*
 * rasterizer.h – Unified rasterizer (CPU)
 *
 * Uses material_definition.h for shading parameters.
 * Supports: Wireframe, Flat, Gouraud, Quadratic, Cubic, and Phong.
 * Transparent triangles are binned into tiles and rendered per-tile
 * with depth testing and writes to handle per-pixel ordering.
 * Each tile is processed by exactly one thread, eliminating race conditions.
 */
#ifndef RASTERIZER_H
#define RASTERIZER_H

#include "common.h"
#include "tags/material.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TILE_SIZE 128
#define MIN_TILES_PER_THREAD 4
#define MAX_TRIS_PER_TILE 1024
#define RENDER_WIDTH  1024
#define RENDER_HEIGHT 576
#define MAX_TRANSPARENT 4096
#define CLIP_LEFT 1
#define CLIP_RIGHT 2
#define CLIP_BOTTOM 4
#define CLIP_TOP 8
#define MAX_CLIPPED_VERTS 12

/* ---------------------------------------------------------------------------*/
/*  Type Definitions                                                          */
/* ---------------------------------------------------------------------------*/

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    vec3 l0, l1, l2;
    vec3 c0, c1, c2;
    vec3 orig_v0, orig_v1, orig_v2;
    vec3 orig_l0, orig_l1, orig_l2;
    vec3 orig_n0, orig_n1, orig_n2;
    real bary0[3], bary1[3], bary2[3];
    i32 is_clipped;
    const struct material_definition *mat;
    enum32 mode;
    real depth;
} tile_tri;

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    vec3 l0, l1, l2;
    vec3 orig_v0, orig_v1, orig_v2;
    vec3 orig_l0, orig_l1, orig_l2;
    vec3 orig_n0, orig_n1, orig_n2;
    real bary0[3], bary1[3], bary2[3];
    i32 is_clipped;
    const struct material_definition *mat;
    enum32 mode;
    real depth;
} tile_transparent_tri;

typedef struct {
    tile_tri tris[MAX_TRIS_PER_TILE];
    i32 tri_count;
    tile_transparent_tri transparent_tris[MAX_TRIS_PER_TILE];
    i32 transparent_count;
} tile_bin;

typedef struct {
    i32 tile_idx;
    i32 tile_x, tile_y;
    i32 tile_w, tile_h;
} tile_job;

typedef struct {
    i32 y_start, y_end;
    i32 x_start, x_end;
    u32 *fb_dst;
    const u32 *fb_src;
    i32 dst_width, dst_height;
    i32 src_width, src_height;
} upscale_job;

typedef struct {
    i32 start_idx;
    i32 end_idx;
} clear_range_job;

typedef struct {
    i32 tile_idx;
    u32 color;
} clear_job;

typedef struct {
    i32 x0, y0, x1, y1;
} tile_bounds;

typedef struct {
    vec3 normal;
    real d;
} frustum_plane;

typedef struct {
    vec3 v, n, l;
    real l0_t, l1_t, l2_t;
} clip_vertex;

/* ---------------------------------------------------------------------------*/
/*  Global State                                                              */
/* ---------------------------------------------------------------------------*/

static tile_bin *tile_bins = NULL;
static i32 num_tiles_x = 0;
static i32 num_tiles_y = 0;
static i32 total_tiles = 0;

static tile_job *job_pool = NULL;
static i32 job_pool_size = 0;
static upscale_job *upscale_jobs = NULL;
static i32 upscale_job_count = 0;
static clear_job *clear_jobs = NULL;
static i32 clear_job_count = 0;

static u32 *fb_front = NULL, *fb_back = NULL, *fb = NULL;
static real *zbuf_front = NULL, *zbuf_back = NULL, *zbuf = NULL;
static i32 fw = 0, fh = 0, fb_pitch = 0;
static u32 *fb_render = NULL;
static real *zbuf_render = NULL;
static i32 internal_pitch = 0;

static real scale_x = 1.0f, scale_y = 1.0f;
static mat4 vp;
static vec3 light_dir, light_col, ambient_col;
static vec3 cam_eye;
static vec3 fog_color;
static real fog_start, fog_end;
static tile_bounds screen_bounds;

static frustum_plane frustum[6];
static real render_time = 0.0f;

/* ---------------------------------------------------------------------------*/
/*  Forward Declarations                                                      */
/* ---------------------------------------------------------------------------*/

static void tile_init(i32 width, i32 height);
static void tile_shutdown(void);
static void tile_bin_triangle(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 c0, vec3 c1, vec3 c2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    real *bary0, real *bary1, real *bary2,
    i32 is_clipped,
    const struct material_definition *mat);
static void tile_bin_transparent_triangle(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    real *bary0, real *bary1, real *bary2,
    i32 is_clipped,
    const struct material_definition *mat);
static void tile_render_all(void);
static void tile_clear_bins(void);
static void upscale_tile(void *arg);
static void clear_tile_range(void *arg);
static void tile_clear_bins_range(void *arg);

/* ---------------------------------------------------------------------------*/
/*  Inline Helpers                                                            */
/* ---------------------------------------------------------------------------*/

static INLINE void swapi(i32 *a, i32 *b)
{
    i32 t = *a;
    *a = *b;
    *b = t;
}

static INLINE void swapr(real *a, real *b)
{
    real t = *a;
    *a = *b;
    *b = t;
}

static INLINE void swapv(vec3 *a, vec3 *b)
{
    vec3 t = *a;
    *a = *b;
    *b = t;
}

static INLINE i32 raster_round(real x)
{
    return (i32)real_floor(x + 0.5f);
}

static INLINE u32 pack_color(u8 r, u8 g, u8 b)
{
    return (r << 16) | (g << 8) | b;
}

static INLINE real saturate(real x)
{
    return real_clamp(x, 0.0f, 1.0f);
}

static INLINE u8 color_to_u8(real x)
{
    x = saturate(x);
    return (u8)(x * 255.0f + 0.5f);
}

static INLINE u32 pack_color_real(real r, real g, real b)
{
    return pack_color(color_to_u8(r), color_to_u8(g), color_to_u8(b));
}

static INLINE vec3 interpolate_color_barycentric(
    vec3 c0, vec3 c1, vec3 c2,
    real a, real b, real c)
{
    vec3 r;
    r.color.r = a * c0.color.r + b * c1.color.r + c * c2.color.r;
    r.color.g = a * c0.color.g + b * c1.color.g + c * c2.color.g;
    r.color.b = a * c0.color.b + b * c1.color.b + c * c2.color.b;
    return r;
}

static INLINE i32 clip_code(i32 x, i32 y, const tile_bounds *b)
{
    i32 c = 0;
    if (x < b->x0)      c |= CLIP_LEFT;
    else if (x >= b->x1) c |= CLIP_RIGHT;
    if (y < b->y0)      c |= CLIP_BOTTOM;
    else if (y >= b->y1) c |= CLIP_TOP;
    return c;
}

static INLINE void project(vec3 w, i32 *sx, i32 *sy, real *iw)
{
    vec4 c = mat4_mul_vec4(vp, vec4_init_from_4(
        w.position.x, w.position.y, w.position.z, 1.0f));

    if (c.rotation.w <= 1e-6f) {
        *sx = -1;
        *sy = -1;
        *iw = 0;
        return;
    }

    *iw = 1.0f / c.rotation.w;
    real ndcx = c.position.x * (*iw);
    real ndcy = c.position.y * (*iw);
    *sx = (i32)((ndcx * 0.5f + 0.5f) * RENDER_WIDTH);
    *sy = (i32)((1.0f - (ndcy * 0.5f + 0.5f)) * RENDER_HEIGHT);
}

/* ---------------------------------------------------------------------------*/
/*  Frustum Culling                                                           */
/* ---------------------------------------------------------------------------*/

static void extract_frustum_planes(void)
{
    vec4 c0 = vp.columns[0];
    vec4 c1 = vp.columns[1];
    vec4 c2 = vp.columns[2];
    vec4 c3 = vp.columns[3];
    i32 i;

    frustum[0].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    frustum[0].d = c3.components[3] + c0.components[3];

    frustum[1].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    frustum[1].d = c3.components[3] - c0.components[3];

    frustum[2].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    frustum[2].d = c3.components[3] + c1.components[3];

    frustum[3].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    frustum[3].d = c3.components[3] - c1.components[3];

    frustum[4].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    frustum[4].d = c3.components[3] + c2.components[3];

    frustum[5].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    frustum[5].d = c3.components[3] - c2.components[3];

    for (i = 0; i < 6; i++) {
        real len = vec3_magnitude(frustum[i].normal);
        if (len > 0.0f) {
            frustum[i].normal = vec3_div_scalar(frustum[i].normal, len);
            frustum[i].d /= len;
        }
    }
}

static INLINE i32 triangle_outside_frustum(vec3 v0, vec3 v1, vec3 v2)
{
    i32 i;
    for (i = 0; i < 6; i++) {
        i32 o0 = (vec3_dot(frustum[i].normal, v0) + frustum[i].d) < 0.0f;
        i32 o1 = (vec3_dot(frustum[i].normal, v1) + frustum[i].d) < 0.0f;
        i32 o2 = (vec3_dot(frustum[i].normal, v2) + frustum[i].d) < 0.0f;
        if (o0 && o1 && o2) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------*/
/*  Clipping                                                                  */
/* ---------------------------------------------------------------------------*/

static INLINE i32 clip_triangle_plane(
    clip_vertex *in, i32 in_count,
    clip_vertex *out,
    vec3 normal, real d)
{
    i32 out_count = 0;
    i32 i, prev;

    for (i = 0, prev = in_count - 1; i < in_count; prev = i, i++) {
        real dp1 = vec3_dot(normal, in[prev].v) + d;
        real dp2 = vec3_dot(normal, in[i].v) + d;
        i32 in1 = (dp1 >= 0.0f);
        i32 in2 = (dp2 >= 0.0f);

        if (in1 && in2) {
            out[out_count++] = in[i];
        }
        else if (in1 && !in2) {
            real t = dp1 / (dp1 - dp2);
            clip_vertex iv;
            iv.v = vec3_add(in[prev].v, vec3_mul_scalar(vec3_sub(in[i].v, in[prev].v), t));
            iv.n = vec3_add(in[prev].n, vec3_mul_scalar(vec3_sub(in[i].n, in[prev].n), t));
            iv.l = vec3_add(in[prev].l, vec3_mul_scalar(vec3_sub(in[i].l, in[prev].l), t));
            iv.l0_t = in[prev].l0_t + t * (in[i].l0_t - in[prev].l0_t);
            iv.l1_t = in[prev].l1_t + t * (in[i].l1_t - in[prev].l1_t);
            iv.l2_t = in[prev].l2_t + t * (in[i].l2_t - in[prev].l2_t);
            out[out_count++] = iv;
        }
        else if (!in1 && in2) {
            real t = dp1 / (dp1 - dp2);
            clip_vertex iv;
            iv.v = vec3_add(in[prev].v, vec3_mul_scalar(vec3_sub(in[i].v, in[prev].v), t));
            iv.n = vec3_add(in[prev].n, vec3_mul_scalar(vec3_sub(in[i].n, in[prev].n), t));
            iv.l = vec3_add(in[prev].l, vec3_mul_scalar(vec3_sub(in[i].l, in[prev].l), t));
            iv.l0_t = in[prev].l0_t + t * (in[i].l0_t - in[prev].l0_t);
            iv.l1_t = in[prev].l1_t + t * (in[i].l1_t - in[prev].l1_t);
            iv.l2_t = in[prev].l2_t + t * (in[i].l2_t - in[prev].l2_t);
            out[out_count++] = iv;
            out[out_count++] = in[i];
        }
    }
    return out_count;
}

static INLINE i32 clip_triangle_full(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 *cv_out, vec3 *cn_out, vec3 *cl_out,
    real *bary_out)
{
    clip_vertex in[MAX_CLIPPED_VERTS];
    clip_vertex out[MAX_CLIPPED_VERTS];
    i32 in_count = 3;
    i32 i, k, j, out_count, all_in;
    i32 tri_count;

    in[0].v = v0; in[0].n = n0; in[0].l = l0;
    in[0].l0_t = 1.0f; in[0].l1_t = 0.0f; in[0].l2_t = 0.0f;
    in[1].v = v1; in[1].n = n1; in[1].l = l1;
    in[1].l0_t = 0.0f; in[1].l1_t = 1.0f; in[1].l2_t = 0.0f;
    in[2].v = v2; in[2].n = n2; in[2].l = l2;
    in[2].l0_t = 0.0f; in[2].l1_t = 0.0f; in[2].l2_t = 1.0f;

    for (i = 0; i < 6; i++) {
        if (in_count < 3) return 0;

        all_in = 1;
        for (k = 0; k < in_count; k++) {
            real d = vec3_dot(frustum[i].normal, in[k].v) + frustum[i].d;
            if (d < 0.0f) all_in = 0;
        }
        if (all_in) continue;

        out_count = clip_triangle_plane(in, in_count, out,
            frustum[i].normal, frustum[i].d);
        if (out_count < 3) return 0;

        for (j = 0; j < out_count; j++) {
            in[j] = out[j];
        }
        in_count = out_count;
    }

    tri_count = in_count - 2;
    for (i = 0; i < tri_count; i++) {
        cv_out[i*3+0] = in[0].v;
        cn_out[i*3+0] = in[0].n;
        cv_out[i*3+1] = in[i+1].v;
        cn_out[i*3+1] = in[i+1].n;
        cv_out[i*3+2] = in[i+2].v;
        cn_out[i*3+2] = in[i+2].n;

        bary_out[(i*3+0)*3+0] = in[0].l0_t;
        bary_out[(i*3+0)*3+1] = in[0].l1_t;
        bary_out[(i*3+0)*3+2] = in[0].l2_t;
        bary_out[(i*3+1)*3+0] = in[i+1].l0_t;
        bary_out[(i*3+1)*3+1] = in[i+1].l1_t;
        bary_out[(i*3+1)*3+2] = in[i+1].l2_t;
        bary_out[(i*3+2)*3+0] = in[i+2].l0_t;
        bary_out[(i*3+2)*3+1] = in[i+2].l1_t;
        bary_out[(i*3+2)*3+2] = in[i+2].l2_t;

        cl_out[i*3+0].position.x = in[0].l0_t*l0.position.x + in[0].l1_t*l1.position.x + in[0].l2_t*l2.position.x;
        cl_out[i*3+0].position.y = in[0].l0_t*l0.position.y + in[0].l1_t*l1.position.y + in[0].l2_t*l2.position.y;
        cl_out[i*3+0].position.z = in[0].l0_t*l0.position.z + in[0].l1_t*l1.position.z + in[0].l2_t*l2.position.z;
        cl_out[i*3+1].position.x = in[i+1].l0_t*l0.position.x + in[i+1].l1_t*l1.position.x + in[i+1].l2_t*l2.position.x;
        cl_out[i*3+1].position.y = in[i+1].l0_t*l0.position.y + in[i+1].l1_t*l1.position.y + in[i+1].l2_t*l2.position.y;
        cl_out[i*3+1].position.z = in[i+1].l0_t*l0.position.z + in[i+1].l1_t*l1.position.z + in[i+1].l2_t*l2.position.z;
        cl_out[i*3+2].position.x = in[i+2].l0_t*l0.position.x + in[i+2].l1_t*l1.position.x + in[i+2].l2_t*l2.position.x;
        cl_out[i*3+2].position.y = in[i+2].l0_t*l0.position.y + in[i+2].l1_t*l1.position.y + in[i+2].l2_t*l2.position.y;
        cl_out[i*3+2].position.z = in[i+2].l0_t*l0.position.z + in[i+2].l1_t*l1.position.z + in[i+2].l2_t*l2.position.z;
    }
    return tri_count;
}

/* ---------------------------------------------------------------------------*/
/*  API Functions                                                             */
/* ---------------------------------------------------------------------------*/

static void render_set_light(vec3 dir, vec3 col, vec3 amb)
{
    light_dir = vec3_normalize(dir);
    light_col = col;
    ambient_col = amb;
}

static void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect)
{
    mat4 proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    mat4 view = mat4_lookat(eye, center, up);
    vp = mat4_mul(proj, view);
    cam_eye = eye;
    extract_frustum_planes();
}

static void render_set_fog(vec3 color, real start, real end)
{
    fog_color = color;
    fog_start = start;
    fog_end = end;
}

static void render_set_time(real t)
{
    render_time = t;
}

static i32 render_init(i32 w, i32 h)
{
    fw = w;
    fh = h;
    fb_pitch = (w + 3) & ~3;

    fb_front = (u32*)malloc(fb_pitch * h * sizeof(u32));
    zbuf_front = (real*)malloc(fb_pitch * h * sizeof(real));
    fb_back = (u32*)malloc(fb_pitch * h * sizeof(u32));
    zbuf_back = (real*)malloc(fb_pitch * h * sizeof(real));

    if (!fb_front || !zbuf_front || !fb_back || !zbuf_back) {
        free(fb_front);
        free(zbuf_front);
        free(fb_back);
        free(zbuf_back);
        fb = NULL;
        zbuf = NULL;
        return -1;
    }

    internal_pitch = (RENDER_WIDTH + 3) & ~3;
    fb_render = (u32*)malloc(internal_pitch * RENDER_HEIGHT * sizeof(u32));
    zbuf_render = (real*)malloc(internal_pitch * RENDER_HEIGHT * sizeof(real));

    if (!fb_render || !zbuf_render) {
        free(fb_render);
        free(zbuf_render);
        fb_render = NULL;
        zbuf_render = NULL;
        return -1;
    }

    fb = fb_back;
    zbuf = zbuf_back;
    scale_x = (real)w / (real)RENDER_WIDTH;
    scale_y = (real)h / (real)RENDER_HEIGHT;
    tile_init(w, h);
    return 0;
}

static void render_clear(u8 r, u8 g, u8 b)
{
    u32 col = pack_color(r, g, b);
    i32 i, j;
    if (!fb_render || !zbuf_render) return;

    if (g_thread_count <= 1) {
        u32 *fb32 = (u32*)fb_render;
        real *zb = (real*)zbuf_render;
        i32 n = internal_pitch * RENDER_HEIGHT;

        for (i = 0; i + 3 < n; i += 4) {
            fb32[i] = col;
            fb32[i+1] = col;
            fb32[i+2] = col;
            fb32[i+3] = col;
            zb[i] = 0.0f;
            zb[i+1] = 0.0f;
            zb[i+2] = 0.0f;
            zb[i+3] = 0.0f;
        }
        return;
    }

    if (total_tiles >= MIN_TILES_PER_THREAD * g_thread_count) {
        if (clear_job_count < total_tiles) {
            if (clear_jobs) free(clear_jobs);
            clear_jobs = (clear_job*)malloc(total_tiles * sizeof(clear_job));
            clear_job_count = total_tiles;
        }

        for (j = 0; j < total_tiles; j++) {
            clear_jobs[j].tile_idx = j;
            clear_jobs[j].color = col;
            thpool_add_work(g_threadpool, clear_tile_range, &clear_jobs[j]);
        }
        thpool_wait(g_threadpool);
    }
    else {
        u32 *fb32 = (u32*)fb_render;
        real *zb = (real*)zbuf_render;
        i32 n = internal_pitch * RENDER_HEIGHT;

        for (i = 0; i + 3 < n; i += 4) {
            fb32[i] = col;
            fb32[i+1] = col;
            fb32[i+2] = col;
            fb32[i+3] = col;
            zb[i] = 0.0f;
            zb[i+1] = 0.0f;
            zb[i+2] = 0.0f;
            zb[i+3] = 0.0f;
        }
    }
}

static const u32* render_get_fb(void)
{
    return fb_front;
}

static void render_shutdown(void)
{
    free(fb_front);
    free(zbuf_front);
    free(fb_back);
    free(zbuf_back);
    fb_front = fb_back = fb = NULL;
    zbuf_front = zbuf_back = zbuf = NULL;
    free(fb_render);
    free(zbuf_render);
    fb_render = NULL;
    zbuf_render = NULL;
    tile_shutdown();
}

static i32 render_resize(i32 new_w, i32 new_h)
{
    if (new_w == fw && new_h == fh) return 0;

    free(fb_front);
    free(zbuf_front);
    free(fb_back);
    free(zbuf_back);

    fw = new_w;
    fh = new_h;
    fb_pitch = (new_w + 3) & ~3;

    fb_front = (u32*)malloc(fb_pitch * new_h * sizeof(u32));
    zbuf_front = (real*)malloc(fb_pitch * new_h * sizeof(real));
    fb_back = (u32*)malloc(fb_pitch * new_h * sizeof(u32));
    zbuf_back = (real*)malloc(fb_pitch * new_h * sizeof(real));

    if (!fb_front || !zbuf_front || !fb_back || !zbuf_back) {
        free(fb_front);
        free(zbuf_front);
        free(fb_back);
        free(zbuf_back);
        fb_front = fb_back = fb = NULL;
        zbuf_front = zbuf_back = zbuf = NULL;
        return -1;
    }

    fb = fb_back;
    zbuf = zbuf_back;
    scale_x = (real)new_w / (real)RENDER_WIDTH;
    scale_y = (real)new_h / (real)RENDER_HEIGHT;
    return 0;
}

/* ---------------------------------------------------------------------------*/
/*  Transparent Pixel Write                                                    */
/* ---------------------------------------------------------------------------*/

static INLINE void write_scaled_transparent_pixel(
    i32 rx, i32 ry, real iw,
    vec3 color, real alpha, i32 effects)
{
    i32 ridx;
    u32 dst, result;
    u8 dr, dg, db, sr, sg, sb, ia;

    if (rx < 0 || rx >= RENDER_WIDTH || ry < 0 || ry >= RENDER_HEIGHT) return;
    if (iw <= 0.0f) return;

    (void)effects;

    ridx = ry * RENDER_WIDTH + rx;
    dst = fb_render[ridx];
    dr = (u8)((dst >> 16) & 0xFF);
    dg = (u8)((dst >> 8) & 0xFF);
    db = (u8)(dst & 0xFF);
    sr = color_to_u8(color.color.r);
    sg = color_to_u8(color.color.g);
    sb = color_to_u8(color.color.b);
    ia = (u8)(alpha * 255.0f + 0.5f);

    result = ((u32)((sr * ia + dr * (255 - ia)) / 255) << 16) |
             ((u32)((sg * ia + dg * (255 - ia)) / 255) << 8) |
             ((u32)((sb * ia + db * (255 - ia)) / 255));
    fb_render[ridx] = result;
}

/* ---------------------------------------------------------------------------*/
/*  Shading                                                                   */
/* ---------------------------------------------------------------------------*/

static INLINE vec3 shade_surface(
    vec3 normal, vec3 world_pos, vec3 local_pos,
    const struct material_definition *mat)
{
    vec3 N = normal;
    real ndotl, ndotv = 0.0f;
    vec3 V;
    enum32 effects = mat->effects;

    if (effects & EFFECT_BUMP) {
        real fx = world_pos.position.x * mat->bump_frequency;
        real fy = world_pos.position.y * mat->bump_frequency;
        real fz = world_pos.position.z * mat->bump_frequency;
        real t = render_time * mat->bump_speed;
        N.position.x += real_sin(fy + fz + t) * mat->bump_amplitude;
        N.position.y += real_sin(fz + fx + t) * mat->bump_amplitude;
        N.position.z += real_sin(fx + fy + t) * mat->bump_amplitude;
        N = vec3_normalize(N);
    }

    ndotl = saturate(vec3_dot(N, light_dir));

    if (effects & (EFFECT_MINNAERT | EFFECT_OREN_NAYAR | EFFECT_RIM |
                   EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_IRIDESCENCE |
                   EFFECT_FRINGE)) {
        V = vec3_normalize(vec3_sub(cam_eye, world_pos));
        ndotv = saturate(vec3_dot(N, V));
    }

    real diffuse_term = ndotl;
    vec3 color = mat->color;

    if (effects & EFFECT_DIFFUSE_WRAP) {
        real t = ndotl;
        ndotl = t * t * (3.0f - 2.0f * t);
    }

    if (effects & EFFECT_CEL_SHADING) {
        real inv = 1.0f / (real)(mat->cel_bands - 1);
        ndotl = real_min(1.0f, real_floor(ndotl * mat->cel_bands) * inv);
    }

    if (effects & EFFECT_MINNAERT) {
        diffuse_term = real_pow(ndotl, mat->minnaert_k) *
                       real_pow(ndotv, 1.0f - mat->minnaert_k);
    }

    if (effects & EFFECT_OREN_NAYAR) {
        real sigma = mat->oren_nayar_sigma;
        real sigma_sq = sigma * sigma;
        real a = 1.0f - 0.5f * sigma_sq / (sigma_sq + 0.33f);
        real b = 0.45f * sigma_sq / (sigma_sq + 0.09f);
        real cos_phi_diff = 0.0f;
        real sin_alpha = 0.0f;
        real tan_beta = 0.0f;

        if (ndotl > 0.0f && ndotv > 0.0f) {
            vec3 Lproj = vec3_sub(light_dir, vec3_mul_scalar(N, ndotl));
            vec3 Vproj = vec3_sub(V, vec3_mul_scalar(N, ndotv));
            real lenL = vec3_magnitude(Lproj);
            real lenV = vec3_magnitude(Vproj);

            if (lenL > 1e-6f && lenV > 1e-6f) {
                Lproj = vec3_div_scalar(Lproj, lenL);
                Vproj = vec3_div_scalar(Vproj, lenV);
                cos_phi_diff = vec3_dot(Lproj, Vproj);
                if (cos_phi_diff < 0.0f) cos_phi_diff = 0.0f;

                real sin_l = real_sqrt(saturate(1.0f - ndotl * ndotl));
                real sin_v = real_sqrt(saturate(1.0f - ndotv * ndotv));

                if (ndotl > ndotv) {
                    sin_alpha = sin_v;
                    tan_beta = sin_l / ndotl;
                }
                else {
                    sin_alpha = sin_l;
                    tan_beta = sin_v / ndotv;
                }
            }
        }
        diffuse_term = ndotl * (a + b * cos_phi_diff * sin_alpha * tan_beta);
        diffuse_term = saturate(diffuse_term);
    }

    if (effects & EFFECT_AMBIENT_LIGHT) {
        color = vec3_add(ambient_col, vec3_mul_scalar(light_col, diffuse_term));
        color = vec3_mul_scalar(color, mat->ambient_light_factor);
    }

    if (effects & EFFECT_GOOCH) {
        real t = (ndotl + 1.0f) * 0.5f;
        vec3 gooch = vec3_add(
            vec3_mul_scalar(mat->gooch_cool, 1.0f - t),
            vec3_mul_scalar(mat->gooch_warm, t));
        color = vec3_mul(color, gooch);
    }
    else {
        color = vec3_mul(color, mat->color);
    }

    if (effects & EFFECT_BACK_GLOW) {
        vec3 ln = vec3_mul_scalar(light_dir, -1.0f);
        real ndotl_neg = vec3_dot(N, ln);
        color = vec3_add(color, vec3_mul_scalar(mat->back_glow_color,
            ndotl_neg < 0.0f ? 0.0f : ndotl_neg));
    }

    if (effects & EFFECT_RIM) {
        real rim = real_pow(1.0f - ndotv, mat->rim_exponent);
        color = vec3_add(color, vec3_mul_scalar(mat->rim_color, rim));
    }

    if (effects & EFFECT_FRESNEL) {
        real fresnel = real_pow(1.0f - ndotv, mat->fresnel_exponent);
        color = vec3_add(
            vec3_mul_scalar(color, 1.0f - fresnel),
            vec3_mul_scalar(mat->fresnel_color, fresnel));
    }

    if (effects & EFFECT_EMISSIVE) {
        vec3 emissive = mat->emissive_color;
        if (effects & EFFECT_EMISSIVE_PULSE) {
            real pulse = 1.0f + mat->emissive_pulse_amplitude *
                real_sin(render_time * mat->emissive_pulse_frequency +
                         mat->emissive_pulse_phase);
            emissive = vec3_mul_scalar(emissive, pulse);
        }
        color = vec3_add(color, emissive);
    }

    if (effects & EFFECT_STROBE) {
        real s = real_sin(render_time * mat->strobe_frequency + mat->strobe_phase);
        s = s * 0.5f + 0.5f;
        color = vec3_add(color, vec3_mul_scalar(mat->strobe_color, s));
    }

    if (effects & EFFECT_SPECULAR) {
        vec3 H = vec3_normalize(vec3_add(light_dir, V));
        real nh = vec3_dot(N, H);
        real spec = real_pow(nh < 0.0f ? 0.0f : nh, mat->specular_exponent);
        if (effects & EFFECT_SPECULAR_THRESH) {
            spec = (spec > mat->specular_threshold) ? 1.0f : 0.0f;
        }
        color = vec3_add(color, vec3_mul_scalar(mat->specular_color, spec));
    }

    if (effects & EFFECT_SATURATION) {
        real luma = color.color.r * 0.299f +
                    color.color.g * 0.587f +
                    color.color.b * 0.114f;
        color.color.r = luma + (color.color.r - luma) * mat->saturation;
        color.color.g = luma + (color.color.g - luma) * mat->saturation;
        color.color.b = luma + (color.color.b - luma) * mat->saturation;
    }

    if (effects & EFFECT_IRIDESCENCE) {
        real angle = ndotv * 2.0f * VECTORS_PI;
        real c = real_cos(angle);
        real s = real_sin(angle);
        real rot[9] = {
            0.299f + 0.701f*c + 0.168f*s,
            0.587f - 0.587f*c + 0.330f*s,
            0.114f - 0.114f*c - 0.497f*s,
            0.299f - 0.299f*c - 0.328f*s,
            0.587f + 0.413f*c + 0.035f*s,
            0.114f - 0.114f*c + 0.292f*s,
            0.299f - 0.300f*c + 1.250f*s,
            0.587f - 0.588f*c - 1.050f*s,
            0.114f + 0.886f*c - 0.203f*s
        };
        real r = color.color.r*rot[0] + color.color.g*rot[1] + color.color.b*rot[2];
        real g = color.color.r*rot[3] + color.color.g*rot[4] + color.color.b*rot[5];
        real b = color.color.r*rot[6] + color.color.g*rot[7] + color.color.b*rot[8];
        real is = mat->iridescence_strength;
        color.color.r = r * is + color.color.r * (1.0f - is);
        color.color.g = g * is + color.color.g * (1.0f - is);
        color.color.b = b * is + color.color.b * (1.0f - is);
    }

    color = vec3_mul(color, mat->tint);

    if (effects & EFFECT_GLITCH) {
        u32 x = (u32)(render_time * 60.0f);
        vec3 wp_q = vec3_floor(vec3_mul_scalar(world_pos, 4096.0f));
        x ^= (u32)wp_q.components[0];
        x = x * 1664525u + 1013904223u;
        x ^= (u32)wp_q.components[1];
        x = x * 1664525u + 1013904223u;
        x ^= (u32)wp_q.components[2];
        x = x * 1664525u + 1013904223u;
        real offset = ((real)x * (1.0f / 4294967296.0f) - 0.5f) *
                      mat->glitch_intensity;
        color.color.r += offset;
        color.color.g += offset * 0.7f;
        color.color.b -= offset;
    }

    if (effects & EFFECT_ROUGHNESS) {
        u32 x = 2166136261u;
        vec3 q = vec3_floor(vec3_mul_scalar(local_pos, 256.0f));
        x ^= (u32)q.components[0];
        x *= 16777619u;
        x ^= (u32)q.components[1];
        x *= 16777619u;
        x ^= (u32)q.components[2];
        x *= 16777619u;
        real offset = ((real)x * (1.0f / 4294967296.0f) - 0.5f) * mat->roughness;
        color.color.r += offset * 0.25f;
        color.color.g += offset * 0.25f;
        color.color.b += offset * 0.25f;
    }

    if (effects & EFFECT_FRINGE) {
        real fringe = real_pow(1.0f - ndotv, 3.0f) * mat->fringe_intensity;
        color.color.r += fringe;
        color.color.b -= fringe;
    }

    if (effects & EFFECT_POSTERIZE) {
        real levels = (real)(mat->posterize_levels);
        color.color.r = real_floor(color.color.r * levels + 0.5f) / levels;
        color.color.g = real_floor(color.color.g * levels + 0.5f) / levels;
        color.color.b = real_floor(color.color.b * levels + 0.5f) / levels;
    }

    if ((effects & EFFECT_FOG) && fog_end > fog_start) {
        real dist = vec3_magnitude(vec3_sub(world_pos, cam_eye));
        real t = (dist - fog_start) / (fog_end - fog_start);
        t = saturate(t);
        color = vec3_add(
            vec3_mul_scalar(color, 1.0f - t),
            vec3_mul_scalar(fog_color, t));
    }

    color.color.r = saturate(color.color.r);
    color.color.g = saturate(color.color.g);
    color.color.b = saturate(color.color.b);
    return color;
}

/* ---------------------------------------------------------------------------*/
/*  Line Drawing (Wireframe)                                                  */
/* ---------------------------------------------------------------------------*/

static void draw_line_z(
    i32 x0, i32 y0, real iw0,
    i32 x1, i32 y1, real iw1,
    vec3 color, real alpha, enum32 effects,
    const tile_bounds *bounds)
{
    i32 code0 = clip_code(x0, y0, bounds);
    i32 code1 = clip_code(x1, y1, bounds);
    i32 accept = 0;

    do {
        if ((code0 | code1) == 0) {
            accept = 1;
            break;
        }
        else if ((code0 & code1) != 0) {
            break;
        }
        else {
            i32 outcode = code0 ? code0 : code1;
            real x = (real)x0;
            real y = (real)y0;

            if (outcode & CLIP_TOP) {
                if (y1 != y0) {
                    x = x0 + (real)(x1 - x0) * (bounds->y1 - 1 - y0) / (y1 - y0);
                    y = bounds->y1 - 1;
                }
            }
            else if (outcode & CLIP_BOTTOM) {
                if (y1 != y0) {
                    x = x0 + (real)(x1 - x0) * (bounds->y0 - y0) / (y1 - y0);
                    y = bounds->y0;
                }
            }
            else if (outcode & CLIP_RIGHT) {
                if (x1 != x0) {
                    y = y0 + (real)(y1 - y0) * (bounds->x1 - 1 - x0) / (x1 - x0);
                    x = bounds->x1 - 1;
                }
            }
            else if (outcode & CLIP_LEFT) {
                if (x1 != x0) {
                    y = y0 + (real)(y1 - y0) * (bounds->x0 - x0) / (x1 - x0);
                    x = bounds->x0;
                }
            }

            real dx_line = (real)(x1 - x0);
            real dy_line = (real)(y1 - y0);
            real len_sq = dx_line * dx_line + dy_line * dy_line;
            if (len_sq > 0) {
                real t;
                real new_iw;
                if (outcode == code0)
                    t = real_sqrt((x - x0)*(x - x0) + (y - y0)*(y - y0)) /
                        real_sqrt(len_sq);
                else
                    t = real_sqrt((x - x1)*(x - x1) + (y - y1)*(y - y1)) /
                        real_sqrt(len_sq);
                new_iw = (outcode == code0) ? iw0 + t * (iw1 - iw0)
                                           : iw1 + t * (iw0 - iw1);
                if (outcode == code0) iw0 = new_iw;
                else iw1 = new_iw;
            }

            if (outcode == code0) {
                x0 = (i32)x;
                y0 = (i32)y;
                code0 = clip_code(x0, y0, bounds);
            }
            else {
                x1 = (i32)x;
                y1 = (i32)y;
                code1 = clip_code(x1, y1, bounds);
            }
        }
    } while (1);

    if (!accept) return;

    {
        i32 dx_abs = abs(x1 - x0);
        i32 sx = x0 < x1 ? 1 : -1;
        i32 dy_abs = -abs(y1 - y0);
        i32 sy = y0 < y1 ? 1 : -1;
        i32 err = dx_abs + dy_abs;
        real steps = (real)(dx_abs > -dy_abs ? dx_abs : -dy_abs);
        real diw, iw;

        if (steps == 0) steps = 1;
        diw = (iw1 - iw0) / steps;
        iw = iw0;

        if (!(effects & EFFECT_ALPHA)) {
            while (1) {
                i32 ridx = y0 * RENDER_WIDTH + x0;
                if (x0 >= 0 && x0 < RENDER_WIDTH &&
                    y0 >= 0 && y0 < RENDER_HEIGHT &&
                    iw > zbuf_render[ridx]) {
                    zbuf_render[ridx] = iw;
                    fb_render[ridx] = pack_color_real(color.color.r,
                        color.color.g, color.color.b);
                }
                if (x0 == x1 && y0 == y1) break;

                i32 e2 = 2 * err;
                if (e2 >= dy_abs) {
                    err += dy_abs;
                    x0 += sx;
                    iw += diw;
                }
                if (e2 <= dx_abs) {
                    err += dx_abs;
                    y0 += sy;
                    iw += diw;
                }
            }
        }
        else {
            while (1) {
                i32 ridx = y0 * RENDER_WIDTH + x0;
                if (iw > zbuf_render[ridx]) {
                    write_scaled_transparent_pixel(x0, y0, iw, color, alpha, effects);
                    zbuf_render[ridx] = iw;
                }
                if (x0 == x1 && y0 == y1) break;

                i32 e2 = 2 * err;
                if (e2 >= dy_abs) {
                    err += dy_abs;
                    x0 += sx;
                    iw += diw;
                }
                if (e2 <= dx_abs) {
                    err += dx_abs;
                    y0 += sy;
                    iw += diw;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Triangle Rasterizers                                                      */
/* ---------------------------------------------------------------------------*/

static void raster_triangle_wireframe(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 edge_color, real alpha, enum32 effects,
    const tile_bounds *bounds)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);
    draw_line_z(x0, y0, iw0, x1, y1, iw1, edge_color, alpha, effects, bounds);
    draw_line_z(x1, y1, iw1, x2, y2, iw2, edge_color, alpha, effects, bounds);
    draw_line_z(x2, y2, iw2, x0, y0, iw0, edge_color, alpha, effects, bounds);
}

static void raster_triangle_flat(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 color,
    const struct material_definition *mat,
    const tile_bounds *bounds)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    if (y0 > y1) { swapi(&y0, &y1); swapi(&x0, &x1); swapr(&iw0, &iw1); }
    if (y1 > y2) { swapi(&y1, &y2); swapi(&x1, &x2); swapr(&iw1, &iw2); }
    if (y0 > y1) { swapi(&y0, &y1); swapi(&x0, &x1); swapr(&iw0, &iw1); }

    {
        real dx0 = 0, diw0 = 0, dx1 = 0, diw1 = 0, dx2 = 0, diw2 = 0;
        if (y1 > y0) { real idy = 1.0f/(y1-y0); dx0 = (x1-x0)*idy; diw0 = (iw1-iw0)*idy; }
        if (y2 > y1) { real idy = 1.0f/(y2-y1); dx1 = (x2-x1)*idy; diw1 = (iw2-iw1)*idy; }
        if (y2 > y0) { real idy = 1.0f/(y2-y0); dx2 = (x2-x0)*idy; diw2 = (iw2-iw0)*idy; }

        i32 y_start = y0 < bounds->y0 ? bounds->y0 : y0;
        i32 y_end = y2 > bounds->y1 ? bounds->y1 : y2;
        i32 y;

        for (y = y_start; y < y_end; y++) {
            i32 sx, ex;
            real siw, eiw;

            if (y < y1) {
                real t = (real)(y - y0);
                sx = x0 + raster_round(dx0 * t);
                ex = x0 + raster_round(dx2 * t);
                siw = iw0 + diw0 * t;
                eiw = iw0 + diw2 * t;
            }
            else {
                real t = (real)(y - y1);
                sx = x1 + raster_round(dx1 * t);
                ex = x0 + raster_round(dx2 * (y - y0));
                siw = iw1 + diw1 * t;
                eiw = iw0 + diw2 * (y - y0);
            }

            if (sx > ex) { swapi(&sx, &ex); swapr(&siw, &eiw); }

            {
                real iw_step = (ex > sx) ? (eiw - siw) / (ex - sx) : 0;

                if (sx < bounds->x0) {
                    siw += (bounds->x0 - sx) * iw_step;
                    sx = bounds->x0;
                }
                if (ex > bounds->x1) ex = bounds->x1;
                if (ex <= sx) continue;

                if (!(mat->effects & EFFECT_ALPHA)) {
                    real iw = siw;
                    u32 col = pack_color_real(color.color.r, color.color.g, color.color.b);
                    i32 x;
                    for (x = sx; x < ex; x++) {
                        i32 ridx = y * RENDER_WIDTH + x;
                        if (iw > zbuf_render[ridx]) {
                            zbuf_render[ridx] = iw;
                            fb_render[ridx] = col;
                        }
                        iw += iw_step;
                    }
                }
                else {
                    real iw = siw;
                    i32 x;
                    for (x = sx; x < ex; x++) {
                        i32 ridx = y * RENDER_WIDTH + x;
                        if (iw > zbuf_render[ridx]) {
                            write_scaled_transparent_pixel(x, y, iw, color,
                                mat->alpha, mat->effects);
                            zbuf_render[ridx] = iw;
                        }
                        iw += iw_step;
                    }
                }
            }
        }
    }
}

static void raster_triangle_gouraud(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 c0, vec3 c1, vec3 c2,
    const struct material_definition *mat,
    const tile_bounds *bounds)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    if (y0 > y1) { swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&c0,&c1); }
    if (y1 > y2) { swapi(&y1,&y2); swapi(&x1,&x2); swapr(&iw1,&iw2); swapv(&c1,&c2); }
    if (y0 > y1) { swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&c0,&c1); }

    {
        real dx0 = 0, diw0 = 0, dx1 = 0, diw1 = 0, dx2 = 0, diw2 = 0;
        vec3 dc0 = {0}, dc1 = {0}, dc2 = {0};

        if (y1 > y0) {
            real idy = 1.0f/(y1-y0);
            dx0 = (x1-x0)*idy; diw0 = (iw1-iw0)*idy;
            dc0.color.r = (c1.color.r-c0.color.r)*idy;
            dc0.color.g = (c1.color.g-c0.color.g)*idy;
            dc0.color.b = (c1.color.b-c0.color.b)*idy;
        }
        if (y2 > y1) {
            real idy = 1.0f/(y2-y1);
            dx1 = (x2-x1)*idy; diw1 = (iw2-iw1)*idy;
            dc1.color.r = (c2.color.r-c1.color.r)*idy;
            dc1.color.g = (c2.color.g-c1.color.g)*idy;
            dc1.color.b = (c2.color.b-c1.color.b)*idy;
        }
        if (y2 > y0) {
            real idy = 1.0f/(y2-y0);
            dx2 = (x2-x0)*idy; diw2 = (iw2-iw0)*idy;
            dc2.color.r = (c2.color.r-c0.color.r)*idy;
            dc2.color.g = (c2.color.g-c0.color.g)*idy;
            dc2.color.b = (c2.color.b-c0.color.b)*idy;
        }

        i32 y_start = y0 < bounds->y0 ? bounds->y0 : y0;
        i32 y_end = y2 > bounds->y1 ? bounds->y1 : y2;
        i32 y;

        for (y = y_start; y < y_end; y++) {
            i32 sx, ex;
            real siw, eiw;
            vec3 cs, ce;

            if (y < y1) {
                real t = (real)(y-y0);
                sx = x0 + raster_round(dx0*t);
                ex = x0 + raster_round(dx2*t);
                siw = iw0 + diw0*t;
                eiw = iw0 + diw2*t;
                cs.color.r = c0.color.r + dc0.color.r*t;
                cs.color.g = c0.color.g + dc0.color.g*t;
                cs.color.b = c0.color.b + dc0.color.b*t;
                ce.color.r = c0.color.r + dc2.color.r*t;
                ce.color.g = c0.color.g + dc2.color.g*t;
                ce.color.b = c0.color.b + dc2.color.b*t;
            }
            else {
                real t = (real)(y-y1);
                sx = x1 + raster_round(dx1*t);
                ex = x0 + raster_round(dx2*(y-y0));
                siw = iw1 + diw1*t;
                eiw = iw0 + diw2*(y-y0);
                cs.color.r = c1.color.r + dc1.color.r*t;
                cs.color.g = c1.color.g + dc1.color.g*t;
                cs.color.b = c1.color.b + dc1.color.b*t;
                ce.color.r = c0.color.r + dc2.color.r*(y-y0);
                ce.color.g = c0.color.g + dc2.color.g*(y-y0);
                ce.color.b = c0.color.b + dc2.color.b*(y-y0);
            }

            if (sx > ex) { swapi(&sx, &ex); swapr(&siw, &eiw); swapv(&cs, &ce); }

            {
                real iw_step = (ex > sx) ? (eiw-siw)/(ex-sx) : 0;
                vec3 dc_step;
                dc_step.color.r = (ex > sx) ? (ce.color.r-cs.color.r)/(ex-sx) : 0;
                dc_step.color.g = (ex > sx) ? (ce.color.g-cs.color.g)/(ex-sx) : 0;
                dc_step.color.b = (ex > sx) ? (ce.color.b-cs.color.b)/(ex-sx) : 0;

                if (sx < bounds->x0) {
                    siw += (bounds->x0-sx)*iw_step;
                    cs.color.r += (bounds->x0-sx)*dc_step.color.r;
                    cs.color.g += (bounds->x0-sx)*dc_step.color.g;
                    cs.color.b += (bounds->x0-sx)*dc_step.color.b;
                    sx = bounds->x0;
                }
                if (ex > bounds->x1) ex = bounds->x1;
                if (ex <= sx) continue;

                if (!(mat->effects & EFFECT_ALPHA)) {
                    real iw = siw;
                    vec3 col = cs;
                    i32 x;
                    for (x = sx; x < ex; x++) {
                        i32 ridx = y*RENDER_WIDTH+x;
                        if (iw > zbuf_render[ridx]) {
                            zbuf_render[ridx] = iw;
                            fb_render[ridx] = pack_color_real(col.color.r,
                                col.color.g, col.color.b);
                        }
                        iw += iw_step;
                        col.color.r += dc_step.color.r;
                        col.color.g += dc_step.color.g;
                        col.color.b += dc_step.color.b;
                    }
                }
                else {
                    real iw = siw;
                    vec3 col = cs;
                    i32 x;
                    for (x = sx; x < ex; x++) {
                        i32 ridx = y*RENDER_WIDTH+x;
                        if (iw > zbuf_render[ridx]) {
                            write_scaled_transparent_pixel(x, y, iw, col,
                                mat->alpha, mat->effects);
                            zbuf_render[ridx] = iw;
                        }
                        iw += iw_step;
                        col.color.r += dc_step.color.r;
                        col.color.g += dc_step.color.g;
                        col.color.b += dc_step.color.b;
                    }
                }
            }
        }
    }
}

static void raster_triangle_phong(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat,
    const tile_bounds *bounds)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    {
        vec3 n0w = vec3_mul_scalar(n0, iw0);
        vec3 n1w = vec3_mul_scalar(n1, iw1);
        vec3 n2w = vec3_mul_scalar(n2, iw2);
        vec3 wp0w = vec3_mul_scalar(v0, iw0);
        vec3 wp1w = vec3_mul_scalar(v1, iw1);
        vec3 wp2w = vec3_mul_scalar(v2, iw2);
        vec3 lp0w = vec3_mul_scalar(l0, iw0);
        vec3 lp1w = vec3_mul_scalar(l1, iw1);
        vec3 lp2w = vec3_mul_scalar(l2, iw2);

        if (y0 > y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1);
            swapv(&n0w,&n1w); swapv(&wp0w,&wp1w); swapv(&lp0w,&lp1w);
        }
        if (y1 > y2) {
            swapi(&y1,&y2); swapi(&x1,&x2); swapr(&iw1,&iw2);
            swapv(&n1w,&n2w); swapv(&wp1w,&wp2w); swapv(&lp1w,&lp2w);
        }
        if (y0 > y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1);
            swapv(&n0w,&n1w); swapv(&wp0w,&wp1w); swapv(&lp0w,&lp1w);
        }

        {
            real dx0=0, diw0=0, dx1=0, diw1=0, dx2=0, diw2=0;
            vec3 dnw0={0}, dnw1={0}, dnw2={0};
            vec3 dwpw0={0}, dwpw1={0}, dwpw2={0};
            vec3 dlpw0={0}, dlpw1={0}, dlpw2={0};

            if (y1 > y0) {
                real idy = 1.0f/(y1-y0);
                dx0 = (x1-x0)*idy; diw0 = (iw1-iw0)*idy;
                dnw0.position.x = (n1w.position.x-n0w.position.x)*idy;
                dnw0.position.y = (n1w.position.y-n0w.position.y)*idy;
                dnw0.position.z = (n1w.position.z-n0w.position.z)*idy;
                dwpw0.position.x = (wp1w.position.x-wp0w.position.x)*idy;
                dwpw0.position.y = (wp1w.position.y-wp0w.position.y)*idy;
                dwpw0.position.z = (wp1w.position.z-wp0w.position.z)*idy;
                dlpw0.position.x = (lp1w.position.x-lp0w.position.x)*idy;
                dlpw0.position.y = (lp1w.position.y-lp0w.position.y)*idy;
                dlpw0.position.z = (lp1w.position.z-lp0w.position.z)*idy;
            }
            if (y2 > y1) {
                real idy = 1.0f/(y2-y1);
                dx1 = (x2-x1)*idy; diw1 = (iw2-iw1)*idy;
                dnw1.position.x = (n2w.position.x-n1w.position.x)*idy;
                dnw1.position.y = (n2w.position.y-n1w.position.y)*idy;
                dnw1.position.z = (n2w.position.z-n1w.position.z)*idy;
                dwpw1.position.x = (wp2w.position.x-wp1w.position.x)*idy;
                dwpw1.position.y = (wp2w.position.y-wp1w.position.y)*idy;
                dwpw1.position.z = (wp2w.position.z-wp1w.position.z)*idy;
                dlpw1.position.x = (lp2w.position.x-lp1w.position.x)*idy;
                dlpw1.position.y = (lp2w.position.y-lp1w.position.y)*idy;
                dlpw1.position.z = (lp2w.position.z-lp1w.position.z)*idy;
            }
            if (y2 > y0) {
                real idy = 1.0f/(y2-y0);
                dx2 = (x2-x0)*idy; diw2 = (iw2-iw0)*idy;
                dnw2.position.x = (n2w.position.x-n0w.position.x)*idy;
                dnw2.position.y = (n2w.position.y-n0w.position.y)*idy;
                dnw2.position.z = (n2w.position.z-n0w.position.z)*idy;
                dwpw2.position.x = (wp2w.position.x-wp0w.position.x)*idy;
                dwpw2.position.y = (wp2w.position.y-wp0w.position.y)*idy;
                dwpw2.position.z = (wp2w.position.z-wp0w.position.z)*idy;
                dlpw2.position.x = (lp2w.position.x-lp0w.position.x)*idy;
                dlpw2.position.y = (lp2w.position.y-lp0w.position.y)*idy;
                dlpw2.position.z = (lp2w.position.z-lp0w.position.z)*idy;
            }

            {
                i32 y_start = y0 < bounds->y0 ? bounds->y0 : y0;
                i32 y_end = y2 > bounds->y1 ? bounds->y1 : y2;
                i32 y;

                for (y = y_start; y < y_end; y++) {
                    i32 sx, ex;
                    real siw, eiw;
                    real nws_x, nws_y, nws_z, nwe_x, nwe_y, nwe_z;
                    real wps_x, wps_y, wps_z, wpe_x, wpe_y, wpe_z;
                    real lps_x, lps_y, lps_z, lpe_x, lpe_y, lpe_z;

                    if (y < y1) {
                        real t = (real)(y-y0);
                        sx = x0 + raster_round(dx0*t);
                        ex = x0 + raster_round(dx2*t);
                        siw = iw0 + diw0*t;
                        eiw = iw0 + diw2*t;
                        nws_x = n0w.position.x + dnw0.position.x*t;
                        nws_y = n0w.position.y + dnw0.position.y*t;
                        nws_z = n0w.position.z + dnw0.position.z*t;
                        nwe_x = n0w.position.x + dnw2.position.x*t;
                        nwe_y = n0w.position.y + dnw2.position.y*t;
                        nwe_z = n0w.position.z + dnw2.position.z*t;
                        wps_x = wp0w.position.x + dwpw0.position.x*t;
                        wps_y = wp0w.position.y + dwpw0.position.y*t;
                        wps_z = wp0w.position.z + dwpw0.position.z*t;
                        wpe_x = wp0w.position.x + dwpw2.position.x*t;
                        wpe_y = wp0w.position.y + dwpw2.position.y*t;
                        wpe_z = wp0w.position.z + dwpw2.position.z*t;
                        lps_x = lp0w.position.x + dlpw0.position.x*t;
                        lps_y = lp0w.position.y + dlpw0.position.y*t;
                        lps_z = lp0w.position.z + dlpw0.position.z*t;
                        lpe_x = lp0w.position.x + dlpw2.position.x*t;
                        lpe_y = lp0w.position.y + dlpw2.position.y*t;
                        lpe_z = lp0w.position.z + dlpw2.position.z*t;
                    }
                    else {
                        real t = (real)(y-y1);
                        sx = x1 + raster_round(dx1*t);
                        ex = x0 + raster_round(dx2*(real)(y-y0));
                        siw = iw1 + diw1*t;
                        eiw = iw0 + diw2*(real)(y-y0);
                        nws_x = n1w.position.x + dnw1.position.x*t;
                        nws_y = n1w.position.y + dnw1.position.y*t;
                        nws_z = n1w.position.z + dnw1.position.z*t;
                        nwe_x = n0w.position.x + dnw2.position.x*(real)(y-y0);
                        nwe_y = n0w.position.y + dnw2.position.y*(real)(y-y0);
                        nwe_z = n0w.position.z + dnw2.position.z*(real)(y-y0);
                        wps_x = wp1w.position.x + dwpw1.position.x*t;
                        wps_y = wp1w.position.y + dwpw1.position.y*t;
                        wps_z = wp1w.position.z + dwpw1.position.z*t;
                        wpe_x = wp0w.position.x + dwpw2.position.x*(real)(y-y0);
                        wpe_y = wp0w.position.y + dwpw2.position.y*(real)(y-y0);
                        wpe_z = wp0w.position.z + dwpw2.position.z*(real)(y-y0);
                        lps_x = lp1w.position.x + dlpw1.position.x*t;
                        lps_y = lp1w.position.y + dlpw1.position.y*t;
                        lps_z = lp1w.position.z + dlpw1.position.z*t;
                        lpe_x = lp0w.position.x + dlpw2.position.x*(real)(y-y0);
                        lpe_y = lp0w.position.y + dlpw2.position.y*(real)(y-y0);
                        lpe_z = lp0w.position.z + dlpw2.position.z*(real)(y-y0);
                    }

                    if (sx > ex) {
                        swapi(&sx, &ex); swapr(&siw, &eiw);
                        { real t; t=nws_x; nws_x=nwe_x; nwe_x=t;
                          t=nws_y; nws_y=nwe_y; nwe_y=t;
                          t=nws_z; nws_z=nwe_z; nwe_z=t; }
                        { real t; t=wps_x; wps_x=wpe_x; wpe_x=t;
                          t=wps_y; wps_y=wpe_y; wpe_y=t;
                          t=wps_z; wps_z=wpe_z; wpe_z=t; }
                        { real t; t=lps_x; lps_x=lpe_x; lpe_x=t;
                          t=lps_y; lps_y=lpe_y; lpe_y=t;
                          t=lps_z; lps_z=lpe_z; lpe_z=t; }
                    }

                    {
                        real iw_step = (ex > sx) ? (eiw-siw)/(real)(ex-sx) : 0;
                        real dnw_step_x, dnw_step_y, dnw_step_z;
                        real dwp_step_x, dwp_step_y, dwp_step_z;
                        real dlp_step_x, dlp_step_y, dlp_step_z;

                        if (ex > sx) {
                            dnw_step_x = (nwe_x-nws_x)/(real)(ex-sx);
                            dnw_step_y = (nwe_y-nws_y)/(real)(ex-sx);
                            dnw_step_z = (nwe_z-nws_z)/(real)(ex-sx);
                            dwp_step_x = (wpe_x-wps_x)/(real)(ex-sx);
                            dwp_step_y = (wpe_y-wps_y)/(real)(ex-sx);
                            dwp_step_z = (wpe_z-wps_z)/(real)(ex-sx);
                            dlp_step_x = (lpe_x-lps_x)/(real)(ex-sx);
                            dlp_step_y = (lpe_y-lps_y)/(real)(ex-sx);
                            dlp_step_z = (lpe_z-lps_z)/(real)(ex-sx);
                        }
                        else {
                            dnw_step_x = dnw_step_y = dnw_step_z = 0;
                            dwp_step_x = dwp_step_y = dwp_step_z = 0;
                            dlp_step_x = dlp_step_y = dlp_step_z = 0;
                        }

                        if (sx < bounds->x0) {
                            i32 clipped = bounds->x0 - sx;
                            siw += (real)clipped * iw_step;
                            nws_x += (real)clipped * dnw_step_x;
                            nws_y += (real)clipped * dnw_step_y;
                            nws_z += (real)clipped * dnw_step_z;
                            wps_x += (real)clipped * dwp_step_x;
                            wps_y += (real)clipped * dwp_step_y;
                            wps_z += (real)clipped * dwp_step_z;
                            lps_x += (real)clipped * dlp_step_x;
                            lps_y += (real)clipped * dlp_step_y;
                            lps_z += (real)clipped * dlp_step_z;
                            sx = bounds->x0;
                        }
                        if (ex > bounds->x1) ex = bounds->x1;
                        if (ex <= sx) continue;

                        {
                            real nw_val_x = nws_x, nw_val_y = nws_y, nw_val_z = nws_z;
                            real wp_val_x = wps_x, wp_val_y = wps_y, wp_val_z = wps_z;
                            real lp_val_x = lps_x, lp_val_y = lps_y, lp_val_z = lps_z;

                            if (!(mat->effects & EFFECT_ALPHA)) {
                                real iw = siw;
                                i32 x;
                                for (x = sx; x < ex; x++) {
                                    if (iw > zbuf_render[y*RENDER_WIDTH+x]) {
                                        real inv_w = 1.0f/iw;
                                        vec3 normal, world_pos, local_pos;
                                        normal.position.x = nw_val_x * inv_w;
                                        normal.position.y = nw_val_y * inv_w;
                                        normal.position.z = nw_val_z * inv_w;
                                        world_pos.position.x = wp_val_x * inv_w;
                                        world_pos.position.y = wp_val_y * inv_w;
                                        world_pos.position.z = wp_val_z * inv_w;
                                        local_pos.position.x = lp_val_x * inv_w;
                                        local_pos.position.y = lp_val_y * inv_w;
                                        local_pos.position.z = lp_val_z * inv_w;
                                        {
                                            vec3 color = shade_surface(normal, world_pos, local_pos, mat);
                                            zbuf_render[y*RENDER_WIDTH+x] = iw;
                                            fb_render[y*RENDER_WIDTH+x] = pack_color_real(
                                                color.color.r, color.color.g, color.color.b);
                                        }
                                    }
                                    iw += iw_step;
                                    nw_val_x += dnw_step_x;
                                    nw_val_y += dnw_step_y;
                                    nw_val_z += dnw_step_z;
                                    wp_val_x += dwp_step_x;
                                    wp_val_y += dwp_step_y;
                                    wp_val_z += dwp_step_z;
                                    lp_val_x += dlp_step_x;
                                    lp_val_y += dlp_step_y;
                                    lp_val_z += dlp_step_z;
                                }
                            }
                            else {
                                real iw = siw;
                                i32 x;
                                for (x = sx; x < ex; x++) {
                                    i32 ridx = y*RENDER_WIDTH+x;
                                    if (iw > zbuf_render[ridx]) {
                                        real inv_w = 1.0f/iw;
                                        vec3 color = shade_surface(
                                            vec3_init_from_3(nw_val_x * inv_w,
                                                nw_val_y * inv_w, nw_val_z * inv_w),
                                            vec3_init_from_3(wp_val_x * inv_w,
                                                wp_val_y * inv_w, wp_val_z * inv_w),
                                            vec3_init_from_3(lp_val_x * inv_w,
                                                lp_val_y * inv_w, lp_val_z * inv_w),
                                            mat);
                                        write_scaled_transparent_pixel(x, y, iw, color,
                                            mat->alpha, mat->effects);
                                        zbuf_render[ridx] = iw;
                                    }
                                    iw += iw_step;
                                    nw_val_x += dnw_step_x;
                                    nw_val_y += dnw_step_y;
                                    nw_val_z += dnw_step_z;
                                    wp_val_x += dwp_step_x;
                                    wp_val_y += dwp_step_y;
                                    wp_val_z += dwp_step_z;
                                    lp_val_x += dlp_step_x;
                                    lp_val_y += dlp_step_y;
                                    lp_val_z += dlp_step_z;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Quadratic Rasterizer                                                      */
/* ---------------------------------------------------------------------------*/

static void raster_triangle_quadratic(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 c0, vec3 c1, vec3 c2,
    real *bary0, real *bary1, real *bary2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    const struct material_definition *mat,
    const tile_bounds *bounds,
    i32 is_clipped)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    {
        vec3 q_c0 = shade_surface(n0, orig_v0, orig_l0, mat);
        vec3 q_c1 = shade_surface(n1, orig_v1, orig_l1, mat);
        vec3 q_c2 = shade_surface(n2, orig_v2, orig_l2, mat);

        vec3 cm01 = shade_surface(
            vec3_mul_scalar(vec3_add(n0, n1), 0.5f),
            vec3_add(vec3_mul_scalar(orig_v0, 0.5f), vec3_mul_scalar(orig_v1, 0.5f)),
            vec3_add(vec3_mul_scalar(orig_l0, 0.5f), vec3_mul_scalar(orig_l1, 0.5f)),
            mat);
        vec3 cm12 = shade_surface(
            vec3_mul_scalar(vec3_add(n1, n2), 0.5f),
            vec3_add(vec3_mul_scalar(orig_v1, 0.5f), vec3_mul_scalar(orig_v2, 0.5f)),
            vec3_add(vec3_mul_scalar(orig_l1, 0.5f), vec3_mul_scalar(orig_l2, 0.5f)),
            mat);
        vec3 cm20 = shade_surface(
            vec3_mul_scalar(vec3_add(n2, n0), 0.5f),
            vec3_add(vec3_mul_scalar(orig_v2, 0.5f), vec3_mul_scalar(orig_v0, 0.5f)),
            vec3_add(vec3_mul_scalar(orig_l2, 0.5f), vec3_mul_scalar(orig_l0, 0.5f)),
            mat);

        if (y0 > y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&v0,&v1);
            if (is_clipped) {
                real t0=bary0[0],t1=bary0[1],t2=bary0[2];
                bary0[0]=bary1[0]; bary0[1]=bary1[1]; bary0[2]=bary1[2];
                bary1[0]=t0; bary1[1]=t1; bary1[2]=t2;
            }
        }
        if (y1 > y2) {
            swapi(&y1,&y2); swapi(&x1,&x2); swapr(&iw1,&iw2); swapv(&v1,&v2);
            if (is_clipped) {
                real t0=bary1[0],t1=bary1[1],t2=bary1[2];
                bary1[0]=bary2[0]; bary1[1]=bary2[1]; bary1[2]=bary2[2];
                bary2[0]=t0; bary2[1]=t1; bary2[2]=t2;
            }
        }
        if (y0 > y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&v0,&v1);
            if (is_clipped) {
                real t0=bary0[0],t1=bary0[1],t2=bary0[2];
                bary0[0]=bary1[0]; bary0[1]=bary1[1]; bary0[2]=bary1[2];
                bary1[0]=t0; bary1[1]=t1; bary1[2]=t2;
            }
        }

        {
            real f0x = (real)(y1-y2);
            real f0y = (real)(x2-x1);
            real f0_offset = (real)(x1*y2 - x2*y1);
            real f1x = (real)(y2-y0);
            real f1y = (real)(x0-x2);
            real f1_offset = (real)(x2*y0 - x0*y2);
            real f2x = (real)(y0-y1);
            real f2y = (real)(x1-x0);
            real f2_offset = (real)(x0*y1 - x1*y0);
            real area = f0x*(real)x0 + f0y*(real)y0 + f0_offset;
            real iarea = 1.0f/(real)(2.0f*area);

            real dx0=0, diw0=0, dx1=0, diw1=0, dx2=0, diw2=0;
            if (y1>y0) { real idy=1.0f/(real)(y1-y0); dx0=(real)(x1-x0)*idy; diw0=(iw1-iw0)*idy; }
            if (y2>y1) { real idy=1.0f/(real)(y2-y1); dx1=(real)(x2-x1)*idy; diw1=(iw2-iw1)*idy; }
            if (y2>y0) { real idy=1.0f/(real)(y2-y0); dx2=(real)(x2-x0)*idy; diw2=(iw2-iw0)*idy; }

            i32 y_start = y0 < bounds->y0 ? bounds->y0 : y0;
            i32 y_end = y2 > bounds->y1 ? bounds->y1 : y2;
            i32 y;

            for (y = y_start; y < y_end; y++) {
                i32 sx, ex;
                real siw, eiw;

                if (y < y1) {
                    real t = (real)(y-y0);
                    sx = x0 + raster_round(dx0*t);
                    ex = x0 + raster_round(dx2*t);
                    siw = iw0 + diw0*t;
                    eiw = iw0 + diw2*t;
                }
                else {
                    real t = (real)(y-y1);
                    sx = x1 + raster_round(dx1*t);
                    ex = x0 + raster_round(dx2*(real)(y-y0));
                    siw = iw1 + diw1*t;
                    eiw = iw0 + diw2*(real)(y-y0);
                }

                if (sx > ex) { swapi(&sx, &ex); swapr(&siw, &eiw); }

                {
                    real iw_step = (ex > sx) ? (eiw-siw)/(real)(ex-sx) : 0;

                    if (sx < bounds->x0) {
                        siw += (real)(bounds->x0-sx)*iw_step;
                        sx = bounds->x0;
                    }
                    if (ex > bounds->x1) ex = bounds->x1;
                    if (ex <= sx) continue;

                    if (!(mat->effects & EFFECT_ALPHA)) {
                        real iw = siw;
                        i32 x;
                        for (x = sx; x < ex; x++) {
                            real la = (f0x*(real)x + f0y*(real)y + f0_offset) * iarea;
                            real lb = (f1x*(real)x + f1y*(real)y + f1_offset) * iarea;
                            real lc = (f2x*(real)x + f2y*(real)y + f2_offset) * iarea;
                            la = (la < 0.0f) ? 0.0f : la;
                            lb = (lb < 0.0f) ? 0.0f : lb;
                            lc = (lc < 0.0f) ? 0.0f : lc;
                            real sum = la + lb + lc;
                            if (sum > 0.0f) {
                                real inv_sum = 1.0f / sum;
                                la *= inv_sum;
                                lb *= inv_sum;
                                lc *= inv_sum;
                            }

                            {
                                real a_val, b_val, c_val;
                                if (is_clipped) {
                                    a_val = la*bary0[0] + lb*bary1[0] + lc*bary2[0];
                                    b_val = la*bary0[1] + lb*bary1[1] + lc*bary2[1];
                                    c_val = la*bary0[2] + lb*bary1[2] + lc*bary2[2];
                                }
                                else {
                                    a_val = la; b_val = lb; c_val = lc;
                                }

                                {
                                    vec3 fc;
                                    fc.color.r = a_val*a_val*q_c0.color.r +
                                                 b_val*b_val*q_c1.color.r +
                                                 c_val*c_val*q_c2.color.r +
                                                 2.0f*a_val*b_val*cm01.color.r +
                                                 2.0f*b_val*c_val*cm12.color.r +
                                                 2.0f*c_val*a_val*cm20.color.r;
                                    fc.color.g = a_val*a_val*q_c0.color.g +
                                                 b_val*b_val*q_c1.color.g +
                                                 c_val*c_val*q_c2.color.g +
                                                 2.0f*a_val*b_val*cm01.color.g +
                                                 2.0f*b_val*c_val*cm12.color.g +
                                                 2.0f*c_val*a_val*cm20.color.g;
                                    fc.color.b = a_val*a_val*q_c0.color.b +
                                                 b_val*b_val*q_c1.color.b +
                                                 c_val*c_val*q_c2.color.b +
                                                 2.0f*a_val*b_val*cm01.color.b +
                                                 2.0f*b_val*c_val*cm12.color.b +
                                                 2.0f*c_val*a_val*cm20.color.b;

                                    {
                                        i32 ridx = y*RENDER_WIDTH + x;
                                        if (iw > zbuf_render[ridx]) {
                                            zbuf_render[ridx] = iw;
                                            fb_render[ridx] = pack_color_real(fc.color.r,
                                                fc.color.g, fc.color.b);
                                        }
                                    }
                                }
                            }
                            iw += iw_step;
                        }
                    }
                    else {
                        real iw = siw;
                        i32 x;
                        for (x = sx; x < ex; x++) {
                            real la = (f0x*(real)x + f0y*(real)y + f0_offset) * iarea;
                            real lb = (f1x*(real)x + f1y*(real)y + f1_offset) * iarea;
                            real lc = (f2x*(real)x + f2y*(real)y + f2_offset) * iarea;
                            la = (la < 0.0f) ? 0.0f : la;
                            lb = (lb < 0.0f) ? 0.0f : lb;
                            lc = (lc < 0.0f) ? 0.0f : lc;
                            real sum = la + lb + lc;
                            if (sum > 0.0f) {
                                real inv_sum = 1.0f / sum;
                                la *= inv_sum;
                                lb *= inv_sum;
                                lc *= inv_sum;
                            }

                            {
                                real a_val, b_val, c_val;
                                if (is_clipped) {
                                    a_val = la*bary0[0] + lb*bary1[0] + lc*bary2[0];
                                    b_val = la*bary0[1] + lb*bary1[1] + lc*bary2[1];
                                    c_val = la*bary0[2] + lb*bary1[2] + lc*bary2[2];
                                }
                                else {
                                    a_val = la; b_val = lb; c_val = lc;
                                }

                                {
                                    vec3 fc;
                                    fc.color.r = a_val*a_val*q_c0.color.r +
                                                 b_val*b_val*q_c1.color.r +
                                                 c_val*c_val*q_c2.color.r +
                                                 2.0f*a_val*b_val*cm01.color.r +
                                                 2.0f*b_val*c_val*cm12.color.r +
                                                 2.0f*c_val*a_val*cm20.color.r;
                                    fc.color.g = a_val*a_val*q_c0.color.g +
                                                 b_val*b_val*q_c1.color.g +
                                                 c_val*c_val*q_c2.color.g +
                                                 2.0f*a_val*b_val*cm01.color.g +
                                                 2.0f*b_val*c_val*cm12.color.g +
                                                 2.0f*c_val*a_val*cm20.color.g;
                                    fc.color.b = a_val*a_val*q_c0.color.b +
                                                 b_val*b_val*q_c1.color.b +
                                                 c_val*c_val*q_c2.color.b +
                                                 2.0f*a_val*b_val*cm01.color.b +
                                                 2.0f*b_val*c_val*cm12.color.b +
                                                 2.0f*c_val*a_val*cm20.color.b;

                                    {
                                        i32 ridx = y*RENDER_WIDTH + x;
                                        if (iw > zbuf_render[ridx]) {
                                            write_scaled_transparent_pixel(x, y, iw, fc,
                                                mat->alpha, mat->effects);
                                            zbuf_render[ridx] = iw;
                                        }
                                    }
                                }
                            }
                            iw += iw_step;
                        }
                    }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Cubic Rasterizer                                                          */
/* ---------------------------------------------------------------------------*/

static void raster_triangle_cubic(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 c0, vec3 c1, vec3 c2,
    real *bary0, real *bary1, real *bary2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    const struct material_definition *mat,
    const tile_bounds *bounds,
    i32 is_clipped)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;
    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    {
        vec3 cu_c0 = shade_surface(n0, orig_v0, orig_l0, mat);
        vec3 cu_c1 = shade_surface(n1, orig_v1, orig_l1, mat);
        vec3 cu_c2 = shade_surface(n2, orig_v2, orig_l2, mat);

        vec3 ct01_1 = shade_surface(
            vec3_add(vec3_mul_scalar(n0,2.0f/3.0f), vec3_mul_scalar(n1,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v0,2.0f/3.0f), vec3_mul_scalar(orig_v1,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l0,2.0f/3.0f), vec3_mul_scalar(orig_l1,1.0f/3.0f)),
            mat);
        vec3 ct01_2 = shade_surface(
            vec3_add(vec3_mul_scalar(n0,1.0f/3.0f), vec3_mul_scalar(n1,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v0,1.0f/3.0f), vec3_mul_scalar(orig_v1,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l0,1.0f/3.0f), vec3_mul_scalar(orig_l1,2.0f/3.0f)),
            mat);
        vec3 ct12_1 = shade_surface(
            vec3_add(vec3_mul_scalar(n1,2.0f/3.0f), vec3_mul_scalar(n2,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v1,2.0f/3.0f), vec3_mul_scalar(orig_v2,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l1,2.0f/3.0f), vec3_mul_scalar(orig_l2,1.0f/3.0f)),
            mat);
        vec3 ct12_2 = shade_surface(
            vec3_add(vec3_mul_scalar(n1,1.0f/3.0f), vec3_mul_scalar(n2,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v1,1.0f/3.0f), vec3_mul_scalar(orig_v2,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l1,1.0f/3.0f), vec3_mul_scalar(orig_l2,2.0f/3.0f)),
            mat);
        vec3 ct20_1 = shade_surface(
            vec3_add(vec3_mul_scalar(n2,2.0f/3.0f), vec3_mul_scalar(n0,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v2,2.0f/3.0f), vec3_mul_scalar(orig_v0,1.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l2,2.0f/3.0f), vec3_mul_scalar(orig_l0,1.0f/3.0f)),
            mat);
        vec3 ct20_2 = shade_surface(
            vec3_add(vec3_mul_scalar(n2,1.0f/3.0f), vec3_mul_scalar(n0,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_v2,1.0f/3.0f), vec3_mul_scalar(orig_v0,2.0f/3.0f)),
            vec3_add(vec3_mul_scalar(orig_l2,1.0f/3.0f), vec3_mul_scalar(orig_l0,2.0f/3.0f)),
            mat);

        vec3 cm01 = shade_surface(
            vec3_mul_scalar(vec3_add(n0,n1),0.5f),
            vec3_add(vec3_mul_scalar(orig_v0,0.5f), vec3_mul_scalar(orig_v1,0.5f)),
            vec3_add(vec3_mul_scalar(orig_l0,0.5f), vec3_mul_scalar(orig_l1,0.5f)),
            mat);
        vec3 cm12 = shade_surface(
            vec3_mul_scalar(vec3_add(n1,n2),0.5f),
            vec3_add(vec3_mul_scalar(orig_v1,0.5f), vec3_mul_scalar(orig_v2,0.5f)),
            vec3_add(vec3_mul_scalar(orig_l1,0.5f), vec3_mul_scalar(orig_l2,0.5f)),
            mat);
        vec3 cm20 = shade_surface(
            vec3_mul_scalar(vec3_add(n2,n0),0.5f),
            vec3_add(vec3_mul_scalar(orig_v2,0.5f), vec3_mul_scalar(orig_v0,0.5f)),
            vec3_add(vec3_mul_scalar(orig_l2,0.5f), vec3_mul_scalar(orig_l0,0.5f)),
            mat);

        vec3 cc = shade_surface(
            vec3_mul_scalar(vec3_add(vec3_add(n0,n1),n2), 1.0f/3.0f),
            vec3_mul_scalar(vec3_add(vec3_add(orig_v0,orig_v1),orig_v2), 1.0f/3.0f),
            vec3_mul_scalar(vec3_add(vec3_add(orig_l0,orig_l1),orig_l2), 1.0f/3.0f),
            mat);

        if (y0>y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&v0,&v1);
            if (is_clipped) {
                real t0=bary0[0],t1=bary0[1],t2=bary0[2];
                bary0[0]=bary1[0]; bary0[1]=bary1[1]; bary0[2]=bary1[2];
                bary1[0]=t0; bary1[1]=t1; bary1[2]=t2;
            }
        }
        if (y1>y2) {
            swapi(&y1,&y2); swapi(&x1,&x2); swapr(&iw1,&iw2); swapv(&v1,&v2);
            if (is_clipped) {
                real t0=bary1[0],t1=bary1[1],t2=bary1[2];
                bary1[0]=bary2[0]; bary1[1]=bary2[1]; bary1[2]=bary2[2];
                bary2[0]=t0; bary2[1]=t1; bary2[2]=t2;
            }
        }
        if (y0>y1) {
            swapi(&y0,&y1); swapi(&x0,&x1); swapr(&iw0,&iw1); swapv(&v0,&v1);
            if (is_clipped) {
                real t0=bary0[0],t1=bary0[1],t2=bary0[2];
                bary0[0]=bary1[0]; bary0[1]=bary1[1]; bary0[2]=bary1[2];
                bary1[0]=t0; bary1[1]=t1; bary1[2]=t2;
            }
        }

        {
            real f0x=(real)(y1-y2), f0y=(real)(x2-x1), f0_offset=(real)(x1*y2-x2*y1);
            real f1x=(real)(y2-y0), f1y=(real)(x0-x2), f1_offset=(real)(x2*y0-x0*y2);
            real f2x=(real)(y0-y1), f2y=(real)(x1-x0), f2_offset=(real)(x0*y1-x1*y0);
            real area = f0x*(real)x0 + f0y*(real)y0 + f0_offset;
            real iarea = 1.0f/(real)(2.0f*area);

            real dx0=0, diw0=0, dx1=0, diw1=0, dx2=0, diw2=0;
            if (y1>y0) { real idy=1.0f/(real)(y1-y0); dx0=(real)(x1-x0)*idy; diw0=(iw1-iw0)*idy; }
            if (y2>y1) { real idy=1.0f/(real)(y2-y1); dx1=(real)(x2-x1)*idy; diw1=(iw2-iw1)*idy; }
            if (y2>y0) { real idy=1.0f/(real)(y2-y0); dx2=(real)(x2-x0)*idy; diw2=(iw2-iw0)*idy; }

            i32 y_start = y0<bounds->y0 ? bounds->y0 : y0;
            i32 y_end = y2>bounds->y1 ? bounds->y1 : y2;
            i32 y;

            for (y = y_start; y<y_end; y++) {
                i32 sx, ex;
                real siw, eiw;

                if (y<y1) {
                    real t=(real)(y-y0);
                    sx=x0+raster_round(dx0*t);
                    ex=x0+raster_round(dx2*t);
                    siw=iw0+diw0*t;
                    eiw=iw0+diw2*t;
                }
                else {
                    real t=(real)(y-y1);
                    sx=x1+raster_round(dx1*t);
                    ex=x0+raster_round(dx2*(real)(y-y0));
                    siw=iw1+diw1*t;
                    eiw=iw0+diw2*(real)(y-y0);
                }

                if (sx>ex) { swapi(&sx,&ex); swapr(&siw,&eiw); }

                {
                    real iw_step=(ex>sx)?(eiw-siw)/(real)(ex-sx):0;

                    if (sx<bounds->x0) {
                        siw+=(real)(bounds->x0-sx)*iw_step;
                        sx=bounds->x0;
                    }
                    if (ex>bounds->x1) ex=bounds->x1;
                    if (ex<=sx) continue;

                    if (!(mat->effects&EFFECT_ALPHA)) {
                        real iw=siw;
                        i32 x;
                        for (x = sx; x<ex; x++) {
                            real la=(f0x*(real)x+f0y*(real)y+f0_offset)*iarea;
                            real lb=(f1x*(real)x+f1y*(real)y+f1_offset)*iarea;
                            real lc=(f2x*(real)x+f2y*(real)y+f2_offset)*iarea;
                            la=(la<0.0f)?0.0f:la; lb=(lb<0.0f)?0.0f:lb; lc=(lc<0.0f)?0.0f:lc;
                            real sum=la+lb+lc;
                            if (sum>0.0f) {
                                real inv_sum = 1.0f / sum;
                                la *= inv_sum;
                                lb *= inv_sum;
                                lc *= inv_sum;
                            }

                            {
                                real a_val,b_val,c_val;
                                if (is_clipped) {
                                    a_val=la*bary0[0]+lb*bary1[0]+lc*bary2[0];
                                    b_val=la*bary0[1]+lb*bary1[1]+lc*bary2[1];
                                    c_val=la*bary0[2]+lb*bary1[2]+lc*bary2[2];
                                }
                                else { a_val=la; b_val=lb; c_val=lc; }

                                {
                                    vec3 fc;
                                    real a_sq=a_val*a_val, b_sq=b_val*b_val, c_sq=c_val*c_val;
                                    fc.color.r = a_sq*a_val*cu_c0.color.r +
                                                 b_sq*b_val*cu_c1.color.r +
                                                 c_sq*c_val*cu_c2.color.r +
                                                 3.0f*a_sq*b_val*ct01_1.color.r +
                                                 3.0f*b_sq*c_val*ct12_1.color.r +
                                                 3.0f*c_sq*a_val*ct20_1.color.r +
                                                 3.0f*a_val*b_sq*ct01_2.color.r +
                                                 3.0f*b_val*c_sq*ct12_2.color.r +
                                                 3.0f*c_val*a_sq*ct20_2.color.r +
                                                 6.0f*a_val*b_val*c_val*cc.color.r;
                                    fc.color.g = a_sq*a_val*cu_c0.color.g +
                                                 b_sq*b_val*cu_c1.color.g +
                                                 c_sq*c_val*cu_c2.color.g +
                                                 3.0f*a_sq*b_val*ct01_1.color.g +
                                                 3.0f*b_sq*c_val*ct12_1.color.g +
                                                 3.0f*c_sq*a_val*ct20_1.color.g +
                                                 3.0f*a_val*b_sq*ct01_2.color.g +
                                                 3.0f*b_val*c_sq*ct12_2.color.g +
                                                 3.0f*c_val*a_sq*ct20_2.color.g +
                                                 6.0f*a_val*b_val*c_val*cc.color.g;
                                    fc.color.b = a_sq*a_val*cu_c0.color.b +
                                                 b_sq*b_val*cu_c1.color.b +
                                                 c_sq*c_val*cu_c2.color.b +
                                                 3.0f*a_sq*b_val*ct01_1.color.b +
                                                 3.0f*b_sq*c_val*ct12_1.color.b +
                                                 3.0f*c_sq*a_val*ct20_1.color.b +
                                                 3.0f*a_val*b_sq*ct01_2.color.b +
                                                 3.0f*b_val*c_sq*ct12_2.color.b +
                                                 3.0f*c_val*a_sq*ct20_2.color.b +
                                                 6.0f*a_val*b_val*c_val*cc.color.b;

                                    {
                                        i32 ridx=y*RENDER_WIDTH+x;
                                        if (iw>zbuf_render[ridx]) {
                                            zbuf_render[ridx]=iw;
                                            fb_render[ridx]=pack_color_real(fc.color.r,
                                                fc.color.g, fc.color.b);
                                        }
                                    }
                                }
                            }
                            iw+=iw_step;
                        }
                    }
                    else {
                        real iw=siw;
                        i32 x;
                        for (x = sx; x<ex; x++) {
                            real la=(f0x*(real)x+f0y*(real)y+f0_offset)*iarea;
                            real lb=(f1x*(real)x+f1y*(real)y+f1_offset)*iarea;
                            real lc=(f2x*(real)x+f2y*(real)y+f2_offset)*iarea;
                            la=(la<0.0f)?0.0f:la; lb=(lb<0.0f)?0.0f:lb; lc=(lc<0.0f)?0.0f:lc;
                            real sum=la+lb+lc;
                            if (sum>0.0f) {
                                real inv_sum = 1.0f / sum;
                                la *= inv_sum;
                                lb *= inv_sum;
                                lc *= inv_sum;
                            }

                            {
                                real a_val,b_val,c_val;
                                if (is_clipped) {
                                    a_val=la*bary0[0]+lb*bary1[0]+lc*bary2[0];
                                    b_val=la*bary0[1]+lb*bary1[1]+lc*bary2[1];
                                    c_val=la*bary0[2]+lb*bary1[2]+lc*bary2[2];
                                }
                                else { a_val=la; b_val=lb; c_val=lc; }

                                {
                                    vec3 fc;
                                    real a_sq=a_val*a_val, b_sq=b_val*b_val, c_sq=c_val*c_val;
                                    fc.color.r = a_sq*a_val*cu_c0.color.r +
                                                 b_sq*b_val*cu_c1.color.r +
                                                 c_sq*c_val*cu_c2.color.r +
                                                 3.0f*a_sq*b_val*ct01_1.color.r +
                                                 3.0f*b_sq*c_val*ct12_1.color.r +
                                                 3.0f*c_sq*a_val*ct20_1.color.r +
                                                 3.0f*a_val*b_sq*ct01_2.color.r +
                                                 3.0f*b_val*c_sq*ct12_2.color.r +
                                                 3.0f*c_val*a_sq*ct20_2.color.r +
                                                 6.0f*a_val*b_val*c_val*cc.color.r;
                                    fc.color.g = a_sq*a_val*cu_c0.color.g +
                                                 b_sq*b_val*cu_c1.color.g +
                                                 c_sq*c_val*cu_c2.color.g +
                                                 3.0f*a_sq*b_val*ct01_1.color.g +
                                                 3.0f*b_sq*c_val*ct12_1.color.g +
                                                 3.0f*c_sq*a_val*ct20_1.color.g +
                                                 3.0f*a_val*b_sq*ct01_2.color.g +
                                                 3.0f*b_val*c_sq*ct12_2.color.g +
                                                 3.0f*c_val*a_sq*ct20_2.color.g +
                                                 6.0f*a_val*b_val*c_val*cc.color.g;
                                    fc.color.b = a_sq*a_val*cu_c0.color.b +
                                                 b_sq*b_val*cu_c1.color.b +
                                                 c_sq*c_val*cu_c2.color.b +
                                                 3.0f*a_sq*b_val*ct01_1.color.b +
                                                 3.0f*b_sq*c_val*ct12_1.color.b +
                                                 3.0f*c_sq*a_val*ct20_1.color.b +
                                                 3.0f*a_val*b_sq*ct01_2.color.b +
                                                 3.0f*b_val*c_sq*ct12_2.color.b +
                                                 3.0f*c_val*a_sq*ct20_2.color.b +
                                                 6.0f*a_val*b_val*c_val*cc.color.b;

                                    {
                                        i32 ridx=y*RENDER_WIDTH+x;
                                        if (iw>zbuf_render[ridx]) {
                                            write_scaled_transparent_pixel(x, y, iw, fc,
                                                mat->alpha, mat->effects);
                                            zbuf_render[ridx]=iw;
                                        }
                                    }
                                }
                            }
                            iw+=iw_step;
                        }
                    }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Internal Triangle Drawing                                                 */
/* ---------------------------------------------------------------------------*/

static void draw_triangle_internal(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 c0, vec3 c1, vec3 c2,
    real *bary0, real *bary1, real *bary2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    const struct material_definition *mat,
    const tile_bounds *bounds,
    i32 is_clipped)
{
    vec3 fn, fc, lc, color;

    switch (mat->mode) {
        case SHADE_WIREFRAME:
            fn = vec3_normalize(vec3_cross(
                vec3_sub(v1, v0), vec3_sub(v2, v0)));
            fc = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f/3.0f);
            lc = vec3_mul_scalar(vec3_add(vec3_add(l0, l1), l2), 1.0f/3.0f);
            color = shade_surface(fn, fc, lc, mat);
            raster_triangle_wireframe(v0, v1, v2, color,
                mat->alpha, mat->effects, bounds);
            return;

        case SHADE_FLAT:
            fn = vec3_normalize(vec3_cross(
                vec3_sub(orig_v1, orig_v0), vec3_sub(orig_v2, orig_v0)));
            fc = vec3_mul_scalar(
                vec3_add(vec3_add(orig_v0, orig_v1), orig_v2), 1.0f/3.0f);
            lc = vec3_mul_scalar(
                vec3_add(vec3_add(orig_l0, orig_l1), orig_l2), 1.0f/3.0f);
            color = shade_surface(fn, fc, lc, mat);
            raster_triangle_flat(v0, v1, v2, color, mat, bounds);
            return;

        case SHADE_GOURAUD:
            raster_triangle_gouraud(v0, v1, v2, c0, c1, c2, mat, bounds);
            return;

        case SHADE_PHONG:
            raster_triangle_phong(v0, v1, v2, n0, n1, n2,
                l0, l1, l2, mat, bounds);
            return;

        case SHADE_QUADRATIC:
            raster_triangle_quadratic(v0, v1, v2,
                n0, n1, n2, l0, l1, l2, c0, c1, c2,
                bary0, bary1, bary2,
                orig_v0, orig_v1, orig_v2,
                orig_l0, orig_l1, orig_l2,
                orig_n0, orig_n1, orig_n2,
                mat, bounds, is_clipped);
            return;

        case SHADE_CUBIC:
            raster_triangle_cubic(v0, v1, v2,
                n0, n1, n2, l0, l1, l2, c0, c1, c2,
                bary0, bary1, bary2,
                orig_v0, orig_v1, orig_v2,
                orig_l0, orig_l1, orig_l2,
                orig_n0, orig_n1, orig_n2,
                mat, bounds, is_clipped);
            return;

        default:
            fn = vec3_normalize(vec3_cross(
                vec3_sub(orig_v1, orig_v0), vec3_sub(orig_v2, orig_v0)));
            fc = vec3_mul_scalar(
                vec3_add(vec3_add(orig_v0, orig_v1), orig_v2), 1.0f/3.0f);
            lc = vec3_mul_scalar(
                vec3_add(vec3_add(orig_l0, orig_l1), orig_l2), 1.0f/3.0f);
            color = shade_surface(fn, fc, lc, mat);
            raster_triangle_flat(v0, v1, v2, color, mat, bounds);
            return;
    }
}

/* ---------------------------------------------------------------------------*/
/*  Main Triangle Dispatch                                                    */
/* ---------------------------------------------------------------------------*/

static void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    if (!mat->double_sided) {
        vec3 fn = vec3_normalize(vec3_cross(
            vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 fcen = vec3_mul_scalar(
            vec3_add(vec3_add(v0, v1), v2), 1.0f/3.0f);
        vec3 vd = vec3_sub(cam_eye, fcen);
        if (vec3_dot(fn, vd) <= 0.0f) return;
    }

    if (triangle_outside_frustum(v0, v1, v2)) return;

    {
        vec3 orig_c0, orig_c1, orig_c2;
        i32 needs_vc = (mat->mode == SHADE_GOURAUD ||
                        mat->mode == SHADE_QUADRATIC ||
                        mat->mode == SHADE_CUBIC);
        if (needs_vc) {
            orig_c0 = shade_surface(n0, v0, l0, mat);
            orig_c1 = shade_surface(n1, v1, l1, mat);
            orig_c2 = shade_surface(n2, v2, l2, mat);
        }
        else {
            orig_c0 = vec3_init_from_3(0, 0, 0);
            orig_c1 = vec3_init_from_3(0, 0, 0);
            orig_c2 = vec3_init_from_3(0, 0, 0);
        }

        {
            i32 needs_clip = 0;
            real d0 = vec3_dot(vec3_sub(v0, cam_eye), vec3_sub(v0, cam_eye));
            real d1 = vec3_dot(vec3_sub(v1, cam_eye), vec3_sub(v1, cam_eye));
            real d2 = vec3_dot(vec3_sub(v2, cam_eye), vec3_sub(v2, cam_eye));
            if (d0 < 10000.0f || d1 < 10000.0f || d2 < 10000.0f) needs_clip = 1;

            if (needs_clip) {
                vec3 cv[MAX_CLIPPED_VERTS * 3];
                vec3 cn[MAX_CLIPPED_VERTS * 3];
                vec3 cl[MAX_CLIPPED_VERTS * 3];
                real bary[MAX_CLIPPED_VERTS * 3 * 3];
                i32 tc = clip_triangle_full(v0, v1, v2, n0, n1, n2, l0, l1, l2,
                                             cv, cn, cl, bary);
                if (tc == 0) return;

                {
                    i32 t;
                    for (t = 0; t < tc; t++) {
                        vec3 cv0 = cv[t*3+0], cv1 = cv[t*3+1], cv2 = cv[t*3+2];
                        vec3 cn0 = cn[t*3+0], cn1 = cn[t*3+1], cn2 = cn[t*3+2];
                        vec3 cl0 = cl[t*3+0], cl1 = cl[t*3+1], cl2 = cl[t*3+2];
                        real *b0 = &bary[(t*3+0)*3];
                        real *b1 = &bary[(t*3+1)*3];
                        real *b2 = &bary[(t*3+2)*3];

                        vec3 cc0, cc1, cc2;
                        if (needs_vc) {
                            cc0 = interpolate_color_barycentric(orig_c0, orig_c1, orig_c2,
                                b0[0], b0[1], b0[2]);
                            cc1 = interpolate_color_barycentric(orig_c0, orig_c1, orig_c2,
                                b1[0], b1[1], b1[2]);
                            cc2 = interpolate_color_barycentric(orig_c0, orig_c1, orig_c2,
                                b2[0], b2[1], b2[2]);
                        }
                        else {
                            cc0 = vec3_init_from_3(0, 0, 0);
                            cc1 = vec3_init_from_3(0, 0, 0);
                            cc2 = vec3_init_from_3(0, 0, 0);
                        }

                        if ((mat->effects & EFFECT_ALPHA)) {
                            tile_bin_transparent_triangle(cv0, cv1, cv2,
                                cn0, cn1, cn2, cl0, cl1, cl2,
                                v0, v1, v2, n0, n1, n2, l0, l1, l2,
                                b0, b1, b2, 1, mat);
                        }
                        else {
                            if (g_thread_count <= 1) {
                                draw_triangle_internal(cv0, cv1, cv2,
                                    cn0, cn1, cn2, cl0, cl1, cl2,
                                    cc0, cc1, cc2, b0, b1, b2,
                                    v0, v1, v2, l0, l1, l2, n0, n1, n2,
                                    mat, &screen_bounds, 1);
                            }
                            else {
                                tile_bin_triangle(cv0, cv1, cv2,
                                    cn0, cn1, cn2, cl0, cl1, cl2,
                                    cc0, cc1, cc2,
                                    v0, v1, v2, l0, l1, l2, n0, n1, n2,
                                    b0, b1, b2, 1, mat);
                            }
                        }
                    }
                }
                return;
            }

            /* No clipping needed */
            if ((mat->effects & EFFECT_ALPHA)) {
                tile_bin_transparent_triangle(v0, v1, v2,
                    n0, n1, n2, l0, l1, l2,
                    v0, v1, v2, n0, n1, n2, l0, l1, l2,
                    NULL, NULL, NULL, 0, mat);
                return;
            }

            /* Draw opaque */
            {
                real zb[9] = {0};
                if (g_thread_count <= 1) {
                    draw_triangle_internal(v0, v1, v2, n0, n1, n2, l0, l1, l2,
                        orig_c0, orig_c1, orig_c2, zb, zb, zb,
                        v0, v1, v2, l0, l1, l2, n0, n1, n2,
                        mat, &screen_bounds, 0);
                }
                else {
                    tile_bin_triangle(v0, v1, v2, n0, n1, n2, l0, l1, l2,
                        orig_c0, orig_c1, orig_c2,
                        v0, v1, v2, l0, l1, l2, n0, n1, n2,
                        zb, zb, zb, 0, mat);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Upscale and Finish                                                        */
/* ---------------------------------------------------------------------------*/

static void upscale_tile(void *arg)
{
    upscale_job *j = (upscale_job*)arg;
    i32 y, x;

    for (y = j->y_start; y < j->y_end; y++) {
        i32 ry = (y * j->src_height) / j->dst_height;
        i32 rb = y * j->dst_width;
        i32 sb = ry * j->src_width;

        for (x = j->x_start; x < j->x_end; x++) {
            i32 rx = (x * j->src_width) / j->dst_width;
            j->fb_dst[rb + x] = j->fb_src[sb + rx];
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Tile Management                                                           */
/* ---------------------------------------------------------------------------*/

static void render_tile(void *arg)
{
    tile_job *j = (tile_job*)arg;
    tile_bin *b = &tile_bins[j->tile_idx];

    tile_bounds bnd;
    bnd.x0 = j->tile_x;
    bnd.y0 = j->tile_y;
    bnd.x1 = (j->tile_x + j->tile_w > RENDER_WIDTH) ?
             RENDER_WIDTH : j->tile_x + j->tile_w;
    bnd.y1 = (j->tile_y + j->tile_h > RENDER_HEIGHT) ?
             RENDER_HEIGHT : j->tile_y + j->tile_h;

    /* Render opaque triangles */
    if (b->tri_count > 0) {
        i32 t;
        for (t = 0; t < b->tri_count; t++) {
            tile_tri *tr = &b->tris[t];

            switch (tr->mode) {
                case SHADE_WIREFRAME: {
                    vec3 fn = vec3_normalize(vec3_cross(
                        vec3_sub(tr->v1, tr->v0), vec3_sub(tr->v2, tr->v0)));
                    vec3 fc = vec3_mul_scalar(
                        vec3_add(vec3_add(tr->v0, tr->v1), tr->v2), 1.0f/3.0f);
                    vec3 lc = vec3_mul_scalar(
                        vec3_add(vec3_add(tr->l0, tr->l1), tr->l2), 1.0f/3.0f);
                    vec3 col = shade_surface(fn, fc, lc, tr->mat);
                    raster_triangle_wireframe(tr->v0, tr->v1, tr->v2, col,
                        tr->mat->alpha, tr->mat->effects, &bnd);
                    break;
                }
                case SHADE_FLAT: {
                    vec3 fn = vec3_normalize(vec3_cross(
                        vec3_sub(tr->orig_v1, tr->orig_v0),
                        vec3_sub(tr->orig_v2, tr->orig_v0)));
                    vec3 fc = vec3_mul_scalar(
                        vec3_add(vec3_add(tr->orig_v0, tr->orig_v1), tr->orig_v2),
                        1.0f/3.0f);
                    vec3 lc = vec3_mul_scalar(
                        vec3_add(vec3_add(tr->orig_l0, tr->orig_l1), tr->orig_l2),
                        1.0f/3.0f);
                    vec3 col = shade_surface(fn, fc, lc, tr->mat);
                    raster_triangle_flat(tr->v0, tr->v1, tr->v2, col, tr->mat, &bnd);
                    break;
                }
                case SHADE_GOURAUD:
                    raster_triangle_gouraud(tr->v0, tr->v1, tr->v2,
                        tr->c0, tr->c1, tr->c2, tr->mat, &bnd);
                    break;

                case SHADE_PHONG:
                    raster_triangle_phong(tr->v0, tr->v1, tr->v2,
                        tr->n0, tr->n1, tr->n2,
                        tr->l0, tr->l1, tr->l2, tr->mat, &bnd);
                    break;

                case SHADE_QUADRATIC:
                    raster_triangle_quadratic(tr->v0, tr->v1, tr->v2,
                        tr->n0, tr->n1, tr->n2,
                        tr->l0, tr->l1, tr->l2,
                        tr->c0, tr->c1, tr->c2,
                        tr->bary0, tr->bary1, tr->bary2,
                        tr->orig_v0, tr->orig_v1, tr->orig_v2,
                        tr->orig_l0, tr->orig_l1, tr->orig_l2,
                        tr->orig_n0, tr->orig_n1, tr->orig_n2,
                        tr->mat, &bnd, tr->is_clipped);
                    break;

                case SHADE_CUBIC:
                    raster_triangle_cubic(tr->v0, tr->v1, tr->v2,
                        tr->n0, tr->n1, tr->n2,
                        tr->l0, tr->l1, tr->l2,
                        tr->c0, tr->c1, tr->c2,
                        tr->bary0, tr->bary1, tr->bary2,
                        tr->orig_v0, tr->orig_v1, tr->orig_v2,
                        tr->orig_l0, tr->orig_l1, tr->orig_l2,
                        tr->orig_n0, tr->orig_n1, tr->orig_n2,
                        tr->mat, &bnd, tr->is_clipped);
                    break;

                default:
                    break;
            }
        }
    }

    /* Sort and render transparent triangles */
    if (b->transparent_count > 0) {
        /* Insertion sort by depth (smallest iw = farthest first) */
        i32 ti, tj;
        for (ti = 1; ti < b->transparent_count; ti++) {
            tile_transparent_tri key = b->transparent_tris[ti];
            tj = ti - 1;
            while (tj >= 0 && b->transparent_tris[tj].depth > key.depth) {
                b->transparent_tris[tj + 1] = b->transparent_tris[tj];
                tj--;
            }
            b->transparent_tris[tj + 1] = key;
        }

        {
            real zb[9] = {0};
            for (ti = 0; ti < b->transparent_count; ti++) {
                tile_transparent_tri *tt = &b->transparent_tris[ti];
                vec3 tc0, tc1, tc2;
                i32 nv = (tt->mode == SHADE_GOURAUD ||
                          tt->mode == SHADE_QUADRATIC ||
                          tt->mode == SHADE_CUBIC);
                if (nv) {
                    tc0 = shade_surface(tt->orig_n0, tt->orig_v0, tt->orig_l0, tt->mat);
                    tc1 = shade_surface(tt->orig_n1, tt->orig_v1, tt->orig_l1, tt->mat);
                    tc2 = shade_surface(tt->orig_n2, tt->orig_v2, tt->orig_l2, tt->mat);
                }
                else {
                    tc0 = vec3_init_from_3(0, 0, 0);
                    tc1 = vec3_init_from_3(0, 0, 0);
                    tc2 = vec3_init_from_3(0, 0, 0);
                }
                draw_triangle_internal(
                    tt->v0, tt->v1, tt->v2,
                    tt->n0, tt->n1, tt->n2,
                    tt->l0, tt->l1, tt->l2,
                    tc0, tc1, tc2,
                    tt->bary0, tt->bary1, tt->bary2,
                    tt->orig_v0, tt->orig_v1, tt->orig_v2,
                    tt->orig_l0, tt->orig_l1, tt->orig_l2,
                    tt->orig_n0, tt->orig_n1, tt->orig_n2,
                    tt->mat, &bnd, tt->is_clipped);
            }
        }
    }
}

static void tile_init(i32 w, i32 h)
{
    i32 i;
    (void)w;
    (void)h;

    screen_bounds.x0 = 0;
    screen_bounds.y0 = 0;
    screen_bounds.x1 = RENDER_WIDTH;
    screen_bounds.y1 = RENDER_HEIGHT;

    num_tiles_x = (RENDER_WIDTH + TILE_SIZE - 1) / TILE_SIZE;
    num_tiles_y = (RENDER_HEIGHT + TILE_SIZE - 1) / TILE_SIZE;
    total_tiles = num_tiles_x * num_tiles_y;

    tile_bins = (tile_bin*)malloc(total_tiles * sizeof(tile_bin));
    for (i = 0; i < total_tiles; i++) {
        tile_bins[i].tri_count = 0;
        tile_bins[i].transparent_count = 0;
    }
}

static void tile_shutdown(void)
{
    if (tile_bins) {
        free(tile_bins);
        tile_bins = NULL;
    }
    if (job_pool) {
        free(job_pool);
        job_pool = NULL;
        job_pool_size = 0;
    }
    if (upscale_jobs) {
        free(upscale_jobs);
        upscale_jobs = NULL;
        upscale_job_count = 0;
    }
    if (clear_jobs) {
        free(clear_jobs);
        clear_jobs = NULL;
        clear_job_count = 0;
    }
}

static void tile_bin_triangle(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 c0, vec3 c1, vec3 c2,
    vec3 ov0, vec3 ov1, vec3 ov2,
    vec3 ol0, vec3 ol1, vec3 ol2,
    vec3 on0, vec3 on1, vec3 on2,
    real *ba0, real *ba1, real *ba2,
    i32 ic,
    const struct material_definition *mat)
{
    i32 sx0, sy0, sx1, sy1, sx2, sy2;
    real iw0, iw1, iw2;
    project(v0, &sx0, &sy0, &iw0);
    project(v1, &sx1, &sy1, &iw1);
    project(v2, &sx2, &sy2, &iw2);

    {
        i32 mx = sx0, Mx = sx0, my = sy0, My = sy0;
        if (sx1 < mx) mx = sx1; if (sx1 > Mx) Mx = sx1;
        if (sx2 < mx) mx = sx2; if (sx2 > Mx) Mx = sx2;
        if (sy1 < my) my = sy1; if (sy1 > My) My = sy1;
        if (sy2 < my) my = sy2; if (sy2 > My) My = sy2;
        mx = (mx < 0) ? 0 : mx;
        my = (my < 0) ? 0 : my;
        Mx = (Mx >= RENDER_WIDTH) ? RENDER_WIDTH - 1 : Mx;
        My = (My >= RENDER_HEIGHT) ? RENDER_HEIGHT - 1 : My;

        {
            i32 tmx = mx / TILE_SIZE;
            i32 tmy = my / TILE_SIZE;
            i32 tMx = Mx / TILE_SIZE;
            i32 tMy = My / TILE_SIZE;
            i32 ty, tx;

            for (ty = tmy; ty <= tMy; ty++) {
                i32 bi = ty * num_tiles_x;
                for (tx = tmx; tx <= tMx; tx++) {
                    i32 idx = bi + tx;
                    if (idx >= total_tiles) continue;

                    {
                        tile_bin *bn = &tile_bins[idx];
                        if (bn->tri_count >= MAX_TRIS_PER_TILE) continue;

                        {
                            tile_tri *tr = &bn->tris[bn->tri_count++];
                            tr->v0 = v0; tr->v1 = v1; tr->v2 = v2;
                            tr->n0 = n0; tr->n1 = n1; tr->n2 = n2;
                            tr->l0 = l0; tr->l1 = l1; tr->l2 = l2;
                            tr->c0 = c0; tr->c1 = c1; tr->c2 = c2;
                            tr->orig_v0 = ov0; tr->orig_v1 = ov1; tr->orig_v2 = ov2;
                            tr->orig_l0 = ol0; tr->orig_l1 = ol1; tr->orig_l2 = ol2;
                            tr->orig_n0 = on0; tr->orig_n1 = on1; tr->orig_n2 = on2;

                            if (ba0) {
                                tr->bary0[0] = ba0[0];
                                tr->bary0[1] = ba0[1];
                                tr->bary0[2] = ba0[2];
                            }
                            else {
                                tr->bary0[0] = 0; tr->bary0[1] = 0; tr->bary0[2] = 0;
                            }
                            if (ba1) {
                                tr->bary1[0] = ba1[0];
                                tr->bary1[1] = ba1[1];
                                tr->bary1[2] = ba1[2];
                            }
                            else {
                                tr->bary1[0] = 0; tr->bary1[1] = 0; tr->bary1[2] = 0;
                            }
                            if (ba2) {
                                tr->bary2[0] = ba2[0];
                                tr->bary2[1] = ba2[1];
                                tr->bary2[2] = ba2[2];
                            }
                            else {
                                tr->bary2[0] = 0; tr->bary2[1] = 0; tr->bary2[2] = 0;
                            }

                            tr->is_clipped = ic;
                            tr->mat = mat;
                            tr->mode = mat->mode;
                        }
                    }
                }
            }
        }
    }
}

static void tile_bin_transparent_triangle(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 orig_v0, vec3 orig_v1, vec3 orig_v2,
    vec3 orig_n0, vec3 orig_n1, vec3 orig_n2,
    vec3 orig_l0, vec3 orig_l1, vec3 orig_l2,
    real *bary0, real *bary1, real *bary2,
    i32 is_clipped,
    const struct material_definition *mat)
{
    i32 sx0, sy0, sx1, sy1, sx2, sy2;
    real iw0, iw1, iw2;
    project(v0, &sx0, &sy0, &iw0);
    project(v1, &sx1, &sy1, &iw1);
    project(v2, &sx2, &sy2, &iw2);

    i32 mx = sx0, Mx = sx0, my = sy0, My = sy0;
    if (sx1 < mx) mx = sx1; if (sx1 > Mx) Mx = sx1;
    if (sx2 < mx) mx = sx2; if (sx2 > Mx) Mx = sx2;
    if (sy1 < my) my = sy1; if (sy1 > My) My = sy1;
    if (sy2 < my) my = sy2; if (sy2 > My) My = sy2;
    mx = (mx < 0) ? 0 : mx;
    my = (my < 0) ? 0 : my;
    Mx = (Mx >= RENDER_WIDTH) ? RENDER_WIDTH - 1 : Mx;
    My = (My >= RENDER_HEIGHT) ? RENDER_HEIGHT - 1 : My;

    i32 tmx = mx / TILE_SIZE;
    i32 tmy = my / TILE_SIZE;
    i32 tMx = Mx / TILE_SIZE;
    i32 tMy = My / TILE_SIZE;
    real depth = (iw0 < iw1) ? ((iw0 < iw2) ? iw0 : iw2)
                             : ((iw1 < iw2) ? iw1 : iw2);
    i32 ty, tx;

    for (ty = tmy; ty <= tMy; ty++) {
        i32 bi = ty * num_tiles_x;
        for (tx = tmx; tx <= tMx; tx++) {
            i32 idx = bi + tx;
            if (idx >= total_tiles) continue;

            tile_bin *bn = &tile_bins[idx];
            if (bn->transparent_count >= MAX_TRIS_PER_TILE) continue;

            tile_transparent_tri *tt = &bn->transparent_tris[bn->transparent_count++];
            tt->v0 = v0; tt->v1 = v1; tt->v2 = v2;
            tt->n0 = n0; tt->n1 = n1; tt->n2 = n2;
            tt->l0 = l0; tt->l1 = l1; tt->l2 = l2;
            tt->orig_v0 = orig_v0; tt->orig_v1 = orig_v1; tt->orig_v2 = orig_v2;
            tt->orig_n0 = orig_n0; tt->orig_n1 = orig_n1; tt->orig_n2 = orig_n2;
            tt->orig_l0 = orig_l0; tt->orig_l1 = orig_l1; tt->orig_l2 = orig_l2;
            if (bary0) {
                tt->bary0[0] = bary0[0]; tt->bary0[1] = bary0[1]; tt->bary0[2] = bary0[2];
                tt->bary1[0] = bary1[0]; tt->bary1[1] = bary1[1]; tt->bary1[2] = bary1[2];
                tt->bary2[0] = bary2[0]; tt->bary2[1] = bary2[1]; tt->bary2[2] = bary2[2];
            }
            else {
                memset(tt->bary0, 0, sizeof(tt->bary0));
                memset(tt->bary1, 0, sizeof(tt->bary1));
                memset(tt->bary2, 0, sizeof(tt->bary2));
            }
            tt->is_clipped = is_clipped;
            tt->mat = mat;
            tt->mode = mat->mode;
            tt->depth = depth;
        }
    }
}

static void tile_render_all(void)
{
    i32 at = 0;
    i32 i;
    for (i = 0; i < total_tiles; i++) {
        if (tile_bins[i].tri_count > 0 || tile_bins[i].transparent_count > 0) at++;
    }

    if (at < MIN_TILES_PER_THREAD * g_thread_count) {
        for (i = 0; i < total_tiles; i++) {
            if (tile_bins[i].tri_count > 0 || tile_bins[i].transparent_count > 0) {
                tile_job jb;
                jb.tile_idx = i;
                jb.tile_x = (i % num_tiles_x) * TILE_SIZE;
                jb.tile_y = (i / num_tiles_x) * TILE_SIZE;
                jb.tile_w = ((i % num_tiles_x) == num_tiles_x - 1) ?
                            (RENDER_WIDTH - jb.tile_x) : TILE_SIZE;
                jb.tile_h = ((i / num_tiles_x) == num_tiles_y - 1) ?
                            (RENDER_HEIGHT - jb.tile_y) : TILE_SIZE;
                render_tile(&jb);
            }
        }
        return;
    }

    if (job_pool_size < total_tiles) {
        if (job_pool) free(job_pool);
        job_pool = (tile_job*)malloc(total_tiles * sizeof(tile_job));
        job_pool_size = total_tiles;
    }

    {
        i32 jc = 0;
        for (i = 0; i < total_tiles; i++) {
            if (tile_bins[i].tri_count > 0 || tile_bins[i].transparent_count > 0) {
                i32 ty = i / num_tiles_x;
                i32 tx = i % num_tiles_x;
                tile_job *jb = &job_pool[jc++];
                jb->tile_idx = i;
                jb->tile_x = tx * TILE_SIZE;
                jb->tile_y = ty * TILE_SIZE;
                jb->tile_w = (tx == num_tiles_x - 1) ?
                             (RENDER_WIDTH - jb->tile_x) : TILE_SIZE;
                jb->tile_h = (ty == num_tiles_y - 1) ?
                             (RENDER_HEIGHT - jb->tile_y) : TILE_SIZE;
                thpool_add_work(g_threadpool, render_tile, jb);
            }
        }
        thpool_wait(g_threadpool);
    }
}

static void tile_clear_bins(void)
{
    if (g_thread_count > 1 &&
        total_tiles >= MIN_TILES_PER_THREAD * g_thread_count) {
        clear_range_job js[32];
        i32 mt = g_thread_count;
        i32 j, bpj;
        if (mt > 32) mt = 32;
        bpj = (total_tiles + mt - 1) / mt;

        for (j = 0; j < mt; j++) {
            js[j].start_idx = j * bpj;
            js[j].end_idx = (j + 1) * bpj;
            if (js[j].end_idx > total_tiles) js[j].end_idx = total_tiles;
            thpool_add_work(g_threadpool, tile_clear_bins_range, &js[j]);
        }
        thpool_wait(g_threadpool);
    }
    else {
        i32 i;
        for (i = 0; i < total_tiles; i++) {
            tile_bins[i].tri_count = 0;
            tile_bins[i].transparent_count = 0;
        }
    }
}

static void tile_clear_bins_range(void *arg)
{
    clear_range_job *j = (clear_range_job*)arg;
    i32 i;
    for (i = j->start_idx; i < j->end_idx; i++) {
        if (i < total_tiles) {
            tile_bins[i].tri_count = 0;
            tile_bins[i].transparent_count = 0;
        }
    }
}

static void clear_tile_range(void *arg)
{
    clear_job *j = (clear_job*)arg;
    u32 *fb32 = (u32*)fb_render;
    real *zbr = (real*)zbuf_render;
    u32 col = j->color;
    i32 ti, ty, tx, tlx, tly, tw, th, y, x, ro;

    ti = j->tile_idx;
    ty = ti / num_tiles_x;
    tx = ti % num_tiles_x;
    tlx = tx * TILE_SIZE;
    tly = ty * TILE_SIZE;
    tw = (tx == num_tiles_x - 1) ? (RENDER_WIDTH - tlx) : TILE_SIZE;
    th = (ty == num_tiles_y - 1) ? (RENDER_HEIGHT - tly) : TILE_SIZE;

    for (y = 0; y < th; y++) {
        ro = (tly + y) * RENDER_WIDTH + tlx;

        for (x = 0; x + 3 < tw; x += 4) {
            fb32[ro + x] = col;
            fb32[ro + x + 1] = col;
            fb32[ro + x + 2] = col;
            fb32[ro + x + 3] = col;
            zbr[ro + x] = 0.0f;
            zbr[ro + x + 1] = 0.0f;
            zbr[ro + x + 2] = 0.0f;
            zbr[ro + x + 3] = 0.0f;
        }
        while (x < tw) {
            fb32[ro + x] = col;
            zbr[ro + x] = 0.0f;
            x++;
        }
    }
}

static void render_finish(void)
{
    if (g_thread_count <= 1) {
        /* Single-threaded: render each tile that has any triangles */
        i32 i;
        for (i = 0; i < total_tiles; i++) {
            if (tile_bins[i].tri_count > 0 || tile_bins[i].transparent_count > 0) {
                tile_job jb;
                jb.tile_idx = i;
                jb.tile_x = (i % num_tiles_x) * TILE_SIZE;
                jb.tile_y = (i / num_tiles_x) * TILE_SIZE;
                jb.tile_w = ((i % num_tiles_x) == num_tiles_x - 1) ?
                            (RENDER_WIDTH - jb.tile_x) : TILE_SIZE;
                jb.tile_h = ((i / num_tiles_x) == num_tiles_y - 1) ?
                            (RENDER_HEIGHT - jb.tile_y) : TILE_SIZE;
                render_tile(&jb);
            }
        }
        tile_clear_bins();
    }
    else {
        tile_render_all();
        tile_clear_bins();
    }

    if (fw == RENDER_WIDTH && fh == RENDER_HEIGHT) {
        memcpy(fb, fb_render, RENDER_WIDTH * RENDER_HEIGHT * sizeof(u32));
    }
    else if (g_thread_count > 1) {
        i32 utx = (fw + TILE_SIZE - 1) / TILE_SIZE;
        i32 uty = (fh + TILE_SIZE - 1) / TILE_SIZE;
        i32 nj = utx * uty;

        if (upscale_job_count < nj) {
            if (upscale_jobs) free(upscale_jobs);
            upscale_jobs = (upscale_job*)malloc(nj * sizeof(upscale_job));
            upscale_job_count = nj;
        }

        {
            i32 ji = 0;
            i32 ty, tx;
            for (ty = 0; ty < fh; ty += TILE_SIZE) {
                i32 ety = ty + TILE_SIZE;
                if (ety > fh) ety = fh;

                for (tx = 0; tx < fw; tx += TILE_SIZE) {
                    i32 etx = tx + TILE_SIZE;
                    if (etx > fw) etx = fw;

                    upscale_jobs[ji].y_start = ty;
                    upscale_jobs[ji].y_end = ety;
                    upscale_jobs[ji].x_start = tx;
                    upscale_jobs[ji].x_end = etx;
                    upscale_jobs[ji].fb_dst = fb;
                    upscale_jobs[ji].fb_src = fb_render;
                    upscale_jobs[ji].dst_width = fw;
                    upscale_jobs[ji].dst_height = fh;
                    upscale_jobs[ji].src_width = RENDER_WIDTH;
                    upscale_jobs[ji].src_height = RENDER_HEIGHT;
                    thpool_add_work(g_threadpool, upscale_tile,
                        &upscale_jobs[ji]);
                    ji++;
                }
            }
            thpool_wait(g_threadpool);
        }
    }
    else {
        i32 y, x;
        for (y = 0; y < fh; y++) {
            i32 ry = (y * RENDER_HEIGHT) / fh;
            i32 rb = y * fw;
            i32 sb = ry * RENDER_WIDTH;

            for (x = 0; x < fw; x++) {
                i32 rx = (x * RENDER_WIDTH) / fw;
                fb[rb + x] = fb_render[sb + rx];
            }
        }
    }

    if (fb_front && fb_back) {
        u32 *tf = fb_front;
        real *tz = zbuf_front;
        fb_front = fb_back;
        zbuf_front = zbuf_back;
        fb_back = tf;
        zbuf_back = tz;
        fb = fb_back;
        zbuf = zbuf_back;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* RASTERIZER_H */