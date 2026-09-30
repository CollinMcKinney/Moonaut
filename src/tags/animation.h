#ifndef ANIMATION_DEFINITION_H
#define ANIMATION_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * glTF 2.0 animation model
 *
 * A clip ('animation') owns a set of samplers (the recorded keyframe data) and
 * a set of channels. Each channel binds one sampler to one target *node* on a
 * given property path. Because the target is a generic node index, the same
 * structure covers every glTF 2.0 animation type:
 *
 *   - translation / rotation / scale  -> joints (skeletal) or any node
 *   - weights                          -> morph-target / blend-shape weights
 *
 * The link from a channel to a skeleton joint (when the target node is a
 * joint) is resolved at runtime by matching the node index against
 * model_joint.node_index.
 * ------------------------------------------------------------------------ */

typedef enum animation_path {
    ANIMATION_PATH_TRANSLATION = 0,   /* TRS: node position (vec3) */
    ANIMATION_PATH_ROTATION,          /* TRS: node orientation (vec4 quaternion) */
    ANIMATION_PATH_SCALE,             /* TRS: node scale (vec3) */
    ANIMATION_PATH_WEIGHTS            /* morph-target weights (N floats per key) */
} animation_path;

TAG_ENUM_BEGIN(animation_path)
    TAG_ENUM_ENTRY(ANIMATION_PATH_TRANSLATION, "translation")
    TAG_ENUM_ENTRY(ANIMATION_PATH_ROTATION,    "rotation")
    TAG_ENUM_ENTRY(ANIMATION_PATH_SCALE,       "scale")
    TAG_ENUM_ENTRY(ANIMATION_PATH_WEIGHTS,     "weights")
TAG_ENUM_END(animation_path)

typedef enum animation_interpolation {
    ANIMATION_INTERPOLATION_LINEAR = 0,
    ANIMATION_INTERPOLATION_STEP,
    ANIMATION_INTERPOLATION_CUBICSPLINE
} animation_interpolation;

TAG_ENUM_BEGIN(animation_interpolation)
    TAG_ENUM_ENTRY(ANIMATION_INTERPOLATION_LINEAR,      "linear")
    TAG_ENUM_ENTRY(ANIMATION_INTERPOLATION_STEP,        "step")
    TAG_ENUM_ENTRY(ANIMATION_INTERPOLATION_CUBICSPLINE, "cubicspline")
TAG_ENUM_END(animation_interpolation)

/* Helper block: raw float keyframe data (times and values). */
TAG_BLOCK_BEGIN(animation_real_block, 1048576, sizeof(real))
    FIELD_REAL("value"),
    FIELD_TERMINATOR
TAG_BLOCK_END(animation_real_block, 1048576, sizeof(real))

/* --------------------------------------------------------------------------
 * Sampler – the recorded keyframe data for one animated property.
 *
 * input  : strictly increasing keyframe times, in seconds.
 * output : values, packed per the target path's component count:
 *            translation / scale -> 3 floats per key
 *            rotation             -> 4 floats per key (quaternion)
 *            weights              -> component_count floats per key
 *          For CUBICSPLINE the output additionally carries an in-tangent and
 *          an out-tangent value around each key (3 * components per key); the
 *          runtime sampler owns that interpretation.
 * ------------------------------------------------------------------------ */
typedef struct animation_sampler {
    struct tag_block input;          /* block of real: keyframe times (seconds) */
    struct tag_block output;         /* block of real: keyframe values */
    u32             component_count; /* floats per key: 3 / 4 / N (weights) */
    enum32          interpolation;   /* animation_interpolation */
} animation_sampler;

TAG_BLOCK_BEGIN(animation_sampler_block, 4096, sizeof(animation_sampler))
    FIELD_BLOCK("input", animation_real_block),
    FIELD_BLOCK("output", animation_real_block),
    FIELD_U32("component_count"),
    FIELD_ENUM("interpolation", animation_interpolation),
    FIELD_TERMINATOR
TAG_BLOCK_END(animation_sampler_block, 4096, sizeof(animation_sampler))

/* --------------------------------------------------------------------------
 * Channel – binds a sampler to a target node + property path.
 * ------------------------------------------------------------------------ */
typedef struct animation_channel {
    i32     target_node;   /* glTF node index being animated */
    i32     sampler;       /* index into the clip's samplers block */
    enum32  path;          /* animation_path */
} animation_channel;

TAG_BLOCK_BEGIN(animation_channel_block, 4096, sizeof(animation_channel))
    FIELD_I32("target_node"),
    FIELD_I32("sampler"),
    FIELD_ENUM("path", animation_path),
    FIELD_TERMINATOR
TAG_BLOCK_END(animation_channel_block, 4096, sizeof(animation_channel))

/* --------------------------------------------------------------------------
 * Animation clip – one glTF animation: a set of samplers (the recorded
 * keyframe data) and a set of channels. Each channel binds one sampler to one
 * target *node* on a given property path. Because the target is a generic node
 * index, the same structure covers every glTF 2.0 animation type:
 *
 *   - translation / rotation / scale  -> joints (skeletal) or any node
 *   - weights                          -> morph-target / blend-shape weights
 *
 * The link from a channel to a skeleton joint (when the target node is a
 * joint) is resolved at runtime by matching the node index against
 * model_joint.node_index of the entity's model.
 * ------------------------------------------------------------------------ */
typedef struct animation_clip {
    string_id        name;      /* interned clip name (may be null) */
    struct tag_block samplers;  /* block of animation_sampler */
    struct tag_block channels;  /* block of animation_channel */
} animation_clip;

TAG_BLOCK_BEGIN(animation_clip_block, 256, sizeof(animation_clip))
    FIELD_STRING_ID("name"),
    FIELD_BLOCK("samplers", animation_sampler_block),
    FIELD_BLOCK("channels", animation_channel_block),
    FIELD_TERMINATOR
TAG_BLOCK_END(animation_clip_block, 256, sizeof(animation_clip))

/* --------------------------------------------------------------------------
 * Animation – one tag instance holding every clip loaded for a model. An
 * entity references this tag alongside its model tag.
 * ------------------------------------------------------------------------ */
typedef struct animation_definition {
    struct tag_block clips;    /* block of animation_clip */
} animation_definition;

TAG_GROUP_BEGIN(animation, TAG_MAGIC_PACK(anim), sizeof(animation_definition))
    FIELD_BLOCK("clips", animation_clip_block),
    FIELD_TERMINATOR
TAG_GROUP_END(animation, sizeof(animation_definition))

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_DEFINITION_H */
