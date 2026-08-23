/*
 * Include this header **once** in your main engine source file.
 * It provides both the interface and the whole implementation.
 */

#ifndef REFLECTION_H
#define REFLECTION_H

#include <stddef.h>   /* NULL */
#include "common.h"  /* your custom types: real, vec2, vec3, vec4,
                         i8, u8, i16, u16, i32, u32, i64, u64, bool,
                         string_id */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------ */
#ifndef TAG_SYSTEM_MAX_TAGS
#define TAG_SYSTEM_MAX_TAGS       4096
#endif
#ifndef TAG_SYSTEM_MAX_PATH
#define TAG_SYSTEM_MAX_PATH        256
#endif
#ifndef TAG_SYSTEM_NAME_LENGTH
#define TAG_SYSTEM_NAME_LENGTH     128
#endif
#ifndef TAG_NULL
#define TAG_NULL(type) ((type)-1)
#endif

#define TAG_MAGIC_UNPACK(group_tag) \
    ((char)(((u32)(group_tag) >> 24) & 0xFF)), \
    ((char)(((u32)(group_tag) >> 16) & 0xFF)), \
    ((char)(((u32)(group_tag) >> 8)  & 0xFF)), \
    ((char)((u32)(group_tag) & 0xFF))

#define TAG_MAGIC_PACK(magic) \
    (((unsigned long)(unsigned char)(#magic)[0] << 24) | \
    ((unsigned long)(unsigned char)(#magic)[1] << 16) | \
    ((unsigned long)(unsigned char)(#magic)[2] <<  8) | \
    (unsigned long)(unsigned char)(#magic)[3])

#ifndef TAG_MALLOC
#define TAG_MALLOC(sz)  malloc(sz)
#endif
#ifndef TAG_FREE
#define TAG_FREE(p)     free(p)
#endif

/* ------------------------------------------------------------------------
 * String table configuration
 * ------------------------------------------------------------------------ */
#ifndef STRING_TABLE_MAX_ENTRIES
#define STRING_TABLE_MAX_ENTRIES  8192
#endif

#ifndef STRING_TABLE_MAX_STRING_LENGTH
#define STRING_TABLE_MAX_STRING_LENGTH  256
#endif

/* ------------------------------------------------------------------------
 * Field types - includes both legacy ENUM/FLAGS and new BITFIELD32
 * ------------------------------------------------------------------------ */
typedef enum tag_field_type {
    TAG_FIELD_TERMINATOR  = 0,
    TAG_FIELD_BOOL,           /* bool */
    TAG_FIELD_I8,             /* i8 */
    TAG_FIELD_U8,             /* u8 */
    TAG_FIELD_I16,            /* i16 */
    TAG_FIELD_U16,            /* u16 */
    TAG_FIELD_I32,            /* i32 */
    TAG_FIELD_U32,            /* u32 */
    TAG_FIELD_I64,            /* i64 */
    TAG_FIELD_U64,            /* u64 */
    TAG_FIELD_REAL,           /* real (32-bit float) */
    TAG_FIELD_ENUM,           /* i32 with named values (enum) */
    TAG_FIELD_FLAGS,          /* i32 bitmask with named bits */
    TAG_FIELD_VEC2,           /* vec2 */
    TAG_FIELD_VEC3,           /* vec3 */
    TAG_FIELD_VEC4,           /* vec4 (also used for quaternions) */
    TAG_FIELD_REAL_BOUNDS,    /* Two reals representing an upper and lower bounds. */
    TAG_FIELD_REAL_BOUNDING_BOX, /* Three bounds representing x, y, and z bounds. */
    TAG_FIELD_MAT2,           /* mat2 */
    TAG_FIELD_MAT3,           /* mat3 */
    TAG_FIELD_MAT4,           /* mat4 */
    TAG_FIELD_STRING_ID,      /* string_id (index into string table) */
    TAG_FIELD_BLOCK,
    TAG_FIELD_REFERENCE,
    TAG_FIELD_PAD,
    TAG_FIELD_BITFIELD32      /* new unified bitfield (enum + flags) */
} tag_field_type;

/* Forward declarations */
struct tag_block_definition;
struct tag_group_definition;
struct tag_reference_definition;
struct tag_enum_definition;

/* ------------------------------------------------------------------------
 * Per‑field reflection descriptor
 * ------------------------------------------------------------------------ */
typedef struct tag_field_definition {
    tag_field_type  type;
    char            name[TAG_SYSTEM_NAME_LENGTH];
    u32             pad_data;       /* For BITFIELD32: number of low bits used for enum part */
    const void     *extra;          /* For BITFIELD32: pointer to tag_bitfield_definition */
} tag_field_definition;

/* ------------------------------------------------------------------------
 * Runtime containers
 * ------------------------------------------------------------------------ */
typedef struct tag_block {
    u32  count;
    void *address;                  /* array of elements */
} tag_block;

typedef struct tag_reference {
    i32 handle;                     /* tag index, -1 if unresolved */
} tag_reference;

/* ------------------------------------------------------------------------
 * Block / reference / group definitions
 * ------------------------------------------------------------------------ */
typedef struct tag_block_definition {
    char                        name[TAG_SYSTEM_NAME_LENGTH];
    u32                         element_size;
    u32                         max_element_count;
    const tag_field_definition *fields;
    void (*postprocess)(void *element);
} tag_block_definition;

typedef struct tag_reference_definition {
    u32 allowed_group_tag;
} tag_reference_definition;

typedef u32 tag;   /* four‑CC group tag */

typedef struct tag_group_definition {
    char                         name[TAG_SYSTEM_NAME_LENGTH];
    tag                          group_tag;
    u32                          total_size;
    const tag_field_definition  *fields;
    void (*postprocess)(void *tag_data);
    struct tag_group_definition *next;
} tag_group_definition;

/* ------------------------------------------------------------------------
 * String table entry - index is implicit (position in array)
 * ------------------------------------------------------------------------ */
typedef struct string_table_entry {
    char string[STRING_TABLE_MAX_STRING_LENGTH];
} string_table_entry;

/* ------------------------------------------------------------------------
 * Enum / Flags support - for both legacy and BITFIELD32
 * ------------------------------------------------------------------------ */
typedef struct tag_enum_value {
    i32         value;
    const char *name;
} tag_enum_value;

typedef struct tag_enum_definition {
    const char          *name;
    u32                  value_count;
    const tag_enum_value *values;
    i32                  is_flags;   /* 1 if the values are bitflags */
} tag_enum_definition;

/* ------------------------------------------------------------------------
 * Bitfield definition - holds enum_bits and the combined enum definition
 * ------------------------------------------------------------------------ */
typedef struct tag_bitfield_definition {
    u32                        enum_bits;   /* number of low bits reserved for enum */
    const tag_enum_definition *def;        /* combined enum (low values + shifted flags) */
} tag_bitfield_definition;

/* ------------------------------------------------------------------------
 * Field macros - both legacy and new
 * ------------------------------------------------------------------------ */
#define FIELD_TERMINATOR    { TAG_FIELD_TERMINATOR,     "",    0, NULL }

#define FIELD_BOOL(desc)    { TAG_FIELD_BOOL,      (desc), 0, NULL }
#define FIELD_I8(desc)      { TAG_FIELD_I8,        (desc), 0, NULL }
#define FIELD_U8(desc)      { TAG_FIELD_U8,        (desc), 0, NULL }
#define FIELD_I16(desc)     { TAG_FIELD_I16,       (desc), 0, NULL }
#define FIELD_U16(desc)     { TAG_FIELD_U16,       (desc), 0, NULL }
#define FIELD_I32(desc)     { TAG_FIELD_I32,       (desc), 0, NULL }
#define FIELD_U32(desc)     { TAG_FIELD_U32,       (desc), 0, NULL }
#define FIELD_I64(desc)     { TAG_FIELD_I64,       (desc), 0, NULL }
#define FIELD_U64(desc)     { TAG_FIELD_U64,       (desc), 0, NULL }
#define FIELD_REAL(desc)    { TAG_FIELD_REAL,      (desc), 0, NULL }

/* Legacy field macros (kept for backward compatibility) */
#define FIELD_ENUM(desc, name_)     { TAG_FIELD_ENUM,   (desc), 0, (const void*)&(name_##_def) }
#define FIELD_FLAGS(desc, name_)    { TAG_FIELD_FLAGS,  (desc), 0, (const void*)&(name_##_def) }

/* New BITFIELD32 field macro */
#define FIELD_BITFIELD32(desc, bitfield_def, enum_bits_) \
    { TAG_FIELD_BITFIELD32, (desc), (enum_bits_), (const void*)&(bitfield_def) }

#define FIELD_VEC2(desc)    { TAG_FIELD_VEC2,      (desc), 0, NULL }
#define FIELD_VEC3(desc)    { TAG_FIELD_VEC3,      (desc), 0, NULL }
#define FIELD_VEC4(desc)    { TAG_FIELD_VEC4,      (desc), 0, NULL }

#define FIELD_REAL_BOUNDS(desc)      { TAG_FIELD_REAL_BOUNDS,      (desc), 0, NULL }
#define FIELD_REAL_BOUNDING_BOX(desc) { TAG_FIELD_REAL_BOUNDING_BOX, (desc), 0, NULL }

#define FIELD_MAT2(desc)    { TAG_FIELD_MAT2,      (desc), 0, NULL }
#define FIELD_MAT3(desc)    { TAG_FIELD_MAT3,      (desc), 0, NULL }
#define FIELD_MAT4(desc)    { TAG_FIELD_MAT4,      (desc), 0, NULL }

#define FIELD_STRING_ID(desc)           { TAG_FIELD_STRING_ID,  (desc), 0, NULL }
#define FIELD_REFERENCE(desc, ref_def)  { TAG_FIELD_REFERENCE,  (desc), 0, (const void*)&(ref_def) }
#define FIELD_BLOCK(desc, block_def)    { TAG_FIELD_BLOCK,      (desc), 0, (const void*)&(block_def) }

#define FIELD_PAD(bytes)    { TAG_FIELD_PAD,            "",    (bytes), NULL }

/* ---- TAG_REFERENCE macro - defines a tag_reference_definition variable ---- */
#define TAG_REFERENCE(name_, allowed_tag_) static const tag_reference_definition name_ = { (allowed_tag_) };

/* ------------------------------------------------------------------------
 * Bitfield definition macros - for defining tag_enum_definition and bitfield
 * from a manually defined enum.
 *
 * Usage:
 *   enum render_method { MODE_WIREFRAME = 0, ... EFFECT_BUMP = (1<<3), ... };
 *
 *   TAG_BITFIELD32_BEGIN(render_method, 3)
 *       TAG_BITFIELD32_ENTRY(MODE_WIREFRAME, "Wireframe")
 *       TAG_BITFIELD32_ENTRY(MODE_FLAT,      "Flat")
 *       ...
 *   TAG_BITFIELD32_END(render_method)
 *
 * This defines:
 *   - render_method_values (array of tag_enum_value)
 *   - render_method_def    (tag_enum_definition)
 *   - render_method_bitfield (tag_bitfield_definition)
 *   - render_method_ENUM_BITS (the enum_bits constant)
 * ------------------------------------------------------------------------ */
#define TAG_BITFIELD32_BEGIN(name_, enum_bits_) \
    enum { name_##_ENUM_BITS = (enum_bits_) }; \
    static const tag_enum_value name_##_values[] = {

#define TAG_BITFIELD32_ENTRY(value_, name_str_) \
    { (value_), (name_str_) },

#define TAG_BITFIELD32_END(name_) \
    }; \
    static const tag_enum_definition name_##_def = { \
        #name_, \
        (u32)(sizeof(name_##_values) / sizeof(tag_enum_value)), \
        name_##_values, \
        0   /* not flags */ \
    }; \
    static const tag_bitfield_definition name_##_bitfield = { \
        name_##_ENUM_BITS, &name_##_def \
    };

/* ------------------------------------------------------------------------
 * Legacy enum/flags macros (kept for backward compatibility)
 * ------------------------------------------------------------------------ */
#define TAG_ENUM_BEGIN(name_) \
    static const tag_enum_value name_##_values[] = {

#define TAG_ENUM_ENTRY(value_, name_)   { (i32)(value_), name_ },

#define TAG_ENUM_END(name_) \
    }; \
    static const tag_enum_definition name_##_def = { \
        #name_, \
        (u32)(sizeof(name_##_values) / sizeof(tag_enum_value)), \
        name_##_values, \
        0   /* not flags */ \
    };

#define TAG_FLAGS_BEGIN(name_) \
    static const tag_enum_value name_##_values[] = {

#define TAG_FLAGS_ENTRY(value_, name_)   { (i32)(value_), name_ },

#define TAG_FLAGS_END(name_) \
    }; \
    static const tag_enum_definition name_##_def = { \
        #name_, \
        (u32)(sizeof(name_##_values) / sizeof(tag_enum_value)), \
        name_##_values, \
        1   /* is_flags */ \
    };

/* ------------------------------------------------------------------------
 * Block definition macros
 * ------------------------------------------------------------------------ */
#define TAG_BLOCK_BEGIN(name_, max_count, elem_size)                      \
    static const tag_field_definition name_##_fields[] = {

#define TAG_BLOCK_END(name_, max_count, elem_size)                        \
    };                                                                     \
    static const tag_block_definition name_ = {                            \
        "", (u32)(elem_size), (u32)(max_count), name_##_fields, NULL       \
    };

/* ------------------------------------------------------------------------
 * Group macros - Four‑CC typed only once, terminator must be
 * the last entry in the field list. Embedded semicolons included.
 * ------------------------------------------------------------------------ */
#define TAG_GROUP_BEGIN(name_, magic_, total_size_)                       \
    enum { TAG_##name_ = (magic_) };                                      \
    static const tag_field_definition name_##_fields[] = {

#define TAG_GROUP_END(name_, total_size_)                                  \
    };                                                                      \
    static tag_group_definition name_ = {                                   \
        #name_,                                                             \
        TAG_##name_,                                                        \
        (u32)(total_size_),                                                 \
        name_##_fields,                                                     \
        NULL,   /* postprocess */                                           \
        NULL    /* next */                                                  \
    };

/* ------------------------------------------------------------------------
 * Access macro for elements inside a tag_block
 * ------------------------------------------------------------------------ */
#define TAG_BLOCK_GET_ELEMENT(block_ptr, index, element_type)     \
    (((element_type*)((block_ptr)->address)) + (index))

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */
static void                        tag_system_init(void);
static void                        tag_register_group(tag_group_definition *def);
static const tag_group_definition *tag_group_get(tag group_tag);
static i32                         tag_load(const char *name, tag group_tag);
static i32                         tag_load_from_memory(const void *buffer, u32 size, tag group_tag);
static void                       *tag_get(i32 tag_index, tag group_tag);
static void                        tag_release(i32 tag_index);
static i32                         tag_reload(i32 tag_index);
static void                        tag_poll_reloads(void);
static i32                         tag_spawn_instance(i32 backup_index);
static i32                         tag_kill_instance(i32 active_index);

/* Enum helper (works for both legacy and BITFIELD32 definitions) */
static const char *tag_enum_get_name(const tag_field_definition *field, i32 value);

/* String table API */
static void        string_table_init(void);
static string_id   string_id_intern(const char *str);
static const char *string_id_lookup(string_id id);
static u32         string_table_get_count(void);

/* ======================================================================
 * IMPLEMENTATION  (included only once in the main .c file)
 * ====================================================================== */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal instance
 * ------------------------------------------------------------------------ */
typedef struct {
    const tag_group_definition *group;
    void                       *backup_data;
    void                       *active_data;
    char                        path[TAG_SYSTEM_MAX_PATH];
    i32                         loaded;
    i32                         postprocessed;
    u32                         ref_count;
    i32                        *deps;
    u32                         dep_count;
    u32                         dep_capacity;
} tag_instance;

/* ------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------ */
static struct {
    i32                    initialized;
    tag_instance           instances[TAG_SYSTEM_MAX_TAGS];
    u32                    instance_count;
    tag_group_definition  *group_list;
} tag_sys;

/* ------------------------------------------------------------------------
 * String table state
 * ------------------------------------------------------------------------ */
static struct {
    string_table_entry  entries[STRING_TABLE_MAX_ENTRIES];
    u32                 count;          /* next free index */
    i32                 initialized;
} g_string_table;

/* ------------------------------------------------------------------------
 * Helper: find group by four‑cc
 * ------------------------------------------------------------------------ */
static tag_group_definition *tag_find_group_internal(tag group_tag) {
    tag_group_definition *g = tag_sys.group_list;
    while (g) {
        if (g->group_tag == group_tag) return g;
        g = g->next;
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * Helper: find already loaded instance by path
 * ------------------------------------------------------------------------ */
static i32 tag_find_instance(const char *path) {
    u32 i;
    for (i = 0; i < tag_sys.instance_count; ++i) {
        if (tag_sys.instances[i].loaded &&
            strcmp(tag_sys.instances[i].path, path) == 0)
            return (i32)i;
    }
    return -1;
}

/* ------------------------------------------------------------------------
 * Free the data owned by an instance (but keep the slot)
 * ------------------------------------------------------------------------ */
static void tag_free_instance_data(i32 idx) {
    tag_instance *inst = &tag_sys.instances[idx];
    if (inst->backup_data) { TAG_FREE(inst->backup_data); inst->backup_data = NULL; }
    if (inst->active_data) { TAG_FREE(inst->active_data); inst->active_data = NULL; }
    if (inst->deps)  { TAG_FREE(inst->deps);  inst->deps = NULL; }
    inst->loaded = 0;
    inst->postprocessed = 0;
}

/* ------------------------------------------------------------------------
 * Allocate a new instance slot
 * ------------------------------------------------------------------------ */
static i32 tag_alloc_instance(const char *path,
                              const tag_group_definition *group) {
    i32 idx;
    if (tag_sys.instance_count >= TAG_SYSTEM_MAX_TAGS)
        return -1;
    idx = (i32)tag_sys.instance_count++;
    memset(&tag_sys.instances[idx], 0, sizeof(tag_instance));
    tag_sys.instances[idx].group = group;
    tag_sys.instances[idx].ref_count = 1;
    tag_sys.instances[idx].backup_data = TAG_MALLOC(group->total_size);
    tag_sys.instances[idx].active_data = TAG_MALLOC(group->total_size);
    if (!tag_sys.instances[idx].backup_data || !tag_sys.instances[idx].active_data) {
        tag_free_instance_data(idx);
        tag_sys.instance_count--;
        return -1;
    }
    memset(tag_sys.instances[idx].backup_data, 0, group->total_size);
    memset(tag_sys.instances[idx].active_data, 0, group->total_size);
    strncpy(tag_sys.instances[idx].path, path, TAG_SYSTEM_MAX_PATH - 1);
    tag_sys.instances[idx].path[TAG_SYSTEM_MAX_PATH - 1] = '\0';
    return idx;
}

/* ------------------------------------------------------------------------
 * Return the alignment requirement of a field type.
 * ------------------------------------------------------------------------ */
static u32 tag_field_alignment(const tag_field_definition *def) {
    switch (def->type) {
        case TAG_FIELD_I16:
        case TAG_FIELD_U16:
            return 2;
        case TAG_FIELD_I32:
        case TAG_FIELD_U32:
        case TAG_FIELD_REAL:
        case TAG_FIELD_ENUM:
        case TAG_FIELD_FLAGS:
        case TAG_FIELD_VEC2:
        case TAG_FIELD_VEC3:
        case TAG_FIELD_VEC4:
        case TAG_FIELD_REAL_BOUNDS:
        case TAG_FIELD_REAL_BOUNDING_BOX:
        case TAG_FIELD_MAT2:
        case TAG_FIELD_MAT3:
        case TAG_FIELD_MAT4:
        case TAG_FIELD_STRING_ID:
        case TAG_FIELD_REFERENCE:
        case TAG_FIELD_BITFIELD32:
            return 4;
        case TAG_FIELD_I64:
        case TAG_FIELD_U64:
            return 8;
        case TAG_FIELD_BLOCK:
            return sizeof(void*);
        case TAG_FIELD_BOOL:
            return sizeof(bool);
        default:
            return 1;
    }
}

/* ------------------------------------------------------------------------
 * Return the byte size of a single field described by `def`
 * ------------------------------------------------------------------------ */
static u32 tag_field_size(const tag_field_definition *def) {
    u32 fsize;
    switch (def->type) {
        case TAG_FIELD_TERMINATOR:           fsize = 0; break;
        case TAG_FIELD_BOOL:                 fsize = sizeof(bool); break;
        case TAG_FIELD_I8:                   fsize = sizeof(i8); break;
        case TAG_FIELD_U8:                   fsize = sizeof(u8); break;
        case TAG_FIELD_I16:                  fsize = sizeof(i16); break;
        case TAG_FIELD_U16:                  fsize = sizeof(u16); break;
        case TAG_FIELD_I32:                  fsize = sizeof(i32); break;
        case TAG_FIELD_U32:                  fsize = sizeof(u32); break;
        case TAG_FIELD_I64:                  fsize = sizeof(i64); break;
        case TAG_FIELD_U64:                  fsize = sizeof(u64); break;
        case TAG_FIELD_REAL:                 fsize = sizeof(real); break;
        case TAG_FIELD_ENUM:                 fsize = sizeof(i32); break;
        case TAG_FIELD_FLAGS:                fsize = sizeof(i32); break;
        case TAG_FIELD_BITFIELD32:           fsize = sizeof(u32); break;
        case TAG_FIELD_VEC2:                 fsize = sizeof(vec2); break;
        case TAG_FIELD_VEC3:                 fsize = sizeof(vec3); break;
        case TAG_FIELD_VEC4:                 fsize = sizeof(vec4); break;
        case TAG_FIELD_REAL_BOUNDS:          fsize = sizeof(real_bounds); break;
        case TAG_FIELD_REAL_BOUNDING_BOX:    fsize = sizeof(real_bounding_box); break;
        case TAG_FIELD_MAT2:                 fsize = sizeof(mat2); break;
        case TAG_FIELD_MAT3:                 fsize = sizeof(mat3); break;
        case TAG_FIELD_MAT4:                 fsize = sizeof(mat4); break;
        case TAG_FIELD_STRING_ID:            fsize = sizeof(string_id); break;
        case TAG_FIELD_BLOCK:                fsize = sizeof(tag_block); break;
        case TAG_FIELD_REFERENCE:            fsize = sizeof(tag_reference); break;
        case TAG_FIELD_PAD:                  fsize = def->pad_data; break;
        default:                             fsize = 0xFFFFFFFF; break;
    }
    return fsize;
}

/* ------------------------------------------------------------------------
 * Walk a struct's fields, calling `callback` for each field.
 * `base` points to the start of the struct.
 * ------------------------------------------------------------------------ */
static void tag_walk_fields(
    void *base,
    const tag_field_definition *fields,
    void (*callback)(const tag_field_definition *f,
                     void *field_addr,
                     void *user),
    void *user)
{
    u8 *ptr = (u8 *)base;
    const tag_field_definition *f;
    for (f = fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8 *)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
        callback(f, ptr, user);
        ptr += tag_field_size(f);
    }
}

/* ------------------------------------------------------------------------
 * Block postprocessing (per element, not recursive yet)
 * ------------------------------------------------------------------------ */
static void tag_postprocess_block_elements(
    tag_block *block,
    const tag_block_definition *block_def)
{
    u32 i;
    u8 *elem;
    if (!block || !block->address || !block_def) return;
    elem = (u8 *)block->address;
    for (i = 0; i < block->count; ++i) {
        if (block_def->postprocess)
            block_def->postprocess(elem);
        elem += block_def->element_size;
    }
}

/* ------------------------------------------------------------------------
 * Whole‑tag postprocessing (calls group->postprocess once)
 * ------------------------------------------------------------------------ */
static void tag_postprocess_tag(i32 idx) {
    tag_instance *inst = &tag_sys.instances[idx];
    if (inst->postprocessed) return;
    if (inst->group->postprocess)
        inst->group->postprocess(inst->active_data);
    inst->postprocessed = 1;
}

/* ------------------------------------------------------------------------
 * String table implementation - index‑based
 * ------------------------------------------------------------------------ */
static void string_table_init(void) {
    if (g_string_table.initialized) return;
    g_string_table.count = 0;
    g_string_table.initialized = 1;
}

static string_id string_id_intern(const char *str) {
    u32 i;
    size_t len;

    if (!str || *str == '\0') return TAG_NULL(string_id);
    if (!g_string_table.initialized) string_table_init();

    for (i = 0; i < g_string_table.count; i++) {
        if (strcmp(g_string_table.entries[i].string, str) == 0)
            return (string_id)i;
    }

    if (g_string_table.count >= STRING_TABLE_MAX_ENTRIES)
        return TAG_NULL(string_id);

    len = strlen(str);
    if (len >= STRING_TABLE_MAX_STRING_LENGTH)
        len = STRING_TABLE_MAX_STRING_LENGTH - 1;

    memcpy(g_string_table.entries[g_string_table.count].string, str, len);
    g_string_table.entries[g_string_table.count].string[len] = '\0';

    return (string_id)(g_string_table.count++);
}

static const char *string_id_lookup(string_id id) {
    if (id == TAG_NULL(string_id)) return NULL;
    if (id >= g_string_table.count) return NULL;
    return g_string_table.entries[id].string;
}

static u32 string_table_get_count(void) {
    return g_string_table.count;
}

/* ------------------------------------------------------------------------
 * Enum value name lookup (works for legacy and BITFIELD32 definitions)
 * ------------------------------------------------------------------------ */
static const char *tag_enum_get_name(const tag_field_definition *field, i32 value) {
    const tag_enum_definition *def;
    u32 i;

    if (field->type == TAG_FIELD_ENUM || field->type == TAG_FIELD_FLAGS) {
        def = (const tag_enum_definition*)field->extra;
        if (!def) return NULL;
        for (i = 0; i < def->value_count; ++i) {
            if (def->values[i].value == value)
                return def->values[i].name;
        }
        return NULL;
    }

    if (field->type == TAG_FIELD_BITFIELD32) {
        const tag_bitfield_definition *bf;
        bf = (const tag_bitfield_definition*)field->extra;
        if (!bf) return NULL;
        def = bf->def;
        if (!def) return NULL;
        for (i = 0; i < def->value_count; ++i) {
            if (def->values[i].value == value)
                return def->values[i].name;
        }
        return NULL;
    }

    return NULL;
}

/* ------------------------------------------------------------------------
 * Binary header for a single tag file / memory buffer
 * ------------------------------------------------------------------------ */
typedef struct { u32 fourcc; u32 struct_size; } tag_file_header;

/* ------------------------------------------------------------------------
 * Generic reader - loads a tag from any source via a read callback
 * ------------------------------------------------------------------------ */
static i32 tag_load_from(
    u32 (*read_fn)(void *dest, u32 size, void *ctx),
    void *read_ctx,
    const char *source_name,
    tag group_tag)
{
    tag_file_header hdr;
    tag_group_definition *group;
    i32 idx;
    tag_instance *inst;
    void *data;
    u32 offset;
    const tag_field_definition *f;

    if (read_fn(&hdr, sizeof(hdr), read_ctx) != sizeof(hdr))
        return -1;
    if (hdr.fourcc != group_tag)
        return -1;

    group = tag_find_group_internal(group_tag);
    if (!group) return -1;

    idx = tag_find_instance(source_name);
    if (idx >= 0) {
        tag_sys.instances[idx].ref_count++;
        return idx;
    }

    idx = tag_alloc_instance(source_name, group);
    if (idx < 0) return -1;
    inst = &tag_sys.instances[idx];

    data = inst->backup_data;

    u32 off = 0;
    for (f = group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        off = (off + align - 1) & ~(align - 1);
        u32 fsize = tag_field_size(f);

        if (f->type == TAG_FIELD_BLOCK) {
            tag_block *blk = (tag_block*)((u8*)data + off);
            u32 count;
            if (read_fn(&count, sizeof(count), read_ctx) != sizeof(count)) goto load_fail;
            blk->count = count;
        } else if (f->type == TAG_FIELD_REFERENCE) {
            tag_reference *ref = (tag_reference*)((u8*)data + off);
            char path_buf[TAG_SYSTEM_MAX_PATH];
            i32 i = 0; char ch;
            while (i < TAG_SYSTEM_MAX_PATH - 1 && read_fn(&ch, 1, read_ctx) == 1 && ch != 0)
                path_buf[i++] = ch;
            path_buf[i] = '\0';

            if (path_buf[0]) {
                ref->handle = tag_load(path_buf, f->extra ? ((tag_reference_definition*)f->extra)->allowed_group_tag : 0);
            } else {
                ref->handle = -1;
            }
        } else {
            if (read_fn((u8*)data + off, fsize, read_ctx) != fsize) goto load_fail;
        }
        off += fsize;
    }

    off = 0;
    for (f = group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        off = (off + align - 1) & ~(align - 1);
        if (f->type == TAG_FIELD_BLOCK) {
            tag_block *blk = (tag_block*)((u8*)data + off);
            if (blk->count > 0) {
                const tag_block_definition *bdef =
                    (const tag_block_definition*)f->extra;
                u32 esize = bdef->element_size;
                void *block_data = TAG_MALLOC(blk->count * esize);
                if (!block_data ||
                    read_fn(block_data, esize * blk->count, read_ctx) != esize * blk->count) goto load_fail;
                blk->address = block_data;
            } else {
                blk->address = NULL;
            }
        }
        off += tag_field_size(f);
    }

    inst->loaded = 1;
    memcpy(inst->active_data, inst->backup_data, group->total_size);

    off = 0;
    for (f = group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        off = (off + align - 1) & ~(align - 1);
        if (f->type == TAG_FIELD_BLOCK) {
            tag_block *blk = (tag_block*)((u8*)data + off);
            if (blk->address)
                tag_postprocess_block_elements(blk, (tag_block_definition*)f->extra);
        }
        off += tag_field_size(f);
    }

    tag_postprocess_tag(idx);
    return idx;

load_fail:
    tag_free_instance_data(idx);
    tag_sys.instance_count--;
    return -1;
}

/* ------------------------------------------------------------------------
 * FILE* read callback
 * ------------------------------------------------------------------------ */
static u32 file_read_fn(void *dest, u32 size, void *ctx) {
    return (u32)fread(dest, 1, size, (FILE*)ctx);
}

/* ------------------------------------------------------------------------
 * Memory buffer read callback
 * ------------------------------------------------------------------------ */
typedef struct { const u8 *buf; u32 size; u32 pos; } membuf_ctx;

static u32 mem_read_fn(void *dest, u32 size, void *ctx) {
    membuf_ctx *m = (membuf_ctx*)ctx;
    if (m->pos + size > m->size) size = m->size - m->pos;
    memcpy(dest, m->buf + m->pos, size);
    m->pos += size;
    return size;
}

/* ------------------------------------------------------------------------
 * Recursive copy for spawning instances
 * ------------------------------------------------------------------------ */
static i32 tag_copy_recursive(i32 source_idx, void *source_data) {
    tag_instance *source_inst = &tag_sys.instances[source_idx];
    i32 new_idx = tag_alloc_instance("", source_inst->group);
    if (new_idx < 0) return -1;
    tag_instance *new_inst = &tag_sys.instances[new_idx];

    memcpy(new_inst->backup_data, source_data, source_inst->group->total_size);
    memcpy(new_inst->active_data, source_data, source_inst->group->total_size);

    u32 off = 0;
    const tag_field_definition *f;
    for (f = source_inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        off = (off + align - 1) & ~(align - 1);
        if (f->type == TAG_FIELD_REFERENCE) {
            tag_reference *ref = (tag_reference*)((u8*)new_inst->active_data + off);
            if (ref->handle >= 0) {
                tag_instance *ref_inst = &tag_sys.instances[ref->handle];
                i32 new_ref_idx = tag_copy_recursive(ref->handle, ref_inst->active_data);
                if (new_ref_idx < 0) {
                    tag_free_instance_data(new_idx);
                    tag_sys.instance_count--;
                    return -1;
                }
                ref->handle = new_ref_idx;
            }
        } else if (f->type == TAG_FIELD_BLOCK) {
            /* TODO: Handle blocks with references inside */
        }
        off += tag_field_size(f);
    }

    new_inst->loaded = 1;
    return new_idx;
}

/* ------------------------------------------------------------------------
 * Public API implementation
 * ------------------------------------------------------------------------ */
static void tag_system_init(void) {
    if (tag_sys.initialized) return;
    fprintf(stderr, "[tag] tag_system_init\n");
    memset(&tag_sys, 0, sizeof(tag_sys));
    tag_sys.initialized = 1;
    string_table_init();
}

static void tag_register_group(tag_group_definition *def) {
    if (!def || !tag_sys.initialized) return;
    fprintf(stderr, "[tag] tag_register_group %s\n", def->name);
    def->next = tag_sys.group_list;
    tag_sys.group_list = def;
}

static const tag_group_definition *tag_group_get(tag group_tag) {
    fprintf(stderr, "[tag] tag_group_get %u\n", (u32)group_tag);
    return tag_find_group_internal(group_tag);
}

static i32 tag_load(const char *name, tag group_tag) {
    FILE *fp;
    i32 idx;
    if (!tag_sys.initialized || !name) return -1;
    fprintf(stderr, "[tag] tag_load '%s.%c%c%c%c'\n", name, TAG_MAGIC_UNPACK(group_tag));

    idx = tag_find_instance(name);
    if (idx >= 0) {
        tag_sys.instances[idx].ref_count++;
        return idx;
    }

    fp = fopen(name, "rb");
    if (!fp) return -1;
    idx = tag_load_from(file_read_fn, fp, name, group_tag);
    fclose(fp);
    return idx;
}

static i32 tag_load_from_memory(const void *buffer, u32 size, tag group_tag) {
    membuf_ctx ctx;
    ctx.buf = (const u8*)buffer;
    ctx.size = size;
    ctx.pos = 0;
    return tag_load_from(mem_read_fn, &ctx, "<memory>", group_tag);
}

static void *tag_get(i32 tag_index, tag group_tag) {
    tag_instance *inst;
    if (tag_index < 0 || (u32)tag_index >= tag_sys.instance_count)
        return NULL;
    inst = &tag_sys.instances[tag_index];
    if (!inst->loaded || inst->group->group_tag != group_tag)
        return NULL;
    return inst->active_data;
}

static void tag_release(i32 tag_index) {
    tag_instance *inst;
    if (tag_index < 0 || (u32)tag_index >= tag_sys.instance_count)
        return;
    inst = &tag_sys.instances[tag_index];
    if (--inst->ref_count == 0) {
        tag_free_instance_data(tag_index);
        if ((u32)tag_index != tag_sys.instance_count - 1) {
            memcpy(&tag_sys.instances[tag_index],
                   &tag_sys.instances[tag_sys.instance_count - 1],
                   sizeof(tag_instance));
        }
        --tag_sys.instance_count;
    }
}

static i32 tag_reload(i32 tag_index) {
    tag_instance *inst;
    if (tag_index < 0 || (u32)tag_index >= tag_sys.instance_count) return -1;
    inst = &tag_sys.instances[tag_index];
    if (!inst->loaded) return -1;
    memcpy(inst->active_data, inst->backup_data, inst->group->total_size);
    return 0;
}

static void tag_poll_reloads(void) {
    /* stub */
}

static i32 tag_spawn_instance(i32 backup_index) {
    tag_instance *inst;
    if (backup_index < 0 || (u32)backup_index >= tag_sys.instance_count) return -1;
    inst = &tag_sys.instances[backup_index];
    if (!inst->loaded) return -1;
    return tag_copy_recursive(backup_index, inst->backup_data);
}

static i32 tag_kill_instance(i32 active_index) {
    tag_instance *inst;
    if (active_index < 0 || (u32)active_index >= tag_sys.instance_count) return -1;
    inst = &tag_sys.instances[active_index];
    if (!inst->loaded) return -1;
    tag_free_instance_data(active_index);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* REFLECTION_H */