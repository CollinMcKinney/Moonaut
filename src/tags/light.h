#ifndef LIGHT_DEFINITION_H
#define LIGHT_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum light_type {
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT,
    LIGHT_SPOT
} light_type;

TAG_ENUM_BEGIN(light_type)
    TAG_ENUM_ENTRY(LIGHT_DIRECTIONAL, "directional")
    TAG_ENUM_ENTRY(LIGHT_POINT,       "point")
    TAG_ENUM_ENTRY(LIGHT_SPOT,        "spot")
TAG_ENUM_END(light_type)

/* Light definition – no separate intensity, use color magnitude. */
typedef struct light_definition {
    enum32      type;
    vec3        color;          /* RGB intensity (e.g. (0.5,0.5,0.5) for dim) */
    vec3        position;       /* for point/spot */
    vec3        direction;      /* for directional/spot (normalised) */
    real        range;          /* attenuation distance (0 = infinite for directional) */
    real        spot_inner_angle;   /* in radians, cos of inner cone */
    real        spot_outer_angle;   /* in radians, cos of outer cone */
    real        spot_falloff;       /* exponent for spot attenuation */
    bool        enabled;
} light_definition;

TAG_GROUP_BEGIN(light, TAG_MAGIC_PACK(ligh), sizeof(struct light_definition))
    FIELD_ENUM("type", light_type),
    FIELD_VEC3("color"),
    FIELD_VEC3("position"),
    FIELD_VEC3("direction"),
    FIELD_REAL("range"),
    FIELD_REAL("spot_inner_angle"),
    FIELD_REAL("spot_outer_angle"),
    FIELD_REAL("spot_falloff"),
    FIELD_BOOL("enabled"),
    FIELD_TERMINATOR
TAG_GROUP_END(light, sizeof(struct light_definition))

#ifdef __cplusplus
}
#endif
#endif