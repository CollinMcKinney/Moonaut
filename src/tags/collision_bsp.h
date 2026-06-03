#ifndef COLLISION_BSP_DEFINITION_H
#define COLLISION_BSP_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bsp_triangle {
    vec3 a;
    vec3 b;
    vec3 c;
} bsp_triangle;

TAG_BLOCK_BEGIN(bsp_triangle_block, 65535, sizeof(struct bsp_triangle))
    FIELD_VEC3("a"),
    FIELD_VEC3("b"),
    FIELD_VEC3("c"),
    FIELD_TERMINATOR
TAG_BLOCK_END(bsp_triangle_block, 65535, sizeof(struct bsp_triangle))

/* ------------------------------------------------------------------------
    BSP plane – defines a splitting surface
    ------------------------------------------------------------------------ */
typedef struct bsp_plane {
    vec3 normal;       /* unit length */
    real distance;     /* signed distance along normal */
} bsp_plane;

TAG_BLOCK_BEGIN(bsp_plane_block, 65535, sizeof(struct bsp_plane))
    FIELD_VEC3("normal"),
    FIELD_REAL("distance"),
    FIELD_TERMINATOR
TAG_BLOCK_END(bsp_plane_block, 65535, sizeof(struct bsp_plane))

/* ------------------------------------------------------------------------
   BSP node – references a plane and two children
   ------------------------------------------------------------------------ */
typedef struct bsp_node {
    i32 plane_index;        /* index into planes array */
    i32 front_child;        /* index into nodes array, or leaf flag */
    i32 back_child;         /* index into nodes array, or leaf flag */
    i32 leaf_index;        /* if child is a leaf, this is the leaf index (-1 otherwise) */
} bsp_node;

TAG_BLOCK_BEGIN(bsp_node_block, 65535, sizeof(struct bsp_node))
    FIELD_I32("plane_index"),
    FIELD_I32("front_child"),
    FIELD_I32("back_child"),
    FIELD_I32("leaf_index"),
    FIELD_TERMINATOR
TAG_BLOCK_END(bsp_node_block, 65535, sizeof(struct bsp_node))

/* ------------------------------------------------------------------------
   BSP leaf – a solid / empty cell
   ------------------------------------------------------------------------ */
typedef struct bsp_leaf {
    i32 solid;   /* 1 = solid, 0 = empty */
    /* future: surface flags, material index, etc. */
} bsp_leaf;

TAG_BLOCK_BEGIN(bsp_leaf_block, 65535, sizeof(struct bsp_leaf))
    FIELD_I32("solid"),
    FIELD_TERMINATOR
TAG_BLOCK_END(bsp_leaf_block, 65535, sizeof(struct bsp_leaf))

/* ------------------------------------------------------------------------
    Top‑level BSP tag
    ------------------------------------------------------------------------ */
typedef struct collision_bsp_definition {
    struct tag_block planes;
    struct tag_block nodes;
    struct tag_block leaves;
    struct tag_block triangles; /* for simple triangle-list collision */
    real_bounding_box bounds;   /* overall bounding box for quick rejection */
} collision_bsp_definition;

TAG_GROUP_BEGIN(collision_bsp, 'cbsp', sizeof(struct collision_bsp_definition))
    FIELD_BLOCK("planes", bsp_plane_block),
    FIELD_BLOCK("nodes", bsp_node_block),
    FIELD_BLOCK("leaves", bsp_leaf_block),
    FIELD_BLOCK("triangles", bsp_triangle_block),
    FIELD_REAL_BOUNDING_BOX("bounds"),
    FIELD_TERMINATOR
TAG_GROUP_END(collision_bsp, sizeof(struct collision_bsp_definition))

#ifdef __cplusplus
}
#endif
#endif /* COLLISION_BSP_DEFINITION_H */