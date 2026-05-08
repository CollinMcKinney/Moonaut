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

/* Current global shading mode (overridden per‑primitive by materials later) */
static shading_mode current_shade_mode = SHADE_FLAT;   /* safe default, not CEL */

static void render_set_shading_mode(shading_mode mode) {
    current_shade_mode = mode;
}

static shading_mode render_get_shading_mode(void) {
    return current_shade_mode;
}

/* -------------------------------------------------------------------------
   Transparent triangle sorting
   ------------------------------------------------------------------------- */
#define MAX_TRANSPARENT 4096

typedef struct transparent_tri {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    const struct material_definition *mat;
    shading_mode mode;
    real  depth;
} transparent_tri;

static struct transparent_tri transparent_queue[MAX_TRANSPARENT];
static i32 transparent_count = 0;
static i32 in_transparent_pass = 0;

/* -------------------------------------------------------------------------
   Setter functions (light, camera, backface)
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
}

static void render_set_fog(vec3 color, real start, real end) {
    fog_color = color;
    fog_start = start;
    fog_end = end;
}

/* -------------------------------------------------------------------------
   Colour packing
   ------------------------------------------------------------------------- */
static u32 pack_color(u8 r, u8 g, u8 b) {
    return (r << 16) | (g << 8) | b;
}

static real saturate(real x) {
    return real_clamp(x, 0.0f, 1.0f);
}

static u8 color_to_u8(real x) {
    x = saturate(x);
    return (u8)(x * 255.0f + 0.5f);
}

static u32 pack_color_real(real r, real g, real b) {
    return pack_color(color_to_u8(r), color_to_u8(g), color_to_u8(b));
}

/* - Transparent / opaque pixel write - */
static void write_pixel(i32 idx, real iw, vec3 color, real alpha)
{
    if (iw <= 0.0f) return;

    alpha = saturate(alpha);
    if (alpha <= 0.0f) return;

    if (alpha >= 1.0f) {
        /* Opaque – normal depth write */
        if (iw > zbuf[idx]) {
            zbuf[idx] = iw;
            fb[idx] = pack_color_real(color.color.r, color.color.g, color.color.b);
        }
    } else {
        /* Transparent – depth test against opaque geometry, but do not update zbuf. */
        if (iw < zbuf[idx]) return;

        u32 bg = fb[idx];
        real br = (real)((bg >> 16) & 0xFF) / 255.0f;
        real bg_g = (real)((bg >> 8)  & 0xFF) / 255.0f;
        real bg_b = (real)(bg & 0xFF) / 255.0f;
        real r = color.color.r * alpha + br * (1.0f - alpha);
        real g = color.color.g * alpha + bg_g * (1.0f - alpha);
        real b = color.color.b * alpha + bg_b * (1.0f - alpha);
        fb[idx] = pack_color_real(r, g, b);
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
static void set_pix(i32 x, i32 y, u8 r, u8 g, u8 b) {
    if (x < 0 || x >= fw || y < 0 || y >= fh) return;
    fb[y * fw + x] = pack_color(r, g, b);
}

/* -------------------------------------------------------------------------
   Projection helpers
   ------------------------------------------------------------------------- */
static void project(vec3 w, i32 *sx, i32 *sy, real *iw) {
    vec4 c = mat4_mul_vec4(vp, vec4_init_from_4(w.position.x, w.position.y, w.position.z, 1.0f));
    if (c.rotation.w <= 1e-6f) { *sx = -1; *sy = -1; *iw = 0; return; }
    *iw = 1.0f / c.rotation.w;
    real ndcx = c.position.x * (*iw), ndcy = c.position.y * (*iw);
    *sx = (i32)((ndcx * 0.5f + 0.5f) * fw);
    *sy = (i32)((1.0f - (ndcy * 0.5f + 0.5f)) * fh);
}

static void swapi(i32 *a, i32 *b) { i32 t = *a; *a = *b; *b = t; }
static void swapr(real *a, real *b) { real t = *a; *a = *b; *b = t; }
static void swapv(vec3 *a, vec3 *b) { vec3 t = *a; *a = *b; *b = t; }
static i32 raster_round(real x) { return (i32)real_floor(x + 0.5f); }

/* Global render time (animates emissive pulse) */
static real render_time = 0.0f;

/* Call this once per frame with the elapsed time in seconds */
static void render_set_time(real t) { render_time = t; }

/* - The unified shading function - */
static vec3 shade_surface(vec3 normal, vec3 world_pos,
                          const struct material_definition *mat)
{
    vec3 N = normal;   /* raw un‑normalised normal */

    /* - Procedural bump (perturb normal using world pos) - */
    if (mat->bump_frequency > 0.0f && mat->bump_amplitude > 0.0f) {
        real fx = world_pos.position.x * mat->bump_frequency;
        real fy = world_pos.position.y * mat->bump_frequency;
        real fz = world_pos.position.z * mat->bump_frequency;
        real t  = render_time * mat->bump_speed;
        N.position.x += real_sin(fy + fz + t) * mat->bump_amplitude;
        N.position.y += real_sin(fz + fx + t) * mat->bump_amplitude;
        N.position.z += real_sin(fx + fy + t) * mat->bump_amplitude;
    }

    /* Normalise now so all further calculations use the bumped normal */
    N = vec3_normalize(N);

    vec3 V = vec3_normalize(vec3_sub(cam_eye, world_pos));
    real ndotl = saturate(vec3_dot(N, light_dir));
    real ndotv = saturate(vec3_dot(N, V));

    /* - Diffuse wrap (soft light falloff) - */
    if (mat->diffuse_wrap) {
        real t = ndotl;
        ndotl = t * t * (3.0f - 2.0f * t);   /* smoothstep */
    }

    /* - Cel banding (posterise N·L) - */
    if (mat->cel_bands > 1) {
        i32 bands = mat->cel_bands;
        real inv = 1.0f / (real)(bands - 1);
        real band = real_floor(ndotl * bands) * inv;
        if (band > 1.0f) band = 1.0f;
        ndotl = band;
    }

    /* - Minnaert limb darkening - */
    real diffuse_term = ndotl;
    if (mat->minnaert_k > 0.0f) {
        diffuse_term = real_pow(ndotl, mat->minnaert_k) *
                       real_pow(ndotv, 1.0f - mat->minnaert_k);
    }

    /* - Oren‑Nayar rough diffuse - */
    if (mat->oren_nayar_sigma > 0.0f) {
        real sigma = mat->oren_nayar_sigma;
        real sigma_sq = sigma * sigma;
        real a = 1.0f - 0.5f * sigma_sq / (sigma_sq + 0.33f);
        real b = 0.45f * sigma_sq / (sigma_sq + 0.09f);
        real oren_term = 0.0f;

        real cos_phi_diff = 0.0f;
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
            }
        }

        if (cos_phi_diff > 0.0f && ndotl > 1e-4f && ndotv > 1e-4f) {
            real sin_l = real_sqrt(saturate(1.0f - ndotl * ndotl));
            real sin_v = real_sqrt(saturate(1.0f - ndotv * ndotv));
            real sin_alpha;
            real tan_beta;

            if (ndotl > ndotv) {
                sin_alpha = sin_v;          /* larger angle: view */
                tan_beta = sin_l / ndotl;   /* smaller angle: light */
            } else {
                sin_alpha = sin_l;          /* larger angle: light */
                tan_beta = sin_v / ndotv;   /* smaller angle: view */
            }

            oren_term = sin_alpha * tan_beta;
            if (oren_term > 1.0f) oren_term = 1.0f;
        }

        diffuse_term = ndotl * (a + b * cos_phi_diff * oren_term);
        diffuse_term = saturate(diffuse_term);
    }

    /* - Base colour accumulation - */
    vec3 color = ambient_col;
    color = vec3_add(color, vec3_mul_scalar(light_col, diffuse_term));

    /* - Gooch colour blending - */
    if (vec3_dot(mat->gooch_cool, mat->gooch_cool) > 0.0001f ||
        vec3_dot(mat->gooch_warm, mat->gooch_warm) > 0.0001f) {
        real t = (ndotl + 1.0f) * 0.5f;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        vec3 gooch = vec3_add(vec3_mul_scalar(mat->gooch_cool, 1.0f - t),
                              vec3_mul_scalar(mat->gooch_warm, t));
        color = vec3_mul(color, gooch);
    } else {
        color = vec3_mul(color, mat->color);
    }

    /* - Back‑face glow (light from behind) - */
    if (vec3_dot(mat->back_glow_color, mat->back_glow_color) > 0.0001f) {
        vec3 light_neg = vec3_mul_scalar(light_dir, -1.0f);
        real ndotl_neg = vec3_dot(N, light_neg);
        if (ndotl_neg < 0.0f) ndotl_neg = 0.0f;
        color = vec3_add(color, vec3_mul_scalar(mat->back_glow_color, ndotl_neg));
    }

    /* - Rim lighting (additive edge glow) - */
    if (mat->rim_exponent > 0.0f) {
        real rim = 1.0f - ndotv;
        rim = real_pow(rim, mat->rim_exponent);
        color = vec3_add(color, vec3_mul_scalar(mat->rim_color, rim));
    }

    /* - Fresnel (glancing colour blend) - */
    if (mat->fresnel_exponent > 0.0f) {
        real fresnel = real_pow(1.0f - ndotv, mat->fresnel_exponent);
        color = vec3_add(vec3_mul_scalar(color, 1.0f - fresnel),
                         vec3_mul_scalar(mat->fresnel_color, fresnel));
    }

    /* - Emissive (with optional pulse animation) - */
    if (vec3_dot(mat->emissive_color, mat->emissive_color) > 0.0001f) {
        vec3 emissive = mat->emissive_color;
        if (mat->emissive_pulse_amplitude > 0.0f) {
            real pulse = 1.0f + mat->emissive_pulse_amplitude *
                         real_sin(render_time * mat->emissive_pulse_frequency + mat->emissive_pulse_phase);
            emissive = vec3_mul_scalar(emissive, pulse);
        }
        color = vec3_add(color, emissive);
    }

    /* Strobe (flashing additive colour) */
    if (vec3_dot(mat->strobe_color, mat->strobe_color) > 0.0001f && mat->strobe_frequency > 0.0f) {
        real s = real_sin(render_time * mat->strobe_frequency + mat->strobe_phase);
        s = s * 0.5f + 0.5f;   /* 0 → 1 */
        color = vec3_add(color, vec3_mul_scalar(mat->strobe_color, s));
    }

    /* - Specular - */
    if (mat->specular_exponent > 0.0f) {
        vec3 H = vec3_normalize(vec3_add(light_dir, V));
        real nh = vec3_dot(N, H);
        if (nh < 0.0f) nh = 0.0f;

        real spec = real_pow(nh, mat->specular_exponent);

        if (mat->specular_threshold > 0.0f) {
            spec = (spec > mat->specular_threshold) ? 1.0f : 0.0f;
        }
        color = vec3_add(color, vec3_mul_scalar(mat->specular_color, spec));
    }

    /* - Ambient lighting factor (multiplicative darkening) - */
    if (mat->ambient_light_factor < 1.0f) {
        color = vec3_mul_scalar(color, mat->ambient_light_factor);
    }

    /* - Saturation control (C89‑friendly) - */
    if (mat->saturation != 1.0f) {
        real luma = color.color.r * 0.299f +
                    color.color.g * 0.587f +
                    color.color.b * 0.114f;
        color.color.r = luma + (color.color.r - luma) * mat->saturation;
        color.color.g = luma + (color.color.g - luma) * mat->saturation;
        color.color.b = luma + (color.color.b - luma) * mat->saturation;
    }

    /* - Iridescence (view‑angle rainbow shift) - */
    if (mat->iridescence_strength > 0.0f) {
        real angle = ndotv * 2.0f * VECTORS_PI;   /* map 0..1 → 0..2π */
        real c = real_cos(angle);
        real s = real_sin(angle);
        /* standard hue‑rotation matrix (weights from ITU‑R BT.709) */
        real rot[9] = {
            0.299f + 0.701f * c + 0.168f * s,  0.587f - 0.587f * c + 0.330f * s,  0.114f - 0.114f * c - 0.497f * s,
            0.299f - 0.299f * c - 0.328f * s,  0.587f + 0.413f * c + 0.035f * s,  0.114f - 0.114f * c + 0.292f * s,
            0.299f - 0.300f * c + 1.250f * s,  0.587f - 0.588f * c - 1.050f * s,  0.114f + 0.886f * c - 0.203f * s
        };
        real r = color.color.r * rot[0] + color.color.g * rot[1] + color.color.b * rot[2];
        real g = color.color.r * rot[3] + color.color.g * rot[4] + color.color.b * rot[5];
        real b = color.color.r * rot[6] + color.color.g * rot[7] + color.color.b * rot[8];
        /* Lerp between original and rotated colour based on strength */
        color.color.r = r * mat->iridescence_strength + color.color.r * (1.0f - mat->iridescence_strength);
        color.color.g = g * mat->iridescence_strength + color.color.g * (1.0f - mat->iridescence_strength);
        color.color.b = b * mat->iridescence_strength + color.color.b * (1.0f - mat->iridescence_strength);
    }

    /* - Tint (post‑lighting colour multiplication) - */
    if (vec3_dot(mat->tint, mat->tint) < 2.999f) { /* not exactly {1,1,1} */
        color = vec3_mul(color, mat->tint);
    }

    /* - Glitch (noise overlay) - */
    if (mat->glitch_intensity > 0.0f) {
        real hash = real_sin(vec3_dot(world_pos, 
            vec3_init_from_3(12.9898f, 78.233f, 45.164f)) * 43758.5453f);
        hash = hash - real_floor(hash);   /* fractional part */
        real offset = (hash - 0.5f) * mat->glitch_intensity;
        color.color.r += offset;
        color.color.g += offset * 0.7f;
        color.color.b -= offset;
    }

    /* - Chromatic aberration (color fringing) - */
    if (mat->fringe_intensity > 0.0f) {
        real fringe = real_pow(1.0f - ndotv, 3.0f) * mat->fringe_intensity;
        color.color.r += fringe;
        color.color.b -= fringe;
    }

    /* - Posterisation (quantise final colour) - */
    if (mat->posterize_levels > 1) {
        real levels = (real)(mat->posterize_levels);
        color.color.r = real_floor(color.color.r * levels + 0.5f) / levels;
        color.color.g = real_floor(color.color.g * levels + 0.5f) / levels;
        color.color.b = real_floor(color.color.b * levels + 0.5f) / levels;
    }

    /* - Global distance fog (unless material opts out) - */
    if (!mat->skip_fog && fog_end > fog_start) {
        real dist = vec3_magnitude(vec3_sub(world_pos, cam_eye));
        real t = (dist - fog_start) / (fog_end - fog_start);
        t = saturate(t);
        color = vec3_add(vec3_mul_scalar(color, 1.0f - t),
                         vec3_mul_scalar(fog_color, t));
    }

    /* - Clamp and return - */
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
    /* Backface culling in 3D space */
    if (!mat->double_sided) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 view_dir = vec3_sub(cam_eye, face_center);
        if (vec3_dot(face_normal, view_dir) <= 0.0f) return;
    }

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
        iw=siw;
        for(x=sx;x<ex;x++){
            i32 idx=y*fw+x;
            write_pixel(idx, iw, color, mat->alpha);
            iw+=iw_step;
        }
    }
}

/* -------------------------------------------------------------------------
   Per‑vertex rasterization with interpolation
   ------------------------------------------------------------------------- */
static void raster_triangle_gouraud(vec3 v0, vec3 v1, vec3 v2,
                                     vec3 c0, vec3 c1, vec3 c2, 
                                     const struct material_definition *mat) {
    /* Backface culling in 3D space */
    if (!mat->double_sided) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 view_dir = vec3_sub(cam_eye, face_center);
        if (vec3_dot(face_normal, view_dir) <= 0.0f) return;
    }

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
        iw=siw; col=cs;
        for(x=sx;x<ex;x++){
            i32 idx=y*fw+x;
            write_pixel(idx, iw, col, mat->alpha);
            iw+=iw_step; col.color.r+=dc_step.color.r; col.color.g+=dc_step.color.g; col.color.b+=dc_step.color.b;
        }
    }
}

/* -------------------------------------------------------------------------
   Per‑pixel rasterization
   ------------------------------------------------------------------------- */
static void raster_triangle_phong(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    const struct material_definition *mat)
{
    /* Backface culling in 3D space */
    if (!mat->double_sided) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 view_dir = vec3_sub(cam_eye, face_center);
        if (vec3_dot(face_normal, view_dir) <= 0.0f) return;
    }

    i32 x0,y0,x1,y1,x2,y2; real iw0,iw1,iw2;
    project(v0,&x0,&y0,&iw0); project(v1,&x1,&y1,&iw1); project(v2,&x2,&y2,&iw2);

    vec3 n0w=vec3_mul_scalar(n0,iw0), n1w=vec3_mul_scalar(n1,iw1), n2w=vec3_mul_scalar(n2,iw2);
    vec3 wp0w=vec3_mul_scalar(v0,iw0), wp1w=vec3_mul_scalar(v1,iw1), wp2w=vec3_mul_scalar(v2,iw2);

    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&n0w,&n1w);swapv(&wp0w,&wp1w);}
    if(y1>y2){swapi(&y1,&y2);swapi(&x1,&x2);swapr(&iw1,&iw2);swapv(&n1w,&n2w);swapv(&wp1w,&wp2w);}
    if(y0>y1){swapi(&y0,&y1);swapi(&x0,&x1);swapr(&iw0,&iw1);swapv(&n0w,&n1w);swapv(&wp0w,&wp1w);}

    real dx0=0,diw0=0, dx1=0,diw1=0, dx2=0,diw2=0;
    vec3 dnw0,dnw1,dnw2, dwpw0,dwpw1,dwpw2;
    dnw0=vec3_init_from_3(0,0,0); dnw1=vec3_init_from_3(0,0,0); dnw2=vec3_init_from_3(0,0,0);
    dwpw0=vec3_init_from_3(0,0,0); dwpw1=vec3_init_from_3(0,0,0); dwpw2=vec3_init_from_3(0,0,0);

    if(y1>y0){ real idy=1.0f/(y1-y0); dx0=(x1-x0)*idy; diw0=(iw1-iw0)*idy;
        dnw0.position.x=(n1w.position.x-n0w.position.x)*idy; dnw0.position.y=(n1w.position.y-n0w.position.y)*idy; dnw0.position.z=(n1w.position.z-n0w.position.z)*idy;
        dwpw0.position.x=(wp1w.position.x-wp0w.position.x)*idy; dwpw0.position.y=(wp1w.position.y-wp0w.position.y)*idy; dwpw0.position.z=(wp1w.position.z-wp0w.position.z)*idy; }
    if(y2>y1){ real idy=1.0f/(y2-y1); dx1=(x2-x1)*idy; diw1=(iw2-iw1)*idy;
        dnw1.position.x=(n2w.position.x-n1w.position.x)*idy; dnw1.position.y=(n2w.position.y-n1w.position.y)*idy; dnw1.position.z=(n2w.position.z-n1w.position.z)*idy;
        dwpw1.position.x=(wp2w.position.x-wp1w.position.x)*idy; dwpw1.position.y=(wp2w.position.y-wp1w.position.y)*idy; dwpw1.position.z=(wp2w.position.z-wp1w.position.z)*idy; }
    if(y2>y0){ real idy=1.0f/(y2-y0); dx2=(x2-x0)*idy; diw2=(iw2-iw0)*idy;
        dnw2.position.x=(n2w.position.x-n0w.position.x)*idy; dnw2.position.y=(n2w.position.y-n0w.position.y)*idy; dnw2.position.z=(n2w.position.z-n0w.position.z)*idy;
        dwpw2.position.x=(wp2w.position.x-wp0w.position.x)*idy; dwpw2.position.y=(wp2w.position.y-wp0w.position.y)*idy; dwpw2.position.z=(wp2w.position.z-wp0w.position.z)*idy; }

    i32 y_start=y0<0?0:y0, y_end=y2>fh?fh:y2;
    i32 y,sx,ex,x; real siw,eiw,iw_step,iw;
    vec3 nws,nwe,nw_val,dnw_step;
    vec3 wps,wpe,wp_val,dwp_step;

    for(y=y_start;y<y_end;y++){
        if(y<y1){ real t=(real)(y-y0); sx=x0+raster_round(dx0*t); ex=x0+raster_round(dx2*t); siw=iw0+diw0*t; eiw=iw0+diw2*t;
            nws.position.x=n0w.position.x+dnw0.position.x*t; nws.position.y=n0w.position.y+dnw0.position.y*t; nws.position.z=n0w.position.z+dnw0.position.z*t;
            nwe.position.x=n0w.position.x+dnw2.position.x*t; nwe.position.y=n0w.position.y+dnw2.position.y*t; nwe.position.z=n0w.position.z+dnw2.position.z*t;
            wps.position.x=wp0w.position.x+dwpw0.position.x*t; wps.position.y=wp0w.position.y+dwpw0.position.y*t; wps.position.z=wp0w.position.z+dwpw0.position.z*t;
            wpe.position.x=wp0w.position.x+dwpw2.position.x*t; wpe.position.y=wp0w.position.y+dwpw2.position.y*t; wpe.position.z=wp0w.position.z+dwpw2.position.z*t; }
        else { real t=(real)(y-y1); sx=x1+raster_round(dx1*t); ex=x0+raster_round(dx2*(y-y0)); siw=iw1+diw1*t; eiw=iw0+diw2*(y-y0);
            nws.position.x=n1w.position.x+dnw1.position.x*t; nws.position.y=n1w.position.y+dnw1.position.y*t; nws.position.z=n1w.position.z+dnw1.position.z*t;
            nwe.position.x=n0w.position.x+dnw2.position.x*(y-y0); nwe.position.y=n0w.position.y+dnw2.position.y*(y-y0); nwe.position.z=n0w.position.z+dnw2.position.z*(y-y0);
            wps.position.x=wp1w.position.x+dwpw1.position.x*t; wps.position.y=wp1w.position.y+dwpw1.position.y*t; wps.position.z=wp1w.position.z+dwpw1.position.z*t;
            wpe.position.x=wp0w.position.x+dwpw2.position.x*(y-y0); wpe.position.y=wp0w.position.y+dwpw2.position.y*(y-y0); wpe.position.z=wp0w.position.z+dwpw2.position.z*(y-y0); }
        if(sx>ex){swapi(&sx,&ex);swapr(&siw,&eiw);swapv(&nws,&nwe);swapv(&wps,&wpe);}
        if(sx<0)sx=0; if(ex>fw)ex=fw;
        if(ex<=sx) continue;
        iw_step=(ex>sx)?(eiw-siw)/(ex-sx):0;
        if(ex>sx){
            dnw_step.position.x=(nwe.position.x-nws.position.x)/(ex-sx); dnw_step.position.y=(nwe.position.y-nws.position.y)/(ex-sx); dnw_step.position.z=(nwe.position.z-nws.position.z)/(ex-sx);
            dwp_step.position.x=(wpe.position.x-wps.position.x)/(ex-sx); dwp_step.position.y=(wpe.position.y-wps.position.y)/(ex-sx); dwp_step.position.z=(wpe.position.z-wps.position.z)/(ex-sx);
        } else { dnw_step=vec3_init_from_3(0,0,0); dwp_step=vec3_init_from_3(0,0,0); }
        iw=siw; nw_val=nws; wp_val=wps;
        for(x=sx;x<ex;x++){
            i32 idx=y*fw+x;
            if(iw > 0.0f && (iw>zbuf[idx] || (mat->alpha < 1.0f && iw >= zbuf[idx]))){
                real inv_w = 1.0f / iw;
                vec3 normal, world_pos;
                normal.position.x = nw_val.position.x * inv_w;
                normal.position.y = nw_val.position.y * inv_w;
                normal.position.z = nw_val.position.z * inv_w;
                world_pos.position.x = wp_val.position.x * inv_w;
                world_pos.position.y = wp_val.position.y * inv_w;
                world_pos.position.z = wp_val.position.z * inv_w;

                vec3 color = shade_surface(normal, world_pos, mat);
                write_pixel(idx, iw, color, mat->alpha);
            }
            iw+=iw_step;
            nw_val.position.x+=dnw_step.position.x; nw_val.position.y+=dnw_step.position.y; nw_val.position.z+=dnw_step.position.z;
            wp_val.position.x+=dwp_step.position.x; wp_val.position.y+=dwp_step.position.y; wp_val.position.z+=dwp_step.position.z;
        }
    }
}

/* -------------------------------------------------------------------------
   Main triangle dispatch (now with automatic transparent queuing)
   ------------------------------------------------------------------------- */
static void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    const struct material_definition *mat,
    shading_mode mode)
{
    /* - Enqueue transparent triangles for later back‑to‑front sorting - */
    if (mat->alpha < 1.0f) {
        if (!in_transparent_pass && transparent_count < MAX_TRANSPARENT) {
            real d0 = vec3_distance(v0, cam_eye);
            real d1 = vec3_distance(v1, cam_eye);
            real d2 = vec3_distance(v2, cam_eye);
            transparent_queue[transparent_count].v0   = v0;
            transparent_queue[transparent_count].v1   = v1;
            transparent_queue[transparent_count].v2   = v2;
            transparent_queue[transparent_count].n0   = n0;
            transparent_queue[transparent_count].n1   = n1;
            transparent_queue[transparent_count].n2   = n2;
            transparent_queue[transparent_count].mat  = mat;
            transparent_queue[transparent_count].mode = mode;
            transparent_queue[transparent_count].depth = (d0 + d1 + d2) * 0.33333333f;
            transparent_count++;
            return;   /* deferred */
        }
        /* else: already in transparent pass or queue full – fall through to draw immediately */
    }

    /* Wireframe */
    if (mode == SHADE_WIREFRAME) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 color = shade_surface(face_normal, face_center, mat);
        raster_triangle_wireframe(v0, v1, v2, color, mat->alpha);
        return;
    }

    /* Flat */
    if (mode == SHADE_FLAT) {
        vec3 face_normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
        vec3 face_center = vec3_mul_scalar(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        vec3 color = shade_surface(face_normal, face_center, mat);
        raster_triangle_flat(v0, v1, v2, color, mat);
        return;
    }

    /* Per‑pixel NPR (Phong mode) */
    if (mode == SHADE_PHONG) {
        raster_triangle_phong(v0, v1, v2, n0, n1, n2, mat);
        return;
    }

    /* Gouraud (fallback) */
    vec3 c0 = shade_surface(n0, v0, mat);
    vec3 c1 = shade_surface(n1, v1, mat);
    vec3 c2 = shade_surface(n2, v2, mat);
    raster_triangle_gouraud(v0, v1, v2, c0, c1, c2, mat);
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
                transparent_queue[i].mat,
                transparent_queue[i].mode
            );
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
