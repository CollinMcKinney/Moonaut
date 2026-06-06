#ifndef SCENARIO_DEFINITION_H
#define SCENARIO_DEFINITION_H

#include "../reflection.h"
#include "../common.h"
#include "globals.h"
#include "camera.h"
#include "entity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scenario_definition {
    struct tag_reference globals;           /* link to 'glbl' */
    struct tag_reference camera;            /* link to 'cmra' */
    struct tag_reference map_collision_bsp; /* cbsp – optional static world collision */
    struct tag_block entities;              /* array of tag_reference to entity_definition */
} scenario_definition;

TAG_REFERENCE(scenario_globals_ref, TAG_globals)
TAG_REFERENCE(scenario_camera_ref, TAG_camera)
TAG_REFERENCE(map_collision_bsp_ref, TAG_collision_bsp)

TAG_REFERENCE(scenario_entity_ref, TAG_entity)   /* a single entity reference */

TAG_BLOCK_BEGIN(scenario_entity_block, 65535, sizeof(tag_reference))
    FIELD_REFERENCE("entity", scenario_entity_ref),   /* each element is a reference to an entity tag */
    FIELD_TERMINATOR
TAG_BLOCK_END(scenario_entity_block, 65535, sizeof(tag_reference))

TAG_GROUP_BEGIN(scenario, TAG_MAGIC_PACK(scnr), sizeof(struct scenario_definition))
    FIELD_REFERENCE("globals", scenario_globals_ref),
    FIELD_REFERENCE("camera", scenario_camera_ref),
    FIELD_REFERENCE("map_collision_bsp", map_collision_bsp_ref),
    FIELD_BLOCK("entities", scenario_entity_block),
    FIELD_TERMINATOR
TAG_GROUP_END(scenario, sizeof(struct scenario_definition))

/* TODO: figure out how to make this const and continue working. */
/* Two entity references: one to "default_entity_sphere", one to "default_entity_box" */
static tag_reference DEFAULT_SCENARIO_ENTITY_REFS[2] = {
    { -1 },   /* handle patched later to point to default_entity_sphere */
    { -1 }    /* handle patched later to point to default_entity_box */
};

/* Default scenario asset.
   - globals/camera references are patched later (defaults init / registration) */
static const struct scenario_definition DEFAULT_SCENARIO = {
    /* globals */        { (i32)-1 }, /* patched by defaults registration */
    /* camera  */        { (i32)-1 }, /* patched by defaults registration */
    /* collision_bsp */ { (i32)-1 },
    /* entities */      { 2u, (void*)DEFAULT_SCENARIO_ENTITY_REFS }
};

#ifdef __cplusplus
}
#endif
#endif /* SCENARIO_DEFINITION_H */
