#ifndef RIGID_BODY_DEFINITION_H
#define RIGID_BODY_DEFINITION_H

#include "../reflection.h"
#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rigid_body_shape {
    RIGID_SHAPE_SPHERE = 0,
    RIGID_SHAPE_BOX,
    RIGID_SHAPE_PLANE,
    RIGID_SHAPE_PILL,
    RIGID_SHAPE_CYLINDER,
    RIGID_SHAPE_CONVEX
} rigid_body_shape;

TAG_ENUM_BEGIN(rigid_body_shape)
    TAG_ENUM_ENTRY(RIGID_SHAPE_SPHERE,   "sphere")
    TAG_ENUM_ENTRY(RIGID_SHAPE_BOX,      "box")
    TAG_ENUM_ENTRY(RIGID_SHAPE_PLANE,    "plane")
    TAG_ENUM_ENTRY(RIGID_SHAPE_PILL,     "pill")
    TAG_ENUM_ENTRY(RIGID_SHAPE_CYLINDER, "cylinder")
    TAG_ENUM_ENTRY(RIGID_SHAPE_CONVEX,   "convex")
TAG_ENUM_END(rigid_body_shape)

/* ------------------------------------------------------------------------
   Convex hull vertex block – simple array of vec3
   ------------------------------------------------------------------------ */
TAG_BLOCK_BEGIN(convex_hull_vertex_block, 256, sizeof(vec3))
    FIELD_VEC3("position"),
    FIELD_TERMINATOR
TAG_BLOCK_END(convex_hull_vertex_block, 256, sizeof(vec3))

/* ------------------------------------------------------------------------
   Rigid body definition
   ------------------------------------------------------------------------ */
typedef struct rigid_body_definition {
    /* - Identity - */
    enum32          shape;
    real            mass;                 /* 0 = static (infinite mass) */

    /* - Collision geometry - */
    real             sphere_radius;        /* for SPHERE */
    vec3             box_half_extents;     /* for BOX */
    vec3             plane_normal;         /* for PLANE (unit length) */
    real             capsule_radius;       /* for PILL */
    real             capsule_height;       /* central cylinder length (excluding end‑caps) */
    real             cylinder_radius;      /* for CYLINDER */
    real             cylinder_height;      /* total height */
    struct tag_block convex_hull_vertices; /* for CONVEX – array of vec3 */

    /* - Movement & damping - */
    vec3            velocity;
    vec3            angular_velocity;
    real            restitution;
    real            friction;
    real            linear_damping;
    real            angular_damping;

    /* - BSP data (placeholder – will become a tag_reference to 'cbsp' later) - */
} rigid_body_definition;

TAG_GROUP_BEGIN(rigid_body, 'rbdy', sizeof(struct rigid_body_definition))
    FIELD_ENUM("shape", rigid_body_shape),
    FIELD_REAL("mass"),
    FIELD_REAL("sphere_radius"),
    FIELD_VEC3("box_half_extents"),
    FIELD_VEC3("plane_normal"),
    FIELD_REAL("capsule_radius"),
    FIELD_REAL("capsule_height"),
    FIELD_REAL("cylinder_radius"),
    FIELD_REAL("cylinder_height"),
    FIELD_BLOCK("convex_hull_vertices", convex_hull_vertex_block),
    FIELD_VEC3("velocity"),
    FIELD_VEC3("angular_velocity"),
    FIELD_REAL("restitution"),
    FIELD_REAL("friction"),
    FIELD_REAL("linear_damping"),
    FIELD_REAL("angular_damping"),
    FIELD_TERMINATOR
TAG_GROUP_END(rigid_body, sizeof(struct rigid_body_definition))

/* Default rigid bodies:
   - sphere: radius 1
   - box: half extents 1,1,1
   Dimensions match DEFAULT_MODEL_SPHERE / DEFAULT_MODEL_BOX. */

static const struct rigid_body_definition DEFAULT_RIGID_BODY_SPHERE = {
    /* shape */            RIGID_SHAPE_SPHERE,
    /* mass */             1.0f,
    /* sphere_radius */    1.0f,
    /* box_half_extents */ { 0.0f, 0.0f, 0.0f },
    /* plane_normal */     { 0.0f, 1.0f, 0.0f },
    /* capsule_radius */   0.0f,
    /* capsule_height */   0.0f,
    /* cylinder_radius */  0.0f,
    /* cylinder_height */  0.0f,
    /* convex_hull_vertices */ { 0u, NULL },

    /* velocity */          { 0.0f, 0.0f, 0.0f },
    /* angular_velocity */ { -10.0f, 0.0f, 0.0f },
    /* restitution */       0.6f,
    /* friction */          0.5f,
    /* linear_damping */   0.0f,
    /* angular_damping */  0.0f
};

static const struct rigid_body_definition DEFAULT_RIGID_BODY_BOX = {
    /* shape */            RIGID_SHAPE_BOX,
    /* mass */             1.0f,
    /* sphere_radius */    0.0f,
    /* box_half_extents */ { 5.0f, 1.0f, 5.0f },
    /* plane_normal */     { 0.0f, 1.0f, 0.0f },
    /* capsule_radius */   0.0f,
    /* capsule_height */   0.0f,
    /* cylinder_radius */  0.0f,
    /* cylinder_height */  0.0f,
    /* convex_hull_vertices */ { 0u, NULL },

    /* velocity */          { 0.0f, 0.0f, 0.0f },
    /* angular_velocity */ { 0.0f, 0.0f, 0.0f },
    /* restitution */       0.3f,
    /* friction */          0.8f,
    /* linear_damping */   0.0f,
    /* angular_damping */  0.0f
};

#ifdef __cplusplus
}
#endif
#endif /* RIGID_BODY_DEFINITION_H */
