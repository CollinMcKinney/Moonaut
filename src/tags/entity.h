#ifndef ENTITY_DEFINITION_H
#define ENTITY_DEFINITION_H

#include "../common.h"
#include "../reflection.h"
#include "model.h"
#include "rigid_body.h"
#include "collision_bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
   Entity type
   ------------------------------------------------------------------------ */
typedef enum entity_type {
    ENTITY_STATIC  = 0,    /* non‑physics (model only) */
    ENTITY_DYNAMIC,        /* physics body (rigid_body + model) */
    ENTITY_TRIGGER         /* invisible physics volume (rigid_body, no model) */
} entity_type;

TAG_ENUM_BEGIN(entity_type)
    TAG_ENUM_ENTRY(ENTITY_STATIC,   "static")
    TAG_ENUM_ENTRY(ENTITY_DYNAMIC,  "dynamic")
    TAG_ENUM_ENTRY(ENTITY_TRIGGER,  "trigger")
TAG_ENUM_END(entity_type)

/* ------------------------------------------------------------------------
   References (only allowed tag groups)
   ------------------------------------------------------------------------ */
TAG_REFERENCE(entity_model_ref,         TAG_model)      /* 'modl' */
TAG_REFERENCE(entity_rigid_body_ref,    TAG_rigid_body) /* 'rbdy' */
TAG_REFERENCE(entity_collision_bsp_ref, TAG_collision_bsp) /* 'rbdy' */

/* ------------------------------------------------------------------------
   Entity - a logical object in the world
   ------------------------------------------------------------------------ */
typedef struct entity_definition {
    enum32          type;
    struct tag_reference model;       /* optional - visual representation */
    struct tag_reference rigid_body;  /* optional - physics properties */
    struct tag_reference collision_bsp; /* complex collision - may be null */
    vec3            position;
    vec4            orientation;      /* quaternion */
    /* future: script reference, sound, etc. */
} entity_definition;

TAG_GROUP_BEGIN(entity, TAG_MAGIC_PACK(enty), sizeof(struct entity_definition))
    FIELD_ENUM("type", entity_type),
    FIELD_REFERENCE("model", entity_model_ref),
    FIELD_REFERENCE("rigid_body", entity_rigid_body_ref),
    FIELD_REFERENCE("collision_bsp", entity_collision_bsp_ref),
    FIELD_VEC3("position"),
    FIELD_VEC4("orientation"),
    FIELD_TERMINATOR
TAG_GROUP_END(entity, sizeof(struct entity_definition))

/* Default entities: one sphere + one box
   Uses renderable default model + rigid-body tag references. */

static const struct entity_definition DEFAULT_ENTITY_SPHERE = {
    /* type */ ENTITY_DYNAMIC,
    /* model */      { (i32)-1 },      /* patched by defaults registration */
    /* rigid_body */ { (i32)-1 },      /* patched by defaults registration */
    /* collision */  { (i32)-1 },
    /* position */   {{ 0.0f, 6.0f, 0.0f }},
    /* orientation */{{ 0.0f, 0.0f, 0.0f, 1.0f }}
};

static const struct entity_definition DEFAULT_ENTITY_BOX = {
    /* type */ ENTITY_STATIC,
    /* model */      { (i32)-1 },      /* patched by defaults registration */
    /* rigid_body */ { (i32)-1 },      /* patched by defaults registration */
    /* collision */  { (i32)-1 },
    /* position */   {{ 0.0f, 0.0f, 0.0f }},
    /* orientation */{{ 0.0f, 0.0f, 0.0f, 1.0f }}
};

#ifdef __cplusplus
}
#endif

#endif /* ENTITY_DEFINITION_H */
