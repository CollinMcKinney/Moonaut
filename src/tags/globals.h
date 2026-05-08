#ifndef GLOBALS_DEFINITION_H
#define GLOBALS_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct globals_definition {
    vec3  gravity;
    real  physics_rate;      /* Hz (e.g., 60) -> fixed_dt = 1/rate */
    vec3  clear_color;
    vec3  fog_color;
    real  fog_start;
    real  fog_end;
    vec3  light_dir;
    vec3  light_col;
    vec3  ambient_col;
    i32   pause_physics;
} globals_definition;

/* Default globals asset */
static const struct globals_definition DEFAULT_GLOBALS = {
    /* gravity */        { 0.0f, -9.8f, 0.0f },
    /* physics_rate */   60.0f,
    /* clear_color */    { 0.0f, 0.0f, 0.0f },
    /* fog_color */      { 0.0f, 0.0f, 0.0f },
    /* fog_start */      10.0f,
    /* fog_end */        1000.0f,
    /* light_dir */      { 0.5f, 1.0f, 0.4f },
    /* light_col */      { 1.0f, 1.0f, 1.0f },
    /* ambient_col */    { 0.2f, 0.2f, 0.25f },
    /* pause_physics */  0
};

TAG_GROUP_BEGIN(globals, 'glbl', sizeof(struct globals_definition))
    FIELD_VEC3("gravity"),
    FIELD_REAL("physics_rate"),
    FIELD_VEC3("clear_color"),
    FIELD_VEC3("fog_color"),
    FIELD_REAL("fog_start"),
    FIELD_REAL("fog_end"),
    FIELD_VEC3("light_dir"),
    FIELD_VEC3("light_col"),
    FIELD_VEC3("ambient_col"),
    FIELD_I32("pause_physics"),
    FIELD_TERMINATOR
TAG_GROUP_END(globals, sizeof(struct globals_definition))

#ifdef __cplusplus
}
#endif
#endif /* GLOBALS_DEFINITION_H */