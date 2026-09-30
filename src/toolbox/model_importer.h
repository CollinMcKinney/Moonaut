#ifndef MODEL_IMPORTER_H
#define MODEL_IMPORTER_H

#include "../tags/model.h"
#include "../tags/animation.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal GLB 2.0 importer for model_definition.
 *
 * Ownership:
 *   - model_importer_import_glb* allocates the model's primitive, material,
 *     vertex, and index blocks.
 *   - Release those allocations with model_importer_free_model.
 *
 * Scope:
 *   - Triangle mesh primitives only.
 *   - Each glTF mesh primitive becomes one model_primitive and one material
 *     slot. This is the unit Blender/glTF commonly uses for submeshes.
 *   - POSITION, NORMAL, TANGENT, TEXCOORD_* (up to 8), COLOR_* (up to 4),
 *     JOINTS_0/1 and WEIGHTS_0/1 accessors are imported.
 *   - Indices may be UNSIGNED_BYTE, UNSIGNED_SHORT, or UNSIGNED_INT – the
 *     final engine primitive uses u32 indices.
 *   - Animation, skins, textures, and glTF material properties are ignored.
 *   - Extra vertex attributes not present are zeroed.
 */

#define MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE  5121u
#define MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_SHORT 5123u
#define MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_INT   5125u
#define MODEL_IMPORTER_GLTF_COMPONENT_FLOAT          5126u
#define MODEL_IMPORTER_GLTF_MODE_TRIANGLES           4

typedef struct model_importer_json_span {
    const char *start;
    const char *end;
} model_importer_json_span;

typedef struct model_importer_buffer_view {
    u32 buffer;
    u32 byte_offset;
    u32 byte_length;
    u32 byte_stride;
} model_importer_buffer_view;

typedef struct model_importer_accessor {
    i32 buffer_view;
    u32 byte_offset;
    u32 component_type;
    u32 count;
    u32 component_count;
    i32 normalized;
} model_importer_accessor;

typedef struct model_importer_mat4 {
    real m[16]; /* row-major, column-vector multiplication */
} model_importer_mat4;

typedef struct model_importer_node {
    i32 mesh;
    i32 skin;                          /* index into glTF skins, -1 if none */
    string_id name;                    /* interned node name */
    i32 parent;                        /* parent node index, -1 for root */
    u32 child_count;
    i32 *children;
    model_importer_mat4 local_transform;
    /* The node's own TRS, kept alongside the composed matrix. A glTF node
     * may carry either a "matrix" or T/R/S, so these are the normalised
     * form of whichever was present, and the skeleton copies them as each
     * joint's rest pose. */
    real rest_tx, rest_ty, rest_tz;
    real rest_qx, rest_qy, rest_qz, rest_qw;
    real rest_sx, rest_sy, rest_sz;
    /* The node's composed scene-space transform at import time. Joints use the
     * world transform of their nearest non-joint ancestor as the base for
     * forward kinematics, because inverseBindMatrix is defined against the
     * joint's scene-space transform and therefore bakes in any transform on
     * the non-joint nodes above the skeleton. */
    model_importer_mat4 world_transform;
    u8 world_valid;                    /* world_transform has been composed */
} model_importer_node;

typedef struct model_importer_context {
    const u8 *bin;
    u32 bin_size;

    model_importer_buffer_view *buffer_views;
    u32 buffer_view_count;

    model_importer_accessor *accessors;
    u32 accessor_count;

    model_importer_json_span meshes;
    u32 mesh_count;
    u32 model_primitive_count;

    model_importer_mat4 *mesh_transforms;
    u8 *mesh_transform_set;
    u8 *mesh_node_is_skinned;           /* 1 if the mesh's node carries a skin */

    /* Node hierarchy, retained after import for skin/ animation resolution. */
    model_importer_node *nodes;
    u32 node_count;

    model_importer_json_span skins;
    u32 skin_count;

    model_importer_json_span animations;
    u32 animation_count;
} model_importer_context;

static char model_importer_error[256];

static const char *model_importer_last_error(void)
{
    return model_importer_error;
}

static int model_importer_set_error(const char *message)
{
    if (!message) message = "unknown model importer error";
    strncpy(model_importer_error, message, sizeof(model_importer_error) - 1);
    model_importer_error[sizeof(model_importer_error) - 1] = '\0';
    return 0;
}

static void *model_importer_calloc_count(u32 count, size_t element_size)
{
    size_t total;
    void *memory;

    if (count == 0 || element_size == 0) return NULL;
    if ((size_t)count > ((size_t)-1) / element_size) return NULL;

    total = (size_t)count * element_size;
    memory = TAG_MALLOC(total);
    if (!memory) return NULL;

    memset(memory, 0, total);
    return memory;
}

static u32 model_importer_read_u32le(const u8 *p)
{
    return ((u32)p[0]) |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u16 model_importer_read_u16le(const u8 *p)
{
    return (u16)(((u32)p[0]) | ((u32)p[1] << 8));
}

static real model_importer_read_f32le(const u8 *p)
{
    u32 bits;
    float value;

    bits = model_importer_read_u32le(p);
    memcpy(&value, &bits, sizeof(value));
    return (real)value;
}

static int model_importer_read_file(const char *path, u8 **out_data, u32 *out_size)
{
    FILE *fp;
    long length;
    u8 *data;

    if (!path || !out_data || !out_size)
        return model_importer_set_error("invalid file read arguments");

    fp = fopen(path, "rb");
    if (!fp)
        return model_importer_set_error("could not open GLB file");

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return model_importer_set_error("could not seek GLB file");
    }

    length = ftell(fp);
    if (length <= 0) {
        fclose(fp);
        return model_importer_set_error("GLB file is empty");
    }

    if ((unsigned long)length > 0xFFFFFFFFul) {
        fclose(fp);
        return model_importer_set_error("GLB file is too large");
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return model_importer_set_error("could not rewind GLB file");
    }

    data = (u8*)TAG_MALLOC((size_t)length);
    if (!data) {
        fclose(fp);
        return model_importer_set_error("out of memory while reading GLB");
    }

    if (fread(data, 1, (size_t)length, fp) != (size_t)length) {
        TAG_FREE(data);
        fclose(fp);
        return model_importer_set_error("could not read GLB file");
    }

    fclose(fp);
    *out_data = data;
    *out_size = (u32)length;
    return 1;
}

static const char *model_importer_json_skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static const char *model_importer_json_skip_string(const char *p, const char *end)
{
    if (p >= end || *p != '"') return NULL;
    ++p;
    while (p < end) {
        if (*p == '\\') {
            ++p;
            if (p >= end) return NULL;
            ++p;
            continue;
        }
        if (*p == '"') return p + 1;
        ++p;
    }
    return NULL;
}

static const char *model_importer_json_skip_number(const char *p, const char *end)
{
    const char *start;

    start = p;
    while (p < end &&
           (*p == '-' || *p == '+' || *p == '.' ||
            *p == 'e' || *p == 'E' ||
            (*p >= '0' && *p <= '9'))) {
        ++p;
    }

    return p != start ? p : NULL;
}

static const char *model_importer_json_skip_compound(const char *p, const char *end,
                                                     char open_ch, char close_ch)
{
    u32 depth;

    if (p >= end || *p != open_ch) return NULL;
    depth = 1;
    ++p;

    while (p < end && depth > 0) {
        if (*p == '"') {
            p = model_importer_json_skip_string(p, end);
            if (!p) return NULL;
            continue;
        }
        if (*p == open_ch) {
            ++depth;
        } else if (*p == close_ch) {
            --depth;
            if (depth == 0) return p + 1;
        }
        ++p;
    }

    return NULL;
}

static const char *model_importer_json_skip_value(const char *p, const char *end)
{
    p = model_importer_json_skip_ws(p, end);
    if (p >= end) return NULL;

    if (*p == '"') return model_importer_json_skip_string(p, end);
    if (*p == '{') return model_importer_json_skip_compound(p, end, '{', '}');
    if (*p == '[') return model_importer_json_skip_compound(p, end, '[', ']');
    if ((*p >= '0' && *p <= '9') || *p == '-') return model_importer_json_skip_number(p, end);

    if ((end - p) >= 4 && memcmp(p, "true", 4) == 0) return p + 4;
    if ((end - p) >= 5 && memcmp(p, "false", 5) == 0) return p + 5;
    if ((end - p) >= 4 && memcmp(p, "null", 4) == 0) return p + 4;

    return NULL;
}

static int model_importer_json_string_equals(const char *p, const char *end, const char *text)
{
    const char *s;

    p = model_importer_json_skip_ws(p, end);
    if (p >= end || *p != '"') return 0;
    ++p;

    s = text;
    while (p < end && *p != '"') {
        if (*p == '\\') return 0;
        if (*s == '\0' || *p != *s) return 0;
        ++p;
        ++s;
    }

    return p < end && *p == '"' && *s == '\0';
}

static int model_importer_json_value_is_string(model_importer_json_span value, const char *text)
{
    return model_importer_json_string_equals(value.start, value.end, text);
}

/* Copy a JSON string value into a fixed buffer (truncating, no escape decoding
 * beyond a simple pass-through - adequate for glTF object names). */
static int model_importer_json_copy_string(model_importer_json_span value, char *out, size_t out_size)
{
    const char *p, *end;
    size_t n = 0;

    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    p = model_importer_json_skip_ws(value.start, value.end);
    if (p >= value.end || *p != '"') return 0;
    ++p;
    end = value.end;

    while (p < end && *p != '"') {
        char c = *p;
        if (c == '\\') {
            ++p;
            if (p >= end) break;
            c = *p;
        }
        if (n + 1 < out_size) out[n++] = c;
        ++p;
    }

    out[n] = '\0';
    return 1;
}

static int model_importer_json_object_find(model_importer_json_span object,
                                           const char *key,
                                           model_importer_json_span *out_value)
{
    const char *p;
    const char *key_start;
    const char *key_end;
    const char *value_start;
    const char *value_end;
    int matched;

    if (!key || !out_value) return 0;

    p = model_importer_json_skip_ws(object.start, object.end);
    if (p >= object.end || *p != '{') return 0;
    ++p;

    while (p < object.end) {
        p = model_importer_json_skip_ws(p, object.end);
        if (p < object.end && *p == '}') return 0;

        key_start = p;
        key_end = model_importer_json_skip_string(p, object.end);
        if (!key_end) return 0;

        matched = model_importer_json_string_equals(key_start, key_end, key);

        p = model_importer_json_skip_ws(key_end, object.end);
        if (p >= object.end || *p != ':') return 0;
        ++p;

        value_start = model_importer_json_skip_ws(p, object.end);
        value_end = model_importer_json_skip_value(value_start, object.end);
        if (!value_end) return 0;

        if (matched) {
            out_value->start = value_start;
            out_value->end = value_end;
            return 1;
        }

        p = model_importer_json_skip_ws(value_end, object.end);
        if (p < object.end && *p == ',') {
            ++p;
            continue;
        }
        if (p < object.end && *p == '}') return 0;
        return 0;
    }

    return 0;
}

static int model_importer_json_array_count(model_importer_json_span array, u32 *out_count)
{
    const char *p;
    const char *value_end;
    u32 count;

    if (!out_count) return 0;

    p = model_importer_json_skip_ws(array.start, array.end);
    if (p >= array.end || *p != '[') return 0;
    ++p;
    count = 0;

    while (p < array.end) {
        p = model_importer_json_skip_ws(p, array.end);
        if (p < array.end && *p == ']') {
            *out_count = count;
            return 1;
        }

        value_end = model_importer_json_skip_value(p, array.end);
        if (!value_end) return 0;
        ++count;

        p = model_importer_json_skip_ws(value_end, array.end);
        if (p < array.end && *p == ',') {
            ++p;
            continue;
        }
        if (p < array.end && *p == ']') {
            *out_count = count;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int model_importer_json_array_get(model_importer_json_span array,
                                         u32 index,
                                         model_importer_json_span *out_value)
{
    const char *p;
    const char *value_start;
    const char *value_end;
    u32 current;

    if (!out_value) return 0;

    p = model_importer_json_skip_ws(array.start, array.end);
    if (p >= array.end || *p != '[') return 0;
    ++p;
    current = 0;

    while (p < array.end) {
        p = model_importer_json_skip_ws(p, array.end);
        if (p < array.end && *p == ']') return 0;

        value_start = p;
        value_end = model_importer_json_skip_value(value_start, array.end);
        if (!value_end) return 0;

        if (current == index) {
            out_value->start = value_start;
            out_value->end = value_end;
            return 1;
        }

        ++current;
        p = model_importer_json_skip_ws(value_end, array.end);
        if (p < array.end && *p == ',') {
            ++p;
            continue;
        }
        if (p < array.end && *p == ']') return 0;
        return 0;
    }

    return 0;
}

static int model_importer_json_parse_i32(model_importer_json_span value, i32 *out_value)
{
    char buffer[64];
    size_t length;
    char *end_ptr;
    long parsed;

    if (!out_value) return 0;

    value.start = model_importer_json_skip_ws(value.start, value.end);
    length = (size_t)(value.end - value.start);
    while (length > 0 &&
           (value.start[length - 1] == ' ' || value.start[length - 1] == '\t' ||
            value.start[length - 1] == '\r' || value.start[length - 1] == '\n')) {
        --length;
    }

    if (length == 0 || length >= sizeof(buffer)) return 0;
    memcpy(buffer, value.start, length);
    buffer[length] = '\0';

    parsed = strtol(buffer, &end_ptr, 10);
    if (end_ptr == buffer || *end_ptr != '\0') return 0;

    *out_value = (i32)parsed;
    return 1;
}

static int model_importer_json_parse_u32(model_importer_json_span value, u32 *out_value)
{
    i32 signed_value;

    if (!model_importer_json_parse_i32(value, &signed_value)) return 0;
    if (signed_value < 0) return 0;
    *out_value = (u32)signed_value;
    return 1;
}

static int model_importer_json_parse_real(model_importer_json_span value, real *out_value)
{
    char buffer[96];
    size_t length;
    char *end_ptr;
    double parsed;

    if (!out_value) return 0;

    value.start = model_importer_json_skip_ws(value.start, value.end);
    length = (size_t)(value.end - value.start);
    while (length > 0 &&
           (value.start[length - 1] == ' ' || value.start[length - 1] == '\t' ||
            value.start[length - 1] == '\r' || value.start[length - 1] == '\n')) {
        --length;
    }

    if (length == 0 || length >= sizeof(buffer)) return 0;
    memcpy(buffer, value.start, length);
    buffer[length] = '\0';

    parsed = strtod(buffer, &end_ptr);
    if (end_ptr == buffer || *end_ptr != '\0') return 0;

    *out_value = (real)parsed;
    return 1;
}

static int model_importer_json_parse_bool(model_importer_json_span value, i32 *out_value)
{
    const char *p;

    if (!out_value) return 0;

    p = model_importer_json_skip_ws(value.start, value.end);
    if ((value.end - p) >= 4 && memcmp(p, "true", 4) == 0) {
        *out_value = 1;
        return 1;
    }
    if ((value.end - p) >= 5 && memcmp(p, "false", 5) == 0) {
        *out_value = 0;
        return 1;
    }

    return 0;
}

static int model_importer_json_array_real(model_importer_json_span array, u32 index, real *out_value)
{
    model_importer_json_span value;

    if (!model_importer_json_array_get(array, index, &value)) return 0;
    return model_importer_json_parse_real(value, out_value);
}

static int model_importer_json_array_i32(model_importer_json_span array, u32 index, i32 *out_value)
{
    model_importer_json_span value;

    if (!model_importer_json_array_get(array, index, &value)) return 0;
    return model_importer_json_parse_i32(value, out_value);
}

static model_importer_mat4 model_importer_mat4_identity(void)
{
    model_importer_mat4 result;
    u32 i;

    for (i = 0; i < 16; ++i) result.m[i] = 0.0f;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

static model_importer_mat4 model_importer_mat4_mul(model_importer_mat4 a,
                                                   model_importer_mat4 b)
{
    model_importer_mat4 result;
    u32 row;
    u32 col;
    u32 k;

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < 4; ++col) {
            real sum;

            sum = 0.0f;
            for (k = 0; k < 4; ++k) {
                sum += a.m[row * 4 + k] * b.m[k * 4 + col];
            }
            result.m[row * 4 + col] = sum;
        }
    }

    return result;
}

static model_importer_mat4 model_importer_mat4_from_gltf_matrix(const real gltf_matrix[16])
{
    model_importer_mat4 result;
    u32 row;
    u32 col;

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < 4; ++col) {
            result.m[row * 4 + col] = gltf_matrix[col * 4 + row];
        }
    }

    return result;
}

/* Both model_importer_mat4 and mat4 are row-major, column-vector real[16], so
 * this is a straight element copy. */
static mat4 model_importer_to_mat4(model_importer_mat4 src)
{
    mat4 result;
    u32 i;
    for (i = 0; i < 16; ++i) result.data[i] = src.m[i];
    return result;
}

static model_importer_mat4 model_importer_mat4_from_mat4(mat4 src)
{
    model_importer_mat4 result;
    u32 i;
    for (i = 0; i < 16; ++i) result.m[i] = src.data[i];
    return result;
}

/* Inverse of a 4x4 by Gauss-Jordan elimination with partial pivoting. Returns
 * 0 for a singular matrix, in which case the caller must fall back. */
static int model_importer_mat4_invert(model_importer_mat4 src, model_importer_mat4 *out)
{
    double a[4][8];
    int i, j, k;

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            a[i][j] = (double)src.m[i * 4 + j];
            a[i][j + 4] = (i == j) ? 1.0 : 0.0;
        }
    }

    for (k = 0; k < 4; ++k) {
        int pivot = k;
        double best = fabs(a[k][k]);
        double inv;
        for (i = k + 1; i < 4; ++i) {
            double v = fabs(a[i][k]);
            if (v > best) { best = v; pivot = i; }
        }
        if (best < 1e-12) return 0;
        if (pivot != k) {
            for (j = 0; j < 8; ++j) {
                double t = a[k][j]; a[k][j] = a[pivot][j]; a[pivot][j] = t;
            }
        }
        inv = 1.0 / a[k][k];
        for (j = 0; j < 8; ++j) a[k][j] *= inv;
        for (i = 0; i < 4; ++i) {
            double f;
            if (i == k) continue;
            f = a[i][k];
            if (f == 0.0) continue;
            for (j = 0; j < 8; ++j) a[i][j] -= f * a[k][j];
        }
    }

    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            out->m[i * 4 + j] = (real)a[i][j + 4];
    return 1;
}

static model_importer_mat4 model_importer_mat4_from_trs(real tx, real ty, real tz,
                                                        real qx, real qy, real qz, real qw,
                                                        real sx, real sy, real sz)
{
    model_importer_mat4 result;
    real xx, yy, zz, xy, xz, yz, wx, wy, wz, length;

    length = (real)sqrt((double)(qx*qx + qy*qy + qz*qz + qw*qw));
    if (length > 0.000001f) {
        qx /= length; qy /= length; qz /= length; qw /= length;
    } else {
        qx = 0.0f; qy = 0.0f; qz = 0.0f; qw = 1.0f;
    }

    xx = qx * qx; yy = qy * qy; zz = qz * qz;
    xy = qx * qy; xz = qx * qz; yz = qy * qz;
    wx = qw * qx; wy = qw * qy; wz = qw * qz;

    result = model_importer_mat4_identity();

    result.m[0]  = (1.0f - 2.0f * (yy + zz)) * sx;
    result.m[1]  = (2.0f * (xy - wz)) * sy;
    result.m[2]  = (2.0f * (xz + wy)) * sz;
    result.m[3]  = tx;

    result.m[4]  = (2.0f * (xy + wz)) * sx;
    result.m[5]  = (1.0f - 2.0f * (xx + zz)) * sy;
    result.m[6]  = (2.0f * (yz - wx)) * sz;
    result.m[7]  = ty;

    result.m[8]  = (2.0f * (xz - wy)) * sx;
    result.m[9]  = (2.0f * (yz + wx)) * sy;
    result.m[10] = (1.0f - 2.0f * (xx + yy)) * sz;
    result.m[11] = tz;

    return result;
}

/* Decompose an affine transform into translation, rotation quaternion and
 * scale. Needed for nodes authored with a raw `matrix` instead of TRS: the
 * animation runtime poses joints from TRS, so the rest pose has to be recovered
 * or a matrix-defined joint falls back to identity and the mesh deforms even
 * when nothing is playing.
 *
 * Assumes the usual glTF TRS basis: columns are the scaled basis vectors, with
 * any hierarchy of scaling folded into per-axis scale, rotation taken from the
 * normalised matrix, and shear discarded. Returns 0 if the basis is degenerate
 * (a zero-length axis), in which case the caller's TRS is left as identity. */
static int model_importer_decompose_trs(const model_importer_mat4 *m,
                                         real *tx, real *ty, real *tz,
                                         real *qx, real *qy, real *qz, real *qw,
                                         real *sx, real *sy, real *sz)
{
    real *col = (real*)m->m;   /* column j starts at m[j*4] */
    real r[3][3];
    real len[3], trace, s;
    real x, y, z, w;
    u32 i, j;

    *tx = m->m[3]; *ty = m->m[7]; *tz = m->m[11];

    for (j = 0; j < 3u; ++j) {
        len[j] = (real)sqrt((double)(col[j*4 + 0]*col[j*4 + 0] +
                                     col[j*4 + 1]*col[j*4 + 1] +
                                     col[j*4 + 2]*col[j*4 + 2]));
    }
    if (len[0] < 1e-8f || len[1] < 1e-8f || len[2] < 1e-8f) return 0;

    for (j = 0; j < 3u; ++j)
        for (i = 0; i < 3u; ++i) r[i][j] = col[j*4 + i] / len[j];

    /* Shear makes the columns non-orthogonal. The trace-based extraction below
     * is only valid for a pure rotation, so detect and reject shear rather than
     * emit a silently wrong quaternion. */
    {
        real c01 = r[0][0]*r[1][0] + r[0][1]*r[1][1] + r[0][2]*r[1][2];
        real c02 = r[0][0]*r[2][0] + r[0][1]*r[2][1] + r[0][2]*r[2][2];
        real c12 = r[1][0]*r[2][0] + r[1][1]*r[2][1] + r[1][2]*r[2][2];
        if (fabs((double)c01) > 1e-3 || fabs((double)c02) > 1e-3 || fabs((double)c12) > 1e-3)
            return 0;
    }

    trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        s = (real)sqrt((double)(trace + 1.0f)) * 2.0f;
        w = 0.25f * s;
        x = (r[2][1] - r[1][2]) / s;
        y = (r[0][2] - r[2][0]) / s;
        z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        s = (real)sqrt((double)(1.0f + r[0][0] - r[1][1] - r[2][2])) * 2.0f;
        w = (r[2][1] - r[1][2]) / s;
        x = 0.25f * s;
        y = (r[0][1] + r[1][0]) / s;
        z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        s = (real)sqrt((double)(1.0f + r[1][1] - r[0][0] - r[2][2])) * 2.0f;
        w = (r[0][2] - r[2][0]) / s;
        x = (r[0][1] + r[1][0]) / s;
        y = 0.25f * s;
        z = (r[1][2] + r[2][1]) / s;
    } else {
        s = (real)sqrt((double)(1.0f + r[2][2] - r[0][0] - r[1][1])) * 2.0f;
        w = (r[1][0] - r[0][1]) / s;
        x = (r[0][2] + r[2][0]) / s;
        y = (r[1][2] + r[2][1]) / s;
        z = 0.25f * s;
    }

    *qx = x; *qy = y; *qz = z; *qw = w;
    *sx = len[0]; *sy = len[1]; *sz = len[2];
    return 1;
}

static vec3 model_importer_transform_point(model_importer_mat4 transform, vec3 point)
{
    vec3 result;
    real x, y, z;

    x = point.position.x; y = point.position.y; z = point.position.z;

    result.position.x = transform.m[0] * x + transform.m[1] * y + transform.m[2]  * z + transform.m[3];
    result.position.y = transform.m[4] * x + transform.m[5] * y + transform.m[6]  * z + transform.m[7];
    result.position.z = transform.m[8] * x + transform.m[9] * y + transform.m[10] * z + transform.m[11];
    return result;
}

static vec3 model_importer_vec3_cross(vec3 a, vec3 b)
{
    vec3 result;
    result.position.x = a.position.y * b.position.z - a.position.z * b.position.y;
    result.position.y = a.position.z * b.position.x - a.position.x * b.position.z;
    result.position.z = a.position.x * b.position.y - a.position.y * b.position.x;
    return result;
}

static vec3 model_importer_vec3_sub(vec3 a, vec3 b)
{
    vec3 result;
    result.position.x = a.position.x - b.position.x;
    result.position.y = a.position.y - b.position.y;
    result.position.z = a.position.z - b.position.z;
    return result;
}

static vec3 model_importer_vec3_add(vec3 a, vec3 b)
{
    vec3 result;
    result.position.x = a.position.x + b.position.x;
    result.position.y = a.position.y + b.position.y;
    result.position.z = a.position.z + b.position.z;
    return result;
}

static vec3 model_importer_vec3_normalize_or_up(vec3 value)
{
    real length;

    length = (real)sqrt((double)(value.position.x * value.position.x +
                                 value.position.y * value.position.y +
                                 value.position.z * value.position.z));

    if (length <= 0.000001f) {
        value.position.x = 0.0f; value.position.y = 1.0f; value.position.z = 0.0f;
        return value;
    }

    value.position.x /= length;
    value.position.y /= length;
    value.position.z /= length;
    return value;
}

static vec3 model_importer_transform_normal(model_importer_mat4 transform, vec3 normal)
{
    real a, b, c, d, e, f, g, h, i, det;
    vec3 result;

    a = transform.m[0]; b = transform.m[1]; c = transform.m[2];
    d = transform.m[4]; e = transform.m[5]; f = transform.m[6];
    g = transform.m[8]; h = transform.m[9]; i = transform.m[10];

    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

    if (real_abs(det) <= 0.000001f) {
        result.position.x = a * normal.position.x + b * normal.position.y + c * normal.position.z;
        result.position.y = d * normal.position.x + e * normal.position.y + f * normal.position.z;
        result.position.z = g * normal.position.x + h * normal.position.y + i * normal.position.z;
        return model_importer_vec3_normalize_or_up(result);
    }

    result.position.x = ((e * i - f * h) * normal.position.x +
                         (f * g - d * i) * normal.position.y +
                         (d * h - e * g) * normal.position.z) / det;
    result.position.y = ((c * h - b * i) * normal.position.x +
                         (a * i - c * g) * normal.position.y +
                         (b * g - a * h) * normal.position.z) / det;
    result.position.z = ((b * f - c * e) * normal.position.x +
                         (c * d - a * f) * normal.position.y +
                         (a * e - b * d) * normal.position.z) / det;

    return model_importer_vec3_normalize_or_up(result);
}

/* --------------------------------------------------------------------------
 * GLB chunk parsing
 * -------------------------------------------------------------------------- */

static int model_importer_parse_glb_chunks(const u8 *data, u32 size,
                                           model_importer_json_span *out_json,
                                           const u8 **out_bin,
                                           u32 *out_bin_size)
{
    u32 magic, version, declared_length, offset;
    int found_json;

    if (!data || size < 20) return model_importer_set_error("GLB file is too small");

    magic = model_importer_read_u32le(data);
    version = model_importer_read_u32le(data + 4);
    declared_length = model_importer_read_u32le(data + 8);

    if (magic != 0x46546C67u) return model_importer_set_error("file is not a GLB");
    if (version != 2u) return model_importer_set_error("only GLB version 2 is supported");
    if (declared_length > size) return model_importer_set_error("GLB declared length exceeds file length");

    offset = 12u;
    found_json = 0;
    *out_bin = NULL;
    *out_bin_size = 0;

    while (offset + 8u <= declared_length) {
        u32 chunk_length, chunk_type;
        const u8 *chunk_data;

        chunk_length = model_importer_read_u32le(data + offset);
        chunk_type = model_importer_read_u32le(data + offset + 4u);
        offset += 8u;

        if (chunk_length > declared_length - offset)
            return model_importer_set_error("GLB chunk length is invalid");

        chunk_data = data + offset;

        if (chunk_type == 0x4E4F534Au) {
            out_json->start = (const char*)chunk_data;
            out_json->end = (const char*)chunk_data + chunk_length;
            found_json = 1;
        } else if (chunk_type == 0x004E4942u) {
            *out_bin = chunk_data;
            *out_bin_size = chunk_length;
        }

        offset += chunk_length;
    }

    if (!found_json) return model_importer_set_error("GLB is missing a JSON chunk");
    if (!*out_bin) return model_importer_set_error("GLB is missing a BIN chunk");

    return 1;
}

static u32 model_importer_accessor_component_size(u32 component_type)
{
    switch (component_type) {
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE:  return 1u;
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_SHORT: return 2u;
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_INT:   return 4u;
        case MODEL_IMPORTER_GLTF_COMPONENT_FLOAT:          return 4u;
        default:                                           return 0u;
    }
}

static u32 model_importer_gltf_type_component_count(model_importer_json_span type_value)
{
    if (model_importer_json_value_is_string(type_value, "SCALAR")) return 1u;
    if (model_importer_json_value_is_string(type_value, "VEC2")) return 2u;
    if (model_importer_json_value_is_string(type_value, "VEC3")) return 3u;
    if (model_importer_json_value_is_string(type_value, "VEC4")) return 4u;
    if (model_importer_json_value_is_string(type_value, "MAT2")) return 4u;
    if (model_importer_json_value_is_string(type_value, "MAT3")) return 9u;
    if (model_importer_json_value_is_string(type_value, "MAT4")) return 16u;
    return 0u;
}

static int model_importer_parse_buffer_views(model_importer_context *ctx,
                                             model_importer_json_span root)
{
    model_importer_json_span views_array;
    u32 i;

    if (!model_importer_json_object_find(root, "bufferViews", &views_array))
        return model_importer_set_error("glTF JSON is missing bufferViews");

    if (!model_importer_json_array_count(views_array, &ctx->buffer_view_count))
        return model_importer_set_error("bufferViews must be an array");

    if (ctx->buffer_view_count == 0)
        return model_importer_set_error("glTF has no bufferViews");

    ctx->buffer_views = (model_importer_buffer_view*)model_importer_calloc_count(
        ctx->buffer_view_count, sizeof(model_importer_buffer_view));
    if (!ctx->buffer_views)
        return model_importer_set_error("out of memory for bufferViews");

    for (i = 0; i < ctx->buffer_view_count; ++i) {
        model_importer_json_span object, value;
        model_importer_buffer_view *view;

        if (!model_importer_json_array_get(views_array, i, &object))
            return model_importer_set_error("could not read bufferView object");

        view = &ctx->buffer_views[i];
        view->buffer = 0u;
        view->byte_offset = 0u;
        view->byte_length = 0u;
        view->byte_stride = 0u;

        if (model_importer_json_object_find(object, "buffer", &value) &&
            !model_importer_json_parse_u32(value, &view->buffer))
            return model_importer_set_error("bufferView.buffer must be an integer");

        if (model_importer_json_object_find(object, "byteOffset", &value) &&
            !model_importer_json_parse_u32(value, &view->byte_offset))
            return model_importer_set_error("bufferView.byteOffset must be an integer");

        if (!model_importer_json_object_find(object, "byteLength", &value) ||
            !model_importer_json_parse_u32(value, &view->byte_length))
            return model_importer_set_error("bufferView.byteLength must be an integer");

        if (model_importer_json_object_find(object, "byteStride", &value) &&
            !model_importer_json_parse_u32(value, &view->byte_stride))
            return model_importer_set_error("bufferView.byteStride must be an integer");
    }

    return 1;
}

static int model_importer_parse_accessors(model_importer_context *ctx,
                                          model_importer_json_span root)
{
    model_importer_json_span accessors_array;
    u32 i;

    if (!model_importer_json_object_find(root, "accessors", &accessors_array))
        return model_importer_set_error("glTF JSON is missing accessors");

    if (!model_importer_json_array_count(accessors_array, &ctx->accessor_count))
        return model_importer_set_error("accessors must be an array");

    if (ctx->accessor_count == 0)
        return model_importer_set_error("glTF has no accessors");

    ctx->accessors = (model_importer_accessor*)model_importer_calloc_count(
        ctx->accessor_count, sizeof(model_importer_accessor));
    if (!ctx->accessors)
        return model_importer_set_error("out of memory for accessors");

    for (i = 0; i < ctx->accessor_count; ++i) {
        model_importer_json_span object, value;
        model_importer_accessor *accessor;
        u32 components;
        i32 normalized;

        if (!model_importer_json_array_get(accessors_array, i, &object))
            return model_importer_set_error("could not read accessor object");

        accessor = &ctx->accessors[i];
        accessor->buffer_view = -1;
        accessor->byte_offset = 0u;
        accessor->component_type = 0u;
        accessor->count = 0u;
        accessor->component_count = 0u;
        accessor->normalized = 0;

        if (model_importer_json_object_find(object, "bufferView", &value) &&
            !model_importer_json_parse_i32(value, &accessor->buffer_view))
            return model_importer_set_error("accessor.bufferView must be an integer");

        if (model_importer_json_object_find(object, "byteOffset", &value) &&
            !model_importer_json_parse_u32(value, &accessor->byte_offset))
            return model_importer_set_error("accessor.byteOffset must be an integer");

        if (!model_importer_json_object_find(object, "componentType", &value) ||
            !model_importer_json_parse_u32(value, &accessor->component_type))
            return model_importer_set_error("accessor.componentType must be an integer");

        if (!model_importer_json_object_find(object, "count", &value) ||
            !model_importer_json_parse_u32(value, &accessor->count))
            return model_importer_set_error("accessor.count must be an integer");

        if (!model_importer_json_object_find(object, "type", &value))
            return model_importer_set_error("accessor.type is missing");

        components = model_importer_gltf_type_component_count(value);
        if (components == 0)
            return model_importer_set_error("accessor.type is unsupported");
        accessor->component_count = components;

        if (model_importer_json_object_find(object, "normalized", &value)) {
            if (!model_importer_json_parse_bool(value, &normalized))
                return model_importer_set_error("accessor.normalized must be a boolean");
            accessor->normalized = normalized;
        }

        if (model_importer_json_object_find(object, "sparse", &value))
            return model_importer_set_error("sparse accessors are not supported");
    }

    return 1;
}

static int model_importer_parse_node(model_importer_node *node,
                                     model_importer_json_span object)
{
    model_importer_json_span value;
    real tx, ty, tz, qx, qy, qz, qw, sx, sy, sz;
    u32 count, i;
    char name_buf[64];

    node->mesh = -1;
    node->skin = -1;
    node->name = TAG_NULL(string_id);
    node->parent = -1;
    node->child_count = 0u;
    node->children = NULL;
    node->local_transform = model_importer_mat4_identity();

    if (model_importer_json_object_find(object, "mesh", &value) &&
        !model_importer_json_parse_i32(value, &node->mesh))
        return model_importer_set_error("node.mesh must be an integer");

    if (model_importer_json_object_find(object, "skin", &value) &&
        !model_importer_json_parse_i32(value, &node->skin))
        return model_importer_set_error("node.skin must be an integer");

    if (model_importer_json_object_find(object, "name", &value) &&
        model_importer_json_copy_string(value, name_buf, sizeof(name_buf)))
        node->name = string_id_intern(name_buf);

    if (model_importer_json_object_find(object, "matrix", &value)) {
        real gltf_matrix[16];
        for (i = 0; i < 16u; ++i) {
            if (!model_importer_json_array_real(value, i, &gltf_matrix[i]))
                return model_importer_set_error("node.matrix must contain 16 numbers");
        }
        node->local_transform = model_importer_mat4_from_gltf_matrix(gltf_matrix);
        /* A node given a raw matrix has no TRS form. Recover it so the
         * animation runtime can pose this joint; falling back to identity TRS
         * would silently deform an unanimated model. */
        if (!model_importer_decompose_trs(&node->local_transform,
                                           &node->rest_tx, &node->rest_ty, &node->rest_tz,
                                           &node->rest_qx, &node->rest_qy, &node->rest_qz, &node->rest_qw,
                                           &node->rest_sx, &node->rest_sy, &node->rest_sz)) {
            /* Sheared or degenerate basis: keep the translation, which is
             * still exact, and pose the rest as a pure translation. */
            node->rest_tx = gltf_matrix[12];
            node->rest_ty = gltf_matrix[13];
            node->rest_tz = gltf_matrix[14];
            node->rest_qx = 0.0f; node->rest_qy = 0.0f;
            node->rest_qz = 0.0f; node->rest_qw = 1.0f;
            node->rest_sx = 1.0f; node->rest_sy = 1.0f; node->rest_sz = 1.0f;
        }
    } else {
        tx = ty = tz = 0.0f;
        qx = qy = qz = 0.0f; qw = 1.0f;
        sx = sy = sz = 1.0f;

        if (model_importer_json_object_find(object, "translation", &value)) {
            if (!model_importer_json_array_real(value, 0u, &tx) ||
                !model_importer_json_array_real(value, 1u, &ty) ||
                !model_importer_json_array_real(value, 2u, &tz))
                return model_importer_set_error("node.translation must contain 3 numbers");
        }
        if (model_importer_json_object_find(object, "rotation", &value)) {
            if (!model_importer_json_array_real(value, 0u, &qx) ||
                !model_importer_json_array_real(value, 1u, &qy) ||
                !model_importer_json_array_real(value, 2u, &qz) ||
                !model_importer_json_array_real(value, 3u, &qw))
                return model_importer_set_error("node.rotation must contain 4 numbers");
        }
        if (model_importer_json_object_find(object, "scale", &value)) {
            if (!model_importer_json_array_real(value, 0u, &sx) ||
                !model_importer_json_array_real(value, 1u, &sy) ||
                !model_importer_json_array_real(value, 2u, &sz))
                return model_importer_set_error("node.scale must contain 3 numbers");
        }
        node->local_transform = model_importer_mat4_from_trs(tx, ty, tz, qx, qy, qz, qw, sx, sy, sz);
        node->rest_tx = tx; node->rest_ty = ty; node->rest_tz = tz;
        node->rest_qx = qx; node->rest_qy = qy;
        node->rest_qz = qz; node->rest_qw = qw;
        node->rest_sx = sx; node->rest_sy = sy; node->rest_sz = sz;
    }

    if (model_importer_json_object_find(object, "children", &value)) {
        if (!model_importer_json_array_count(value, &count))
            return model_importer_set_error("node.children must be an array");

        node->child_count = count;
        if (count > 0u) {
            node->children = (i32*)model_importer_calloc_count(count, sizeof(i32));
            if (!node->children)
                return model_importer_set_error("out of memory for node children");

            for (i = 0; i < count; ++i) {
                if (!model_importer_json_array_i32(value, i, &node->children[i]))
                    return model_importer_set_error("node child index must be an integer");
            }
        }
    }

    return 1;
}

static void model_importer_free_nodes(model_importer_node *nodes, u32 node_count)
{
    u32 i;
    if (!nodes) return;
    for (i = 0; i < node_count; ++i) {
        if (nodes[i].children) TAG_FREE(nodes[i].children);
    }
    TAG_FREE(nodes);
}

static int model_importer_visit_node(model_importer_node *nodes,
                                     u32 node_count,
                                     i32 node_index,
                                     model_importer_mat4 parent_transform,
                                     model_importer_mat4 *mesh_transforms,
                                     u8 *mesh_transform_set,
                                     u8 *mesh_node_is_skinned,
                                     u32 mesh_count,
                                     u32 depth)
{
    model_importer_node *node;
    model_importer_mat4 world_transform;
    u32 i;

    if (node_index < 0 || (u32)node_index >= node_count)
        return model_importer_set_error("scene references an invalid node");
    if (depth > node_count)
        return model_importer_set_error("node hierarchy contains a cycle");

    node = &nodes[node_index];
    world_transform = model_importer_mat4_mul(parent_transform, node->local_transform);
    node->world_transform = world_transform;
    node->world_valid = 1u;

    if (node->mesh >= 0 && (u32)node->mesh < mesh_count) {
        /* A skinned node's transform takes precedence over a static instancing
         * of the same mesh: the skinned path is handled at runtime, not baked. */
        if (node->skin >= 0) {
            mesh_transforms[node->mesh] = world_transform;
            mesh_transform_set[node->mesh] = 1u;
            if (mesh_node_is_skinned) mesh_node_is_skinned[node->mesh] = 1u;
        } else if (!mesh_transform_set[node->mesh]) {
            mesh_transforms[node->mesh] = world_transform;
            mesh_transform_set[node->mesh] = 1u;
            if (mesh_node_is_skinned) mesh_node_is_skinned[node->mesh] = 0u;
        }
    }

    for (i = 0; i < node->child_count; ++i) {
        if (!model_importer_visit_node(nodes, node_count, node->children[i], world_transform,
                                       mesh_transforms, mesh_transform_set,
                                       mesh_node_is_skinned, mesh_count, depth + 1u))
            return 0;
    }

    return 1;
}

static int model_importer_parse_mesh_transforms(model_importer_context *ctx,
                                                model_importer_json_span root)
{
    model_importer_json_span nodes_array, scenes_array, scene_object, scene_nodes, value;
    model_importer_node *nodes;
    model_importer_mat4 identity;
    u32 node_count, scene_count, root_count, i, c;
    i32 scene_index;
    int visited_scene;

    ctx->mesh_transforms = (model_importer_mat4*)model_importer_calloc_count(
        ctx->mesh_count, sizeof(model_importer_mat4));
    ctx->mesh_transform_set = (u8*)model_importer_calloc_count(ctx->mesh_count, sizeof(u8));
    ctx->mesh_node_is_skinned = (u8*)model_importer_calloc_count(ctx->mesh_count, sizeof(u8));
    if (!ctx->mesh_transforms || !ctx->mesh_transform_set || !ctx->mesh_node_is_skinned)
        return model_importer_set_error("out of memory for mesh transforms");

    identity = model_importer_mat4_identity();
    for (i = 0; i < ctx->mesh_count; ++i) ctx->mesh_transforms[i] = identity;

    if (!model_importer_json_object_find(root, "nodes", &nodes_array))
        return 1;

    if (!model_importer_json_array_count(nodes_array, &node_count))
        return model_importer_set_error("nodes must be an array");

    if (node_count == 0u) return 1;

    nodes = (model_importer_node*)model_importer_calloc_count(node_count, sizeof(model_importer_node));
    if (!nodes) return model_importer_set_error("out of memory for nodes");

    for (i = 0; i < node_count; ++i) {
        model_importer_json_span node_object;
        if (!model_importer_json_array_get(nodes_array, i, &node_object) ||
            !model_importer_parse_node(&nodes[i], node_object)) {
            model_importer_free_nodes(nodes, node_count);
            return 0;
        }
    }

    /* Build the parent index for every node (single-parent, last writer wins on
     * the pathological case of a node listed as a child of two parents). */
    for (i = 0; i < node_count; ++i) {
        for (c = 0; c < nodes[i].child_count; ++c) {
            i32 child = nodes[i].children[c];
            if (child >= 0 && (u32)child < node_count)
                nodes[child].parent = (i32)i;
        }
    }

    visited_scene = 0;
    scene_index = 0;
    if (model_importer_json_object_find(root, "scene", &value) &&
        !model_importer_json_parse_i32(value, &scene_index)) {
        model_importer_free_nodes(nodes, node_count);
        return model_importer_set_error("scene must be an integer");
    }

    if (model_importer_json_object_find(root, "scenes", &scenes_array) &&
        model_importer_json_array_count(scenes_array, &scene_count) &&
        scene_count > 0u) {
        if (scene_index < 0 || (u32)scene_index >= scene_count) scene_index = 0;

        if (model_importer_json_array_get(scenes_array, (u32)scene_index, &scene_object) &&
            model_importer_json_object_find(scene_object, "nodes", &scene_nodes) &&
            model_importer_json_array_count(scene_nodes, &root_count)) {
            for (i = 0; i < root_count; ++i) {
                i32 root_node;
                if (!model_importer_json_array_i32(scene_nodes, i, &root_node) ||
                    !model_importer_visit_node(nodes, node_count, root_node, identity,
                                               ctx->mesh_transforms, ctx->mesh_transform_set,
                                               ctx->mesh_node_is_skinned, ctx->mesh_count, 0u)) {
                    model_importer_free_nodes(nodes, node_count);
                    return 0;
                }
            }
            visited_scene = 1;
        }
    }

    (void)visited_scene;

    /* Sweep any node the scene walk did not reach, so a mesh stored outside the
     * active scene still gets a transform. Nodes already reached keep their
     * composed transform: re-deriving them from identity here would drop every
     * transform above them, and inverseBindMatrix is defined against the real
     * scene-space transform. */
    for (i = 0; i < node_count; ++i) {
        if (nodes[i].world_valid) continue;
        if (!model_importer_visit_node(nodes, node_count, (i32)i, identity,
                                       ctx->mesh_transforms, ctx->mesh_transform_set,
                                       ctx->mesh_node_is_skinned, ctx->mesh_count, 0u)) {
            model_importer_free_nodes(nodes, node_count);
            return 0;
        }
    }

    /* Retain the node array in the context for skin / animation resolution.
     * The caller frees it via model_importer_context_free. */
    ctx->nodes = nodes;
    ctx->node_count = node_count;
    return 1;
}

static int model_importer_count_model_primitives(model_importer_context *ctx)
{
    u32 mesh_index, total = 0u;

    for (mesh_index = 0; mesh_index < ctx->mesh_count; ++mesh_index) {
        model_importer_json_span mesh, primitives;
        u32 primitive_count;

        if (!model_importer_json_array_get(ctx->meshes, mesh_index, &mesh))
            return model_importer_set_error("could not read mesh object");

        if (!model_importer_json_object_find(mesh, "primitives", &primitives))
            return model_importer_set_error("mesh is missing primitives");

        if (!model_importer_json_array_count(primitives, &primitive_count))
            return model_importer_set_error("mesh.primitives must be an array");

        if (primitive_count == 0u)
            return model_importer_set_error("mesh has no primitives");

        if (primitive_count > model_primitive_block.max_element_count - total)
            return model_importer_set_error("GLB has more mesh primitives than model_primitive_block supports");

        total += primitive_count;
    }

    if (total == 0u) return model_importer_set_error("glTF has no mesh primitives");
    if (total > model_material_block.max_element_count)
        return model_importer_set_error("GLB has more mesh primitives than model_material_block supports");

    ctx->model_primitive_count = total;
    return 1;
}

static void model_importer_context_free(model_importer_context *ctx)
{
    if (!ctx) return;
    if (ctx->buffer_views) TAG_FREE(ctx->buffer_views);
    if (ctx->accessors) TAG_FREE(ctx->accessors);
    if (ctx->mesh_transforms) TAG_FREE(ctx->mesh_transforms);
    if (ctx->mesh_transform_set) TAG_FREE(ctx->mesh_transform_set);
    if (ctx->mesh_node_is_skinned) TAG_FREE(ctx->mesh_node_is_skinned);
    if (ctx->nodes) model_importer_free_nodes(ctx->nodes, ctx->node_count);
    memset(ctx, 0, sizeof(*ctx));
}

static int model_importer_context_init(model_importer_context *ctx,
                                       model_importer_json_span root,
                                       const u8 *bin,
                                       u32 bin_size)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->bin = bin;
    ctx->bin_size = bin_size;

    if (!model_importer_parse_buffer_views(ctx, root)) return 0;
    if (!model_importer_parse_accessors(ctx, root)) return 0;

    if (!model_importer_json_object_find(root, "meshes", &ctx->meshes))
        return model_importer_set_error("glTF JSON is missing meshes");

    if (!model_importer_json_array_count(ctx->meshes, &ctx->mesh_count))
        return model_importer_set_error("meshes must be an array");

    if (ctx->mesh_count == 0u) return model_importer_set_error("glTF has no meshes");

    if (!model_importer_parse_mesh_transforms(ctx, root)) return 0;
    if (!model_importer_count_model_primitives(ctx)) return 0;

    /* Optional animation top-level arrays. */
    if (model_importer_json_object_find(root, "skins", &ctx->skins)) {
        if (!model_importer_json_array_count(ctx->skins, &ctx->skin_count))
            return model_importer_set_error("skins must be an array");
    }

    if (model_importer_json_object_find(root, "animations", &ctx->animations)) {
        if (!model_importer_json_array_count(ctx->animations, &ctx->animation_count))
            return model_importer_set_error("animations must be an array");
    }

    return 1;
}

static int model_importer_accessor_element_ptr(model_importer_context *ctx,
                                               i32 accessor_index,
                                               u32 element_index,
                                               const u8 **out_ptr)
{
    model_importer_accessor *accessor;
    model_importer_buffer_view *view;
    u32 component_size;
    size_t stride, element_size, view_offset, view_length, accessor_offset, relative_offset;

    if (!out_ptr) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("accessor index is out of range");

    accessor = &ctx->accessors[accessor_index];
    if (element_index >= accessor->count)
        return model_importer_set_error("accessor element is out of range");
    if (accessor->buffer_view < 0 || (u32)accessor->buffer_view >= ctx->buffer_view_count)
        return model_importer_set_error("accessor has an invalid bufferView");

    view = &ctx->buffer_views[accessor->buffer_view];
    if (view->buffer != 0u) return model_importer_set_error("only GLB buffer 0 is supported");

    component_size = model_importer_accessor_component_size(accessor->component_type);
    if (component_size == 0u)
        return model_importer_set_error("accessor component type is unsupported");

    element_size = (size_t)component_size * (size_t)accessor->component_count;
    stride = view->byte_stride ? (size_t)view->byte_stride : element_size;
    view_offset = (size_t)view->byte_offset;
    view_length = (size_t)view->byte_length;
    accessor_offset = (size_t)accessor->byte_offset;

    if (view_offset > (size_t)ctx->bin_size)
        return model_importer_set_error("bufferView offset is outside BIN chunk");
    if (view_length > (size_t)ctx->bin_size - view_offset)
        return model_importer_set_error("bufferView length is outside BIN chunk");
    if (accessor_offset > view_length)
        return model_importer_set_error("accessor offset is outside bufferView");
    if (stride != 0u && (size_t)element_index > ((size_t)-1 - accessor_offset) / stride)
        return model_importer_set_error("accessor byte offset overflowed");

    relative_offset = accessor_offset + (size_t)element_index * stride;
    if (relative_offset > view_length || element_size > view_length - relative_offset)
        return model_importer_set_error("accessor element is outside bufferView");

    *out_ptr = ctx->bin + view_offset + relative_offset;
    return 1;
}

static int model_importer_read_accessor_vec3(model_importer_context *ctx,
                                             i32 accessor_index,
                                             u32 element_index,
                                             vec3 *out_value)
{
    model_importer_accessor *accessor;
    const u8 *ptr;

    if (!out_value) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("vec3 accessor index is out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT ||
        accessor->component_count != 3u)
        return model_importer_set_error("POSITION/NORMAL accessors must be FLOAT VEC3");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    out_value->position.x = model_importer_read_f32le(ptr);
    out_value->position.y = model_importer_read_f32le(ptr + 4);
    out_value->position.z = model_importer_read_f32le(ptr + 8);
    return 1;
}

static int model_importer_read_accessor_index(model_importer_context *ctx,
                                              i32 accessor_index,
                                              u32 element_index,
                                              u32 *out_value)
{
    model_importer_accessor *accessor;
    const u8 *ptr;

    if (!out_value) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("index accessor is out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_count != 1u)
        return model_importer_set_error("index accessor must be SCALAR");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    switch (accessor->component_type) {
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE:
            *out_value = (u32)ptr[0];
            return 1;
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_SHORT:
            *out_value = (u32)model_importer_read_u16le(ptr);
            return 1;
        case MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_INT:
            *out_value = model_importer_read_u32le(ptr);
            return 1;
        default:
            return model_importer_set_error("index accessor component type is unsupported");
    }
}

/* --------------------------------------------------------------------------
 * New helpers for reading extended vertex attributes
 * -------------------------------------------------------------------------- */

static int model_importer_read_accessor_vec2(model_importer_context *ctx,
                                             i32 accessor_index,
                                             u32 element_index,
                                             vec2 *out_value)
{
    model_importer_accessor *accessor;
    const u8 *ptr;

    if (!out_value) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("vec2 accessor index out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT ||
        accessor->component_count != 2u)
        return model_importer_set_error("TEXCOORD accessor must be FLOAT VEC2");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    out_value->textcoord.s = model_importer_read_f32le(ptr);
    out_value->textcoord.t = model_importer_read_f32le(ptr + 4);
    return 1;
}

static int model_importer_read_accessor_vec4(model_importer_context *ctx,
                                             i32 accessor_index,
                                             u32 element_index,
                                             vec4 *out_value)
{
    model_importer_accessor *accessor;
    const u8 *ptr;

    if (!out_value) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("vec4 accessor index out of range");

    accessor = &ctx->accessors[accessor_index];

    /* TANGENT is always FLOAT VEC4. COLORS can be FLOAT VEC4 or UNSIGNED_BYTE normalized */
    if (accessor->component_type == MODEL_IMPORTER_GLTF_COMPONENT_FLOAT &&
        accessor->component_count == 4u) {
        if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
            return 0;
        out_value->rotation.i = model_importer_read_f32le(ptr);
        out_value->rotation.j = model_importer_read_f32le(ptr + 4);
        out_value->rotation.k = model_importer_read_f32le(ptr + 8);
        out_value->rotation.w = model_importer_read_f32le(ptr + 12);
        return 1;
    }

    if (accessor->component_type == MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE &&
        accessor->component_count == 4u && accessor->normalized) {
        if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
            return 0;
        out_value->color.r = ptr[0] / 255.0f;
        out_value->color.g = ptr[1] / 255.0f;
        out_value->color.b = ptr[2] / 255.0f;
        out_value->color.a = ptr[3] / 255.0f;
        return 1;
    }

    return model_importer_set_error("unsupported accessor type for vec4 (must be FLOAT VEC4 or normalized UNSIGNED_BYTE VEC4)");
}

static int model_importer_read_accessor_joints(model_importer_context *ctx,
                                               i32 accessor_index,
                                               u32 element_index,
                                               u16 out[4])
{
    model_importer_accessor *accessor;
    const u8 *ptr;
    u32 i;

    if (!out) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("joints accessor index out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_count != 4u)
        return model_importer_set_error("JOINTS accessor must be VEC4");

    if (accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE &&
        accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_SHORT)
        return model_importer_set_error("JOINTS accessor must be UNSIGNED_BYTE or UNSIGNED_SHORT");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    for (i = 0; i < 4; ++i) {
        if (accessor->component_type == MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE)
            out[i] = ptr[i];
        else
            out[i] = model_importer_read_u16le(ptr + i * 2);
    }
    return 1;
}

static int model_importer_read_accessor_weights(model_importer_context *ctx,
                                                i32 accessor_index,
                                                u32 element_index,
                                                u8 out[4])
{
    model_importer_accessor *accessor;
    const u8 *ptr;
    u32 i;

    if (!out) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("weights accessor index out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_count != 4u)
        return model_importer_set_error("WEIGHTS accessor must be VEC4");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    if (accessor->component_type == MODEL_IMPORTER_GLTF_COMPONENT_FLOAT) {
        real f;
        for (i = 0; i < 4; ++i) {
            f = model_importer_read_f32le(ptr + i * 4);
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            out[i] = (u8)(f * 255.0f + 0.5f);
        }
        return 1;
    }

    if (accessor->component_type == MODEL_IMPORTER_GLTF_COMPONENT_UNSIGNED_BYTE &&
        accessor->normalized) {
        for (i = 0; i < 4; ++i)
            out[i] = ptr[i];
        return 1;
    }

    return model_importer_set_error("unsupported WEIGHTS type (must be FLOAT VEC4 or normalized UNSIGNED_BYTE VEC4)");
}

/* --------------------------------------------------------------------------
 * Accessor readers for skinning + animation data
 * -------------------------------------------------------------------------- */

/* Read a FLOAT MAT4 accessor element (e.g. skin.inverseBindMatrices) into a
 * row-major mat4, transposing glTF's column-major storage. */
static int model_importer_read_accessor_mat4(model_importer_context *ctx,
                                             i32 accessor_index,
                                             u32 element_index,
                                             mat4 *out_value)
{
    model_importer_accessor *accessor;
    const u8 *ptr;
    real gltf_matrix[16];
    u32 i;

    if (!out_value) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("MAT4 accessor index out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT ||
        accessor->component_count != 16u)
        return model_importer_set_error("MAT4 accessor must be FLOAT");

    if (!model_importer_accessor_element_ptr(ctx, accessor_index, element_index, &ptr))
        return 0;

    for (i = 0; i < 16u; ++i) gltf_matrix[i] = model_importer_read_f32le(ptr + i * 4);
    *out_value = model_importer_to_mat4(model_importer_mat4_from_gltf_matrix(gltf_matrix));
    return 1;
}

/* Read every float component of a FLOAT accessor into a freshly allocated real
 * array (count * component_count entries). Used for animation sampler input
 * (times) and output (values). The caller TAG_FREEs the result. */
static int model_importer_read_accessor_float_array(model_importer_context *ctx,
                                                    i32 accessor_index,
                                                    real **out_values,
                                                    u32 *out_count)
{
    model_importer_accessor *accessor;
    real *values;
    u32 total, element, component;
    u32 component_count;

    if (!out_values || !out_count) return 0;
    *out_values = NULL;
    *out_count = 0u;

    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("animation accessor index out of range");

    accessor = &ctx->accessors[accessor_index];
    if (accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT)
        return model_importer_set_error("animation sampler accessors must be FLOAT");

    component_count = accessor->component_count;
    total = accessor->count * component_count;
    if (total == 0u) {
        *out_count = 0u;
        return 1;   /* legitimately empty; leave values NULL */
    }

    values = (real*)model_importer_calloc_count(total, sizeof(real));
    if (!values) return model_importer_set_error("out of memory for animation sampler data");

    for (element = 0; element < accessor->count; ++element) {
        const u8 *ptr;
        if (!model_importer_accessor_element_ptr(ctx, accessor_index, element, &ptr)) {
            TAG_FREE(values);
            return 0;
        }
        for (component = 0; component < component_count; ++component)
            values[element * component_count + component] =
                model_importer_read_f32le(ptr + component * 4);
    }

    *out_values = values;
    *out_count = total;
    return 1;
}

/* --------------------------------------------------------------------------
 * Mesh primitive filling with all attributes
 * -------------------------------------------------------------------------- */

static int model_importer_parse_mesh_primitive(model_importer_json_span primitive,
                                               i32 *out_position_accessor,
                                               i32 *out_normal_accessor,
                                               i32 *out_index_accessor,
                                               i32 *out_mode)
{
    model_importer_json_span attributes, value;

    if (!out_position_accessor || !out_normal_accessor || !out_index_accessor || !out_mode)
        return model_importer_set_error("invalid primitive parse arguments");

    *out_position_accessor = -1;
    *out_normal_accessor = -1;
    *out_index_accessor = -1;
    *out_mode = MODEL_IMPORTER_GLTF_MODE_TRIANGLES;

    if (!model_importer_json_object_find(primitive, "attributes", &attributes))
        return model_importer_set_error("mesh primitive is missing attributes");

    if (!model_importer_json_object_find(attributes, "POSITION", &value) ||
        !model_importer_json_parse_i32(value, out_position_accessor))
        return model_importer_set_error("mesh primitive is missing POSITION");

    if (model_importer_json_object_find(attributes, "NORMAL", &value) &&
        !model_importer_json_parse_i32(value, out_normal_accessor))
        return model_importer_set_error("mesh primitive NORMAL must be an integer");

    if (model_importer_json_object_find(primitive, "indices", &value) &&
        !model_importer_json_parse_i32(value, out_index_accessor))
        return model_importer_set_error("mesh primitive indices must be an integer");

    if (model_importer_json_object_find(primitive, "mode", &value) &&
        !model_importer_json_parse_i32(value, out_mode))
        return model_importer_set_error("mesh primitive mode must be an integer");

    return 1;
}

static int model_importer_primitive_counts(model_importer_context *ctx,
                                           model_importer_json_span primitive,
                                           u32 *out_vertex_count,
                                           u32 *out_index_count,
                                           i32 *out_needs_normals)
{
    model_importer_accessor *position_accessor, *index_accessor;
    i32 position_index, normal_index, index_index, mode;
    u32 index_count;

    if (!model_importer_parse_mesh_primitive(primitive, &position_index, &normal_index, &index_index, &mode))
        return 0;

    if (mode != MODEL_IMPORTER_GLTF_MODE_TRIANGLES)
        return model_importer_set_error("only triangle mesh primitives are supported");

    if (position_index < 0 || (u32)position_index >= ctx->accessor_count)
        return model_importer_set_error("POSITION accessor is out of range");

    position_accessor = &ctx->accessors[position_index];
    if (position_accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT ||
        position_accessor->component_count != 3u)
        return model_importer_set_error("POSITION accessor must be FLOAT VEC3");

    if (normal_index >= 0) {
        model_importer_accessor *normal_accessor;
        if ((u32)normal_index >= ctx->accessor_count)
            return model_importer_set_error("NORMAL accessor is out of range");
        normal_accessor = &ctx->accessors[normal_index];
        if (normal_accessor->component_type != MODEL_IMPORTER_GLTF_COMPONENT_FLOAT ||
            normal_accessor->component_count != 3u ||
            normal_accessor->count != position_accessor->count)
            return model_importer_set_error("NORMAL accessor must be FLOAT VEC3 and match POSITION count");
    }

    if (index_index >= 0) {
        if ((u32)index_index >= ctx->accessor_count)
            return model_importer_set_error("indices accessor is out of range");
        index_accessor = &ctx->accessors[index_index];
        index_count = index_accessor->count;
    } else {
        index_count = position_accessor->count;
    }

    if ((index_count % 3u) != 0u)
        return model_importer_set_error("triangle primitive index count must be divisible by 3");

    if (position_accessor->count > model_vertex_block.max_element_count)
        return model_importer_set_error("mesh primitive has too many vertices for model_vertex_block");
    if (index_count > model_index_block.max_element_count)
        return model_importer_set_error("mesh primitive has too many indices for model_index_block");

    *out_vertex_count = position_accessor->count;
    *out_index_count = index_count;
    *out_needs_normals = normal_index < 0;
    return 1;
}

static void model_importer_bounds_include(real_bounding_box *bounds, vec3 point, i32 *has_bounds)
{
    if (!*has_bounds) {
        bounds->x.lower = bounds->x.upper = point.position.x;
        bounds->y.lower = bounds->y.upper = point.position.y;
        bounds->z.lower = bounds->z.upper = point.position.z;
        *has_bounds = 1;
        return;
    }
    if (point.position.x < bounds->x.lower) bounds->x.lower = point.position.x;
    if (point.position.x > bounds->x.upper) bounds->x.upper = point.position.x;
    if (point.position.y < bounds->y.lower) bounds->y.lower = point.position.y;
    if (point.position.y > bounds->y.upper) bounds->y.upper = point.position.y;
    if (point.position.z < bounds->z.lower) bounds->z.lower = point.position.z;
    if (point.position.z > bounds->z.upper) bounds->z.upper = point.position.z;
}

static void model_importer_compute_normals(model_vertex *vertices, u32 vertex_count,
                                           const u32 *indices, u32 index_count)
{
    u32 i;

    for (i = 0; i < vertex_count; ++i) {
        vertices[i].normal.position.x = 0.0f;
        vertices[i].normal.position.y = 0.0f;
        vertices[i].normal.position.z = 0.0f;
    }

    for (i = 0; i + 2u < index_count; i += 3u) {
        u32 i0 = indices[i + 0u], i1 = indices[i + 1u], i2 = indices[i + 2u];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) continue;

        vec3 edge_a = model_importer_vec3_sub(vertices[i1].position, vertices[i0].position);
        vec3 edge_b = model_importer_vec3_sub(vertices[i2].position, vertices[i0].position);
        vec3 face_normal = model_importer_vec3_cross(edge_a, edge_b);

        vertices[i0].normal = model_importer_vec3_add(vertices[i0].normal, face_normal);
        vertices[i1].normal = model_importer_vec3_add(vertices[i1].normal, face_normal);
        vertices[i2].normal = model_importer_vec3_add(vertices[i2].normal, face_normal);
    }

    for (i = 0; i < vertex_count; ++i) {
        vertices[i].normal = model_importer_vec3_normalize_or_up(vertices[i].normal);
    }
}

/* --------------------------------------------------------------------------
 * Morph targets (blend shapes) - per-primitive position/normal/tangent deltas.
 * Animation channels with path == 'weights' drive these, so the data must be
 * loaded for weights animation to have any visible effect.
 * -------------------------------------------------------------------------- */
static int model_importer_fill_morph_targets(model_importer_context *ctx,
                                             model_importer_json_span primitive,
                                             u32 vertex_count,
                                             const real *default_weights,
                                             u32 default_weight_count,
                                             struct tag_block *out_targets)
{
    model_importer_json_span targets_json;
    model_morph_target *targets;
    u32 target_count, t;
    u32 max_targets = (u32)model_morph_target_block.max_element_count;

    out_targets->count = 0;
    out_targets->address = NULL;

    if (!model_importer_json_object_find(primitive, "targets", &targets_json))
        return 1;   /* no morph targets */
    if (!model_importer_json_array_count(targets_json, &target_count))
        return model_importer_set_error("primitive.targets must be an array");
    if (target_count == 0u) return 1;
    if (target_count > max_targets)
        return model_importer_set_error("primitive has more morph targets than model_morph_target_block supports");

    targets = (model_morph_target*)model_importer_calloc_count(target_count, sizeof(model_morph_target));
    if (!targets)
        return model_importer_set_error("out of memory for morph targets");

    for (t = 0; t < target_count; ++t) {
        model_importer_json_span target, aval;
        i32 pos_index = -1, nrm_index = -1, tan_index = -1;
        real *deltas = NULL;
        u32 count = 0u;

        if (!model_importer_json_array_get(targets_json, t, &target)) {
            TAG_FREE(targets);
            return model_importer_set_error("could not read morph target");
        }
        /* glTF morph targets carry their accessors directly on the target
         * object (POSITION / NORMAL / TANGENT), with no "attributes" wrapper. */
        if (model_importer_json_object_find(target, "POSITION", &aval) &&
            !model_importer_json_parse_i32(aval, &pos_index)) { TAG_FREE(targets); return model_importer_set_error("morph POSITION must be an integer"); }
        if (model_importer_json_object_find(target, "NORMAL", &aval) &&
            !model_importer_json_parse_i32(aval, &nrm_index)) { TAG_FREE(targets); return model_importer_set_error("morph NORMAL must be an integer"); }
        if (model_importer_json_object_find(target, "TANGENT", &aval) &&
            !model_importer_json_parse_i32(aval, &tan_index)) { TAG_FREE(targets); return model_importer_set_error("morph TANGENT must be an integer"); }

        /* Deltas: one entry per vertex. Stored as raw float arrays; vec3/vec4
         * share the same layout so the arrays can back the vec3/vec4 blocks. */
        if (pos_index >= 0) {
            if (!model_importer_read_accessor_float_array(ctx, pos_index, &deltas, &count) ||
                count != vertex_count * 3u) {
                if (deltas) TAG_FREE(deltas);
                TAG_FREE(targets);
                return model_importer_set_error("morph POSITION deltas must be VEC3 per vertex");
            }
            targets[t].position_deltas.count = vertex_count;
            targets[t].position_deltas.address = deltas;
        }
        if (nrm_index >= 0) {
            deltas = NULL;
            if (!model_importer_read_accessor_float_array(ctx, nrm_index, &deltas, &count) ||
                count != vertex_count * 3u) {
                if (deltas) TAG_FREE(deltas);
                TAG_FREE(targets);
                return model_importer_set_error("morph NORMAL deltas must be VEC3 per vertex");
            }
            targets[t].normal_deltas.count = vertex_count;
            targets[t].normal_deltas.address = deltas;
        }
        if (tan_index >= 0) {
            deltas = NULL;
            /* glTF morph TANGENT carries xyz deltas; the w handedness bit is
             * optional and not a delta, so VEC3 is the normal case. Accept
             * VEC4 and keep only xyz, since tangent_deltas is a vec3 block. */
            if (!model_importer_read_accessor_float_array(ctx, tan_index, &deltas, &count) ||
                (count != vertex_count * 3u && count != vertex_count * 4u)) {
                if (deltas) TAG_FREE(deltas);
                TAG_FREE(targets);
                return model_importer_set_error("morph TANGENT deltas must be VEC3 or VEC4 per vertex");
            }
            if (count == vertex_count * 4u) {
                real *src = deltas;
                real *dst = deltas;
                u32 vi;
                for (vi = 0; vi < vertex_count; ++vi) {
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                    dst += 3; src += 4;
                }
            }
            targets[t].tangent_deltas.count = vertex_count;
            targets[t].tangent_deltas.address = deltas;
        }

        targets[t].default_weight = (t < default_weight_count) ? default_weights[t] : 0.0f;
    }

    out_targets->count = target_count;
    out_targets->address = targets;
    return 1;
}

static int model_importer_fill_mesh_primitive(model_importer_context *ctx,
                                              u32 mesh_index,
                                              u32 material_index,
                                              model_importer_json_span primitive,
                                              model_primitive *out_primitive,
                                              const real *default_weights,
                                              u32 default_weight_count,
                                              real_bounding_box *bounds,
                                              i32 *has_bounds)
{
    model_vertex *vertices;
    u32 *indices;
    u32 vertex_count, index_count;
    i32 needs_normals;
    model_importer_mat4 transform;
    i32 position_index, normal_index, index_index, mode;
    u32 i;

    if (!model_importer_primitive_counts(ctx, primitive, &vertex_count, &index_count, &needs_normals))
        return 0;

    vertices = (model_vertex*)model_importer_calloc_count(vertex_count, sizeof(model_vertex));
    indices = (u32*)model_importer_calloc_count(index_count, sizeof(u32));
    if (!vertices || !indices) {
        if (vertices) TAG_FREE(vertices);
        if (indices) TAG_FREE(indices);
        return model_importer_set_error("out of memory for mesh geometry");
    }

    out_primitive->vertices.count = vertex_count;
    out_primitive->vertices.address = vertices;
    out_primitive->indices.count = index_count;
    out_primitive->indices.address = indices;
    out_primitive->material_index = (i32)material_index;
    out_primitive->is_skinned = 0;
    out_primitive->node_transform = model_importer_to_mat4(model_importer_mat4_identity());
    /* morph_targets block already zero-initialised by calloc on the primitive itself */

    transform = ctx->mesh_transforms ? ctx->mesh_transforms[mesh_index] : model_importer_mat4_identity();

    if (!model_importer_parse_mesh_primitive(primitive, &position_index, &normal_index, &index_index, &mode))
        return 0;
    (void)mode;

    /* --- Parse additional attribute accessor indices --- */
    model_importer_json_span attributes;
    model_importer_json_span value;
    i32 tangent_index = -1;
    i32 texcoord_indices[8];
    i32 color_indices[4];
    i32 joints0_index = -1, joints1_index = -1;
    i32 weights0_index = -1, weights1_index = -1;
    char key[32];

    for (i = 0; i < 8; ++i) texcoord_indices[i] = -1;
    for (i = 0; i < 4; ++i) color_indices[i] = -1;

    if (!model_importer_json_object_find(primitive, "attributes", &attributes))
        return model_importer_set_error("attributes missing (should have been checked earlier)");

    if (model_importer_json_object_find(attributes, "TANGENT", &value) &&
        !model_importer_json_parse_i32(value, &tangent_index))
        return model_importer_set_error("TANGENT must be an integer");

    for (i = 0; i < 8; ++i) {
        snprintf(key, sizeof(key), "TEXCOORD_%u", i);
        if (model_importer_json_object_find(attributes, key, &value) &&
            !model_importer_json_parse_i32(value, &texcoord_indices[i]))
            return model_importer_set_error("TEXCOORD index must be an integer");
    }

    for (i = 0; i < 4; ++i) {
        snprintf(key, sizeof(key), "COLOR_%u", i);
        if (model_importer_json_object_find(attributes, key, &value) &&
            !model_importer_json_parse_i32(value, &color_indices[i]))
            return model_importer_set_error("COLOR index must be an integer");
    }

    if (model_importer_json_object_find(attributes, "JOINTS_0", &value) &&
        !model_importer_json_parse_i32(value, &joints0_index))
        return model_importer_set_error("JOINTS_0 must be an integer");
    if (model_importer_json_object_find(attributes, "JOINTS_1", &value) &&
        !model_importer_json_parse_i32(value, &joints1_index))
        return model_importer_set_error("JOINTS_1 must be an integer");
    if (model_importer_json_object_find(attributes, "WEIGHTS_0", &value) &&
        !model_importer_json_parse_i32(value, &weights0_index))
        return model_importer_set_error("WEIGHTS_0 must be an integer");
    if (model_importer_json_object_find(attributes, "WEIGHTS_1", &value) &&
        !model_importer_json_parse_i32(value, &weights1_index))
        return model_importer_set_error("WEIGHTS_1 must be an integer");

    /* Verify that all present attribute accessors have matching vertex count */
    #define CHECK_ACCESSOR_COUNT(idx) \
        if ((idx) >= 0) { \
            if ((u32)(idx) >= ctx->accessor_count) \
                return model_importer_set_error("accessor index out of range"); \
            if (ctx->accessors[(idx)].count != vertex_count) \
                return model_importer_set_error("attribute accessor count does not match vertex count"); \
        }

    CHECK_ACCESSOR_COUNT(tangent_index);
    for (i = 0; i < 8; ++i) CHECK_ACCESSOR_COUNT(texcoord_indices[i]);
    for (i = 0; i < 4; ++i) CHECK_ACCESSOR_COUNT(color_indices[i]);
    CHECK_ACCESSOR_COUNT(joints0_index);
    CHECK_ACCESSOR_COUNT(joints1_index);
    CHECK_ACCESSOR_COUNT(weights0_index);
    CHECK_ACCESSOR_COUNT(weights1_index);

    /* We also need to check that JOINT_1 and WEIGHT_1 are either both present or both absent */
    if ((joints1_index >= 0) != (weights1_index >= 0))
        return model_importer_set_error("JOINTS_1 and WEIGHTS_1 must be both present or both absent");

    /* A primitive is skinned when it carries joint influences. For a skinned
     * primitive the mesh node's world transform is NOT baked into the vertices
     * (the runtime composes node_transform with the skinning matrices), so we
     * switch the bake transform to identity and stash the real transform. */
    if (joints0_index >= 0) {
        out_primitive->is_skinned = 1;
        out_primitive->node_transform = model_importer_to_mat4(transform);
        transform = model_importer_mat4_identity();
    }

    /* --- Read per-vertex data --- */
    for (i = 0; i < vertex_count; ++i) {
        vec3 position, normal;

        /* Position */
        if (!model_importer_read_accessor_vec3(ctx, position_index, i, &position))
            return 0;
        position = model_importer_transform_point(transform, position);
        vertices[i].position = position;
        model_importer_bounds_include(bounds, position, has_bounds);

        /* Normal */
        if (normal_index >= 0) {
            if (!model_importer_read_accessor_vec3(ctx, normal_index, i, &normal))
                return 0;
            vertices[i].normal = model_importer_transform_normal(transform, normal);
        }

        /* Tangent */
        if (tangent_index >= 0) {
            vec4 tangent;
            if (!model_importer_read_accessor_vec4(ctx, tangent_index, i, &tangent))
                return 0;
            /* Transform tangent? Usually tangent is a direction, but glTF tangents are in local space.
             * We can transform it like a normal (using the same matrix) but we need to handle w separately. */
            vec3 tang_vec = { tangent.rotation.i, tangent.rotation.j, tangent.rotation.k };
            tang_vec = model_importer_transform_normal(transform, tang_vec);
            vertices[i].tangent.rotation.i = tang_vec.rotation.i;
            vertices[i].tangent.rotation.j = tang_vec.rotation.j;
            vertices[i].tangent.rotation.k = tang_vec.rotation.k;
            vertices[i].tangent.rotation.w = tangent.rotation.w; /* handedness stays as is */
        }

        /* UVs */
        for (u32 uv = 0; uv < 8; ++uv) {
            if (texcoord_indices[uv] >= 0) {
                vec2 uv_val;
                if (!model_importer_read_accessor_vec2(ctx, texcoord_indices[uv], i, &uv_val))
                    return 0;
                switch (uv) {
                    case 0: vertices[i].uv0 = uv_val; break;
                    case 1: vertices[i].uv1 = uv_val; break;
                    case 2: vertices[i].uv2 = uv_val; break;
                    case 3: vertices[i].uv3 = uv_val; break;
                    case 4: vertices[i].uv4 = uv_val; break;
                    case 5: vertices[i].uv5 = uv_val; break;
                    case 6: vertices[i].uv6 = uv_val; break;
                    case 7: vertices[i].uv7 = uv_val; break;
                }
            }
        }

        /* Colors */
        for (u32 col = 0; col < 4; ++col) {
            if (color_indices[col] >= 0) {
                vec4 col_val;
                if (!model_importer_read_accessor_vec4(ctx, color_indices[col], i, &col_val))
                    return 0;
                switch (col) {
                    case 0: vertices[i].color0 = col_val; break;
                    case 1: vertices[i].color1 = col_val; break;
                    case 2: vertices[i].color2 = col_val; break;
                    case 3: vertices[i].color3 = col_val; break;
                }
            }
        }

        /* Bone influences. glTF supplies two sets of four (JOINTS_0/WEIGHTS_0
         * and optionally JOINTS_1/WEIGHTS_1). The GPU path blends the four
         * heaviest influences, so merge both sets, keep the four largest
         * weights, and renormalise them to sum to 255. Joints with zero
         * weight are dropped so the shader's fixed loop stays branch-free on
         * a weight of zero rather than indexing a stale joint. */
        if (joints0_index >= 0 && weights0_index >= 0) {
            u16 j[8]; u8 w[8];
            u32 k, n = 0;
            if (!model_importer_read_accessor_joints(ctx, joints0_index, i, j) ||
                !model_importer_read_accessor_weights(ctx, weights0_index, i, w))
                return 0;
            for (k = 0; k < 4; ++k) { j[n] = j[k]; w[n] = w[k]; ++n; }
            if (joints1_index >= 0 && weights1_index >= 0) {
                if (!model_importer_read_accessor_joints(ctx, joints1_index, i, j + 4) ||
                    !model_importer_read_accessor_weights(ctx, weights1_index, i, w + 4))
                    return 0;
                n = 8;
            }

            /* Selection sort for the top 4 by weight. Eight elements, so the
             * O(n^2) cost is irrelevant and it needs no extra storage. */
            for (k = 0; k < 4; ++k) {
                u32 best = k, b;
                for (b = k + 1u; b < n; ++b)
                    if (w[b] > w[best]) best = b;
                if (best != k) {
                    u16 tj = j[k]; u8 tw = w[k];
                    j[k] = j[best]; w[k] = w[best];
                    j[best] = tj;  w[best] = tw;
                }
            }

            {
                u32 total = (u32)w[0] + w[1] + w[2] + w[3];
                u32 acc = 0;
                u16 *idx_out[4];
                u8  *wgt_out[4];
                idx_out[0] = &vertices[i].bone_index0; wgt_out[0] = &vertices[i].bone_weight0;
                idx_out[1] = &vertices[i].bone_index1; wgt_out[1] = &vertices[i].bone_weight1;
                idx_out[2] = &vertices[i].bone_index2; wgt_out[2] = &vertices[i].bone_weight2;
                idx_out[3] = &vertices[i].bone_index3; wgt_out[3] = &vertices[i].bone_weight3;
                for (k = 0; k < 4u; ++k) {
                    u8 nw;
                    if (total == 0u) {
                        nw = 0u;
                    } else if (k == 3u) {
                        /* The last slot absorbs the remainder so the four
                         * weights always sum to exactly 255. The first three use
                         * floor division, which keeps their running total at or
                         * below 255 -- rounding up instead can overshoot, and the
                         * subtraction below would then underflow to 255 and give
                         * this vertex a doubled total weight. */
                        nw = (u8)(255u - acc);
                    } else {
                        nw = (u8)(((u32)w[k] * 255u) / total);
                        acc += nw;
                    }
                    *wgt_out[k] = nw;
                    *idx_out[k] = nw ? j[k] : 0u;
                }
            }
        }
    }

    /* Indices */
    for (i = 0; i < index_count; ++i) {
        u32 local_index;
        if (index_index >= 0) {
            if (!model_importer_read_accessor_index(ctx, index_index, i, &local_index))
                return 0;
        } else {
            local_index = i;
        }
        if (local_index >= vertex_count)
            return model_importer_set_error("mesh index references a missing vertex");
        indices[i] = local_index;
    }

    if (needs_normals)
        model_importer_compute_normals(vertices, vertex_count, indices, index_count);

    if (!model_importer_fill_morph_targets(ctx, primitive, vertex_count,
                                          default_weights, default_weight_count,
                                          &out_primitive->morph_targets))
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------
 * Skins -> model.skeleton
 *
 * The model has a single skeleton block, so the first skin is used. Each joint
 * records its glTF node index (so animation channels can be resolved to joints
 * at runtime), its rest-pose local transform, and its inverse bind matrix.
 * Parents are the nearest ancestor node that is itself a joint.
 * -------------------------------------------------------------------------- */
static int model_importer_parse_skins(model_importer_context *ctx, model_definition *model)
{
    model_importer_json_span skin, joints, value;
    model_joint *skeleton;
    i32 *node_to_joint;
    u32 joint_count, i;
    i32 ibm_index = -1;
    model_importer_mat4 identity = model_importer_mat4_identity();

    model->skeleton.count = 0;
    model->skeleton.address = NULL;

    if (ctx->skin_count == 0u) return 1;

    if (!model_importer_json_array_get(ctx->skins, 0u, &skin))
        return model_importer_set_error("could not read skin object");

    if (!model_importer_json_object_find(skin, "joints", &joints))
        return 1;   /* skin carries no joints - nothing to import */
    if (!model_importer_json_array_count(joints, &joint_count))
        return model_importer_set_error("skin.joints must be an array");
    if (joint_count == 0u) return 1;
    if (joint_count > (u32)model_joint_block.max_element_count)
        return model_importer_set_error("skin has more joints than model_joint_block supports");

    if (model_importer_json_object_find(skin, "inverseBindMatrices", &value) &&
        !model_importer_json_parse_i32(value, &ibm_index))
        return model_importer_set_error("skin.inverseBindMatrices must be an integer");

    skeleton = (model_joint*)model_importer_calloc_count(joint_count, sizeof(model_joint));
    node_to_joint = (i32*)model_importer_calloc_count(ctx->node_count ? ctx->node_count : 1u, sizeof(i32));
    if (!skeleton || !node_to_joint) {
        if (skeleton) TAG_FREE(skeleton);
        if (node_to_joint) TAG_FREE(node_to_joint);
        return model_importer_set_error("out of memory for skeleton");
    }
    for (i = 0; i < ctx->node_count; ++i) node_to_joint[i] = -1;

    for (i = 0; i < joint_count; ++i) {
        i32 node_index;
        if (!model_importer_json_array_i32(joints, i, &node_index)) {
            TAG_FREE(skeleton);
            TAG_FREE(node_to_joint);
            return model_importer_set_error("skin joint index must be an integer");
        }
        if (node_index < 0 || (u32)node_index >= ctx->node_count) {
            TAG_FREE(skeleton);
            TAG_FREE(node_to_joint);
            return model_importer_set_error("skin joint references an invalid node");
        }

        skeleton[i].parent = -1;
        skeleton[i].node_index = node_index;
        skeleton[i].name = ctx->nodes[node_index].name;
        skeleton[i].bind_local = model_importer_to_mat4(ctx->nodes[node_index].local_transform);
        skeleton[i].rest_translation = vec3_init_from_3(ctx->nodes[node_index].rest_tx,
                                                        ctx->nodes[node_index].rest_ty,
                                                        ctx->nodes[node_index].rest_tz);
        skeleton[i].rest_rotation = vec4_init_from_4(ctx->nodes[node_index].rest_qx,
                                                      ctx->nodes[node_index].rest_qy,
                                                      ctx->nodes[node_index].rest_qz,
                                                      ctx->nodes[node_index].rest_qw);
        skeleton[i].rest_scale = vec3_init_from_3(ctx->nodes[node_index].rest_sx,
                                                   ctx->nodes[node_index].rest_sy,
                                                   ctx->nodes[node_index].rest_sz);
        node_to_joint[node_index] = (i32)i;

        /* The inverse bind matrix is the file's own statement of where the
         * mesh sat relative to each joint, and it is the only place that
         * relationship exists. Deriving it from the node rest pose instead
         * silently forces the rest pose to be a no-op, which looks correct at
         * t=0 and wrong for every other frame whenever the two disagree.
         * station.glb is exactly that case: the mesh is authored in a T-pose
         * (hand span ~1.23) while the joint hierarchy rests arms-down (hand
         * span ~0.51), and only inverseBindMatrices bridges the two. */
        skeleton[i].inv_bind_matrix = model_importer_to_mat4(identity);
        if (ibm_index >= 0) {
            mat4 file_ibm;
            if (!model_importer_read_accessor_mat4(ctx, ibm_index, i, &file_ibm)) {
                TAG_FREE(skeleton);
                TAG_FREE(node_to_joint);
                return 0;
            }
            skeleton[i].inv_bind_matrix = file_ibm;
        }
    }

    /* Resolve each joint's parent to the nearest ancestor that is also a joint. */
    for (i = 0; i < joint_count; ++i) {
        i32 parent_node = ctx->nodes[skeleton[i].node_index].parent;
        skeleton[i].bind_root_world = model_importer_to_mat4(identity);
        if (parent_node >= 0 && (u32)parent_node < ctx->node_count) {
            i32 parent_joint = node_to_joint[parent_node];
            if (parent_joint >= 0) skeleton[i].parent = parent_joint;
        }
    }

    /* A root joint's parent node is by definition not a joint, and its
     * scene-space transform already includes every non-joint ancestor above it
     * -- the armature root and any unit scale on it. The mesh vertices live in
     * that frame, so the joint chain has to be composed onto it. */
    for (i = 0; i < joint_count; ++i) {
        i32 n;
        if (skeleton[i].parent >= 0) continue;   /* only root joints carry a base */
        n = ctx->nodes[skeleton[i].node_index].parent;
        if (n >= 0 && (u32)n < ctx->node_count && node_to_joint[n] < 0)
            skeleton[i].bind_root_world = model_importer_to_mat4(ctx->nodes[n].world_transform);
    }

    /* Bind matrices. skin.inverseBindMatrices, when present, is authoritative
     * and was stored per joint above; the block below only has to work out the
     * base that a skinned primitive's node transform is expressed relative to,
     * and supplies a rest-pose-derived bind for the files that omit the
     * accessor entirely. */
    {
        model_importer_mat4 base_inverse;
        int have_base = 0;

        /* Only a file that omits skin.inverseBindMatrices needs a bind derived
         * from the rest pose. The accessor is optional in glTF, and when it is
         * absent the rest pose is the only bind information that exists, so the
         * forward-kinematics fallback below is the correct reading rather than
         * a correction. When the accessor is present it is authoritative and
         * was already stored per joint above. */
        if (ibm_index < 0) {
            model_importer_mat4 *bind_world =
                (model_importer_mat4*)model_importer_calloc_count(joint_count, sizeof(model_importer_mat4));
            if (!bind_world) {
                TAG_FREE(skeleton);
                TAG_FREE(node_to_joint);
                return model_importer_set_error("out of memory for joint bind transforms");
            }

            /* Joints arrive parents-first, so one pass is enough. This must mirror
             * animation_eval_pose's forward kinematics exactly, otherwise the two
             * disagree by a transform and the rest pose stops resolving to identity.
             * A root joint is composed as bind_root_world * bind_local -- the root
             * bone's own rest offset is part of its bind frame even though
             * bind_root_world alone would look like the right answer. */
            for (i = 0; i < joint_count; ++i) {
                i32 parent = skeleton[i].parent;
                model_importer_mat4 local = model_importer_mat4_from_mat4(skeleton[i].bind_local);
                if (parent >= 0 && (u32)parent < joint_count && (u32)parent < i)
                    bind_world[i] = model_importer_mat4_mul(bind_world[parent], local);
                else
                    bind_world[i] = model_importer_mat4_mul(
                        model_importer_mat4_from_mat4(skeleton[i].bind_root_world), local);
            }

            for (i = 0; i < joint_count; ++i) {
                model_importer_mat4 inverted;
                if (model_importer_mat4_invert(bind_world[i], &inverted))
                    skeleton[i].inv_bind_matrix = model_importer_to_mat4(inverted);
                else
                    skeleton[i].inv_bind_matrix = model_importer_to_mat4(identity);
            }
            TAG_FREE(bind_world);
        }

        /* A skinned primitive is drawn as node_transform * palette, and the
         * palette already carries the skeleton base (bind_root_world) because
         * the joint world transforms include it. Leaving the same base on the
         * node transform as well would apply it twice -- station.glb's armature
         * root scales by 0.01, which would shrink the model a hundredfold
         * while leaving the motion itself correct.
         *
         * The base is the transform in force above the skeleton, so it is read
         * from bind_root_world rather than from the bind matrices. Those are
         * independent: station.glb states its root joint's bind frame as
         * identity while the node hierarchy puts that joint under an armature
         * scaled by 0.01, and only the latter is what the palette carries. For
         * a conformant file the base is identity and this changes nothing. */
        for (i = 0; i < joint_count; ++i) {
            if (skeleton[i].parent >= 0) continue;
            base_inverse = identity;
            have_base = model_importer_mat4_invert(
                model_importer_mat4_from_mat4(skeleton[i].bind_root_world), &base_inverse);
            break;
        }

        if (have_base && model->primitives.count > 0u && model->primitives.address) {
            model_primitive *prims = (model_primitive*)model->primitives.address;
            mat4 base_inv_mat = model_importer_to_mat4(base_inverse);
            for (i = 0; i < model->primitives.count; ++i) {
                if (!prims[i].is_skinned) continue;
                prims[i].node_transform = mat4_mul(prims[i].node_transform, base_inv_mat);
            }
        }
    }

    TAG_FREE(node_to_joint);

    model->skeleton.count = joint_count;
    model->skeleton.address = skeleton;
    /* Animation channels address glTF nodes, not joints, so the runtime needs
     * the node count to size its node -> joint lookup table. */
    model->node_count = ctx->node_count;
    return 1;
}

/* --------------------------------------------------------------------------
 * Animations -> a single animation_definition holding every clip
 *
 * The parsed clips are backed by heap blocks. This is instantiated into one
 * 'anim' tag by model_importer_import_model_with_material, which owns the
 * tag-system interaction. Loading only: no playback or lifetime management
 * beyond the import is performed here.
 * -------------------------------------------------------------------------- */
static void model_importer_free_animation(animation_definition *anim)
{
    animation_clip *clips;
    u32 i, s;

    if (!anim) return;
    clips = (animation_clip*)anim->clips.address;
    if (!clips) { anim->clips.count = 0u; return; }

    for (i = 0; i < anim->clips.count; ++i) {
        animation_sampler *samplers = (animation_sampler*)clips[i].samplers.address;
        if (samplers) {
            for (s = 0; s < clips[i].samplers.count; ++s) {
                if (samplers[s].input.address) TAG_FREE(samplers[s].input.address);
                if (samplers[s].output.address) TAG_FREE(samplers[s].output.address);
            }
            TAG_FREE(samplers);
        }
        if (clips[i].channels.address) TAG_FREE(clips[i].channels.address);
    }
    TAG_FREE(clips);
    anim->clips.address = NULL;
    anim->clips.count = 0u;
}

static i32 model_importer_path_from_string(const char *s)
{
    if (!s) return ANIMATION_PATH_TRANSLATION;
    if (strcmp(s, "rotation") == 0)    return ANIMATION_PATH_ROTATION;
    if (strcmp(s, "scale") == 0)       return ANIMATION_PATH_SCALE;
    if (strcmp(s, "weights") == 0)     return ANIMATION_PATH_WEIGHTS;
    return ANIMATION_PATH_TRANSLATION;  /* translation */
}

static i32 model_importer_interp_from_string(const char *s)
{
    if (!s) return ANIMATION_INTERPOLATION_LINEAR;
    if (strcmp(s, "STEP") == 0)        return ANIMATION_INTERPOLATION_STEP;
    if (strcmp(s, "CUBICSPLINE") == 0) return ANIMATION_INTERPOLATION_CUBICSPLINE;
    return ANIMATION_INTERPOLATION_LINEAR;
}

static int model_importer_parse_animations(model_importer_context *ctx,
                                            animation_definition *out_anim)
{
    model_importer_json_span anims;
    u32 anim_count, ai, si, ci;
    char name_buf[64], path_buf[32];
    animation_clip *clips;

    out_anim->clips.count = 0u;
    out_anim->clips.address = NULL;

    if (ctx->animation_count == 0u) return 1;

    anims = ctx->animations;
    if (!model_importer_json_array_count(anims, &anim_count))
        return model_importer_set_error("animations must be an array");

    if (anim_count > (u32)animation_clip_block.max_element_count)
        return model_importer_set_error("glTF has more animations than animation_clip_block supports");

    clips = (animation_clip*)model_importer_calloc_count(anim_count, sizeof(animation_clip));
    if (!clips)
        return model_importer_set_error("out of memory for animation clips");

    out_anim->clips.count = anim_count;
    out_anim->clips.address = clips;

    for (ai = 0; ai < anim_count; ++ai) {
        model_importer_json_span anim, samplers_json, channels_json, value;
        animation_sampler *samplers;
        animation_channel *channels;
        u32 sampler_count, channel_count;
        animation_clip *clip = &clips[ai];

        clip->name = TAG_NULL(string_id);

        if (!model_importer_json_array_get(anims, ai, &anim)) {
            model_importer_free_animation(out_anim);
            return model_importer_set_error("could not read animation object");
        }

        if (model_importer_json_object_find(anim, "name", &value) &&
            model_importer_json_copy_string(value, name_buf, sizeof(name_buf)))
            clip->name = string_id_intern(name_buf);

        /* ---- samplers ---- */
        sampler_count = 0u;
        samplers = NULL;
        if (model_importer_json_object_find(anim, "samplers", &samplers_json)) {
            if (!model_importer_json_array_count(samplers_json, &sampler_count)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("animation.samplers must be an array");
            }
            if (sampler_count > 0u) {
                samplers = (animation_sampler*)model_importer_calloc_count(
                    sampler_count, sizeof(animation_sampler));
                if (!samplers) {
                    model_importer_free_animation(out_anim);
                    return model_importer_set_error("out of memory for animation samplers");
                }
            }
        }
        clip->samplers.count = sampler_count;
        clip->samplers.address = samplers;

        for (si = 0; si < sampler_count; ++si) {
            model_importer_json_span sampler, sval;
            i32 input_accessor = -1, output_accessor = -1;
            real *input_data = NULL, *output_data = NULL;
            u32 input_count = 0u, output_count = 0u;
            i32 interp = ANIMATION_INTERPOLATION_LINEAR;

            if (!model_importer_json_array_get(samplers_json, si, &sampler)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("could not read animation sampler");
            }
            if (model_importer_json_object_find(sampler, "input", &sval) &&
                !model_importer_json_parse_i32(sval, &input_accessor)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("sampler.input must be an integer");
            }
            if (model_importer_json_object_find(sampler, "output", &sval) &&
                !model_importer_json_parse_i32(sval, &output_accessor)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("sampler.output must be an integer");
            }
            if (model_importer_json_object_find(sampler, "interpolation", &sval) &&
                model_importer_json_copy_string(sval, path_buf, sizeof(path_buf)))
                interp = model_importer_interp_from_string(path_buf);

            if (!model_importer_read_accessor_float_array(ctx, input_accessor, &input_data, &input_count) ||
                !model_importer_read_accessor_float_array(ctx, output_accessor, &output_data, &output_count)) {
                if (input_data) TAG_FREE(input_data);
                if (output_data) TAG_FREE(output_data);
                model_importer_free_animation(out_anim);
                return 0;
            }

            samplers[si].input.count = input_count;
            samplers[si].input.address = input_data;
            samplers[si].output.count = output_count;
            samplers[si].output.address = output_data;
            samplers[si].interpolation = (enum32)interp;

            /* Values-per-key = output_total / input_total. Correct for
             * translation/scale (3), rotation (4), weights (numMorphs), and
             * stays correct for CUBICSPLINE (both sides 3x key count). */
            if (input_count > 0u && output_count % input_count == 0u)
                samplers[si].component_count = output_count / input_count;
            else
                samplers[si].component_count = (output_accessor >= 0 && (u32)output_accessor < ctx->accessor_count)
                                                 ? ctx->accessors[output_accessor].component_count : 1u;
        }

        /* ---- channels ---- */
        channel_count = 0u;
        channels = NULL;
        if (model_importer_json_object_find(anim, "channels", &channels_json)) {
            if (!model_importer_json_array_count(channels_json, &channel_count)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("animation.channels must be an array");
            }
            if (channel_count > 0u) {
                channels = (animation_channel*)model_importer_calloc_count(
                    channel_count, sizeof(animation_channel));
                if (!channels) {
                    model_importer_free_animation(out_anim);
                    return model_importer_set_error("out of memory for animation channels");
                }
            }
        }
        clip->channels.count = channel_count;
        clip->channels.address = channels;

        for (ci = 0; ci < channel_count; ++ci) {
            model_importer_json_span channel, cval, target;
            i32 sampler_index = -1, target_node = -1;

            if (!model_importer_json_array_get(channels_json, ci, &channel)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("could not read animation channel");
            }
            if (model_importer_json_object_find(channel, "sampler", &cval) &&
                !model_importer_json_parse_i32(cval, &sampler_index)) {
                model_importer_free_animation(out_anim);
                return model_importer_set_error("channel.sampler must be an integer");
            }

            channels[ci].sampler = sampler_index;
            channels[ci].target_node = -1;
            channels[ci].path = ANIMATION_PATH_TRANSLATION;

            if (model_importer_json_object_find(channel, "target", &target)) {
                if (model_importer_json_object_find(target, "node", &cval)) {
                    if (!model_importer_json_parse_i32(cval, &target_node)) {
                        model_importer_free_animation(out_anim);
                        return model_importer_set_error("channel.target.node must be an integer");
                    }
                }
                if (model_importer_json_object_find(target, "path", &cval) &&
                    model_importer_json_copy_string(cval, path_buf, sizeof(path_buf)))
                    channels[ci].path = (enum32)model_importer_path_from_string(path_buf);
            }
            channels[ci].target_node = target_node;
        }
    }

    return 1;
}

static void model_importer_free_model(model_definition *model)
{
    u32 i;

    if (!model) return;

    if (model->primitives.address) {
        model_primitive *primitives = (model_primitive*)model->primitives.address;
        for (i = 0; i < model->primitives.count; ++i) {
            if (primitives[i].vertices.address) TAG_FREE(primitives[i].vertices.address);
            if (primitives[i].indices.address) TAG_FREE(primitives[i].indices.address);
            if (primitives[i].morph_targets.address) {
                model_morph_target *targets = (model_morph_target*)primitives[i].morph_targets.address;
                u32 t;
                for (t = 0; t < primitives[i].morph_targets.count; ++t) {
                    if (targets[t].position_deltas.address) TAG_FREE(targets[t].position_deltas.address);
                    if (targets[t].normal_deltas.address) TAG_FREE(targets[t].normal_deltas.address);
                    if (targets[t].tangent_deltas.address) TAG_FREE(targets[t].tangent_deltas.address);
                }
                TAG_FREE(primitives[i].morph_targets.address);
            }
        }
        TAG_FREE(model->primitives.address);
    }

    if (model->materials.address) TAG_FREE(model->materials.address);
    if (model->skeleton.address) TAG_FREE(model->skeleton.address);

    memset(model, 0, sizeof(*model));
}

static int model_importer_import_glb_with_material(const char *path,
                                                   i32 default_material_handle,
                                                   model_definition *out_model,
                                                   animation_definition *out_anim)
{
    u8 *file_data;
    u32 file_size;
    model_importer_json_span root;
    const u8 *bin;
    u32 bin_size;
    model_importer_context ctx;
    model_definition model;
    model_primitive *primitives;
    tag_reference *materials;
    u32 i;
    i32 has_bounds;

    if (!out_model) return model_importer_set_error("output model pointer is null");
    if (out_anim) memset(out_anim, 0, sizeof(*out_anim));

    model_importer_error[0] = '\0';
    file_data = NULL;
    file_size = 0u;
    memset(&ctx, 0, sizeof(ctx));
    memset(&model, 0, sizeof(model));

    if (!model_importer_read_file(path, &file_data, &file_size))
        return 0;

    if (!model_importer_parse_glb_chunks(file_data, file_size, &root, &bin, &bin_size)) {
        TAG_FREE(file_data);
        return 0;
    }

    if (!model_importer_context_init(&ctx, root, bin, bin_size)) {
        model_importer_context_free(&ctx);
        TAG_FREE(file_data);
        return 0;
    }

    primitives = (model_primitive*)model_importer_calloc_count(ctx.model_primitive_count, sizeof(model_primitive));
    materials = (tag_reference*)model_importer_calloc_count(ctx.model_primitive_count, sizeof(tag_reference));
    if (!primitives || !materials) {
        if (primitives) TAG_FREE(primitives);
        if (materials) TAG_FREE(materials);
        model_importer_context_free(&ctx);
        TAG_FREE(file_data);
        return model_importer_set_error("out of memory for model blocks");
    }

    model.primitives.count = ctx.model_primitive_count;
    model.primitives.address = primitives;
    model.materials.count = ctx.model_primitive_count;
    model.materials.address = materials;
    model.skeleton.count = 0;
    model.skeleton.address = NULL;

    for (i = 0; i < ctx.model_primitive_count; ++i) {
        materials[i].handle = default_material_handle;
    }

    has_bounds = 0;

    {
        u32 model_primitive_index = 0u;
        for (i = 0; i < ctx.mesh_count; ++i) {
            model_importer_json_span mesh, mesh_primitives, mval;
            u32 mesh_primitive_count, mesh_primitive_index;
            real *mesh_weights = NULL;
            u32 mesh_weight_count = 0u;

            if (!model_importer_json_array_get(ctx.meshes, i, &mesh) ||
                !model_importer_json_object_find(mesh, "primitives", &mesh_primitives) ||
                !model_importer_json_array_count(mesh_primitives, &mesh_primitive_count)) {
                model_importer_free_model(&model);
                model_importer_context_free(&ctx);
                TAG_FREE(file_data);
                return model_importer_set_error("mesh.primitives must be an array");
            }

            /* Per-mesh default morph weights (mesh.weights). */
            if (model_importer_json_object_find(mesh, "weights", &mval) &&
                model_importer_json_array_count(mval, &mesh_weight_count) &&
                mesh_weight_count > 0u) {
                mesh_weights = (real*)model_importer_calloc_count(mesh_weight_count, sizeof(real));
                if (mesh_weights) {
                    u32 w;
                    for (w = 0; w < mesh_weight_count; ++w)
                        if (!model_importer_json_array_real(mval, w, &mesh_weights[w])) { mesh_weight_count = w; break; }
                } else {
                    mesh_weight_count = 0u;
                }
            }

            for (mesh_primitive_index = 0u; mesh_primitive_index < mesh_primitive_count; ++mesh_primitive_index) {
                model_importer_json_span mesh_primitive;
                if (!model_importer_json_array_get(mesh_primitives, mesh_primitive_index, &mesh_primitive) ||
                    !model_importer_fill_mesh_primitive(&ctx, i, mesh_primitive_index, mesh_primitive,
                                                       &primitives[model_primitive_index],
                                                       mesh_weights, mesh_weight_count,
                                                       &model.bounding_box, &has_bounds)) {
                    if (mesh_weights) TAG_FREE(mesh_weights);
                    model_importer_free_model(&model);
                    model_importer_context_free(&ctx);
                    TAG_FREE(file_data);
                    return 0;
                }
                ++model_primitive_index;
            }
            if (mesh_weights) TAG_FREE(mesh_weights);
        }
    }

    if (!has_bounds) {
        model.bounding_box.x.lower = model.bounding_box.x.upper = 0.0f;
        model.bounding_box.y.lower = model.bounding_box.y.upper = 0.0f;
        model.bounding_box.z.lower = model.bounding_box.z.upper = 0.0f;
    }

    if (!model_importer_parse_skins(&ctx, &model)) {
        model_importer_free_model(&model);
        model_importer_context_free(&ctx);
        TAG_FREE(file_data);
        return 0;
    }

    /* parse_skins sets this for skinned models; do it unconditionally so a
     * static model still carries its node count. */
    model.node_count = ctx.node_count;

    if (out_anim && !model_importer_parse_animations(&ctx, out_anim)) {
        model_importer_free_model(&model);
        model_importer_context_free(&ctx);
        TAG_FREE(file_data);
        return 0;
    }

    *out_model = model;

    model_importer_context_free(&ctx);
    TAG_FREE(file_data);
    return 1;
}

static int model_importer_import_glb(const char *path, model_definition *out_model)
{
    animation_definition anim;
    int ok = model_importer_import_glb_with_material(path, -1, out_model, &anim);
    model_importer_free_animation(&anim);
    return ok;
}

static i32 model_importer_import_model_with_material(const char *path,
                                                     i32 default_material_handle,
                                                     i32 *out_anim_handle)
{
    const tag_group_definition *group, *anim_group;
    model_definition model;
    animation_definition anim;
    tag_instance *inst;
    i32 existing_handle;
    i32 handle, anim_handle = -1;
    char anim_name[256];

    if (out_anim_handle) *out_anim_handle = -1;
    if (!path) return -1;

    model_importer_error[0] = '\0';
    memset(&anim, 0, sizeof(anim));

    if (!tag_sys.initialized) {
        model_importer_set_error("tag system is not initialized");
        return -1;
    }

    group = tag_find_group_internal(TAG_model);
    if (!group) {
        model_importer_set_error("model tag group is not registered");
        return -1;
    }

    snprintf(anim_name, sizeof(anim_name), "%s#anim", path);

    existing_handle = tag_find_instance(path);
    if (existing_handle >= 0) {
        if (!tag_get(existing_handle, TAG_model)) {
            model_importer_set_error("an existing non-model tag uses this path");
            return -1;
        }
        tag_sys.instances[existing_handle].ref_count++;
        if (out_anim_handle) {
            i32 cached_anim = tag_find_instance(anim_name);
            if (cached_anim >= 0 && tag_get(cached_anim, TAG_animation)) {
                tag_sys.instances[cached_anim].ref_count++;
                *out_anim_handle = cached_anim;
            }
        }
        return existing_handle;
    }

    memset(&model, 0, sizeof(model));
    if (!model_importer_import_glb_with_material(path, default_material_handle, &model, &anim)) {
        model_importer_free_animation(&anim);
        return -1;
    }

    /* One 'anim' tag holds every clip for the model. Playback is deferred; here
     * we only load the data. The anim tag keeps the parsed clip/sampler/channel
     * blocks alive (ownership passes to the instance's active/backup data). */
    anim_group = tag_find_group_internal(TAG_animation);
    if (anim.clips.count > 0u) {
        if (!anim_group) {
            model_importer_free_model(&model);
            model_importer_free_animation(&anim);
            model_importer_set_error("animation tag group is not registered");
            return -1;
        }
        anim_handle = tag_alloc_instance(anim_name, anim_group);
        if (anim_handle < 0) {
            model_importer_free_model(&model);
            model_importer_free_animation(&anim);
            model_importer_set_error("could not allocate animation tag instance");
            return -1;
        }
        inst = &tag_sys.instances[anim_handle];
        memcpy(inst->backup_data, &anim, sizeof(animation_definition));
        memcpy(inst->active_data, &anim, sizeof(animation_definition));
        inst->loaded = 1;
        tag_postprocess_tag(anim_handle);
    }

    handle = tag_alloc_instance(path, group);
    if (handle < 0) {
        if (anim_handle >= 0) tag_release(anim_handle);
        model_importer_free_model(&model);
        model_importer_free_animation(&anim);
        model_importer_set_error("could not allocate model tag instance");
        return -1;
    }

    inst = &tag_sys.instances[handle];
    memcpy(inst->backup_data, &model, sizeof(model_definition));
    memcpy(inst->active_data, &model, sizeof(model_definition));
    inst->loaded = 1;
    tag_postprocess_tag(handle);

    /* The clip/sampler/channel blocks are now owned by the anim tag instance;
     * detach them from the temporary container so it cannot free them. */
    anim.clips.address = NULL;
    anim.clips.count = 0u;

    if (out_anim_handle) *out_anim_handle = anim_handle;
    return handle;
}

static i32 model_importer_import_model(const char *path)
{
    return model_importer_import_model_with_material(path, -1, NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* MODEL_IMPORTER_H */