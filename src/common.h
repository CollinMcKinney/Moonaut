

#ifndef COMMON_H
#define COMMON_H

#include "vectors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef i32 enum32;
STATIC_ASSERT(sizeof(enum32)  == 0x4, enum32_size_wrong);

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