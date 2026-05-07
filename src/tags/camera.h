#ifndef CAMERA_DEFINITION_H
#define CAMERA_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct camera_definition {
    vec3  eye;
    vec3  center;
    vec3  up;
    real  fov;       /* degrees */
} camera_definition;

/* Default camera asset */
static const struct camera_definition DEFAULT_CAMERA = {
    /* eye */    { 8.0f, 8.0f, 12.0f },
    /* center */ { 0.0f, 1.0f, 0.0f },
    /* up */     { 0.0f, 1.0f, 0.0f },
    /* fov */    60.0f
};

TAG_GROUP_BEGIN(camera, 'cmra', sizeof(struct camera_definition))
    FIELD_VEC3("eye"),
    FIELD_VEC3("center"),
    FIELD_VEC3("up"),
    FIELD_REAL("fov"),
    FIELD_TERMINATOR
TAG_GROUP_END(camera, sizeof(struct camera_definition))

#ifdef __cplusplus
}
#endif
#endif /* CAMERA_DEFINITION_H */