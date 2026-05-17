/*
 * rasterizer.h – Unified rasterizer (CPU)
 *
 * Uses material_definition.h for shading parameters.
 * Supports: Wireframe, Flat, Gouraud, and Phong.
 * Transparent triangles are automatically sorted and drawn back‑to‑front.
 */
#ifndef RASTERIZER_H
#define RASTERIZER_H

#include "common.h"
#include "tags/material.h"   /* shading_mode enum, struct material_definition */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
   Global render state
   ------------------------------------------------------------------------- */
/* Double‑buffered framebuffer and depth buffer */
static u32  *fb_front = NULL;   /* displayed buffer */
static real *zbuf_front = NULL;
static u32  *fb_back  = NULL;   /* rendered‑to buffer */
static real *zbuf_back = NULL;

static u32  *fb   = NULL;       /* currently active back buffer (points to fb_back or fb_front after swap) */
static real *zbuf = NULL;

static i32  fw, fh;

static mat4 vp;
static vec3 light_dir, light_col, ambient_col;
static vec3 cam_eye;

static vec3 fog_color;
static real fog_start;
static real fog_end;

/* -------------------------------------------------------------------------
    View frustum planes (extracted from VP matrix)
    Each plane: ax + by + cz + d = 0, where (a,b,c) is the normal
    Points inside frustum satisfy: a*x + b*y + c*z + d >= 0 for all planes
    ------------------------------------------------------------------------- */
typedef struct {
    vec3 normal;
    real d;
} frustum_plane;

static frustum_plane frustum[6];

/* Extract frustum planes from view-projection matrix */
static void extract_frustum_planes(void) {
    vec4 c0, c1, c2, c3;
    c0 = vp.columns[0];
    c1 = vp.columns[1];
    c2 = vp.columns[2];
    c3 = vp.columns[3];
    
    /* Left plane: c3 + c0 */
    frustum[0].normal = vec3_add(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    frustum[0].d = c3.components[3] + c0.components[3];
    
    /* Right plane: c3 - c0 */
    frustum[1].normal = vec3_sub(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    frustum[1].d = c3.components[3] - c0.components[3];
    
    /* Bottom plane: c3 + c1 */
    frustum[2].normal = vec3_add(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    frustum[2].d = c3.components[3] + c1.components[3];
    
    /* Top plane: c3 - c1 */
    frustum[3].normal = vec3_sub(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    frustum[3].d = c3.components[3] - c1.components[3];
    
    /* Near plane: c3 + c2 */
    frustum[4].normal = vec3_add(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    frustum[4].d = c3.components[3] + c2.components[3];
    
    /* Far plane: c3 - c2 */
    frustum[5].normal = vec3_sub(vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
                                   vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    frustum[5].d = c3.components[3] - c2.components[3];
    
    /* Normalize all planes */
    i32 i;
    for (i = 0; i < 6; i++) {
        real len = vec3_magnitude(frustum[i].normal);
        if (len > 0.0f) {
            frustum[i].normal = vec3_div_scalar(frustum[i].normal, len);
            frustum[i].d /= len;
        }
    }
}

/* Test if a triangle is completely outside the frustum */
static INLINE i32 triangle_outside_frustum(vec3 v0, vec3 v1, vec3 v2) {
    i32 i;
    for (i = 0; i < 6; i++) {
        i32 out0 = vec3_dot(frustum[i].normal, v0) + frustum[i].d < 0.0f;
        i32 out1 = vec3_dot(frustum[i].normal, v1) + frustum[i].d < 0.0f;
        i32 out2 = vec3_dot(frustum[i].normal, v2) + frustum[i].d < 0.0f;
        if (out0 && out1 && out2) {
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
    Sutherland-Hodgman clipping for triangles against a single plane
    Returns the number of output vertices (0-4 for a clipped triangle)
    ------------------------------------------------------------------------- */
#define MAX_CLIPPED_VERTS 12

typedef struct {
    vec3 v, n, l;  /* position, normal, local_pos */
} clip_vertex;

static INLINE i32 clip_triangle_plane(
    clip_vertex *in, i32 in_count,
    clip_vertex *out,
    i32 plane_idx,
    vec3 normal, real d)
{
    i32 out_count = 0;
    i32 i, prev;
    
    for (i = 0, prev = in_count - 1; i < in_count; prev = i, i++) {
        real dp1 = vec3_dot(normal, in[prev].v) + d;
        real dp2 = vec3_dot(normal, in[i].v) + d;
        i32 in1 = dp1 >= 0.0f;
        i32 in2 = dp2 >= 0.0f;
        
        if (in1 && in2) {
            /* Both inside - add second vertex */
            out[out_count++] = in[i];
        } else if (in1 && !in2) {
            /* Leaving - add intersection */
            real t = dp1 / (dp1 - dp2);
            clip_vertex iv;
            iv.v = vec3_add(in[prev].v, vec3_mul_scalar(vec3_sub(in[i].v, in[prev].v), t));
            iv.n = vec3_add(in[prev].n, vec3_mul_scalar(vec3_sub(in[i].n, in[prev].n), t));
            iv.l = vec3_add(in[prev].l, vec3_mul_scalar(vec3_sub(in[i].l, in[prev].l), t));
            out[out_count++] = iv;
        } else if (!in1 && in2) {
            /* Entering - add intersection and second vertex */
            real t = dp1 / (dp1 - dp2);
            clip_vertex iv;
            iv.v = vec3_add(in[prev].v, vec3_mul_scalar(vec3_sub(in[i].v, in[prev].v), t));
            iv.n = vec3_add(in[prev].n, vec3_mul_scalar(vec3_sub(in[i].n, in[prev].n), t));
            iv.l = vec3_add(in[prev].l, vec3_mul_scalar(vec3_sub(in[i].l, in[prev].l), t));
            out[out_count++] = iv;
            out[out_count++] = in[i];
        }
        /* Else both outside - add nothing */
    }
    
    return out_count;
}

/* Clip triangle against all frustum planes, returning 0 (culled) or the clipped tri count */
static INLINE i32 clip_triangle_full(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    vec3 *cv_out, vec3 *cn_out, vec3 *cl_out)
{
    clip_vertex in[MAX_CLIPPED_VERTS], out[MAX_CLIPPED_VERTS];
    i32 in_count, out_count, i;
    
    /* Initialize with input triangle */
    in[0].v = v0; in[0].n = n0; in[0].l = l0;
    in[1].v = v1; in[1].n = n1; in[1].l = l1;
    in[2].v = v2; in[2].n = n2; in[2].l = l2;
    in_count = 3;
    
    /* Clip against each frustum plane */
    i32 j;
    for (i = 0; i < 6; i++) {
        if (in_count < 3) return 0;  /* Nothing left after previous clip */
        out_count = clip_triangle_plane(in, in_count, out, i, frustum[i].normal, frustum[i].d);
        if (out_count < 3) return 0;  /* Degenerate result */
        /* Swap in/out for next iteration */
        for (j = 0; j < out_count; j++) in[j] = out[j];
        in_count = out_count;
    }
    
    /* Triangulate the clipped polygon and store results */
    /* For convex polygons (result of clipping), fan triangulate from first vertex */
    i32 tri_count = in_count - 2;
    for (i = 0; i < tri_count; i++) {
        cv_out[i*3 + 0] = in[0].v;   cn_out[i*3 + 0] = in[0].n;   cl_out[i*3 + 0] = in[0].l;
        cv_out[i*3 + 1] = in[i+1].v; cn_out[i*3 + 1] = in[i+1].n; cl_out[i*3 + 1] = in[i+1].l;
        cv_out[i*3 + 2] = in[i+2].v; cn_out[i*3 + 2] = in[i+2].n; cl_out[i*3 + 2] = in[i+2].l;
    }
    
    return tri_count;
}

/* -------------------------------------------------------------------------
   Transparent triangle sorting
   ------------------------------------------------------------------------- */
#define MAX_TRANSPARENT 4096

typedef struct transparent_tri {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    vec3 l0, l1, l2;
    const struct material_definition *mat;
    shading_mode mode;
    real  depth;
} transparent_tri;

static struct transparent_tri transparent_queue[MAX_TRANSPARENT];
static i32 transparent_count = 0;
static i32 in_transparent_pass = 0;

/* -------------------------------------------------------------------------
   Setter functions
   ------------------------------------------------------------------------- */
static void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    light_dir = vec3_normalize(dir);
    light_col = col;
    ambient_col = amb;
}

static void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    mat4 proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    mat4 view = mat4_lookat(eye, center, up);
    vp = mat4_mul(proj, view);
    cam_eye = eye;
    extract_frustum_planes();
}

static void render_set_fog(vec3 color, real start, real end) {
    fog_color = color;
    fog_start = start;
    fog_end = end;
}

/* -------------------------------------------------------------------------
   Colour packing
   ------------------------------------------------------------------------- */
static INLINE u32 pack_color(u8 r, u8 g, u8 b) {
    return (r << 16) | (g << 8) | b;
}

static INLINE real saturate(real x) {
    return real_clamp(x, 0.0f, 1.0f);
}

static INLINE u8 color_to_u8(real x) {
    x = saturate(x);
    return (u8)(x * 255.0f + 0.5f);
}

static INLINE u32 pack_color_real(real r, real g, real b) {
    return pack_color(color_to_u8(r), color_to_u8(g), color_to_u8(b));
}

/* - Transparent / opaque pixel write - */
/* Optimized: inlined color packing, reduced branching */
static INLINE void write_pixel(i32 idx, real iw, vec3 color, real alpha)
{
    if (iw <= 0.0f) return;

    if (alpha >= 1.0f) {
        if (iw > zbuf[idx]) {
            zbuf[idx] = iw;
            /* Direct packing - inlined saturate and color_to_u8 */
            real r = saturate(color.color.r);
            real g = saturate(color.color.g);
            real b = saturate(color.color.b);
            u8 ur = (u8)(r * 255.0f + 0.5f);
            u8 ug = (u8)(g * 255.0f + 0.5f);
            u8 ub = (u8)(b * 255.0f + 0.5f);
            fb[idx] = (u32)((ur << 16) | (ug << 8) | ub);
        }
    } else if (alpha > 0.0f) {
        if (iw < zbuf[idx]) return;
        vec3 col = color;
        vec3 bg = vec3_init_from_3(
            (real)((fb[idx] >> 16) & 0xFF) * (1.0f/255.0f),
            (real)((fb[idx] >> 8) & 0xFF) * (1.0f/255.0f),
            (real)(fb[idx] & 0xFF) * (1.0f/255.0f)
        );
        vec3 blended = vec3_add(vec3_mul_scalar(col, alpha), vec3_mul_scalar(bg, 1.0f - alpha));
        u8 ur = (u8)(saturate(blended.color.r) * 255.0f + 0.5f);
        u8 ug = (u8)(saturate(blended.color.g) * 255.0f + 0.5f);
        u8 ub = (u8)(saturate(blended.color.b) * 255.0f + 0.5f);
        fb[idx] = (u32)((ur << 16) | (ug << 8) | ub);
    }
}

/* -------------------------------------------------------------------------
   Framebuffer management
   ------------------------------------------------------------------------- */
static i32 render_init(i32 w, i32 h) {
    fw = w; fh = h;
    fb_front = (u32*)malloc(w * h * sizeof(u32));
    zbuf_front = (real*)malloc(w * h * sizeof(real));
    fb_back  = (u32*)malloc(w * h * sizeof(u32));
    zbuf_back = (real*)malloc(w * h * sizeof(real));
    if (!fb_front || !zbuf_front || !fb_back || !zbuf_back) {
        free(fb_front); free(zbuf_front);
        free(fb_back); free(zbuf_back);
        fb = NULL; zbuf = NULL;
        return -1;
    }
    /* Initially render into the back buffer */
    fb   = fb_back;
    zbuf = zbuf_back;
    return 0;
}

static void render_clear(u8 r, u8 g, u8 b) {
    u32 col = pack_color(r, g, b);
    i32 n = fw * fh;
    i32 i;
    /*TODO: for some reason memset is slower here.*/
    /*memset(fb, col2, n * sizeof(u32));*/
    /*memset(zbuf, 0, n * sizeof(real));*/
    for (i = 0; i < n; i++) {
        fb[i] = col;
        zbuf[i] = 0;
    }
}

static const u32* render_get_fb(void) { return fb_front; }

static void render_shutdown(void) {
    free(fb_front); free(zbuf_front);
    free(fb_back);  free(zbuf_back);
    fb_front = fb_back = fb = NULL;
    zbuf_front = zbuf_back = zbuf = NULL;
}

/* -------------------------------------------------------------------------
   Pixel write (wireframe helper)
   ------------------------------------------------------------------------- */
static INLINE void set_pix(i32 x, i32 y, u8 r, u8 g, u8 b) {
    if (x < 0 || x >= fw || y < 0 || y >= fh) return;
    fb[y * fw + x] = pack_color(r, g, b);
}

/* -------------------------------------------------------------------------
   Projection helpers
   ------------------------------------------------------------------------- */
static INLINE void project(vec3 w, i32 *sx, i32 *sy, real *iw) {
    vec4 c = mat4_mul_vec4(vp, vec4_init_from_4(w.position.x, w.position.y, w.position.z, 1.0f));
    if (c.rotation.w <= 1e-6f) { *sx = -1; *sy = -1; *iw = 0; return; }
    *iw = 1.0f / c.rotation.w;
    real ndcx = c.position.x * (*iw), ndcy = c.position.y * (*iw);
    *sx = (i32)((ndcx * 0.5f + 0.5f) * fw);
    *sy = (i32)((1.0f - (ndcy * 0.5f + 0.5f)) * fh);
}

static INLINE void swapi(i32 *a, i32 *b) { i32 t = *a; *a = *b; *b = t; }
static INLINE void swapr(real *a, real *b) { real t = *a; *a = *b; *b = t; }
static INLINE void swapv(vec3 *a, vec3 *b) { vec3 t = *a; *a = *b; *b = t; }
static INLINE i32 raster_round(real x) { return (i32)real_floor(x + 0.5f); }

/* Global render time (animates emissive pulse) */
static real render_time = 0.0f;

/* Call this once per frame with the elapsed time in seconds */
static void render_set_time(real t) { render_time = t; }

/* - The unified shading function - */
/* Optimized with precomputed material flags for branch reduction */
static INLINE vec3 shade_surface(vec3 normal, vec3 world_pos, vec3 local_pos,
                           const struct material_definition *mat)
{
    vec3 N = normal;
    real ndotl, ndotv = 0.0f;
    vec3 V;
    
    enum32 effects = mat->effects;
    
    /* Procedural bump mapping */
    if (effects & EFFECT_BUMP) {
        real fx = world_pos.position.x * mat->bump_frequency;
        real fy = world_pos.position.y * mat->bump_frequency;
        real fz = world_pos.position.z * mat->bump_frequency;
        real t  = render_time * mat->bump_speed;
        N.position.x += real_sin(fy + fz + t) * mat->bump_amplitude;
        N.position.y += real_sin(fz + fx + t) * mat->bump_amplitude;
        N.position.z += real_sin(fx + fy + t) * mat->bump_amplitude;
        N = vec3_normalize(N);
    }

    ndotl = saturate(vec3_dot(N, light_dir));

    /* Check if we need view vector (for rim, fresnel, specular) */
    if (effects & (EFFECT_MINNAERT | EFFECT_OREN_NAYAR | EFFECT_RIM | EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_IRIDESCENCE | EFFECT_FRINGE)) {
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
        diffuse_term = real_pow(ndotl, mat->minnaert_k) * real_pow(ndotv, 1.0f - mat->minnaert_k);
    }

    if (effects & EFFECT_OREN_NAYAR) {
        real sigma = mat->oren_nayar_sigma;
        real sigma_sq = sigma * sigma;
        real a = 1.0f - 0.5f * sigma_sq / (sigma_sq + 0.33f);
        real b = 0.45f * sigma_sq / (sigma_sq + 0.09f);
        real cos_phi_diff = 0.0f;
        real sin_alpha = 0.0f, tan_beta = 0.0f;

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
                } else {
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
        vec3 gooch = vec3_add(vec3_mul_scalar(mat->gooch_cool, 1.0f - t),
                              vec3_mul_scalar(mat->gooch_warm, t));
        color = vec3_mul(color, gooch);
    } else {
        color = vec3_mul(color, mat->color);
    }

    if (effects & EFFECT_BACK_GLOW) {
        vec3 light_neg = vec3_mul_scalar(light_dir, -1.0f);
        real ndotl_neg = vec3_dot(N, light_neg);
        color = vec3_add(color, vec3_mul_scalar(mat->back_glow_color, ndotl_neg < 0.0f ? 0.0f : ndotl_neg));
    }

    if (effects & EFFECT_RIM) {
        real rim = real_pow(1.0f - ndotv, mat->rim_exponent);
        color = vec3_add(color, vec3_mul_scalar(mat->rim_color, rim));
    }

    if (effects & EFFECT_FRESNEL) {
        real fresnel = real_pow(1.0f - ndotv, mat->fresnel_exponent);
        color = vec3_add(vec3_mul_scalar(color, 1.0f - fresnel),
                         vec3_mul_scalar(mat->fresnel_color, fresnel));
    }

    if (effects & EFFECT_EMISSIVE) {
        vec3 emissive = mat->emissive_color;
        if (effects & EFFECT_EMISSIVE_PULSE) {
            real pulse = 1.0f + mat->emissive_pulse_amplitude *
                         real_sin(render_time * mat->emissive_pulse_frequency + mat->emissive_pulse_phase);
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
        real luma = color.color.r * 0.299f + color.color.g * 0.587f + color.color.b * 0.114f;
        color.color.r = luma + (color.color.r - luma) * mat->saturation;
        color.color.g = luma + (color.color.g - luma) * mat->saturation;
        color.color.b = luma + (color.color.b - luma) * mat->saturation;
    }

    if (effects & EFFECT_IRIDESCENCE) {
        real angle = ndotv * 2.0f * VECTORS_PI;
        real c = real_cos(angle);
        real s = real_sin(angle);
        real rot[9] = {
            0.299f + 0.701f * c + 0.168f * s,  0.587f - 0.587f * c + 0.330f * s,  0.114f - 0.114f * c - 0.497f * s,
            0.299f - 0.299f * c - 0.328f * s,  0.587f + 0.413f * c + 0.035f * s,  0.114f - 0.114f * c + 0.292f * s,
            0.299f - 0.300f * c + 1.250f * s,  0.587f - 0.588f * c - 1.050f * s,  0.114f + 0.886f * c - 0.203f * s
        };
        real r = color.color.r * rot[0] + color.color.g * rot[1] + color.color.b * rot[2];
        real g = color.color.r * rot[3] + color.color.g * rot[4] + color.color.b * rot[5];
        real b = color.color.r * rot[6] + color.color.g * rot[7] + color.color.b * rot[8];
        real is = mat->iridescence_strength;
        color.color.r = r * is + color.color.r * (1.0f - is);
        color.color.g = g * is + color.color.g * (1.0f - is);
        color.color.b = b * is + color.color.b * (1.0f - is);
    }

    color = vec3_mul(color, mat->tint);

    if (effects & EFFECT_GLITCH) {
        u32 x = (u32)(render_time * 60.0f);
        vec3 wp_q = vec3_floor(vec3_mul_scalar(world_pos, 4096.0f));
        x ^= (u32)wp_q.components[0]; x = x * 1664525u + 1013904223u;
        x ^= (u32)wp_q.components[1]; x = x * 1664525u + 1013904223u;
        x ^= (u32)wp_q.components[2]; x = x * 1664525u + 1013904223u;
        real offset = ((real)x * (1.0f / 4294967296.0f) - 0.5f) * mat->glitch_intensity;
        color.color.r += offset;
        color.color.g += offset * 0.7f;
        color.color.b -= offset;
    }

    if (effects & EFFECT_ROUGHNESS) {
        u32 x = 2166136261u;
        vec3 q = vec3_floor(vec3_mul_scalar(local_pos, 256.0f));
        x ^= (u32)q.components[0]; x *= 16777619u;
        x ^= (u32)q.components[1]; x *= 16777619u;
        x ^= (u32)q.components[2]; x *= 16777619u;
        real offset = ((real)x * (1.0f / 4294967296.0f) - 0.5f) * mat->roughness;
        color.color.r += offset;
        color.color.g += offset;
        color.color.b += offset;
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
        color = vec3_add(vec3_mul_scalar(color, 1.0f - t), vec3_mul_scalar(fog_color, t));
    }

    color.color.r = saturate(color.color.r);
    color.color.g = saturate(color.color.g);
    color.color.b = saturate(color.color.b);
    return color;
}

/* -------------------------------------------------------------------------
   Wireframe rasterization
   ------------------------------------------------------------------------- */
/* Bresenham line drawing with z-buffer */
static void draw_line_z(i32 x0, i32 y0, real iw0, i32 x1, i32 y1, real iw1, vec3 color, real alpha)
{
    i32 dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    i32 dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy, e2;

    real steps = (real)(dx > dy ? dx : dy);
    if (steps == 0) steps = 1;
    real diw = (iw1 - iw0) / steps;
    real iw = iw0;

    while (1) {
        if (x0 >= 0 && x0 < fw && y0 >= 0 && y0 < fh) {
            i32 idx = y0 * fw + x0;
            write_pixel(idx, iw, color, alpha);
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; iw += diw; }
        if (e2 <= dx) { err += dx; y0 += sy; iw += diw; }
    }
}

static void raster_triangle_wireframe(vec3 v0, vec3 v1, vec3 v2, vec3 edge_color, real alpha)
{
    i32 x0, y0, x1, y1, x2, y2;
    real iw0, iw1, iw2;

    project(v0, &x0, &y0, &iw0);
    project(v1, &x1, &y1, &iw1);
    project(v2, &x2, &y2, &iw2);

    /* All vertices must be in front of the camera */
    if (x0 < 0 || x1 < 0 || x2 < 0) return;

    /* Bresenham line drawing for each edge with z */
    draw_line_z(x0, y0, iw0, x1, y1, iw1, edge_color, alpha);
    draw_line_z(x1, y1, iw1, x2, y2, iw2, edge_color, alpha);
    draw_line_z(x2, y2, iw2, x0, y0, iw0, edge_color, alpha);
}

/* -------------------------------------------------------------------------
   Per‑face/triangle rasterization
   ------------------------------------------------------------------------- */
static void raster_triangle_flat(vec3 v0, vec3 v1, vec3 v2, vec3 color, 
                                const struct material_definition *mat) {

    i32 x0,y0,x1,y1,x2,y2; real iw0,iw1,iw2;
    project(v0,&x0,&y0,&iw0); project(v1,&x1,&y1,&iw1); project(v2,&x2,&y2,&iw2);

    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);}
    if(y1>y2){swapi(&y1,&y2);swapi(&x1,&x2);swapr(&iw1,&iw2);}
    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);}

    real dx0=0,diw0=0, dx1=0,diw1=0, dx2=0,diw2=0;
    if(y1>y0){ real idy=1.0f/(y1-y0); dx0=(x1-x0)*idy; diw0=(iw1-iw0)*idy; }
    if(y2>y1){ real idy=1.0f/(y2-y1); dx1=(x2-x1)*idy; diw1=(iw2-iw1)*idy; }
    if(y2>y0){ real idy=1.0f/(y2-y0); dx2=(x2-x0)*idy; diw2=(iw2-iw0)*idy; }

    /* Half-open coverage keeps shared mesh edges from being blended twice. */
    i32 y_start=y0<0?0:y0, y_end=y2>fh?fh:y2;
    i32 y,sx,ex,x; real siw,eiw,iw_step,iw;
    for(y=y_start;y<y_end;y++){
        if(y<y1){
            real t=(real)(y-y0);
            sx=x0+raster_round(dx0*t); ex=x0+raster_round(dx2*t);
            siw=iw0+diw0*t; eiw=iw0+diw2*t;
        } else {
            real t=(real)(y-y1);
            sx=x1+raster_round(dx1*t); ex=x0+raster_round(dx2*(y-y0));
            siw=iw1+diw1*t; eiw=iw0+diw2*(y-y0);
        }
        if(sx>ex){swapi(&sx,&ex);swapr(&siw,&eiw);}
        if(sx<0)sx=0; if(ex>fw)ex=fw;
        if(ex<=sx) continue;
        iw_step=(ex>sx)?(eiw-siw)/(ex-sx):0;
        
        /* Fully inlined opaque write for maximum speed (common case) */
        iw=siw;
        if (mat->alpha >= 1.0f) {
            for(x=sx;x<ex;x++){
                i32 idx=y*fw+x;
                if(iw > zbuf[idx]){
                    zbuf[idx] = iw;
                    fb[idx] = pack_color_real(color.color.r, color.color.g, color.color.b);
                }
                iw+=iw_step;
            }
        } else {
            iw=siw;
            for(x=sx;x<ex;x++){
                i32 idx=y*fw+x;
                write_pixel(idx, iw, color, mat->alpha);
                iw+=iw_step;
            }
        }
    }
}

/* -------------------------------------------------------------------------
   Per‑vertex rasterization with interpolation
   ------------------------------------------------------------------------- */
static void raster_triangle_gouraud(vec3 v0, vec3 v1, vec3 v2,
                                     vec3 c0, vec3 c1, vec3 c2, 
                                     const struct material_definition *mat) {
                                        
    i32 x0,y0,x1,y1,x2,y2; real iw0,iw1,iw2;
    project(v0,&x0,&y0,&iw0); project(v1,&x1,&y1,&iw1); project(v2,&x2,&y2,&iw2);

    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&c0,&c1);}
    if(y1>y2){swapi(&y1,&y2);swapi(&x1,&x2);swapr(&iw1,&iw2);swapv(&c1,&c2);}
    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&c0,&c1);}

    real dx0=0,diw0=0, dx1=0,diw1=0, dx2=0,diw2=0;
    vec3 dc0,dc1,dc2; dc0=vec3_init_from_3(0,0,0); dc1=vec3_init_from_3(0,0,0); dc2=vec3_init_from_3(0,0,0);
    if(y1>y0){ real idy=1.0f/(y1-y0); dx0=(x1-x0)*idy; diw0=(iw1-iw0)*idy;
        dc0.color.r=(c1.color.r-c0.color.r)*idy; dc0.color.g=(c1.color.g-c0.color.g)*idy; dc0.color.b=(c1.color.b-c0.color.b)*idy; }
    if(y2>y1){ real idy=1.0f/(y2-y1); dx1=(x2-x1)*idy; diw1=(iw2-iw1)*idy;
        dc1.color.r=(c2.color.r-c1.color.r)*idy; dc1.color.g=(c2.color.g-c1.color.g)*idy; dc1.color.b=(c2.color.b-c1.color.b)*idy; }
    if(y2>y0){ real idy=1.0f/(y2-y0); dx2=(x2-x0)*idy; diw2=(iw2-iw0)*idy;
        dc2.color.r=(c2.color.r-c0.color.r)*idy; dc2.color.g=(c2.color.g-c0.color.g)*idy; dc2.color.b=(c2.color.b-c0.color.b)*idy; }

    i32 y_start=y0<0?0:y0, y_end=y2>fh?fh:y2;
    i32 y,sx,ex,x; real siw,eiw,iw_step,iw;
    vec3 cs,ce,col,dc_step;
    for(y=y_start;y<y_end;y++){
        if(y<y1){ real t=(real)(y-y0); sx=x0+raster_round(dx0*t); ex=x0+raster_round(dx2*t); siw=iw0+diw0*t; eiw=iw0+diw2*t;
            cs.color.r=c0.color.r+dc0.color.r*t; cs.color.g=c0.color.g+dc0.color.g*t; cs.color.b=c0.color.b+dc0.color.b*t;
            ce.color.r=c0.color.r+dc2.color.r*t; ce.color.g=c0.color.g+dc2.color.g*t; ce.color.b=c0.color.b+dc2.color.b*t; }
        else { real t=(real)(y-y1); sx=x1+raster_round(dx1*t); ex=x0+raster_round(dx2*(y-y0)); siw=iw1+diw1*t; eiw=iw0+diw2*(y-y0);
            cs.color.r=c1.color.r+dc1.color.r*t; cs.color.g=c1.color.g+dc1.color.g*t; cs.color.b=c1.color.b+dc1.color.b*t;
            ce.color.r=c0.color.r+dc2.color.r*(y-y0); ce.color.g=c0.color.g+dc2.color.g*(y-y0); ce.color.b=c0.color.b+dc2.color.b*(y-y0); }
        if(sx>ex){swapi(&sx,&ex);swapr(&siw,&eiw);swapv(&cs,&ce);}
        if(sx<0)sx=0; if(ex>fw)ex=fw;
        if(ex<=sx) continue;
        iw_step=(ex>sx)?(eiw-siw)/(ex-sx):0;
        dc_step.color.r=(ex>sx)?(ce.color.r-cs.color.r)/(ex-sx):0;
        dc_step.color.g=(ex>sx)?(ce.color.g-cs.color.g)/(ex-sx):0;
        dc_step.color.b=(ex>sx)?(ce.color.b-cs.color.b)/(ex-sx):0;
        
        /* Fully inlined opaque write for maximum speed (common case) */
        if (mat->alpha >= 1.0f) {
            iw=siw; col=cs;
            for(x=sx;x<ex;x++){
                i32 idx=y*fw+x;
                if(iw > zbuf[idx]){
                    zbuf[idx] = iw;
                    fb[idx] = pack_color_real(col.color.r, col.color.g, col.color.b);
                }
                iw+=iw_step; col.color.r+=dc_step.color.r; col.color.g+=dc_step.color.g; col.color.b+=dc_step.color.b;
            }
        } else {
            iw=siw; col=cs;
            for(x=sx;x<ex;x++){
                i32 idx=y*fw+x;
                write_pixel(idx, iw, col, mat->alpha);
                iw+=iw_step; col.color.r+=dc_step.color.r; col.color.g+=dc_step.color.g; col.color.b+=dc_step.color.b;
            }
        }
    }
}

/* -------------------------------------------------------------------------
   Per‑pixel rasterization
   ------------------------------------------------------------------------- */
static void raster_triangle_phong(
        vec3 v0, vec3 v1, vec3 v2,
        vec3 n0, vec3 n1, vec3 n2,
        vec3 l0, vec3 l1, vec3 l2,
        const struct material_definition *mat) {

    i32 x0,y0,x1,y1,x2,y2; real iw0,iw1,iw2;
    project(v0,&x0,&y0,&iw0); project(v1,&x1,&y1,&iw1); project(v2,&x2,&y2,&iw2);

    vec3 n0w=vec3_mul_scalar(n0,iw0), n1w=vec3_mul_scalar(n1,iw1), n2w=vec3_mul_scalar(n2,iw2);
    vec3 wp0w=vec3_mul_scalar(v0,iw0), wp1w=vec3_mul_scalar(v1,iw1), wp2w=vec3_mul_scalar(v2,iw2);
    vec3 lp0w=vec3_mul_scalar(l0,iw0), lp1w=vec3_mul_scalar(l1,iw1), lp2w=vec3_mul_scalar(l2,iw2);

    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&n0w,&n1w);swapv(&wp0w,&wp1w);swapv(&lp0w,&lp1w);}
    if(y1>y2){swapi(&y1,&y2);swapi(&x1,&x2);swapr(&iw1,&iw2);swapv(&n1w,&n2w);swapv(&wp1w,&wp2w);swapv(&lp1w,&lp2w);}
    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&n0w,&n1w);swapv(&wp0w,&wp1w);swapv(&lp0w,&lp1w);}

    real dx0=0,diw0=0, dx1=0,diw1=0, dx2=0,diw2=0;
    vec3 dnw0,dnw1,dnw2, dwpw0,dwpw1,dwpw2, dlpw0,dlpw1,dlpw2;
    dnw0=vec3_init_from_3(0,0,0); dnw1=vec3_init_from_3(0,0,0); dnw2=vec3_init_from_3(0,0,0);
    dwpw0=vec3_init_from_3(0,0,0); dwpw1=vec3_init_from_3(0,0,0); dwpw2=vec3_init_from_3(0,0,0);
    dlpw0=vec3_init_from_3(0,0,0); dlpw1=vec3_init_from_3(0,0,0); dlpw2=vec3_init_from_3(0,0,0);

    if(y1>y0){ real idy=1.0f/(y1-y0); dx0=(x1-x0)*idy; diw0=(iw1-iw0)*idy;
        dnw0.position.x=(n1w.position.x-n0w.position.x)*idy; dnw0.position.y=(n1w.position.y-n0w.position.y)*idy; dnw0.position.z=(n1w.position.z-n0w.position.z)*idy;
        dwpw0.position.x=(wp1w.position.x-wp0w.position.x)*idy; dwpw0.position.y=(wp1w.position.y-wp0w.position.y)*idy; dwpw0.position.z=(wp1w.position.z-wp0w.position.z)*idy;
        dlpw0.position.x=(lp1w.position.x-lp0w.position.x)*idy; dlpw0.position.y=(lp1w.position.y-lp0w.position.y)*idy; dlpw0.position.z=(lp1w.position.z-lp0w.position.z)*idy; }
    if(y2>y1){ real idy=1.0f/(y2-y1); dx1=(x2-x1)*idy; diw1=(iw2-iw1)*idy;
        dnw1.position.x=(n2w.position.x-n1w.position.x)*idy; dnw1.position.y=(n2w.position.y-n1w.position.y)*idy; dnw1.position.z=(n2w.position.z-n1w.position.z)*idy;
        dwpw1.position.x=(wp2w.position.x-wp1w.position.x)*idy; dwpw1.position.y=(wp2w.position.y-wp1w.position.y)*idy; dwpw1.position.z=(wp2w.position.z-wp1w.position.z)*idy;
        dlpw1.position.x=(lp2w.position.x-lp1w.position.x)*idy; dlpw1.position.y=(lp2w.position.y-lp1w.position.y)*idy; dlpw1.position.z=(lp2w.position.z-lp1w.position.z)*idy; }
    if(y2>y0){ real idy=1.0f/(y2-y0); dx2=(x2-x0)*idy; diw2=(iw2-iw0)*idy;
        dnw2.position.x=(n2w.position.x-n0w.position.x)*idy; dnw2.position.y=(n2w.position.y-n0w.position.y)*idy; dnw2.position.z=(n2w.position.z-n0w.position.z)*idy;
        dwpw2.position.x=(wp2w.position.x-wp0w.position.x)*idy; dwpw2.position.y=(wp2w.position.y-wp0w.position.y)*idy; dwpw2.position.z=(wp2w.position.z-wp0w.position.z)*idy;
        dlpw2.position.x=(lp2w.position.x-lp0w.position.x)*idy; dlpw2.position.y=(lp2w.position.y-lp0w.position.y)*idy; dlpw2.position.z=(lp2w.position.z-lp0w.position.z)*idy; }

    i32 y_start=y0<0?0:y0, y_end=y2>fh?fh:y2;
    i32 y,sx,ex,x; real siw,eiw,iw_step,iw;
    vec3 nws,nwe,nw_val,dnw_step;
    vec3 wps,wpe,wp_val,dwp_step;
    vec3 lps,lpe,lp_val,dlp_step;

    for(y=y_start;y<y_end;y++){
        if(y<y1){ real t=(real)(y-y0); sx=x0+raster_round(dx0*t); ex=x0+raster_round(dx2*t); siw=iw0+diw0*t; eiw=iw0+diw2*t;
            nws.position.x=n0w.position.x+dnw0.position.x*t; nws.position.y=n0w.position.y+dnw0.position.y*t; nws.position.z=n0w.position.z+dnw0.position.z*t;
            nwe.position.x=n0w.position.x+dnw2.position.x*t; nwe.position.y=n0w.position.y+dnw2.position.y*t; nwe.position.z=n0w.position.z+dnw2.position.z*t;
            wps.position.x=wp0w.position.x+dwpw0.position.x*t; wps.position.y=wp0w.position.y+dwpw0.position.y*t; wps.position.z=wp0w.position.z+dwpw0.position.z*t;
            wpe.position.x=wp0w.position.x+dwpw2.position.x*t; wpe.position.y=wp0w.position.y+dwpw2.position.y*t; wpe.position.z=wp0w.position.z+dwpw2.position.z*t;
            lps.position.x=lp0w.position.x+dlpw0.position.x*t; lps.position.y=lp0w.position.y+dlpw0.position.y*t; lps.position.z=lp0w.position.z+dlpw0.position.z*t;
            lpe.position.x=lp0w.position.x+dlpw2.position.x*t; lpe.position.y=lp0w.position.y+dlpw2.position.y*t; lpe.position.z=lp0w.position.z+dlpw2.position.z*t; }
        else { real t=(real)(y-y1); sx=x1+raster_round(dx1*t); ex=x0+raster_round(dx2*(y-y0)); siw=iw1+diw1*t; eiw=iw0+diw2*(y-y0);
            nws.position.x=n1w.position.x+dnw1.position.x*t; nws.position.y=n1w.position.y+dnw1.position.y*t; nws.position.z=n1w.position.z+dnw1.position.z*t;
            nwe.position.x=n0w.position.x+dnw2.position.x*(y-y0); nwe.position.y=n0w.position.y+dnw2.position.y*(y-y0); nwe.position.z=n0w.position.z+dnw2.position.z*(y-y0);
            wps.position.x=wp1w.position.x+dwpw1.position.x*t; wps.position.y=wp1w.position.y+dwpw1.position.y*t; wps.position.z=wp1w.position.z+dwpw1.position.z*t;
            wpe.position.x=wp0w.position.x+dwpw2.position.x*(y-y0); wpe.position.y=wp0w.position.y+dwpw2.position.y*(y-y0); wpe.position.z=wp0w.position.z+dwpw2.position.z*(y-y0);
            lps.position.x=lp1w.position.x+dlpw1.position.x*t; lps.position.y=lp1w.position.y+dlpw1.position.y*t; lps.position.z=lp1w.position.z+dlpw1.position.z*t;
            lpe.position.x=lp0w.position.x+dlpw2.position.x*(y-y0); lpe.position.y=lp0w.position.y+dlpw2.position.y*(y-y0); lpe.position.z=lp0w.position.z+dlpw2.position.z*(y-y0); }
        if(sx>ex){swapi(&sx,&ex);swapr(&siw,&eiw);swapv(&nws,&nwe);swapv(&wps,&wpe);swapv(&lps,&lpe);}
        if(sx<0)sx=0; if(ex>fw)ex=fw;
        if(ex<=sx) continue;
        iw_step=(ex>sx)?(eiw-siw)/(ex-sx):0;
        if(ex>sx){
            dnw_step.position.x=(nwe.position.x-nws.position.x)/(ex-sx); dnw_step.position.y=(nwe.position.y-nws.position.y)/(ex-sx); dnw_step.position.z=(nwe.position.z-nws.position.z)/(ex-sx);
            dwp_step.position.x=(wpe.position.x-wps.position.x)/(ex-sx); dwp_step.position.y=(wpe.position.y-wps.position.y)/(ex-sx); dwp_step.position.z=(wpe.position.z-wps.position.z)/(ex-sx);
            dlp_step.position.x=(lpe.position.x-lps.position.x)/(ex-sx); dlp_step.position.y=(lpe.position.y-lps.position.y)/(ex-sx); dlp_step.position.z=(lpe.position.z-lps.position.z)/(ex-sx);
        } else { dnw_step=vec3_init_from_3(0,0,0); dwp_step=vec3_init_from_3(0,0,0); dlp_step=vec3_init_from_3(0,0,0); }
        iw=siw; nw_val=nws; wp_val=wps; lp_val=lps;
        for(x=sx;x<ex;x++){
            i32 idx=y*fw+x;
            if(iw > 0.0f && (iw>zbuf[idx] || (mat->alpha < 1.0f && iw >= zbuf[idx]))){
                real inv_w = 1.0f / iw;
                vec3 normal, world_pos, local_pos;
                normal.position.x = nw_val.position.x * inv_w;
                normal.position.y = nw_val.position.y * inv_w;
                normal.position.z = nw_val.position.z * inv_w;
                world_pos.position.x = wp_val.position.x * inv_w;
                world_pos.position.y = wp_val.position.y * inv_w;
                world_pos.position.z = wp_val.position.z * inv_w;
                local_pos.position.x = lp_val.position.x * inv_w;
                local_pos.position.y = lp_val.position.y * inv_w;
                local_pos.position.z = lp_val.position.z * inv_w;

                vec3 color = shade_surface(normal, world_pos, local_pos, mat);
                write_pixel(idx, iw, color, mat->alpha);
            }
            iw+=iw_step;
            nw_val.position.x+=dnw_step.position.x; nw_val.position.y+=dnw_step.position.y; nw_val.position.z+=dnw_step.position.z;
            wp_val.position.x+=dwp_step.position.x; wp_val.position.y+=dwp_step.position.y; wp_val.position.z+=dwp_step.position.z;
            lp_val.position.x+=dlp_step.position.x; lp_val.position.y+=dlp_step.position.y; lp_val.position.z+=dlp_step.position.z;
        }
    }
}

/* -------------------------------------------------------------------------
     Internal triangle drawing (no clipping)
     ------------------------------------------------------------------------- */
static void draw_triangle_internal(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    vec3 face_normal, face_center, local_center, color;
    vec3 c0, c1, c2;

    switch (mat->mode) {
        case SHADE_WIREFRAME:
            face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
            face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
            local_center = vec3_mul_scalar(vec3_add(vec3_add(l0, l1), l2), 1.0f / 3.0f);
            color = shade_surface(face_normal, face_center, local_center, mat);
            raster_triangle_wireframe(v0, v1, v2, color, mat->alpha);
            return;
        case SHADE_FLAT:
            face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
            face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
            local_center = vec3_mul_scalar(vec3_add(vec3_add(l0, l1), l2), 1.0f / 3.0f);
            color = shade_surface(face_normal, face_center, local_center, mat);
            raster_triangle_flat(v0, v1, v2, color, mat);
            return;
        case SHADE_GOURAUD:
            c0 = shade_surface(n0, v0, l0, mat);
            c1 = shade_surface(n1, v1, l1, mat);
            c2 = shade_surface(n2, v2, l2, mat);
            raster_triangle_gouraud(v0, v1, v2, c0, c1, c2, mat);
            return;
        case SHADE_PHONG:
            raster_triangle_phong(v0, v1, v2, n0, n1, n2, l0, l1, l2, mat);
            return;
        default:
            face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
            face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
            local_center = vec3_mul_scalar(vec3_add(vec3_add(l0, l1), l2), 1.0f / 3.0f);
            color = shade_surface(face_normal, face_center, local_center, mat);
            raster_triangle_flat(v0, v1, v2, color, mat);
            return;
    }
}

/* -------------------------------------------------------------------------
    Main triangle dispatch
    ------------------------------------------------------------------------- */
static void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat) {
    
    /* Backface culling */
    if (!mat->double_sided) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 view_dir = vec3_sub(cam_eye, face_center);
        if (vec3_dot(face_normal, view_dir) <= 0.0f) return;
    }

    /* Frustum culling - skip triangles completely outside view volume */
    if (triangle_outside_frustum(v0, v1, v2)) return;
     
    /* Check if triangle intersects any frustum plane for clipping */
    i32 needs_clip = 0;
    real min_dist_sq = 100.0f * 100.0f;
    real d0_sq = vec3_dot(vec3_sub(v0, cam_eye), vec3_sub(v0, cam_eye));
    real d1_sq = vec3_dot(vec3_sub(v1, cam_eye), vec3_sub(v1, cam_eye));
    real d2_sq = vec3_dot(vec3_sub(v2, cam_eye), vec3_sub(v2, cam_eye));

    if (d0_sq < min_dist_sq || d1_sq < min_dist_sq || d2_sq < min_dist_sq) {
        needs_clip = 1;
    }

    /* - Enqueue transparent triangles for later back‑to‑front sorting - */
    if (mat->alpha < 1.0f && !in_transparent_pass && transparent_count < MAX_TRANSPARENT) {
        /* Use squared distances for sorting to avoid sqrt */
        real d0 = vec3_dot(vec3_sub(v0, cam_eye), vec3_sub(v0, cam_eye));
        real d1 = vec3_dot(vec3_sub(v1, cam_eye), vec3_sub(v1, cam_eye));
        real d2 = vec3_dot(vec3_sub(v2, cam_eye), vec3_sub(v2, cam_eye));
        transparent_queue[transparent_count].v0   = v0;
        transparent_queue[transparent_count].v1   = v1;
        transparent_queue[transparent_count].v2   = v2;
        transparent_queue[transparent_count].n0   = n0;
        transparent_queue[transparent_count].n1   = n1;
        transparent_queue[transparent_count].n2   = n2;
        transparent_queue[transparent_count].l0   = l0;
        transparent_queue[transparent_count].l1   = l1;
        transparent_queue[transparent_count].l2   = l2;
        transparent_queue[transparent_count].mat  = mat;
        transparent_queue[transparent_count].mode = mat->mode;
        transparent_queue[transparent_count].depth = (d0 + d1 + d2) * 0.33333333f;
        transparent_count++;
        return;   /* deferred */
    }

    /* Clip triangle against all frustum planes */
    if (needs_clip) {
        vec3 cv[MAX_CLIPPED_VERTS * 3];
        vec3 cn[MAX_CLIPPED_VERTS * 3];
        vec3 cl[MAX_CLIPPED_VERTS * 3];
        i32 tri_count = clip_triangle_full(v0, v1, v2, n0, n1, n2, l0, l1, l2, cv, cn, cl);
        if (tri_count == 0) return;

        /* Draw each clipped triangle */
        i32 t;
        for (t = 0; t < tri_count; t++) {
            vec3 cv0 = cv[t*3 + 0], cv1 = cv[t*3 + 1], cv2 = cv[t*3 + 2];
            vec3 cn0 = cn[t*3 + 0], cn1 = cn[t*3 + 1], cn2 = cn[t*3 + 2];
            vec3 cl0_t = cl[t*3 + 0], cl1_t = cl[t*3 + 1], cl2_t = cl[t*3 + 2];

            draw_triangle_internal(cv0, cv1, cv2, cn0, cn1, cn2, cl0_t, cl1_t, cl2_t, mat);
        }
        return;
    }

    draw_triangle_internal(v0, v1, v2, n0, n1, n2, l0, l1, l2, mat);
}

/* -------------------------------------------------------------------------
    Finish frame: sort and draw all transparent triangles (called after all
    draw_triangle_shaded calls, before reading the framebuffer).
    ------------------------------------------------------------------------- */
static void render_finish(void)
{
    if (transparent_count > 0) {
        /* Simple insertion sort – back to front (largest depth first) */
        i32 i, j;
        for (i = 1; i < transparent_count; i++) {
            j = i;
            while (j > 0 && transparent_queue[j - 1].depth < transparent_queue[j].depth) {
                struct transparent_tri tmp;
                tmp = transparent_queue[j - 1];
                transparent_queue[j - 1] = transparent_queue[j];
                transparent_queue[j] = tmp;
                j--;
            }
        }

        in_transparent_pass = 1;
        for (i = 0; i < transparent_count; i++) {
            draw_triangle_shaded(
                transparent_queue[i].v0, transparent_queue[i].v1, transparent_queue[i].v2,
                transparent_queue[i].n0, transparent_queue[i].n1, transparent_queue[i].n2,
                transparent_queue[i].l0, transparent_queue[i].l1, transparent_queue[i].l2,
                transparent_queue[i].mat);
        }

        transparent_count = 0;
        in_transparent_pass = 0;
    }

    /* Swap buffers – present the back buffer, make the old front buffer the new back */
    u32  *tmp_fb   = fb_front;
    real *tmp_zbuf = zbuf_front;
    fb_front   = fb_back;
    zbuf_front = zbuf_back;
    fb_back    = tmp_fb;
    zbuf_back  = tmp_zbuf;

    /* fb / zbuf now point to the new back buffer */
    fb   = fb_back;
    zbuf = zbuf_back;
}

#ifdef __cplusplus
}
#endif
#endif /* RASTERIZER_H */
