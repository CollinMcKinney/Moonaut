/*
 * rasterizer_DX11.h - DirectX 11 rasterizer with maximum-FPS flip-model + tearing.
 *
 * Public API remains identical to rasterizer_SW.h / rasterizer_GL.h.
 * Uses a swap chain for direct presentation; no CPU readback.
 * render_get_fb() returns NULL.
 * render_finish() presents the back buffer with vsync disabled and tearing allowed.
 *
 * To use:
 *   #define RASTERIZER_DX11_IMPLEMENTATION
 *   #include "rasterizer_DX11.h"
 * in exactly one .c file.
 *
 * Shader files "material.vs.hlsl" and "material.ps.hlsl" must be present
 * in the working directory.
 *
 * Link with: d3d11.lib, dxgi.lib, d3dcompiler_47.lib (or d3dcompiler.lib)
 */

#ifndef RASTERIZER_DX11_H
#define RASTERIZER_DX11_H

#include "common.h"
#include "tags/material.h"
#include "tags/entity.h"
#include "tags/model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public API (identical to rasterizer_SW.h) ---- */
int  render_init(i32 window_width, i32 window_height);
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

void render_draw_entity(const struct entity_definition *ent);
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

#endif /* RASTERIZER_DX11_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#ifdef RASTERIZER_DX11_IMPLEMENTATION

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "window.h"

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x200UL
#endif

/* ---------------------------------------------------------------------------*/
/*  Constants                                                                  */
/* ---------------------------------------------------------------------------*/

/* Changed: material cbuffer now uses 18 float4s (was 15) to accommodate clearcoat & sheen */
#define MATERIAL_CB_SIZE_FLOAT4 18
#define MATERIAL_CB_SIZE_BYTES  (MATERIAL_CB_SIZE_FLOAT4 * 16)
#define MAX_MODEL_MATRICES       1024
#define MAX_BATCHES              128
#define MAX_TRANSPARENT_TRIS     8192
#define MAX_VERTICES_PER_FRAME   (1024 * 1024)
#define MAX_INDICES_PER_FRAME    (MAX_VERTICES_PER_FRAME * 3)
/* Vertex layout: pos(3) + normal(3) + localPos(3) + modelIndex(1) + localFaceNormal(3) + localCentroid(3) = 16 */
#define VERTEX_STRIDE_FLOATS     16
#define VERTEX_STRIDE_BYTES      (VERTEX_STRIDE_FLOATS * sizeof(float))

/* ---------------------------------------------------------------------------*/
/*  Internal Types                                                             */
/* ---------------------------------------------------------------------------*/

typedef struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader  *ps;
    render_method       key;
    int                 hit_logged;
} dx_shader_variant_t;

typedef struct {
    float data[MATERIAL_CB_SIZE_FLOAT4][4];  /* now 18 entries */
} dx_material_cb_t;

typedef struct {
    float view_proj[16];   /* column-major float4x4 (transposed from mat4) */
    float light_dir[4];
    float light_col[4];
    float ambient_col[4];
    float cam_eye[4];
    float time;
    float fog_color[4];
    float fog_start;
    float fog_end;
    float pad[5];          /* total size = 176 bytes (multiple of 16) */
} dx_globals_cb_t;

typedef struct {
    float data[MAX_MODEL_MATRICES][16];   /* column-major float4x4 */
} dx_model_cb_t;

typedef struct {
    const struct material_definition *mat;
    size_t vertex_offset;
    size_t index_offset;
    int    vertex_count;
    int    index_count;
    int    mode;
    int    is_transparent;
} dx_batch_t;

typedef struct {
    vec3 v0, v1, v2;
    vec3 n0, n1, n2;
    const struct material_definition *mat;
    float depth;
    float entity_depth;
    int   id;
    int   model_index;
} dx_transparent_tri_t;

typedef struct {
    vec3 normal;
    real d;
} dx_frustum_plane_t;

typedef struct dx_entity_sort_s {
    struct entity_definition *ent;
    float depth;
    int model_index;
} dx_entity_sort_t;

/* ---------------------------------------------------------------------------*/
/*  Global State                                                              */
/* ---------------------------------------------------------------------------*/

static ID3D11Device            *dx_device            = NULL;
static ID3D11DeviceContext     *dx_context           = NULL;
static IDXGISwapChain          *dx_swapchain         = NULL;

static ID3D11RenderTargetView  *dx_rtv               = NULL;
static ID3D11DepthStencilView  *dx_dsv               = NULL;
static ID3D11Texture2D         *dx_depth_tex         = NULL;

static ID3D11InputLayout       *dx_input_layout      = NULL;

static ID3D11Buffer            *dx_vb                = NULL;
static ID3D11Buffer            *dx_ib                = NULL;
static ID3D11Buffer            *dx_material_cb       = NULL;
static ID3D11Buffer            *dx_globals_cb        = NULL;
static ID3D11Buffer            *dx_model_cb          = NULL;

static ID3D11RasterizerState   *dx_rast_cull_none    = NULL;
static ID3D11RasterizerState   *dx_rast_cull_back    = NULL;
static ID3D11RasterizerState   *dx_rast_wireframe_none = NULL;
static ID3D11RasterizerState   *dx_rast_wireframe_back = NULL;
static ID3D11DepthStencilState *dx_depth_opaque      = NULL;
static ID3D11DepthStencilState *dx_depth_transparent = NULL;
static ID3D11BlendState        *dx_blend_opaque      = NULL;
static ID3D11BlendState        *dx_blend_alpha       = NULL;

static dx_shader_variant_t     *dx_shader_cache      = NULL;
static int                      dx_shader_cache_size = 0;
static int                      dx_shader_cache_count = 0;
static int                      dx_shader_compilations = 0;

#define SHADER_CACHE_INITIAL_SIZE 64
#define SHADER_CACHE_MAX_LOAD_FACTOR 0.7f

static int dx_allow_tearing = 0;

static i32 dx_win_width  = 0;
static i32 dx_win_height = 0;

static mat4 dx_view, dx_proj, dx_view_proj;
static vec3 dx_cam_eye;

static vec3 dx_light_dir;
static vec3 dx_light_col;
static vec3 dx_ambient_col;
static vec3 dx_fog_color;
static real dx_fog_start;
static real dx_fog_end;
static real dx_time;

static dx_frustum_plane_t dx_frustum[6];

static float *dx_vertex_pool = NULL;
static size_t dx_pool_capacity_floats = 0;
static size_t dx_pool_used_floats = 0;

static u16 *dx_index_pool = NULL;
static size_t dx_index_pool_capacity = 0;
static size_t dx_index_pool_used = 0;

static dx_batch_t dx_batches[MAX_BATCHES];
static int dx_batch_count = 0;

static dx_transparent_tri_t dx_transparent_tris[MAX_TRANSPARENT_TRIS];
static i32 dx_transparent_count = 0;
static i32 dx_transparent_triangle_id = 0;

static mat4 dx_model_matrices[MAX_MODEL_MATRICES];
static int dx_model_count = 0;

static dx_material_cb_t dx_material_cb_data;
static dx_globals_cb_t  dx_globals_cb_data;
static dx_model_cb_t    dx_model_cb_data;

/* Cached states */
static const struct material_definition *dx_last_material = NULL;
static ID3D11RasterizerState   *dx_last_rasterizer   = NULL;
static int                      dx_last_is_transparent = -1;

/* ---------------------------------------------------------------------------*/
/*  Helpers                                                                    */
/* ---------------------------------------------------------------------------*/

static INLINE u8 color_to_u8(real x) {
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255;
    return (u8)(x * 255.0f + 0.5f);
}

static void dx_matrix_to_cb(mat4 *m, float *dst) {
    i32 i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            dst[i * 4 + j] = (float)m->columns[j].components[i];
        }
    }
}

static void dx_extract_frustum_planes(void) {
    vec4 c0 = dx_view_proj.columns[0];
    vec4 c1 = dx_view_proj.columns[1];
    vec4 c2 = dx_view_proj.columns[2];
    vec4 c3 = dx_view_proj.columns[3];
    i32 i;

    dx_frustum[0].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    dx_frustum[0].d = c3.components[3] + c0.components[3];
    dx_frustum[1].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c0.components[0], c0.components[1], c0.components[2]));
    dx_frustum[1].d = c3.components[3] - c0.components[3];
    dx_frustum[2].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    dx_frustum[2].d = c3.components[3] + c1.components[3];
    dx_frustum[3].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c1.components[0], c1.components[1], c1.components[2]));
    dx_frustum[3].d = c3.components[3] - c1.components[3];
    dx_frustum[4].normal = vec3_add(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    dx_frustum[4].d = c3.components[3] + c2.components[3];
    dx_frustum[5].normal = vec3_sub(
        vec3_init_from_3(c3.components[0], c3.components[1], c3.components[2]),
        vec3_init_from_3(c2.components[0], c2.components[1], c2.components[2]));
    dx_frustum[5].d = c3.components[3] - c2.components[3];

    for (i = 0; i < 6; i++) {
        real len = vec3_magnitude(dx_frustum[i].normal);
        if (len > 0.0f) {
            dx_frustum[i].normal = vec3_div_scalar(dx_frustum[i].normal, len);
            dx_frustum[i].d /= len;
        }
    }
}

static INLINE i32 dx_triangle_outside_frustum(vec3 v0, vec3 v1, vec3 v2) {
    i32 i;
    for (i = 0; i < 6; i++) {
        i32 o0 = (vec3_dot(dx_frustum[i].normal, v0) + dx_frustum[i].d) < 0.0f;
        i32 o1 = (vec3_dot(dx_frustum[i].normal, v1) + dx_frustum[i].d) < 0.0f;
        i32 o2 = (vec3_dot(dx_frustum[i].normal, v2) + dx_frustum[i].d) < 0.0f;
        if (o0 && o1 && o2) return 1;
    }
    return 0;
}

static mat4 dx_entity_model_matrix(const struct entity_definition *ent) {
    mat4 R = quat_to_mat4(ent->orientation);
    mat4 T = mat4_translation(ent->position);
    return mat4_mul(T, R);
}

/* ---------------------------------------------------------------------------*/
/*  Shader loading and compilation                                             */
/* ---------------------------------------------------------------------------*/

static char* dx_read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
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

static void dx_generate_defines(render_method key, char* out, size_t out_size) {
    char* p = out;
    size_t remaining = out_size;
    int n;

    n = snprintf(p, remaining, "#define USE_MODEL_CB 1\n");
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
    /* New effects */
    if (effects & EFFECT_CLEARCOAT)       { n = snprintf(p, remaining, "#define EFFECT_CLEARCOAT\n"); p += n; remaining -= n; }
    if (effects & EFFECT_SHEEN)           { n = snprintf(p, remaining, "#define EFFECT_SHEEN\n"); p += n; remaining -= n; }
}

static ID3DBlob* dx_compile_shader_with_defines(const char* filename, const char* defines, const char* entry, const char* target) {
    char* source = dx_read_file(filename);
    if (!source) {
        printf("ERROR: Failed to open shader file: %s\n", filename);
        return NULL;
    }

    size_t def_len = strlen(defines);
    size_t src_len = strlen(source);
    char* combined = (char*)malloc(def_len + src_len + 1);
    if (!combined) { free(source); return 0; }
    strcpy(combined, defines);
    strcat(combined, source);
    free(source);

    ID3DBlob *blob = NULL;
    ID3DBlob *err  = NULL;
    HRESULT hr = D3DCompile(
        combined,
        strlen(combined),
        filename,
        NULL,
        NULL,
        entry,
        target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &blob,
        &err
    );
    free(combined);

    if (FAILED(hr)) {
        if (err) {
            printf("Shader %s compile error:\n%s\n", filename, (char*)err->lpVtbl->GetBufferPointer(err));
            err->lpVtbl->Release(err);
        }
        return NULL;
    }
    return blob;
}

/* ---------------------------------------------------------------------------*/
/*  Shader variant cache                                                       */
/* ---------------------------------------------------------------------------*/

static void dx_shader_cache_resize(int new_size) {
    dx_shader_variant_t *old = dx_shader_cache;
    int old_size = dx_shader_cache_size;
    int i;
    dx_shader_cache = (dx_shader_variant_t*)calloc(new_size, sizeof(dx_shader_variant_t));
    dx_shader_cache_size = new_size;
    dx_shader_cache_count = 0;
    for (i = 0; i < old_size; i++) {
        if (old[i].vs || old[i].ps) {
            int index = (unsigned)old[i].key % new_size;
            while (dx_shader_cache[index].vs || dx_shader_cache[index].ps)
                index = (index + 1) % new_size;
            dx_shader_cache[index] = old[i];
            dx_shader_cache_count++;
        }
    }
    free(old);
}

static dx_shader_variant_t* dx_get_program_for_method(render_method key) {
    if (!dx_shader_cache) {
        dx_shader_cache_size = SHADER_CACHE_INITIAL_SIZE;
        dx_shader_cache = (dx_shader_variant_t*)calloc(dx_shader_cache_size, sizeof(dx_shader_variant_t));
        dx_shader_cache_count = 0;
    }

    int index = (unsigned)key % dx_shader_cache_size;
    while (dx_shader_cache[index].vs || dx_shader_cache[index].ps) {
        if (dx_shader_cache[index].key == key) {
            if (!dx_shader_cache[index].hit_logged) {
                printf("[SHADER CACHE] Hit for key 0x%x\n", (unsigned)key);
                dx_shader_cache[index].hit_logged = 1;
            }
            return &dx_shader_cache[index];
        }
        index = (index + 1) % dx_shader_cache_size;
    }

    printf("[SHADER CACHE] Miss for key 0x%x - compiling new variant...\n", (unsigned)key);

    char defines[4096];
    dx_generate_defines(key, defines, sizeof(defines));

    ID3DBlob *vs_blob = dx_compile_shader_with_defines("material.vs.hlsl", defines, "main", "vs_4_0");
    ID3DBlob *ps_blob = dx_compile_shader_with_defines("material.ps.hlsl", defines, "main", "ps_4_0");

    if (!vs_blob || !ps_blob) {
        if (vs_blob) vs_blob->lpVtbl->Release(vs_blob);
        if (ps_blob) ps_blob->lpVtbl->Release(ps_blob);
        printf("[SHADER CACHE] ERROR: Failed to compile shaders for key 0x%x\n", (unsigned)key);
        return NULL;
    }

    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader  *ps = NULL;
    HRESULT hr;

    hr = dx_device->lpVtbl->CreateVertexShader(dx_device,
                                               vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                               vs_blob->lpVtbl->GetBufferSize(vs_blob),
                                               NULL,
                                               &vs);
    if (FAILED(hr)) { vs_blob->lpVtbl->Release(vs_blob); ps_blob->lpVtbl->Release(ps_blob); return NULL; }

    hr = dx_device->lpVtbl->CreatePixelShader(dx_device,
                                              ps_blob->lpVtbl->GetBufferPointer(ps_blob),
                                              ps_blob->lpVtbl->GetBufferSize(ps_blob),
                                              NULL,
                                              &ps);
    vs_blob->lpVtbl->Release(vs_blob);
    ps_blob->lpVtbl->Release(ps_blob);

    if (FAILED(hr)) { vs->lpVtbl->Release(vs); return NULL; }

    if ((float)(dx_shader_cache_count + 1) / dx_shader_cache_size > SHADER_CACHE_MAX_LOAD_FACTOR) {
        dx_shader_cache_resize(dx_shader_cache_size * 2);
        index = (unsigned)key % dx_shader_cache_size;
        while (dx_shader_cache[index].vs || dx_shader_cache[index].ps)
            index = (index + 1) % dx_shader_cache_size;
    }

    dx_shader_variant_t *entry = &dx_shader_cache[index];
    entry->key = key;
    entry->vs = vs;
    entry->ps = ps;
    entry->hit_logged = 0;
    dx_shader_cache_count++;
    dx_shader_compilations++;
    printf("[SHADER CACHE] Compiled variant #%d for key 0x%x\n", dx_shader_compilations, (unsigned)key);
    return entry;
}

/* ---------------------------------------------------------------------------*/
/*  Resource creation                                                          */
/* ---------------------------------------------------------------------------*/

static void dx_release_com_objects(void) {
    if (dx_rtv)            dx_rtv->lpVtbl->Release(dx_rtv);
    if (dx_dsv)            dx_dsv->lpVtbl->Release(dx_dsv);
    if (dx_depth_tex)      dx_depth_tex->lpVtbl->Release(dx_depth_tex);
    if (dx_input_layout)   dx_input_layout->lpVtbl->Release(dx_input_layout);
    if (dx_vb)             dx_vb->lpVtbl->Release(dx_vb);
    if (dx_ib)             dx_ib->lpVtbl->Release(dx_ib);
    if (dx_material_cb)    dx_material_cb->lpVtbl->Release(dx_material_cb);
    if (dx_globals_cb)     dx_globals_cb->lpVtbl->Release(dx_globals_cb);
    if (dx_model_cb)       dx_model_cb->lpVtbl->Release(dx_model_cb);
    if (dx_rast_cull_none) dx_rast_cull_none->lpVtbl->Release(dx_rast_cull_none);
    if (dx_rast_cull_back) dx_rast_cull_back->lpVtbl->Release(dx_rast_cull_back);
    if (dx_rast_wireframe_none) dx_rast_wireframe_none->lpVtbl->Release(dx_rast_wireframe_none);
    if (dx_rast_wireframe_back) dx_rast_wireframe_back->lpVtbl->Release(dx_rast_wireframe_back);
    if (dx_depth_opaque)   dx_depth_opaque->lpVtbl->Release(dx_depth_opaque);
    if (dx_depth_transparent) dx_depth_transparent->lpVtbl->Release(dx_depth_transparent);
    if (dx_blend_opaque)   dx_blend_opaque->lpVtbl->Release(dx_blend_opaque);
    if (dx_blend_alpha)    dx_blend_alpha->lpVtbl->Release(dx_blend_alpha);
    if (dx_shader_cache) {
        int i;
        for (i = 0; i < dx_shader_cache_size; i++) {
            if (dx_shader_cache[i].vs) dx_shader_cache[i].vs->lpVtbl->Release(dx_shader_cache[i].vs);
            if (dx_shader_cache[i].ps) dx_shader_cache[i].ps->lpVtbl->Release(dx_shader_cache[i].ps);
        }
        free(dx_shader_cache);
        dx_shader_cache = NULL;
    }
    if (dx_swapchain) dx_swapchain->lpVtbl->Release(dx_swapchain);
    if (dx_context)   dx_context->lpVtbl->Release(dx_context);
    if (dx_device)    dx_device->lpVtbl->Release(dx_device);
}

static int dx_create_swapchain_and_views(void) {
    HRESULT hr;
    C89FW_window_t *win;
    C89FW_native_handles_t handles;
    HWND hwnd;
    IDXGIFactory2 *factory2 = NULL;
    IDXGIFactory  *factory1 = NULL;
    ID3D11Texture2D *back_buffer = NULL;

    win = window_get();
    if (!win) {
        fprintf(stderr, "Error: window_get() returned NULL\n");
        return 0;
    }
    handles = C89FW_get_native_handles(win);
    if (!handles.hwnd) {
        fprintf(stderr, "Error: native window handle is NULL\n");
        return 0;
    }
    hwnd = (HWND)handles.hwnd;

    /* Try to create flip-model with tearing */
    hr = CreateDXGIFactory(&IID_IDXGIFactory2, (void**)&factory2);
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 sc_desc1;
        IDXGISwapChain1 *swapchain1 = NULL;

        memset(&sc_desc1, 0, sizeof(sc_desc1));
        sc_desc1.Width  = dx_win_width;
        sc_desc1.Height = dx_win_height;
        sc_desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc_desc1.SampleDesc.Count = 1;
        sc_desc1.SampleDesc.Quality = 0;
        sc_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc1.BufferCount = 2;
        sc_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc_desc1.Scaling = DXGI_SCALING_STRETCH;
        sc_desc1.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        hr = factory2->lpVtbl->CreateSwapChainForHwnd(
            factory2,
            (IUnknown*)dx_device,
            hwnd,
            &sc_desc1,
            NULL,
            NULL,
            &swapchain1);
        factory2->lpVtbl->Release(factory2);
        factory2 = NULL;

        if (SUCCEEDED(hr)) {
            dx_swapchain = (IDXGISwapChain*)swapchain1;
            dx_allow_tearing = 1;
            printf("Flip-model swap chain created with tearing support.\n");
            goto get_backbuffer;
        }
        printf("Flip-model with tearing failed (HRESULT=0x%x), falling back to legacy.\n", (unsigned)hr);
    }

    /* Fallback: legacy */
    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory1);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateDXGIFactory failed (HRESULT=0x%x)\n", (unsigned)hr);
        return 0;
    }

    {
        DXGI_SWAP_CHAIN_DESC sc_desc;
        memset(&sc_desc, 0, sizeof(sc_desc));
        sc_desc.BufferCount = 2;
        sc_desc.BufferDesc.Width  = dx_win_width;
        sc_desc.BufferDesc.Height = dx_win_height;
        sc_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc_desc.BufferDesc.RefreshRate.Numerator = 60;
        sc_desc.BufferDesc.RefreshRate.Denominator = 1;
        sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc.OutputWindow = hwnd;
        sc_desc.SampleDesc.Count = 1;
        sc_desc.SampleDesc.Quality = 0;
        sc_desc.Windowed = TRUE;
        sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        hr = factory1->lpVtbl->CreateSwapChain(factory1, (IUnknown*)dx_device, &sc_desc, &dx_swapchain);
        factory1->lpVtbl->Release(factory1);
        factory1 = NULL;
        if (FAILED(hr)) {
            fprintf(stderr, "Legacy CreateSwapChain failed (HRESULT=0x%x)\n", (unsigned)hr);
            return 0;
        }
        dx_allow_tearing = 0;
        printf("Legacy swap chain created.\n");
    }

get_backbuffer:
    hr = dx_swapchain->lpVtbl->GetBuffer(dx_swapchain, 0, &IID_ID3D11Texture2D, (void**)&back_buffer);
    if (FAILED(hr)) return 0;

    hr = dx_device->lpVtbl->CreateRenderTargetView(dx_device, (ID3D11Resource*)back_buffer, NULL, &dx_rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    if (FAILED(hr)) return 0;

    D3D11_TEXTURE2D_DESC depth_desc;
    memset(&depth_desc, 0, sizeof(depth_desc));
    depth_desc.Width              = dx_win_width;
    depth_desc.Height             = dx_win_height;
    depth_desc.MipLevels          = 1;
    depth_desc.ArraySize          = 1;
    depth_desc.Format             = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count   = 1;
    depth_desc.SampleDesc.Quality = 0;
    depth_desc.Usage              = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    depth_desc.CPUAccessFlags     = 0;
    depth_desc.MiscFlags          = 0;

    hr = dx_device->lpVtbl->CreateTexture2D(dx_device, &depth_desc, NULL, &dx_depth_tex);
    if (FAILED(hr)) return 0;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format             = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;

    hr = dx_device->lpVtbl->CreateDepthStencilView(dx_device, (ID3D11Resource*)dx_depth_tex,
                                                   &dsv_desc, &dx_dsv);
    if (FAILED(hr)) return 0;

    return 1;
}

static int dx_create_states(void) {
    HRESULT hr;
    D3D11_RASTERIZER_DESC rast_desc;
    D3D11_DEPTH_STENCIL_DESC depth_desc;
    D3D11_BLEND_DESC blend_desc;

    /* Solid, cull none */
    memset(&rast_desc, 0, sizeof(rast_desc));
    rast_desc.FillMode              = D3D11_FILL_SOLID;
    rast_desc.CullMode              = D3D11_CULL_NONE;
    rast_desc.FrontCounterClockwise = FALSE;
    rast_desc.DepthClipEnable       = TRUE;
    rast_desc.MultisampleEnable     = FALSE;
    hr = dx_device->lpVtbl->CreateRasterizerState(dx_device, &rast_desc, &dx_rast_cull_none);
    if (FAILED(hr)) return 0;

    /* Solid, cull back */
    rast_desc.CullMode              = D3D11_CULL_BACK;
    rast_desc.FrontCounterClockwise = TRUE;
    hr = dx_device->lpVtbl->CreateRasterizerState(dx_device, &rast_desc, &dx_rast_cull_back);
    if (FAILED(hr)) return 0;

    /* Wireframe, cull none */
    rast_desc.FillMode              = D3D11_FILL_WIREFRAME;
    rast_desc.CullMode              = D3D11_CULL_NONE;
    rast_desc.FrontCounterClockwise = FALSE;
    hr = dx_device->lpVtbl->CreateRasterizerState(dx_device, &rast_desc, &dx_rast_wireframe_none);
    if (FAILED(hr)) return 0;

    /* Wireframe, cull back */
    rast_desc.CullMode              = D3D11_CULL_BACK;
    rast_desc.FrontCounterClockwise = TRUE;
    hr = dx_device->lpVtbl->CreateRasterizerState(dx_device, &rast_desc, &dx_rast_wireframe_back);
    if (FAILED(hr)) return 0;

    memset(&depth_desc, 0, sizeof(depth_desc));
    depth_desc.DepthEnable      = TRUE;
    depth_desc.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc        = D3D11_COMPARISON_LESS;
    hr = dx_device->lpVtbl->CreateDepthStencilState(dx_device, &depth_desc, &dx_depth_opaque);
    if (FAILED(hr)) return 0;

    depth_desc.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = dx_device->lpVtbl->CreateDepthStencilState(dx_device, &depth_desc, &dx_depth_transparent);
    if (FAILED(hr)) return 0;

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = dx_device->lpVtbl->CreateBlendState(dx_device, &blend_desc, &dx_blend_opaque);
    if (FAILED(hr)) return 0;

    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = dx_device->lpVtbl->CreateBlendState(dx_device, &blend_desc, &dx_blend_alpha);
    if (FAILED(hr)) return 0;

    return 1;
}

static int dx_create_buffers(void) {
    HRESULT hr;
    D3D11_BUFFER_DESC cb_desc;
    D3D11_BUFFER_DESC vb_desc;
    D3D11_BUFFER_DESC ib_desc;

    memset(&cb_desc, 0, sizeof(cb_desc));
    cb_desc.ByteWidth      = sizeof(dx_material_cb_t);  /* now 288 bytes */
    cb_desc.Usage          = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = 0;
    hr = dx_device->lpVtbl->CreateBuffer(dx_device, &cb_desc, NULL, &dx_material_cb);
    if (FAILED(hr)) return 0;

    cb_desc.ByteWidth = sizeof(dx_globals_cb_t);
    hr = dx_device->lpVtbl->CreateBuffer(dx_device, &cb_desc, NULL, &dx_globals_cb);
    if (FAILED(hr)) return 0;

    cb_desc.ByteWidth = sizeof(dx_model_cb_t);
    hr = dx_device->lpVtbl->CreateBuffer(dx_device, &cb_desc, NULL, &dx_model_cb);
    if (FAILED(hr)) return 0;

    memset(&vb_desc, 0, sizeof(vb_desc));
    vb_desc.ByteWidth      = MAX_VERTICES_PER_FRAME * VERTEX_STRIDE_BYTES;
    vb_desc.Usage          = D3D11_USAGE_DYNAMIC;
    vb_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = dx_device->lpVtbl->CreateBuffer(dx_device, &vb_desc, NULL, &dx_vb);
    if (FAILED(hr)) return 0;

    memset(&ib_desc, 0, sizeof(ib_desc));
    ib_desc.ByteWidth      = MAX_INDICES_PER_FRAME * sizeof(u16);
    ib_desc.Usage          = D3D11_USAGE_DYNAMIC;
    ib_desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
    ib_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = dx_device->lpVtbl->CreateBuffer(dx_device, &ib_desc, NULL, &dx_ib);
    if (FAILED(hr)) return 0;

    return 1;
}

/* ---------------------------------------------------------------------------*/
/*  Constant buffer updates using UpdateSubresource (fast path)                */
/* ---------------------------------------------------------------------------*/

static void dx_update_material_cb(const struct material_definition *mat) {
    dx_material_cb_t *cb = &dx_material_cb_data;
    memset(cb, 0, sizeof(*cb));

    cb->data[0][0] = (float)mat->color.position.x;
    cb->data[0][1] = (float)mat->color.position.y;
    cb->data[0][2] = (float)mat->color.position.z;

    cb->data[1][0] = (float)mat->tint.position.x;
    cb->data[1][1] = (float)mat->tint.position.y;
    cb->data[1][2] = (float)mat->tint.position.z;
    cb->data[1][3] = (float)mat->alpha;

    cb->data[2][0] = (float)mat->emissive_color.position.x;
    cb->data[2][1] = (float)mat->emissive_color.position.y;
    cb->data[2][2] = (float)mat->emissive_color.position.z;
    cb->data[2][3] = (float)mat->emissive_pulse_amplitude;
    cb->data[3][0] = (float)mat->emissive_pulse_frequency;
    cb->data[3][1] = (float)mat->emissive_pulse_phase;
    cb->data[3][2] = (float)mat->specular_exponent;

    cb->data[4][0] = (float)mat->specular_color.position.x;
    cb->data[4][1] = (float)mat->specular_color.position.y;
    cb->data[4][2] = (float)mat->specular_color.position.z;
    cb->data[4][3] = (float)mat->specular_threshold;

    cb->data[5][0] = (float)mat->rim_color.position.x;
    cb->data[5][1] = (float)mat->rim_color.position.y;
    cb->data[5][2] = (float)mat->rim_color.position.z;
    cb->data[5][3] = (float)mat->rim_exponent;

    cb->data[6][0] = (float)mat->fresnel_color.position.x;
    cb->data[6][1] = (float)mat->fresnel_color.position.y;
    cb->data[6][2] = (float)mat->fresnel_color.position.z;
    cb->data[6][3] = (float)mat->fresnel_exponent;

    cb->data[7][0] = (float)mat->gooch_cool.position.x;
    cb->data[7][1] = (float)mat->gooch_cool.position.y;
    cb->data[7][2] = (float)mat->gooch_cool.position.z;

    cb->data[8][0] = (float)mat->gooch_warm.position.x;
    cb->data[8][1] = (float)mat->gooch_warm.position.y;
    cb->data[8][2] = (float)mat->gooch_warm.position.z;
    cb->data[8][3] = (float)mat->ambient_light_factor;

    cb->data[9][0] = (float)mat->oren_nayar_sigma;
    cb->data[9][1] = (float)mat->minnaert_k;
    cb->data[9][2] = (float)mat->saturation;
    cb->data[9][3] = (float)mat->iridescence_strength;

    cb->data[10][0] = (float)mat->back_glow_color.position.x;
    cb->data[10][1] = (float)mat->back_glow_color.position.y;
    cb->data[10][2] = (float)mat->back_glow_color.position.z;
    cb->data[10][3] = (float)mat->bump_amplitude;

    cb->data[11][0] = (float)mat->bump_frequency;
    cb->data[11][1] = (float)mat->bump_speed;
    cb->data[11][2] = (float)mat->roughness;
    cb->data[11][3] = (float)mat->fringe_intensity;

    cb->data[12][0] = (float)mat->cel_bands;
    cb->data[12][1] = (float)mat->glitch_intensity;
    cb->data[12][2] = (float)mat->posterize_levels;

    cb->data[13][0] = (float)mat->strobe_color.position.x;
    cb->data[13][1] = (float)mat->strobe_color.position.y;
    cb->data[13][2] = (float)mat->strobe_color.position.z;
    cb->data[13][3] = (float)mat->strobe_frequency;
    cb->data[14][0] = (float)mat->strobe_phase;

    /* ---- New clearcoat & sheen fields (indices 15,16,17) ---- */
    cb->data[15][0] = (float)mat->clearcoat_color.position.x;
    cb->data[15][1] = (float)mat->clearcoat_color.position.y;
    cb->data[15][2] = (float)mat->clearcoat_color.position.z;
    cb->data[15][3] = (float)mat->clearcoat_exponent;

    cb->data[16][0] = (float)mat->clearcoat_strength;
    cb->data[16][1] = (float)mat->sheen_color.position.x;
    cb->data[16][2] = (float)mat->sheen_color.position.y;
    cb->data[16][3] = (float)mat->sheen_color.position.z;

    cb->data[17][0] = (float)mat->sheen_exponent;
    cb->data[17][1] = (float)mat->sheen_strength;
    /* data[17][2] and data[17][3] remain 0 */

    dx_context->lpVtbl->UpdateSubresource(dx_context,
                                          (ID3D11Resource*)dx_material_cb,
                                          0,
                                          NULL,
                                          cb,
                                          0,
                                          0);
}

static void dx_update_globals_cb(void) {
    dx_globals_cb_t cb;
    memset(&cb, 0, sizeof(cb));

    dx_matrix_to_cb(&dx_view_proj, cb.view_proj);

    cb.light_dir[0] = (float)dx_light_dir.position.x;
    cb.light_dir[1] = (float)dx_light_dir.position.y;
    cb.light_dir[2] = (float)dx_light_dir.position.z;

    cb.light_col[0] = (float)dx_light_col.position.x;
    cb.light_col[1] = (float)dx_light_col.position.y;
    cb.light_col[2] = (float)dx_light_col.position.z;

    cb.ambient_col[0] = (float)dx_ambient_col.position.x;
    cb.ambient_col[1] = (float)dx_ambient_col.position.y;
    cb.ambient_col[2] = (float)dx_ambient_col.position.z;

    cb.cam_eye[0] = (float)dx_cam_eye.position.x;
    cb.cam_eye[1] = (float)dx_cam_eye.position.y;
    cb.cam_eye[2] = (float)dx_cam_eye.position.z;

    cb.time = (float)dx_time;

    cb.fog_color[0] = (float)dx_fog_color.position.x;
    cb.fog_color[1] = (float)dx_fog_color.position.y;
    cb.fog_color[2] = (float)dx_fog_color.position.z;

    cb.fog_start = (float)dx_fog_start;
    cb.fog_end   = (float)dx_fog_end;

    dx_context->lpVtbl->UpdateSubresource(dx_context,
                                          (ID3D11Resource*)dx_globals_cb,
                                          0,
                                          NULL,
                                          &cb,
                                          0,
                                          0);
}

static void dx_update_model_cb(void) {
    if (dx_model_count > 0) {
        dx_context->lpVtbl->UpdateSubresource(dx_context,
                                              (ID3D11Resource*)dx_model_cb,
                                              0,
                                              NULL,
                                              dx_model_cb_data.data,
                                              dx_model_count * sizeof(float) * 16,
                                              0);
    }
}

/* ---------------------------------------------------------------------------*/
/*  Batching and transparent sorting                                           */
/* ---------------------------------------------------------------------------*/

static int dx_transparent_compare(const void* a, const void* b) {
    const dx_transparent_tri_t* ta = (const dx_transparent_tri_t*)a;
    const dx_transparent_tri_t* tb = (const dx_transparent_tri_t*)b;
    if (ta->entity_depth > tb->entity_depth) return -1;
    if (ta->entity_depth < tb->entity_depth) return 1;
    if (ta->depth > tb->depth) return -1;
    if (ta->depth < tb->depth) return 1;
    if (ta->mat < tb->mat) return -1;
    if (ta->mat > tb->mat) return 1;
    return (ta->id < tb->id) ? -1 : (ta->id > tb->id) ? 1 : 0;
}

static int dx_entity_sort_compare(const void* a, const void* b) {
    const dx_entity_sort_t* sa = (const dx_entity_sort_t*)a;
    const dx_entity_sort_t* sb = (const dx_entity_sort_t*)b;
    if (sa->depth > sb->depth) return -1;
    if (sa->depth < sb->depth) return 1;
    return 0;
}

static void dx_flush_transparent_batches(void) {
    if (dx_transparent_count == 0) return;
    qsort(dx_transparent_tris, dx_transparent_count, sizeof(dx_transparent_tri_t),
          dx_transparent_compare);

    int i = 0;
    while (i < dx_transparent_count) {
        const struct material_definition *mat = dx_transparent_tris[i].mat;
        int start = i;
        while (i < dx_transparent_count && dx_transparent_tris[i].mat == mat) i++;

        if (dx_batch_count < MAX_BATCHES) {
            dx_batch_t *b = &dx_batches[dx_batch_count++];
            b->mat = mat;
            b->mode = (mat->render_method & 0x7);
            b->vertex_offset = dx_pool_used_floats / VERTEX_STRIDE_FLOATS;
            b->index_offset = dx_index_pool_used;
            b->vertex_count = 0;
            b->index_count = 0;
            b->is_transparent = 1;

            for (int j = start; j < i; j++) {
                dx_transparent_tri_t *t = &dx_transparent_tris[j];
                if (dx_pool_used_floats + (3 * VERTEX_STRIDE_FLOATS) > dx_pool_capacity_floats ||
                    dx_index_pool_used + 3 > dx_index_pool_capacity) {
                    size_t new_cap = dx_pool_capacity_floats ? dx_pool_capacity_floats * 2 : 1024 * VERTEX_STRIDE_FLOATS;
                    float *new_pool = (float*)realloc(dx_vertex_pool, new_cap * sizeof(float));
                    if (!new_pool) return;
                    dx_vertex_pool = new_pool;
                    dx_pool_capacity_floats = new_cap;
                    size_t new_idx_cap = dx_index_pool_capacity ? dx_index_pool_capacity * 2 : 1024 * 3;
                    u16 *new_idx = (u16*)realloc(dx_index_pool, new_idx_cap * sizeof(u16));
                    if (!new_idx) return;
                    dx_index_pool = new_idx;
                    dx_index_pool_capacity = new_idx_cap;
                }
                float *ptr = &dx_vertex_pool[dx_pool_used_floats];
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
                u16 base = (u16)(dx_pool_used_floats / VERTEX_STRIDE_FLOATS);
                dx_index_pool[dx_index_pool_used++] = base;
                dx_index_pool[dx_index_pool_used++] = base + 1;
                dx_index_pool[dx_index_pool_used++] = base + 2;
                dx_pool_used_floats += 3 * VERTEX_STRIDE_FLOATS;
                b->vertex_count += 3;
                b->index_count += 3;
            }
        }
    }
    dx_transparent_count = 0;
    dx_transparent_triangle_id = 0;
}

/* ---------------------------------------------------------------------------*/
/*  Internal draw helpers                                                      */
/* ---------------------------------------------------------------------------*/

static void dx_draw_triangle_indexed(
    vec3 local_v0, vec3 local_v1, vec3 local_v2,
    vec3 local_n0, vec3 local_n1, vec3 local_n2,
    const struct material_definition *mat,
    float entity_depth,
    int model_index)
{
    mat4 model = dx_model_matrices[model_index];
    vec3 world_v0 = mat4_mul_vec3(model, local_v0);
    vec3 world_v1 = mat4_mul_vec3(model, local_v1);
    vec3 world_v2 = mat4_mul_vec3(model, local_v2);

    if (dx_triangle_outside_frustum(world_v0, world_v1, world_v2)) return;

    /* Compute local face normal and centroid for FLAT and WIREFRAME */
    vec3 localFaceNormal = {0,0,0}, localCentroid = {0,0,0};
    u32 mode = mat->render_method & 0x7;
    if (mode == MODE_FLAT || mode == MODE_WIREFRAME) {
        localFaceNormal = vec3_normalize(vec3_cross(vec3_sub(local_v1, local_v0), vec3_sub(local_v2, local_v0)));
        localCentroid = vec3_div_scalar(vec3_add(vec3_add(local_v0, local_v1), local_v2), 3.0f);
    }

    if (mat->render_method & EFFECT_ALPHA) {
        if (dx_transparent_count >= MAX_TRANSPARENT_TRIS) {
            dx_flush_transparent_batches();
        }
        dx_transparent_tri_t *t = &dx_transparent_tris[dx_transparent_count++];
        t->v0 = local_v0; t->v1 = local_v1; t->v2 = local_v2;
        t->n0 = local_n0; t->n1 = local_n1; t->n2 = local_n2;
        t->mat = mat;
        t->entity_depth = entity_depth;
        t->id = dx_transparent_triangle_id++;
        t->model_index = model_index;
        vec4 c0 = mat4_mul_vec4(dx_view, vec4_init_from_4(world_v0.position.x, world_v0.position.y, world_v0.position.z, 1.0f));
        vec4 c1 = mat4_mul_vec4(dx_view, vec4_init_from_4(world_v1.position.x, world_v1.position.y, world_v1.position.z, 1.0f));
        vec4 c2 = mat4_mul_vec4(dx_view, vec4_init_from_4(world_v2.position.x, world_v2.position.y, world_v2.position.z, 1.0f));
        float d0 = -c0.position.z, d1 = -c1.position.z, d2 = -c2.position.z;
        t->depth = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
        return;
    }

    int batch_idx = -1;
    for (int i = 0; i < dx_batch_count; i++) {
        if (dx_batches[i].mat == mat && dx_batches[i].mode == mode) {
            batch_idx = i;
            break;
        }
    }
    if (batch_idx == -1) {
        if (dx_batch_count >= MAX_BATCHES) return;
        batch_idx = dx_batch_count++;
        dx_batches[batch_idx].mat = mat;
        dx_batches[batch_idx].mode = mode;
        dx_batches[batch_idx].vertex_offset = dx_pool_used_floats / VERTEX_STRIDE_FLOATS;
        dx_batches[batch_idx].index_offset = dx_index_pool_used;
        dx_batches[batch_idx].vertex_count = 0;
        dx_batches[batch_idx].index_count = 0;
        dx_batches[batch_idx].is_transparent = 0;
    }

    dx_batch_t *b = &dx_batches[batch_idx];

    if (dx_pool_used_floats + (3 * VERTEX_STRIDE_FLOATS) > dx_pool_capacity_floats ||
        dx_index_pool_used + 3 > dx_index_pool_capacity) {
        return;
    }

    float *ptr = &dx_vertex_pool[dx_pool_used_floats];
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

    u16 base = (u16)(dx_pool_used_floats / VERTEX_STRIDE_FLOATS);
    dx_index_pool[dx_index_pool_used++] = base;
    dx_index_pool[dx_index_pool_used++] = base + 1;
    dx_index_pool[dx_index_pool_used++] = base + 2;

    dx_pool_used_floats += 3 * VERTEX_STRIDE_FLOATS;
    b->vertex_count += 3;
    b->index_count += 3;
}

static void dx_draw_entity_with_model_index(const struct entity_definition *ent, int model_index) {
    if (!ent || ent->model.handle < 0) return;
    model_definition *mod = (model_definition*)tag_get(ent->model.handle, TAG_model);
    if (!mod) return;

    float entity_depth;
    {
        vec4 c = mat4_mul_vec4(dx_view, vec4_init_from_4(
            ent->position.position.x, ent->position.position.y, ent->position.position.z, 1.0f));
        entity_depth = -c.position.z;
    }

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
            static material_definition fallback = DEFAULT_MATERIAL_PLASTIC;
            mat = &fallback;
        }

        model_vertex *verts = (model_vertex*)prim->vertices.address;
        u16 *indices = (u16*)prim->indices.address;
        u32 tri_count = prim->indices.count / 3;
        u32 t;
        for (t = 0; t < tri_count; ++t) {
            u16 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
            vec3 local_v0 = verts[i0].position;
            vec3 local_v1 = verts[i1].position;
            vec3 local_v2 = verts[i2].position;
            vec3 local_n0 = verts[i0].normal;
            vec3 local_n1 = verts[i1].normal;
            vec3 local_n2 = verts[i2].normal;
            dx_draw_triangle_indexed(local_v0, local_v1, local_v2,
                                     local_n0, local_n1, local_n2,
                                     mat, entity_depth, model_index);
        }
    }
}

/* ---------------------------------------------------------------------------*/
/*  Public API                                                                 */
/* ---------------------------------------------------------------------------*/

int render_init(i32 window_width, i32 window_height) {
    printf("render_init: width=%d height=%d\n", window_width, window_height);
    if (dx_device) return 1;

    dx_win_width = window_width;
    dx_win_height = window_height;

    HRESULT hr;
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL chosen_level;
    D3D_DRIVER_TYPE dx_driver_type;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           feature_levels, 3, D3D11_SDK_VERSION,
                           &dx_device, &chosen_level, &dx_context);
    if (SUCCEEDED(hr)) {
        dx_driver_type = D3D_DRIVER_TYPE_HARDWARE;
    } else {
        fprintf(stderr, "D3D11CreateDevice(HARDWARE) failed (HRESULT=0x%x), trying WARP...\n", (unsigned)hr);
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                               feature_levels, 3, D3D11_SDK_VERSION,
                               &dx_device, &chosen_level, &dx_context);
        if (SUCCEEDED(hr)) {
            dx_driver_type = D3D_DRIVER_TYPE_WARP;
        } else {
            fprintf(stderr, "D3D11CreateDevice(WARP) also failed (HRESULT=0x%x)\n", (unsigned)hr);
            return 0;
        }
    }

    printf("D3D11 driver type: %s\n", dx_driver_type == D3D_DRIVER_TYPE_HARDWARE ? "HARDWARE" : "WARP");
    printf("D3D11 feature level: 0x%x\n", (unsigned)chosen_level);

    if (!dx_create_swapchain_and_views() ||
        !dx_create_states() ||
        !dx_create_buffers()) {
        dx_release_com_objects();
        return 0;
    }

    D3D11_INPUT_ELEMENT_DESC layout_desc[6];
    int layout_index = 0;
    /* POSITION */
    layout_desc[layout_index].SemanticName         = "POSITION";
    layout_desc[layout_index].SemanticIndex        = 0;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 0;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;
    /* NORMAL */
    layout_desc[layout_index].SemanticName         = "NORMAL";
    layout_desc[layout_index].SemanticIndex        = 0;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 12;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;
    /* TEXCOORD0 (local position) */
    layout_desc[layout_index].SemanticName         = "TEXCOORD";
    layout_desc[layout_index].SemanticIndex        = 0;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 24;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;
    /* TEXCOORD1 (model index) */
    layout_desc[layout_index].SemanticName         = "TEXCOORD";
    layout_desc[layout_index].SemanticIndex        = 1;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 36;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;
    /* TEXCOORD2 (local face normal) */
    layout_desc[layout_index].SemanticName         = "TEXCOORD";
    layout_desc[layout_index].SemanticIndex        = 2;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 40;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;
    /* TEXCOORD3 (local centroid) */
    layout_desc[layout_index].SemanticName         = "TEXCOORD";
    layout_desc[layout_index].SemanticIndex        = 3;
    layout_desc[layout_index].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    layout_desc[layout_index].InputSlot            = 0;
    layout_desc[layout_index].AlignedByteOffset    = 52;
    layout_desc[layout_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    layout_desc[layout_index].InstanceDataStepRate = 0;
    layout_index++;

    char defines[] = "#define DUMMY\n";
    ID3DBlob *vs_blob = dx_compile_shader_with_defines("material.vs.hlsl", defines, "main", "vs_4_0");
    if (!vs_blob) {
        printf("ERROR: Failed to compile initial vertex shader for input layout.\n");
        dx_release_com_objects();
        return 0;
    }

    hr = dx_device->lpVtbl->CreateInputLayout(dx_device, layout_desc, layout_index,
                                              vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                              vs_blob->lpVtbl->GetBufferSize(vs_blob),
                                              &dx_input_layout);
    vs_blob->lpVtbl->Release(vs_blob);
    if (FAILED(hr)) {
        printf("CreateInputLayout failed (HRESULT=0x%x)\n", (unsigned)hr);
        dx_release_com_objects();
        return 0;
    }

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width    = (float)dx_win_width;
    viewport.Height   = (float)dx_win_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    dx_context->lpVtbl->RSSetViewports(dx_context, 1, &viewport);

    {
        float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        dx_context->lpVtbl->ClearRenderTargetView(dx_context, dx_rtv, clear_color);
        dx_context->lpVtbl->ClearDepthStencilView(dx_context, dx_dsv,
                                                  D3D11_CLEAR_DEPTH, 1.0f, 0);
    }

    dx_pool_capacity_floats = MAX_VERTICES_PER_FRAME * VERTEX_STRIDE_FLOATS;
    dx_vertex_pool = (float*)malloc(dx_pool_capacity_floats * sizeof(float));
    dx_index_pool_capacity = MAX_INDICES_PER_FRAME;
    dx_index_pool = (u16*)malloc(dx_index_pool_capacity * sizeof(u16));
    if (!dx_vertex_pool || !dx_index_pool) {
        free(dx_vertex_pool); free(dx_index_pool);
        dx_release_com_objects();
        return 0;
    }

    dx_transparent_count = 0;
    dx_batch_count = 0;
    dx_pool_used_floats = 0;
    dx_index_pool_used = 0;
    dx_model_count = 0;
    dx_shader_compilations = 0;

    return 1;
}

void render_shutdown(void) {
    dx_release_com_objects();

    free(dx_vertex_pool);
    free(dx_index_pool);

    dx_vertex_pool = NULL;
    dx_index_pool = NULL;
    dx_pool_capacity_floats = 0;
    dx_index_pool_capacity = 0;
    dx_pool_used_floats = 0;
    dx_index_pool_used = 0;
    dx_batch_count = 0;
    dx_transparent_count = 0;
    dx_model_count = 0;
    dx_device = NULL;
    dx_context = NULL;
}

void render_set_light(vec3 dir, vec3 col, vec3 amb) {
    dx_light_dir = dir;
    dx_light_col = col;
    dx_ambient_col = amb;
}

void render_set_camera(vec3 eye, vec3 center, vec3 up, real fov, real aspect) {
    dx_cam_eye = eye;
    dx_view = mat4_lookat(eye, center, up);
    dx_proj = mat4_perspective(fov, aspect, 0.05f, 1000.0f);
    dx_view_proj = mat4_mul(dx_proj, dx_view);
    dx_extract_frustum_planes();
}

void render_set_fog(vec3 color, real start, real end) {
    dx_fog_color = color;
    dx_fog_start = start;
    dx_fog_end = end;
}

void render_set_time(real t) {
    dx_time = t;
}

void render_clear(u8 r, u8 g, u8 b) {
    render_clear_color(r / 255.0f, g / 255.0f, b / 255.0f);
}

void render_clear_color(real r, real g, real b) {
    if (!dx_context || !dx_rtv || !dx_dsv) return;
    float clear_color[4] = {(float)r, (float)g, (float)b, 1.0f};
    dx_context->lpVtbl->ClearRenderTargetView(dx_context, dx_rtv, clear_color);
    dx_context->lpVtbl->ClearDepthStencilView(dx_context, dx_dsv,
                                              D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void draw_triangle_shaded(
    vec3 v0, vec3 v1, vec3 v2,
    vec3 n0, vec3 n1, vec3 n2,
    vec3 l0, vec3 l1, vec3 l2,
    const struct material_definition *mat)
{
    (void)v0; (void)v1; (void)v2;
    (void)n0; (void)n1; (void)n2;
    (void)l0; (void)l1; (void)l2;
    (void)mat;
}

void render_draw_entity(const struct entity_definition *ent) {
    if (!ent || ent->model.handle < 0) return;
    dx_model_count = 0;
    dx_model_matrices[0] = dx_entity_model_matrix(ent);
    dx_model_count = 1;
    dx_draw_entity_with_model_index(ent, 0);
}

void render_draw_entities(struct entity_definition **entities, int count) {
    if (!entities || count <= 0) return;

    dx_model_count = 0;
    dx_batch_count = 0;
    dx_transparent_count = 0;
    dx_pool_used_floats = 0;
    dx_index_pool_used = 0;

    {
        int i;
        for (i = 0; i < count && dx_model_count < MAX_MODEL_MATRICES; i++) {
            entity_definition *ent = entities[i];
            if (!ent || ent->model.handle < 0) continue;
            dx_model_matrices[dx_model_count] = dx_entity_model_matrix(ent);
            dx_model_count++;
        }
    }

    {
        static dx_entity_sort_t *sorted = NULL;
        static int sorted_capacity = 0;
        int i;
        int valid_count = 0;

        if (sorted_capacity < count) {
            sorted = (dx_entity_sort_t*)realloc(sorted, count * sizeof(dx_entity_sort_t));
            sorted_capacity = count;
        }

        for (i = 0; i < count; i++) {
            if (!entities[i] || entities[i]->model.handle < 0) continue;
            sorted[valid_count].ent = entities[i];
            sorted[valid_count].model_index = valid_count;
            {
                vec4 c = mat4_mul_vec4(dx_view, vec4_init_from_4(
                    entities[i]->position.position.x,
                    entities[i]->position.position.y,
                    entities[i]->position.position.z,
                    1.0f));
                sorted[valid_count].depth = -c.position.z;
            }
            valid_count++;
        }

        qsort(sorted, valid_count, sizeof(dx_entity_sort_t), dx_entity_sort_compare);

        for (i = 0; i < valid_count; i++) {
            dx_draw_entity_with_model_index(sorted[i].ent, sorted[i].model_index);
        }
    }

    dx_flush_transparent_batches();
}

/* ---------------------------------------------------------------------------*/
/*  render_finish                                                              */
/* ---------------------------------------------------------------------------*/

static int dx_batch_compare_mode(const void* a, const void* b) {
    const dx_batch_t* ba = (const dx_batch_t*)a;
    const dx_batch_t* bb = (const dx_batch_t*)b;
    if (ba->is_transparent != bb->is_transparent)
        return ba->is_transparent - bb->is_transparent;
    if (ba->mode < bb->mode) return -1;
    if (ba->mode > bb->mode) return 1;
    if (ba->mat < bb->mat) return -1;
    if (ba->mat > bb->mat) return 1;
    return 0;
}

void render_finish(void) {
    if (!dx_device || !dx_context) return;

    dx_update_globals_cb();

    {
        int i;
        for (i = 0; i < dx_model_count; i++) {
            dx_matrix_to_cb(&dx_model_matrices[i], dx_model_cb_data.data[i]);
        }
    }
    dx_update_model_cb();

    if (dx_pool_used_floats > 0 && dx_index_pool_used > 0) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dx_context->lpVtbl->Map(dx_context, (ID3D11Resource*)dx_vb, 0,
                                              D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, dx_vertex_pool, dx_pool_used_floats * sizeof(float));
            dx_context->lpVtbl->Unmap(dx_context, (ID3D11Resource*)dx_vb, 0);
        }
        if (SUCCEEDED(dx_context->lpVtbl->Map(dx_context, (ID3D11Resource*)dx_ib, 0,
                                              D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, dx_index_pool, dx_index_pool_used * sizeof(u16));
            dx_context->lpVtbl->Unmap(dx_context, (ID3D11Resource*)dx_ib, 0);
        }
    }

    UINT stride = VERTEX_STRIDE_BYTES;
    UINT offset = 0;
    dx_context->lpVtbl->IASetInputLayout(dx_context, dx_input_layout);
    dx_context->lpVtbl->IASetPrimitiveTopology(dx_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dx_context->lpVtbl->IASetVertexBuffers(dx_context, 0, 1, &dx_vb, &stride, &offset);
    dx_context->lpVtbl->IASetIndexBuffer(dx_context, dx_ib, DXGI_FORMAT_R16_UINT, 0);

    ID3D11Buffer *cbs[3] = { dx_material_cb, dx_globals_cb, dx_model_cb };
    dx_context->lpVtbl->VSSetConstantBuffers(dx_context, 0, 3, cbs);
    dx_context->lpVtbl->PSSetConstantBuffers(dx_context, 0, 3, cbs);

    if (dx_batch_count > 0) {
        qsort(dx_batches, dx_batch_count, sizeof(dx_batch_t), dx_batch_compare_mode);

        ID3D11VertexShader *current_vs = NULL;
        ID3D11PixelShader  *current_ps = NULL;
        const struct material_definition *last_material = NULL;
        int i;
        for (i = 0; i < dx_batch_count; i++) {
            dx_batch_t *b = &dx_batches[i];
            dx_shader_variant_t *variant = dx_get_program_for_method(b->mat->render_method);
            if (!variant) continue;

            if (current_vs != variant->vs) {
                dx_context->lpVtbl->VSSetShader(dx_context, variant->vs, NULL, 0);
                current_vs = variant->vs;
            }
            if (current_ps != variant->ps) {
                dx_context->lpVtbl->PSSetShader(dx_context, variant->ps, NULL, 0);
                current_ps = variant->ps;
            }

            /* Update material CB only when material changes */
            if (b->mat != last_material) {
                dx_update_material_cb(b->mat);
                last_material = b->mat;
            }

            if (b->is_transparent != dx_last_is_transparent) {
                if (b->is_transparent) {
                    dx_context->lpVtbl->OMSetDepthStencilState(dx_context, dx_depth_transparent, 0);
                    dx_context->lpVtbl->OMSetBlendState(dx_context, dx_blend_alpha, NULL, 0xffffffff);
                } else {
                    dx_context->lpVtbl->OMSetDepthStencilState(dx_context, dx_depth_opaque, 0);
                    dx_context->lpVtbl->OMSetBlendState(dx_context, dx_blend_opaque, NULL, 0xffffffff);
                }
                dx_last_is_transparent = b->is_transparent;
            }

            /* Select rasterizer state based on mode and double_sided */
            ID3D11RasterizerState *want_rs;
            if (b->mode == MODE_WIREFRAME) {
                want_rs = b->mat->double_sided ? dx_rast_wireframe_none : dx_rast_wireframe_back;
            } else {
                want_rs = b->mat->double_sided ? dx_rast_cull_none : dx_rast_cull_back;
            }
            if (want_rs != dx_last_rasterizer) {
                dx_context->lpVtbl->RSSetState(dx_context, want_rs);
                dx_last_rasterizer = want_rs;
            }

            ID3D11RenderTargetView *rtvs[1] = { dx_rtv };
            dx_context->lpVtbl->OMSetRenderTargets(dx_context, 1, rtvs, dx_dsv);

            dx_context->lpVtbl->DrawIndexed(dx_context,
                                            b->index_count,
                                            (UINT)b->index_offset,
                                            0);
        }
    }

    /* Present with tearing if available, otherwise vsync off */
    if (dx_allow_tearing) {
        dx_swapchain->lpVtbl->Present(dx_swapchain, 0, DXGI_PRESENT_ALLOW_TEARING);
    } else {
        dx_swapchain->lpVtbl->Present(dx_swapchain, 0, 0);
    }

    dx_pool_used_floats = 0;
    dx_index_pool_used = 0;
    dx_batch_count = 0;
    dx_transparent_count = 0;
    dx_transparent_triangle_id = 0;
    dx_model_count = 0;
}

const u32* render_get_fb(void) {
    return NULL;   /* DirectX presents directly */
}

int render_resize(i32 new_w, i32 new_h) {
    if (dx_win_width == new_w && dx_win_height == new_h) return 0;
    dx_win_width = new_w;
    dx_win_height = new_h;

    if (dx_swapchain) {
        dx_context->lpVtbl->OMSetRenderTargets(dx_context, 0, NULL, NULL);
        if (dx_rtv) { dx_rtv->lpVtbl->Release(dx_rtv); dx_rtv = NULL; }
        if (dx_dsv) { dx_dsv->lpVtbl->Release(dx_dsv); dx_dsv = NULL; }
        if (dx_depth_tex) { dx_depth_tex->lpVtbl->Release(dx_depth_tex); dx_depth_tex = NULL; }

        HRESULT hr = dx_swapchain->lpVtbl->ResizeBuffers(dx_swapchain, 2, new_w, new_h,
                                                         DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(hr)) return -1;

        ID3D11Texture2D *back_buffer = NULL;
        hr = dx_swapchain->lpVtbl->GetBuffer(dx_swapchain, 0, &IID_ID3D11Texture2D, (void**)&back_buffer);
        if (FAILED(hr)) return -1;

        hr = dx_device->lpVtbl->CreateRenderTargetView(dx_device, (ID3D11Resource*)back_buffer, NULL, &dx_rtv);
        back_buffer->lpVtbl->Release(back_buffer);
        if (FAILED(hr)) return -1;

        D3D11_TEXTURE2D_DESC depth_desc;
        memset(&depth_desc, 0, sizeof(depth_desc));
        depth_desc.Width              = new_w;
        depth_desc.Height             = new_h;
        depth_desc.MipLevels          = 1;
        depth_desc.ArraySize          = 1;
        depth_desc.Format             = DXGI_FORMAT_D32_FLOAT;
        depth_desc.SampleDesc.Count   = 1;
        depth_desc.Usage              = D3D11_USAGE_DEFAULT;
        depth_desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        hr = dx_device->lpVtbl->CreateTexture2D(dx_device, &depth_desc, NULL, &dx_depth_tex);
        if (FAILED(hr)) return -1;

        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.Format             = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Texture2D.MipSlice = 0;
        hr = dx_device->lpVtbl->CreateDepthStencilView(dx_device, (ID3D11Resource*)dx_depth_tex,
                                                       &dsv_desc, &dx_dsv);
        if (FAILED(hr)) return -1;

        D3D11_VIEWPORT viewport;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width    = (float)new_w;
        viewport.Height   = (float)new_h;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        dx_context->lpVtbl->RSSetViewports(dx_context, 1, &viewport);
    }

    return 0;
}

void render_set_render_resolution(i32 render_width, i32 render_height) {
    (void)render_width; (void)render_height;
}

i32 render_get_render_width(void) {
    return dx_win_width;
}

i32 render_get_render_height(void) {
    return dx_win_height;
}

#endif /* RASTERIZER_DX11_IMPLEMENTATION */