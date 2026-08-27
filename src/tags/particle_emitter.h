#ifndef PARTICLE_EMITTER_DEFINITION_H
#define PARTICLE_EMITTER_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Particle emitter definition ---- */
typedef struct particle_emitter_definition {
    vec3       position;      /* world position of emitter */
    vec3       color;         /* base color of particles */
    real       alpha;         /* base alpha */
    real       size;          /* initial size */
    real       lifetime;      /* seconds each particle lives */
    real       speed;         /* initial speed magnitude */
    real       spread;        /* random direction spread factor */
    real       gravity;       /* downward acceleration (scalar) */
    bool       loop;          /* 1 = continuous, 0 = single burst */
    real       duration;      /* unused (reserved) */
    real       emission_rate; /* particles per second */
} particle_emitter_definition;

TAG_GROUP_BEGIN(particle_emitter, TAG_MAGIC_PACK(part), sizeof(struct particle_emitter_definition))
    FIELD_VEC3("position"),
    FIELD_VEC3("color"),
    FIELD_REAL("alpha"),
    FIELD_REAL("size"),
    FIELD_REAL("lifetime"),
    FIELD_REAL("speed"),
    FIELD_REAL("spread"),
    FIELD_REAL("gravity"),
    FIELD_BOOL("loop"),
    FIELD_REAL("duration"),
    FIELD_REAL("emission_rate"),
    FIELD_TERMINATOR
TAG_GROUP_END(particle_emitter, sizeof(struct particle_emitter_definition))

/* Default emitter (spark‑like) */
static const struct particle_emitter_definition DEFAULT_PARTICLE_EMITTER = {
    .position      = {0.0f, 0.0f, 0.0f},
    .color         = {1.0f, 0.8f, 0.4f},
    .alpha         = 1.0f,
    .size          = 0.1f,
    .lifetime      = 0.5f,
    .speed         = 2.0f,
    .spread        = 0.5f,
    .gravity       = 0.0f,
    .loop          = true,
    .duration      = 1.0f,
    .emission_rate = 30.0f
};

#ifdef __cplusplus
}
#endif

#endif /* PARTICLE_EMITTER_DEFINITION_H */