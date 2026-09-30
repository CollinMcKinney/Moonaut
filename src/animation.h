#ifndef ANIMATION_EVAL_H
#define ANIMATION_EVAL_H

/* --------------------------------------------------------------------------
 * Animation playback and skin evaluation.
 *
 * Data loading lives in the glTF importer; this file is the runtime half:
 * sampling a clip at a point in time, running forward kinematics over the
 * joint hierarchy, and producing the matrix palette the vertex shader blends.
 *
 * This follows what a shipped engine does: the CPU evaluates one pose per
 * animated entity per frame and the GPU does the per-vertex blending. The
 * palette for an entity is written into one contiguous run of the shared
 * joint buffer and every draw call for that entity shares a base slot, so
 * there is no per-draw binding and no per-vertex CPU work.
 * -------------------------------------------------------------------------- */

#include "common.h"
#include "tags/animation.h"
#include "tags/model.h"
#include "tags/entity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANIM_MAX_COMPONENTS 4

/* Scratch buffers for pose evaluation, grown on demand and reused. These are
 * static so the per-frame path never allocates. One scratch is enough: pose
 * evaluation completes before the palette is handed to the GPU. */
typedef struct animation_eval_scratch {
    mat4 *local;                 /* per joint: rest or posed local transform */
    mat4 *global;                /* per joint: posed world transform */
    mat4 *palette;               /* per joint: world * inverse bind */
    real *translation;           /* 3 per joint */
    real *rotation;              /* 4 per joint */
    real *scale;                 /* 3 per joint */
    u8   *mask;                  /* bit 0 T, bit 1 R, bit 2 S overridden */
    int  *node_to_joint;         /* glTF node index -> joint index, -1 if none */
    int   joint_cap;
    int   node_cap;
} animation_eval_scratch;

static animation_eval_scratch g_anim_scratch = { 0 };

static void animation_eval_scratch_release(void) {
    animation_eval_scratch *s = &g_anim_scratch;
    if (s->local)           free(s->local);
    if (s->global)          free(s->global);
    if (s->palette)         free(s->palette);
    if (s->translation)     free(s->translation);
    if (s->rotation)        free(s->rotation);
    if (s->scale)           free(s->scale);
    if (s->mask)            free(s->mask);
    if (s->node_to_joint)   free(s->node_to_joint);
    memset(s, 0, sizeof(*s));
}

/* Ensure the scratch can hold `joint_count` joints and a node->joint map for
 * `node_count` nodes. Returns 0 on allocation failure. */
static int animation_eval_scratch_reserve(u32 joint_count, u32 node_count) {
    animation_eval_scratch *s = &g_anim_scratch;
    u32 i;

    if (node_count < 1u) node_count = 1u;
    if ((u32)s->joint_cap < joint_count || (u32)s->node_cap < (int)node_count) {
        animation_eval_scratch_release();
        s->local        = (mat4*)calloc(joint_count, sizeof(mat4));
        s->global       = (mat4*)calloc(joint_count, sizeof(mat4));
        s->palette      = (mat4*)calloc(joint_count, sizeof(mat4));
        s->translation  = (real*)calloc((size_t)joint_count * 3u, sizeof(real));
        s->rotation     = (real*)calloc((size_t)joint_count * 4u, sizeof(real));
        s->scale        = (real*)calloc((size_t)joint_count * 3u, sizeof(real));
        s->mask         = (u8*)calloc(joint_count, sizeof(u8));
        s->node_to_joint = (int*)malloc((size_t)node_count * sizeof(int));
        if (!s->local || !s->global || !s->palette || !s->translation ||
            !s->rotation || !s->scale || !s->mask || !s->node_to_joint) {
            animation_eval_scratch_release();
            return 0;
        }
        s->joint_cap = (int)joint_count;
        s->node_cap  = (int)node_count;
    }
    for (i = 0; i < node_count; ++i) s->node_to_joint[i] = -1;
    return 1;
}

/* Sample one sampler at `time` seconds into `out`, which has room for
 * `components` floats.
 *
 * LINEAR and STEP are read directly. CUBICSPLINE stores in-tangent, value and
 * out-tangent per key, so the per-key stride is three times the component
 * count and the value lives in the middle of each triple.
 *
 * `quaternion` selects spherical interpolation for the 4-component rotation
 * case: componentwise lerp of two unit quaternions does not stay on the unit
 * sphere, so it drifts off the rotation manifold and skews geometry. */
static void animation_sample_sampler(const animation_sampler *s, real time,
                                     real *out, int components, int quaternion) {
    const real *times;
    const real *values;
    u32 key_count, i;
    int stride, cubic;
    int c;

    for (c = 0; c < components; ++c) out[c] = 0.0f;
    if (!s || !s->input.address || !s->output.address || s->input.count == 0u)
        return;
    if (components > ANIM_MAX_COMPONENTS) return;

    times  = (const real*)s->input.address;
    values = (const real*)s->output.address;
    key_count = s->input.count;

    cubic  = (s->interpolation == ANIMATION_INTERPOLATION_CUBICSPLINE);
    stride = components * (cubic ? 3 : 1);

    /* Outside the key range, hold the nearest end value. */
    if (time <= times[0]) {
        const real *v = cubic ? (values + stride) : values;
        for (c = 0; c < components; ++c) out[c] = v[c];
        return;
    }
    if (time >= times[key_count - 1u]) {
        const real *v = cubic ? (values + (size_t)(key_count - 1u) * 3u * (u32)stride + stride)
                              : (values + (size_t)(key_count - 1u) * (u32)stride);
        for (c = 0; c < components; ++c) out[c] = v[c];
        return;
    }

    /* Locate the key interval containing `time`. Clips here have few keys, so
     * a linear scan is competitive with a binary search and branch-free. */
    for (i = 0; i + 1u < key_count; ++i)
        if (time < times[i + 1u]) break;

    if (s->interpolation == ANIMATION_INTERPOLATION_STEP) {
        const real *v = cubic ? (values + (size_t)i * 3u * (u32)stride + stride)
                              : (values + (size_t)i * (u32)stride);
        for (c = 0; c < components; ++c) out[c] = v[c];
        return;
    }

    {
        real t0 = times[i], t1 = times[i + 1u];
        real span = t1 - t0;
        real u = (span > 0.0f) ? (time - t0) / span : 0.0f;

        if (cubic) {
            const real *b0 = values + (size_t)i * 3u * (u32)stride;
            const real *b1 = values + (size_t)(i + 1u) * 3u * (u32)stride;
            real u2 = u * u;
            real u3 = u2 * u;
            real h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            real h10 = u3 - 2.0f * u2 + u;
            real h01 = -2.0f * u3 + 3.0f * u2;
            real h11 = u3 - u2;
            for (c = 0; c < components; ++c) {
                out[c] = h00 * b0[stride + c]                       /* value at key i   */
                       + h10 * span * b0[2 * stride + c]             /* its out-tangent   */
                       + h01 * b1[stride + c]                       /* value at key i+1 */
                       + h11 * span * b1[c];                        /* its in-tangent    */
            }
        } else if (quaternion && components == 4) {
            const real *v0 = values + (size_t)i * (u32)stride;
            const real *v1 = values + (size_t)(i + 1u) * (u32)stride;
            real b0[4], b1[4];
            real dot = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2] + v0[3] * v1[3];
            for (c = 0; c < 4; ++c) { b0[c] = v0[c]; b1[c] = v1[c]; }
            if (dot < 0.0f) {   /* take the short way around the sphere */
                for (c = 0; c < 4; ++c) b1[c] = -b1[c];
                dot = -dot;
            }
            if (dot > 0.9995f) {
                /* Nearly parallel: lerp then renormalise is stable and
                 * indistinguishable from slerp at this angle. */
                for (c = 0; c < 4; ++c) out[c] = b0[c] + (b1[c] - b0[c]) * u;
            } else {
                double d = (double)(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
                double theta = acos(d);
                double s0 = sin((1.0 - (double)u) * theta) / sin(theta);
                double s1 = sin((double)u * theta) / sin(theta);
                for (c = 0; c < 4; ++c) out[c] = (real)((double)b0[c] * s0 + (double)b1[c] * s1);
            }
            /* Renormalise to absorb accumulated error from the key data. */
            {
                double len = sqrt((double)out[0]*out[0] + (double)out[1]*out[1] +
                                  (double)out[2]*out[2] + (double)out[3]*out[3]);
                if (len > 1e-9) { out[0] = (real)(out[0]/len); out[1] = (real)(out[1]/len);
                                  out[2] = (real)(out[2]/len); out[3] = (real)(out[3]/len); }
                else { out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f; }
            }
        } else {
            const real *v0 = values + (size_t)i * (u32)stride;
            const real *v1 = values + (size_t)(i + 1u) * (u32)stride;
            for (c = 0; c < components; ++c)
                out[c] = v0[c] + (v1[c] - v0[c]) * u;
        }
    }
}

/* Compose translation * rotation * scale into a row-major, column-vector mat4.
 *
 * This deliberately does not use quat_to_mat4: that helper writes the rotation
 * transposed (a +90 degree turn about Z maps +x to -y), which is the wrong
 * handedness for glTF. The formulas below match the importer's TRS builder
 * exactly, so a joint's rest pose reproduces its bind transform bit for bit and
 * an unanimated model is untouched. */
static mat4 animation_trs_to_mat4(vec3 t, vec4 q, vec3 s) {
    mat4 m;
    real x = q.position.x, y = q.position.y, z = q.position.z, w = q.position.w;
    real len, xx, yy, zz, xy, xz, yz, wx, wy, wz;
    real sx = s.position.x, sy = s.position.y, sz = s.position.z;

    len = (real)sqrt((double)(x*x + y*y + z*z + w*w));
    if (len > 1e-6f) { x /= len; y /= len; z /= len; w /= len; }
    else { x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f; }

    xx = x*x; yy = y*y; zz = z*z;
    xy = x*y; xz = x*z; yz = y*z;
    wx = w*x; wy = w*y; wz = w*z;

    m.data[0]  = (1.0f - 2.0f * (yy + zz)) * sx;
    m.data[1]  = (2.0f * (xy - wz)) * sy;
    m.data[2]  = (2.0f * (xz + wy)) * sz;
    m.data[3]  = t.position.x;

    m.data[4]  = (2.0f * (xy + wz)) * sx;
    m.data[5]  = (1.0f - 2.0f * (xx + zz)) * sy;
    m.data[6]  = (2.0f * (yz - wx)) * sz;
    m.data[7]  = t.position.y;

    m.data[8]  = (2.0f * (xz - wy)) * sx;
    m.data[9]  = (2.0f * (yz + wx)) * sy;
    m.data[10] = (1.0f - 2.0f * (xx + yy)) * sz;
    m.data[11] = t.position.z;

    m.data[12] = 0.0f; m.data[13] = 0.0f; m.data[14] = 0.0f; m.data[15] = 1.0f;
    return m;
}

/* Evaluate `clip` at `time` into the scratch palette.
 *
 * Returns the joint count, or 0 when there is nothing to evaluate. The
 * resulting palette is in s->palette and is valid until the next call. */
static u32 animation_eval_pose(const model_definition *model,
                               const animation_clip *clip,
                               real time) {
    animation_eval_scratch *s = &g_anim_scratch;
    const model_joint *joints;
    const animation_sampler *samplers;
    const animation_channel *channels;
    u32 joint_count, sampler_count, channel_count, i;

    if (!model || !clip || model->skeleton.count == 0u) return 0u;
    joints = (const model_joint*)model->skeleton.address;
    joint_count = model->skeleton.count;
    if (!joints) return 0u;

    if (!animation_eval_scratch_reserve(joint_count, model->node_count)) return 0u;
    for (i = 0; i < joint_count; ++i)
        s->node_to_joint[joints[i].node_index] = (int)i;

    samplers = (const animation_sampler*)clip->samplers.address;
    channels = (const animation_channel*)clip->channels.address;
    sampler_count = clip->samplers.count;
    channel_count = clip->channels.count;

    for (i = 0; i < joint_count; ++i) s->mask[i] = 0u;

    for (i = 0; i < channel_count; ++i) {
        const animation_channel *ch = &channels[i];
        const animation_sampler *sm;
        int joint, comps;

        if (ch->path == ANIMATION_PATH_WEIGHTS) continue;  /* morph, not skeletal */
        if (ch->sampler < 0 || (u32)ch->sampler >= sampler_count) continue;
        if (ch->target_node < 0 || ch->target_node >= s->node_cap) continue;

        joint = s->node_to_joint[ch->target_node];
        if (joint < 0) continue;   /* animates a node that is not a joint */

        sm = &samplers[ch->sampler];
        switch (ch->path) {
            case ANIMATION_PATH_TRANSLATION:
                animation_sample_sampler(sm, time, s->translation + (size_t)joint * 3, 3, 0);
                s->mask[joint] |= 1u;
                break;
            case ANIMATION_PATH_ROTATION:
                animation_sample_sampler(sm, time, s->rotation + (size_t)joint * 4, 4, 1);
                s->mask[joint] |= 2u;
                break;
            case ANIMATION_PATH_SCALE:
                animation_sample_sampler(sm, time, s->scale + (size_t)joint * 3, 3, 0);
                s->mask[joint] |= 4u;
                break;
            default:
                break;
        }
    }

    /* Compose each joint's local transform. Parts the clip does not animate
     * fall back to the rest pose stored at import time. */
    for (i = 0; i < joint_count; ++i) {
        const model_joint *j = &joints[i];
        vec3 t = j->rest_translation;
        vec3 sc = j->rest_scale;
        vec4 q = j->rest_rotation;
        mat4 m;

        if (s->mask[i] & 1u) {
            t.position.x = s->translation[(size_t)i * 3 + 0];
            t.position.y = s->translation[(size_t)i * 3 + 1];
            t.position.z = s->translation[(size_t)i * 3 + 2];
        }
        if (s->mask[i] & 4u) {
            sc.position.x = s->scale[(size_t)i * 3 + 0];
            sc.position.y = s->scale[(size_t)i * 3 + 1];
            sc.position.z = s->scale[(size_t)i * 3 + 2];
        }
        if (s->mask[i] & 2u) {
            q.position.x = s->rotation[(size_t)i * 4 + 0];
            q.position.y = s->rotation[(size_t)i * 4 + 1];
            q.position.z = s->rotation[(size_t)i * 4 + 2];
            q.position.w = s->rotation[(size_t)i * 4 + 3];
        }

        m = animation_trs_to_mat4(t, q, sc);
        s->local[i] = m;
    }

    /* Forward kinematics. The importer emits joints parents-first, so a
     * parent's world matrix is final by the time its children are reached.
     * Root joints are composed onto the transform in force above the skeleton
     * (bind_root_world), which is what inverseBindMatrix was built against. */
    for (i = 0; i < joint_count; ++i) {
        i32 parent = joints[i].parent;
        if (parent >= 0 && (u32)parent < joint_count && (u32)parent < i)
            s->global[i] = mat4_mul(s->global[parent], s->local[i]);
        else
            s->global[i] = mat4_mul(joints[i].bind_root_world, s->local[i]);
    }

    /* Palette matrix = posed world transform * inverse bind matrix. This maps
     * a vertex from model space into the posed joint space the shader wants. */
    for (i = 0; i < joint_count; ++i)
        s->palette[i] = mat4_mul(s->global[i], joints[i].inv_bind_matrix);

    return joint_count;
}

/* --------------------------------------------------------------------------
 * Playback state
 *
 * Playback is runtime state, not tag data: an entity's 'anim' reference says
 * *which* clips it can play, while the current clip, time and speed live in
 * this table keyed by entity handle. Keeping them here means they are not
 * serialised into the .enty file and can be changed per frame.
 * -------------------------------------------------------------------------- */
typedef struct animation_player {
    i32   entity;        /* entity tag handle this player drives, -1 if free */
    i32   clip;          /* index into the animation tag's clips */
    real  time;          /* playhead in seconds */
    real  speed;         /* playback rate multiplier; negative reverses */
    u8    playing;
    u8    looping;
} animation_player;

#define MAX_ANIM_PLAYERS 64
static animation_player g_anim_players[MAX_ANIM_PLAYERS] = { 0 };

static void animation_players_init(void) {
    int i;
    for (i = 0; i < MAX_ANIM_PLAYERS; ++i) {
        g_anim_players[i].entity = -1;
        g_anim_players[i].clip = 0;
        g_anim_players[i].time = 0.0f;
        g_anim_players[i].speed = 1.0f;
        g_anim_players[i].playing = 1;
        g_anim_players[i].looping = 1;
    }
}

static animation_player* animation_player_for(i32 entity) {
    int i;
    animation_player *free_slot = NULL;
    for (i = 0; i < MAX_ANIM_PLAYERS; ++i) {
        if (g_anim_players[i].entity == entity) return &g_anim_players[i];
        if (g_anim_players[i].entity < 0 && !free_slot) free_slot = &g_anim_players[i];
    }
    return free_slot;
}

/* Length of a clip in seconds, taken from the last keyframe that actually
 * drives the skeleton. Returns 0 for an empty or unreadable clip.
 *
 * glTF defines a clip's duration as the largest keyframe time across all of
 * its samplers, and that is what a full implementation wants. It is the wrong
 * number here, because morph weights are not played back yet: station.glb's
 * weight channels run to 12.98s while its translation/rotation/scale channels
 * stop at 10.82s. Using the all-sampler maximum leaves the skeleton holding its
 * final pose, frozen, for the last 2.16s of every loop before snapping back.
 * Only channels that feed the skeleton count until morph playback lands.
 *
 * Walking sampler[0] alone -- as this did before -- is not a safe shortcut
 * either: samplers are free to cover different spans, and there is nothing
 * about index 0 that makes it the longest. */
static real animation_clip_duration(const animation_clip *clip) {
    const animation_sampler *s;
    const animation_channel *ch;
    real best = 0.0f;
    u32 i, n;
    if (!clip || !clip->samplers.address || clip->samplers.count == 0u) return 0.0f;
    if (!clip->channels.address || clip->channels.count == 0u) return 0.0f;
    s = (const animation_sampler*)clip->samplers.address;
    ch = (const animation_channel*)clip->channels.address;
    n = clip->channels.count;
    for (i = 0; i < n; ++i) {
        const real *times;
        real last;
        if (ch[i].path == ANIMATION_PATH_WEIGHTS) continue;   /* morph: not played back */
        if (ch[i].sampler < 0 || (u32)ch[i].sampler >= clip->samplers.count) continue;
        if (!s[ch[i].sampler].input.address || s[ch[i].sampler].input.count == 0u) continue;
        times = (const real*)s[ch[i].sampler].input.address;
        last = times[s[ch[i].sampler].input.count - 1u];
        if (last > best) best = last;
    }
    return best;
}

/* Advance a player's playhead and wrap or clamp at the end of the clip. */
static void animation_player_advance(animation_player *p, const animation_clip *clip, real dt) {
    real duration;
    if (!p || !p->playing || p->speed == 0.0f) return;
    duration = animation_clip_duration(clip);
    p->time += dt * p->speed;
    if (duration <= 0.0f) return;
    if (p->looping) {
        /* fmod keeps the playhead inside [0, duration) for both directions,
         * so a long-running loop does not accumulate drift. */
        real m = (real)fmod((double)p->time, (double)duration);
        if (m < 0.0f) m += duration;
        p->time = m;
    } else if (p->time >= duration) {
        p->time = duration;
        p->playing = 0;
    } else if (p->time <= 0.0f) {
        p->time = 0.0f;
        p->playing = 0;
    }
}

/* Advance every bound player's playhead. Called once per fixed step from the
 * main loop, before the scene is submitted for drawing. */
static void animation_players_update(real dt) {
    int i;
    for (i = 0; i < MAX_ANIM_PLAYERS; ++i) {
        animation_player *p = &g_anim_players[i];
        if (p->entity < 0 || !p->playing) continue;
        {
            const entity_definition *ent =
                (const entity_definition*)tag_get(p->entity, TAG_entity);
            const animation_definition *adef =
                (ent && ent->animation.handle >= 0)
                ? (const animation_definition*)tag_get(ent->animation.handle, TAG_animation)
                : NULL;
            const animation_clip *clips;
            if (!adef || !adef->clips.address || adef->clips.count == 0u) continue;
            if (p->clip < 0 || (u32)p->clip >= adef->clips.count) p->clip = 0;
            clips = (const animation_clip*)adef->clips.address;
            animation_player_advance(p, &clips[p->clip], dt);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_EVAL_H */
