#ifndef MODEL_IMPORTER_H
#define MODEL_IMPORTER_H

#include "../tags/model.h"

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
 *   - POSITION and NORMAL accessors must be FLOAT VEC3.
 *   - Indices may be UNSIGNED_BYTE, UNSIGNED_SHORT, or UNSIGNED_INT, but the
 *     final engine primitive must fit u16 indices.
 *   - Animation, skins, textures, and glTF material properties are ignored.
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
    u32 child_count;
    i32 *children;
    model_importer_mat4 local_transform;
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

static model_importer_mat4 model_importer_mat4_from_trs(real tx, real ty, real tz,
                                                        real qx, real qy, real qz, real qw,
                                                        real sx, real sy, real sz)
{
    model_importer_mat4 result;
    real xx;
    real yy;
    real zz;
    real xy;
    real xz;
    real yz;
    real wx;
    real wy;
    real wz;
    real length;

    length = (real)sqrt((double)(qx*qx + qy*qy + qz*qz + qw*qw));
    if (length > 0.000001f) {
        qx /= length;
        qy /= length;
        qz /= length;
        qw /= length;
    } else {
        qx = 0.0f;
        qy = 0.0f;
        qz = 0.0f;
        qw = 1.0f;
    }

    xx = qx * qx;
    yy = qy * qy;
    zz = qz * qz;
    xy = qx * qy;
    xz = qx * qz;
    yz = qy * qz;
    wx = qw * qx;
    wy = qw * qy;
    wz = qw * qz;

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

static vec3 model_importer_transform_point(model_importer_mat4 transform, vec3 point)
{
    vec3 result;
    real x;
    real y;
    real z;

    x = point.position.x;
    y = point.position.y;
    z = point.position.z;

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

    length = (real)sqrt((double)(
        value.position.x * value.position.x +
        value.position.y * value.position.y +
        value.position.z * value.position.z));

    if (length <= 0.000001f) {
        value.position.x = 0.0f;
        value.position.y = 1.0f;
        value.position.z = 0.0f;
        return value;
    }

    value.position.x /= length;
    value.position.y /= length;
    value.position.z /= length;
    return value;
}

static vec3 model_importer_transform_normal(model_importer_mat4 transform, vec3 normal)
{
    real a;
    real b;
    real c;
    real d;
    real e;
    real f;
    real g;
    real h;
    real i;
    real det;
    vec3 result;

    a = transform.m[0];
    b = transform.m[1];
    c = transform.m[2];
    d = transform.m[4];
    e = transform.m[5];
    f = transform.m[6];
    g = transform.m[8];
    h = transform.m[9];
    i = transform.m[10];

    det = a * (e * i - f * h) -
          b * (d * i - f * g) +
          c * (d * h - e * g);

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

static int model_importer_parse_glb_chunks(const u8 *data, u32 size,
                                           model_importer_json_span *out_json,
                                           const u8 **out_bin,
                                           u32 *out_bin_size)
{
    u32 magic;
    u32 version;
    u32 declared_length;
    u32 offset;
    int found_json;

    if (!data || size < 20)
        return model_importer_set_error("GLB file is too small");

    magic = model_importer_read_u32le(data);
    version = model_importer_read_u32le(data + 4);
    declared_length = model_importer_read_u32le(data + 8);

    if (magic != 0x46546C67u)
        return model_importer_set_error("file is not a GLB");
    if (version != 2u)
        return model_importer_set_error("only GLB version 2 is supported");
    if (declared_length > size)
        return model_importer_set_error("GLB declared length exceeds file length");

    offset = 12u;
    found_json = 0;
    *out_bin = NULL;
    *out_bin_size = 0;

    while (offset + 8u <= declared_length) {
        u32 chunk_length;
        u32 chunk_type;
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

    if (!found_json)
        return model_importer_set_error("GLB is missing a JSON chunk");
    if (!*out_bin)
        return model_importer_set_error("GLB is missing a BIN chunk");

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
        model_importer_json_span object;
        model_importer_json_span value;
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
        model_importer_json_span object;
        model_importer_json_span value;
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
    real tx;
    real ty;
    real tz;
    real qx;
    real qy;
    real qz;
    real qw;
    real sx;
    real sy;
    real sz;
    u32 count;
    u32 i;

    node->mesh = -1;
    node->child_count = 0u;
    node->children = NULL;
    node->local_transform = model_importer_mat4_identity();

    if (model_importer_json_object_find(object, "mesh", &value) &&
        !model_importer_json_parse_i32(value, &node->mesh))
        return model_importer_set_error("node.mesh must be an integer");

    if (model_importer_json_object_find(object, "matrix", &value)) {
        real gltf_matrix[16];

        for (i = 0; i < 16u; ++i) {
            if (!model_importer_json_array_real(value, i, &gltf_matrix[i]))
                return model_importer_set_error("node.matrix must contain 16 numbers");
        }
        node->local_transform = model_importer_mat4_from_gltf_matrix(gltf_matrix);
    } else {
        tx = 0.0f;
        ty = 0.0f;
        tz = 0.0f;
        qx = 0.0f;
        qy = 0.0f;
        qz = 0.0f;
        qw = 1.0f;
        sx = 1.0f;
        sy = 1.0f;
        sz = 1.0f;

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

    if (node->mesh >= 0 && (u32)node->mesh < mesh_count) {
        if (!mesh_transform_set[node->mesh]) {
            mesh_transforms[node->mesh] = world_transform;
            mesh_transform_set[node->mesh] = 1u;
        }
    }

    for (i = 0; i < node->child_count; ++i) {
        if (!model_importer_visit_node(nodes, node_count, node->children[i], world_transform,
                                       mesh_transforms, mesh_transform_set, mesh_count, depth + 1u))
            return 0;
    }

    return 1;
}

static int model_importer_parse_mesh_transforms(model_importer_context *ctx,
                                                model_importer_json_span root)
{
    model_importer_json_span nodes_array;
    model_importer_json_span scenes_array;
    model_importer_json_span scene_object;
    model_importer_json_span scene_nodes;
    model_importer_json_span value;
    model_importer_node *nodes;
    model_importer_mat4 identity;
    u32 node_count;
    u32 scene_count;
    u32 root_count;
    u32 i;
    i32 scene_index;
    int visited_scene;

    ctx->mesh_transforms = (model_importer_mat4*)model_importer_calloc_count(
        ctx->mesh_count, sizeof(model_importer_mat4));
    ctx->mesh_transform_set = (u8*)model_importer_calloc_count(ctx->mesh_count, sizeof(u8));
    if (!ctx->mesh_transforms || !ctx->mesh_transform_set)
        return model_importer_set_error("out of memory for mesh transforms");

    identity = model_importer_mat4_identity();
    for (i = 0; i < ctx->mesh_count; ++i) ctx->mesh_transforms[i] = identity;

    if (!model_importer_json_object_find(root, "nodes", &nodes_array))
        return 1;

    if (!model_importer_json_array_count(nodes_array, &node_count))
        return model_importer_set_error("nodes must be an array");

    if (node_count == 0u) return 1;

    nodes = (model_importer_node*)model_importer_calloc_count(node_count, sizeof(model_importer_node));
    if (!nodes)
        return model_importer_set_error("out of memory for nodes");

    for (i = 0; i < node_count; ++i) {
        model_importer_json_span node_object;

        if (!model_importer_json_array_get(nodes_array, i, &node_object) ||
            !model_importer_parse_node(&nodes[i], node_object)) {
            model_importer_free_nodes(nodes, node_count);
            return 0;
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
        if (scene_index < 0 || (u32)scene_index >= scene_count)
            scene_index = 0;

        if (model_importer_json_array_get(scenes_array, (u32)scene_index, &scene_object) &&
            model_importer_json_object_find(scene_object, "nodes", &scene_nodes) &&
            model_importer_json_array_count(scene_nodes, &root_count)) {
            for (i = 0; i < root_count; ++i) {
                i32 root_node;

                if (!model_importer_json_array_i32(scene_nodes, i, &root_node) ||
                    !model_importer_visit_node(nodes, node_count, root_node, identity,
                                               ctx->mesh_transforms, ctx->mesh_transform_set,
                                               ctx->mesh_count, 0u)) {
                    model_importer_free_nodes(nodes, node_count);
                    return 0;
                }
            }
            visited_scene = 1;
        }
    }

    if (!visited_scene) {
        for (i = 0; i < node_count; ++i) {
            if (!model_importer_visit_node(nodes, node_count, (i32)i, identity,
                                           ctx->mesh_transforms, ctx->mesh_transform_set,
                                           ctx->mesh_count, 0u)) {
                model_importer_free_nodes(nodes, node_count);
                return 0;
            }
        }
    } else {
        for (i = 0; i < node_count; ++i) {
            if (!model_importer_visit_node(nodes, node_count, (i32)i, identity,
                                           ctx->mesh_transforms, ctx->mesh_transform_set,
                                           ctx->mesh_count, 0u)) {
                model_importer_free_nodes(nodes, node_count);
                return 0;
            }
        }
    }

    model_importer_free_nodes(nodes, node_count);
    return 1;
}

static int model_importer_count_model_primitives(model_importer_context *ctx)
{
    u32 mesh_index;
    u32 total;

    total = 0u;

    for (mesh_index = 0; mesh_index < ctx->mesh_count; ++mesh_index) {
        model_importer_json_span mesh;
        model_importer_json_span primitives;
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

    if (total == 0u)
        return model_importer_set_error("glTF has no mesh primitives");

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

    if (ctx->mesh_count == 0u)
        return model_importer_set_error("glTF has no meshes");

    if (!model_importer_parse_mesh_transforms(ctx, root)) return 0;
    if (!model_importer_count_model_primitives(ctx)) return 0;

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
    size_t stride;
    size_t element_size;
    size_t view_offset;
    size_t view_length;
    size_t accessor_offset;
    size_t relative_offset;

    if (!out_ptr) return 0;
    if (accessor_index < 0 || (u32)accessor_index >= ctx->accessor_count)
        return model_importer_set_error("accessor index is out of range");

    accessor = &ctx->accessors[accessor_index];
    if (element_index >= accessor->count)
        return model_importer_set_error("accessor element is out of range");
    if (accessor->buffer_view < 0 || (u32)accessor->buffer_view >= ctx->buffer_view_count)
        return model_importer_set_error("accessor has an invalid bufferView");

    view = &ctx->buffer_views[accessor->buffer_view];
    if (view->buffer != 0u)
        return model_importer_set_error("only GLB buffer 0 is supported");

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

static int model_importer_parse_mesh_primitive(model_importer_json_span primitive,
                                               i32 *out_position_accessor,
                                               i32 *out_normal_accessor,
                                               i32 *out_index_accessor,
                                               i32 *out_mode)
{
    model_importer_json_span attributes;
    model_importer_json_span value;

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
    model_importer_accessor *position_accessor;
    model_importer_accessor *index_accessor;
    i32 position_index;
    i32 normal_index;
    i32 index_index;
    i32 mode;
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
        bounds->x.lower = point.position.x;
        bounds->x.upper = point.position.x;
        bounds->y.lower = point.position.y;
        bounds->y.upper = point.position.y;
        bounds->z.lower = point.position.z;
        bounds->z.upper = point.position.z;
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
                                           const u16 *indices, u32 index_count)
{
    u32 i;

    for (i = 0; i < vertex_count; ++i) {
        vertices[i].normal.position.x = 0.0f;
        vertices[i].normal.position.y = 0.0f;
        vertices[i].normal.position.z = 0.0f;
    }

    for (i = 0; i + 2u < index_count; i += 3u) {
        u16 i0;
        u16 i1;
        u16 i2;
        vec3 edge_a;
        vec3 edge_b;
        vec3 face_normal;

        i0 = indices[i + 0u];
        i1 = indices[i + 1u];
        i2 = indices[i + 2u];

        if ((u32)i0 >= vertex_count || (u32)i1 >= vertex_count || (u32)i2 >= vertex_count)
            continue;

        edge_a = model_importer_vec3_sub(vertices[i1].position, vertices[i0].position);
        edge_b = model_importer_vec3_sub(vertices[i2].position, vertices[i0].position);
        face_normal = model_importer_vec3_cross(edge_a, edge_b);

        vertices[i0].normal = model_importer_vec3_add(vertices[i0].normal, face_normal);
        vertices[i1].normal = model_importer_vec3_add(vertices[i1].normal, face_normal);
        vertices[i2].normal = model_importer_vec3_add(vertices[i2].normal, face_normal);
    }

    for (i = 0; i < vertex_count; ++i) {
        vertices[i].normal = model_importer_vec3_normalize_or_up(vertices[i].normal);
    }
}

static int model_importer_fill_mesh_primitive(model_importer_context *ctx,
                                              u32 mesh_index,
                                              u32 material_index,
                                              model_importer_json_span primitive,
                                              model_primitive *out_primitive,
                                              real_bounding_box *bounds,
                                              i32 *has_bounds)
{
    model_vertex *vertices;
    u16 *indices;
    u32 vertex_count;
    u32 index_count;
    i32 needs_normals;
    model_importer_mat4 transform;
    i32 position_index;
    i32 normal_index;
    i32 index_index;
    i32 mode;
    u32 i;

    if (!model_importer_primitive_counts(ctx, primitive, &vertex_count, &index_count, &needs_normals))
        return 0;

    vertices = (model_vertex*)model_importer_calloc_count(vertex_count, sizeof(model_vertex));
    indices = (u16*)model_importer_calloc_count(index_count, sizeof(u16));
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

    transform = ctx->mesh_transforms ? ctx->mesh_transforms[mesh_index] : model_importer_mat4_identity();

    if (!model_importer_parse_mesh_primitive(primitive, &position_index, &normal_index, &index_index, &mode))
        return 0;
    (void)mode;

    for (i = 0; i < vertex_count; ++i) {
        vec3 position;
        vec3 normal;

        if (!model_importer_read_accessor_vec3(ctx, position_index, i, &position))
            return 0;

        position = model_importer_transform_point(transform, position);
        vertices[i].position = position;
        model_importer_bounds_include(bounds, position, has_bounds);

        if (normal_index >= 0) {
            if (!model_importer_read_accessor_vec3(ctx, normal_index, i, &normal))
                return 0;
            vertices[i].normal = model_importer_transform_normal(transform, normal);
        }
    }

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
        if (local_index > 0xFFFFu)
            return model_importer_set_error("mesh index exceeds u16");

        indices[i] = (u16)local_index;
    }

    if (needs_normals)
        model_importer_compute_normals(vertices, vertex_count, indices, index_count);

    return 1;
}

static void model_importer_free_model(model_definition *model)
{
    u32 i;

    if (!model) return;

    if (model->primitives.address) {
        model_primitive *primitives;

        primitives = (model_primitive*)model->primitives.address;
        for (i = 0; i < model->primitives.count; ++i) {
            if (primitives[i].vertices.address) TAG_FREE(primitives[i].vertices.address);
            if (primitives[i].indices.address) TAG_FREE(primitives[i].indices.address);
        }
        TAG_FREE(model->primitives.address);
    }

    if (model->materials.address)
        TAG_FREE(model->materials.address);

    memset(model, 0, sizeof(*model));
}

static int model_importer_import_glb_with_material(const char *path,
                                                   i32 default_material_handle,
                                                   model_definition *out_model)
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

    if (!out_model)
        return model_importer_set_error("output model pointer is null");

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

    for (i = 0; i < ctx.model_primitive_count; ++i) {
        materials[i].handle = default_material_handle;
    }

    has_bounds = 0;

    {
        u32 model_primitive_index;

        model_primitive_index = 0u;

        for (i = 0; i < ctx.mesh_count; ++i) {
            model_importer_json_span mesh;
            model_importer_json_span mesh_primitives;
            u32 mesh_primitive_count;
            u32 mesh_primitive_index;

            if (!model_importer_json_array_get(ctx.meshes, i, &mesh) ||
                !model_importer_json_object_find(mesh, "primitives", &mesh_primitives) ||
                !model_importer_json_array_count(mesh_primitives, &mesh_primitive_count)) {
                model_importer_free_model(&model);
                model_importer_context_free(&ctx);
                TAG_FREE(file_data);
                return model_importer_set_error("mesh.primitives must be an array");
            }

            for (mesh_primitive_index = 0u;
                 mesh_primitive_index < mesh_primitive_count;
                 ++mesh_primitive_index) {
                model_importer_json_span mesh_primitive;

                if (!model_importer_json_array_get(mesh_primitives, mesh_primitive_index, &mesh_primitive) ||
                    !model_importer_fill_mesh_primitive(&ctx, i, model_primitive_index, mesh_primitive,
                                                       &primitives[model_primitive_index],
                                                       &model.bounding_box, &has_bounds)) {
                    model_importer_free_model(&model);
                    model_importer_context_free(&ctx);
                    TAG_FREE(file_data);
                    return 0;
                }

                ++model_primitive_index;
            }
        }
    }

    if (!has_bounds) {
        model.bounding_box.x.lower = 0.0f;
        model.bounding_box.x.upper = 0.0f;
        model.bounding_box.y.lower = 0.0f;
        model.bounding_box.y.upper = 0.0f;
        model.bounding_box.z.lower = 0.0f;
        model.bounding_box.z.upper = 0.0f;
    }

    *out_model = model;

    model_importer_context_free(&ctx);
    TAG_FREE(file_data);
    return 1;
}

static int model_importer_import_glb(const char *path, model_definition *out_model)
{
    return model_importer_import_glb_with_material(path, -1, out_model);
}

static i32 model_importer_import_model_with_material(const char *path,
                                                     i32 default_material_handle)
{
    const tag_group_definition *group;
    model_definition model;
    tag_instance *inst;
    i32 existing_handle;
    i32 handle;

    if (!path)
        return -1;

    model_importer_error[0] = '\0';

    if (!tag_sys.initialized) {
        model_importer_set_error("tag system is not initialized");
        return -1;
    }

    group = tag_find_group_internal(TAG_model);
    if (!group) {
        model_importer_set_error("model tag group is not registered");
        return -1;
    }

    existing_handle = tag_find_instance(path);
    if (existing_handle >= 0) {
        if (!tag_get(existing_handle, TAG_model)) {
            model_importer_set_error("an existing non-model tag uses this path");
            return -1;
        }

        tag_sys.instances[existing_handle].ref_count++;
        return existing_handle;
    }

    memset(&model, 0, sizeof(model));
    if (!model_importer_import_glb_with_material(path, default_material_handle, &model))
        return -1;

    handle = tag_alloc_instance(path, group);
    if (handle < 0) {
        model_importer_free_model(&model);
        model_importer_set_error("could not allocate model tag instance");
        return -1;
    }

    inst = &tag_sys.instances[handle];
    memcpy(inst->backup_data, &model, sizeof(model_definition));
    memcpy(inst->active_data, &model, sizeof(model_definition));
    inst->loaded = 1;
    tag_postprocess_tag(handle);

    return handle;
}

static i32 model_importer_import_model(const char *path)
{
    return model_importer_import_model_with_material(path, -1);
}

#ifdef __cplusplus
}
#endif

#endif /* MODEL_IMPORTER_H */
