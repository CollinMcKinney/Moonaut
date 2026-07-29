

#ifndef COMMON_H
#define COMMON_H

#include "vectors.h"
#include "../libs/jobgraph/jobgraph.h"

#ifdef __cplusplus
extern "C" {
#endif

static jobgraph_t *g_jobgraph = ((void*)0);
static i32 g_thread_count = 0;

typedef i32 enum32;
STATIC_ASSERT(sizeof(enum32)  == 0x4, enum32_size_wrong);

typedef u32 flags32;
STATIC_ASSERT(sizeof(flags32) == 0x4, flags32_size_wrong);

typedef u32 string_id;
STATIC_ASSERT(sizeof(string_id) == 0x4, string_id_size_wrong);

typedef struct real_bounds {
    real lower;
    real upper;
} real_bounds;

typedef struct real_bounding_box {
    real_bounds x;
    real_bounds y;
    real_bounds z;
} real_bounding_box;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
