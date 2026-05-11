/*
 * physics.h – Rigid‑body physics using entity_definition
 *
 * All shape / mass properties are read once from the entity's
 * rigid_body_definition tag when the body is registered.
 * Mutable runtime state (velocity, force, inertia, etc.) is kept
 * in an internal array.  Entity position / orientation are updated
 * directly.
 */
#ifndef PHYSICS_H
#define PHYSICS_H

#include "common.h"
#include "tags/entity.h"
#include "tags/rigid_body.h"

#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PHYSICS_MAX_BODIES
#define PHYSICS_MAX_BODIES  128
#endif
#ifndef PHYSICS_MAX_CONTACTS
#define PHYSICS_MAX_CONTACTS 512
#endif
#ifndef PHYSICS_SOLVER_ITERATIONS
#define PHYSICS_SOLVER_ITERATIONS 10
#endif

/* ------------------------------------------------------------------------
   Internal mutable state for one physics body
   ------------------------------------------------------------------------ */
typedef struct {
    i32   entity_index;               /* which entity in the world */
    rigid_body_definition *rigid_body; /* pointer to the tag data */
    vec3  force;
    vec3  torque;
    real  inverse_mass;
    mat3  inertia_tensor;             /* local space */
    mat3  inverse_inertia_tensor;
    i32   asleep;                     /* 1 if at rest */
} physics_body; /* Now uses pointer to rigid_body tag for shared state */

/* ------------------------------------------------------------------------
   Contact between two bodies
   ------------------------------------------------------------------------ */
typedef struct {
    vec3  point;
    vec3  normal;
    real  penetration;
    i32   body_a;              /* indices into physics body array */
    i32   body_b;
} contact;

/* ------------------------------------------------------------------------
   Physics world
   ------------------------------------------------------------------------ */
typedef struct {
    entity_definition **entities;         /* pointer to array of entity pointers */
    i32                max_entities;      /* size of that array */
    physics_body       bodies[PHYSICS_MAX_BODIES];
    i32                body_count;
    i32                entity_to_body[PHYSICS_MAX_BODIES];  /* -1 if not dynamic */
    contact            contacts[PHYSICS_MAX_CONTACTS];
    i32                contact_count;
    vec3               gravity;
} physics_world;

/* ====================================================================
   Internal helpers – access entity fields via body index
   ==================================================================== */
static vec3  phys_ent_pos(const physics_world *w, i32 b) {
    return w->entities[w->bodies[b].entity_index]->position;
}
static void phys_set_pos(physics_world *w, i32 b, vec3 v) {
    w->entities[w->bodies[b].entity_index]->position = v;
}
static vec4 phys_ent_orient(const physics_world *w, i32 b) {
    return w->entities[w->bodies[b].entity_index]->orientation;
}
static void phys_set_orient(physics_world *w, i32 b, vec4 q) {
    w->entities[w->bodies[b].entity_index]->orientation = q;
}

/* ====================================================================
   Inertia helpers (unchanged math)
   ==================================================================== */
static mat3 world_inv_inertia_phys(const physics_body *b, vec4 orient) {
    mat3 r   = mat3_from_quat(orient);
    mat3 Rt  = mat3_transpose(r);
    mat3 tmp = mat3_mul(r, b->inverse_inertia_tensor);
    return mat3_mul(tmp, Rt);
}

static mat3 inertia_sphere_phys(real mass, real radius) {
    real i = 0.4f * mass * radius * radius;
    mat3 m;
    m.data[0]=i; m.data[1]=0; m.data[2]=0;
    m.data[3]=0; m.data[4]=i; m.data[5]=0;
    m.data[6]=0; m.data[7]=0; m.data[8]=i;
    return m;
}

static mat3 inertia_box_phys(real mass, vec3 he) {
    real x = he.position.x * 2.0f;
    real y = he.position.y * 2.0f;
    real z = he.position.z * 2.0f;
    real factor = mass / 12.0f;
    mat3 m;
    m.data[0]=factor*(y*y+z*z); m.data[1]=0; m.data[2]=0;
    m.data[3]=0; m.data[4]=factor*(x*x+z*z); m.data[5]=0;
    m.data[6]=0; m.data[7]=0; m.data[8]=factor*(x*x+y*y);
    return m;
}

/* ====================================================================
   World management
   ==================================================================== */
static void physics_init(physics_world *w, entity_definition **entities,
                         i32 max_entities, vec3 gravity) {
    w->entities     = entities;
    w->max_entities = max_entities;
    w->body_count   = 0;
    w->gravity      = gravity;
    {
        i32 i;
        for (i = 0; i < max_entities; ++i) w->entity_to_body[i] = -1;
    }
}

/* Add an entity to the physics world.  The entity must have a valid
   rigid_body reference.  Returns body handle or -1. */
static i32 physics_add_entity(physics_world *w, i32 entity_index) {
    if (entity_index < 0 || entity_index >= w->max_entities) return -1;
    if (w->entity_to_body[entity_index] >= 0) return w->entity_to_body[entity_index];
    if (w->body_count >= PHYSICS_MAX_BODIES) return -1;

    entity_definition *ent = w->entities[entity_index];
    if (ent->rigid_body.handle < 0) return -1;   /* no physics data */

    rigid_body_definition *tag =
        (rigid_body_definition*)tag_get(ent->rigid_body.handle, TAG_rigid_body);
    if (!tag) return -1;

    i32 idx = w->body_count++;
    physics_body *b = &w->bodies[idx];
    memset(b, 0, sizeof(*b));

    b->entity_index = entity_index;
    b->rigid_body   = tag;
    b->asleep       = 0;

    real mass = tag->mass;
    if (ent->type == ENTITY_STATIC) mass = 0.0f;

    if (mass > 0.0f) {
        b->inverse_mass = 1.0f / mass;
        if (tag->shape == RIGID_SHAPE_SPHERE)
            b->inertia_tensor = inertia_sphere_phys(mass, tag->sphere_radius);
        else
            b->inertia_tensor = inertia_box_phys(mass, tag->box_half_extents);
        b->inverse_inertia_tensor = mat3_inverse_diagonal(b->inertia_tensor);
    } else {
        b->inverse_mass = 0.0f;
        memset(&b->inertia_tensor, 0, sizeof(b->inertia_tensor));
        memset(&b->inverse_inertia_tensor, 0, sizeof(b->inverse_inertia_tensor));
    }

    w->entity_to_body[entity_index] = idx;
    return idx;
}

/* Remove an entity from the simulation. */
static void physics_remove_entity(physics_world *w, i32 entity_index) {
    i32 idx = w->entity_to_body[entity_index];
    if (idx < 0) return;

    /* swap‑remove */
    i32 last = w->body_count - 1;
    if (idx != last) {
        w->bodies[idx] = w->bodies[last];
        w->entity_to_body[w->bodies[idx].entity_index] = idx;
    }
    w->entity_to_body[entity_index] = -1;
    w->body_count--;
}

/* ====================================================================
   Collision detection  (adapted to use physics_world + body indices)
   ==================================================================== */
static i32 sphere_sphere_contact(physics_world *w, i32 ia, i32 ib, contact *c) {
    physics_body *a = &w->bodies[ia], *b = &w->bodies[ib];
    vec3 pa = phys_ent_pos(w, ia), pb = phys_ent_pos(w, ib);
    vec3 d = vec3_sub(pb, pa);
    real dist = vec3_distance(pa, pb);
    real sumr = a->rigid_body->sphere_radius + b->rigid_body->sphere_radius;
    if (dist >= sumr) return 0;
    if (dist < 1e-6f) {
        c->normal = vec3_init_from_3(0,1,0);
        c->penetration = sumr;
        c->point = pa;
    } else {
        c->normal = vec3_div_scalar(d, dist);
        c->penetration = sumr - dist;
        c->point = vec3_add(pa, vec3_mul_scalar(c->normal,
                            a->rigid_body->sphere_radius - c->penetration*0.5f));
    }
    c->body_a = ia; c->body_b = ib;
    return 1;
}

static vec3 closest_point_on_box(vec3 box_pos, vec4 orient, vec3 he, vec3 point) {
    vec4 inv = quat_conjugate(orient);
    vec3 local = quat_rotate_vec3(inv, vec3_sub(point, box_pos));
    vec3 clamped = vec3_clamp(local, vec3_negate(he), he);
    return vec3_add(box_pos, quat_rotate_vec3(orient, clamped));
}

static i32 sphere_box_contact(physics_world *w, i32 sphere_idx, i32 box_idx, contact *c) {
    physics_body *sphere = &w->bodies[sphere_idx];
    physics_body *box    = &w->bodies[box_idx];
    vec3 sp = phys_ent_pos(w, sphere_idx);
    vec3 bp = phys_ent_pos(w, box_idx);
    vec4 bo = phys_ent_orient(w, box_idx);
    vec3 closest = closest_point_on_box(bp, bo, box->rigid_body->box_half_extents, sp);
    real dist = vec3_distance(sp, closest);
    real r = sphere->rigid_body->sphere_radius;
    if (dist > r) return 0;
    if (dist < 1e-6f) {
        c->normal = vec3_init_from_3(0,1,0);
        c->penetration = r;
        c->point = sp;
    } else {
        c->normal = vec3_normalize(vec3_sub(closest, sp));
        c->penetration = r - dist;
        c->point = closest;
    }
    c->body_a = sphere_idx; c->body_b = box_idx;
    return 1;
}

/* ====================================================================
   Simulation step
   ==================================================================== */
static void physics_step(physics_world *w, real dt) {
    i32 i;

    /* apply forces & damping */
    for (i = 0; i < w->body_count; i++) {
        physics_body *b = &w->bodies[i];
        if (b->inverse_mass <= 0.0f) continue;
        b->force = vec3_add(b->force, vec3_mul_scalar(w->gravity, b->rigid_body->mass));
        b->rigid_body->velocity = vec3_mul_scalar(b->rigid_body->velocity,
                        (real)(1.0 - b->rigid_body->linear_damping*dt));
        b->rigid_body->angular_velocity = vec3_mul_scalar(b->rigid_body->angular_velocity,
                                (real)(1.0 - b->rigid_body->angular_damping*dt));

        /* Set to rest if below thresholds  */
        /*TODO: higher values may be needed to cover all cases.
        This works for the default sphere colliding with the default box in the default scenario
        with default globals. */
        /* real lin_thresh = 0.03770751953125f; */
        /* real ang_thresh = 0.03770751953125f; */
        real lin_thresh = 0.05;
        real ang_thresh = 0.05;
        if (vec3_magnitude(b->rigid_body->velocity) < lin_thresh &&
            vec3_magnitude(b->rigid_body->angular_velocity) < ang_thresh) {
            b->rigid_body->velocity = vec3_init_from_3(0,0,0);
            b->rigid_body->angular_velocity = vec3_init_from_3(0,0,0);
            b->asleep = 1;
        } else {
            b->asleep = 0;
        }
    }

    /* integrate */
    for (i = 0; i < w->body_count; i++) {
        physics_body *b = &w->bodies[i];
        if (b->inverse_mass <= 0.0f) continue;
        if (b->asleep) continue;
        vec3 accel = vec3_mul_scalar(b->force, b->inverse_mass);
        b->rigid_body->velocity = vec3_add(b->rigid_body->velocity, vec3_mul_scalar(accel, dt));
        vec3 pos = phys_ent_pos(w, i);
        phys_set_pos(w, i, vec3_add(pos, vec3_mul_scalar(b->rigid_body->velocity, dt)));

        vec4 orient = phys_ent_orient(w, i);
        mat3 invI = world_inv_inertia_phys(b, orient);
        vec3 ang_accel = mat3_mul_vec3(invI, b->torque);
        b->rigid_body->angular_velocity = vec3_add(b->rigid_body->angular_velocity,
                                vec3_mul_scalar(ang_accel, dt));

        vec4 wq = vec4_init_from_4(b->rigid_body->angular_velocity.position.x,
                                   b->rigid_body->angular_velocity.position.y,
                                   b->rigid_body->angular_velocity.position.z, 0);
        vec4 qdot = vec4_mul_scalar(quat_mul(wq, orient), 0.5f);
        orient = vec4_add(orient, vec4_mul_scalar(qdot, dt));
        phys_set_orient(w, i, vec4_normalize(orient));

        b->force  = vec3_init_from_3(0,0,0);
        b->torque = vec3_init_from_3(0,0,0);
    }

    /* broadphase O(n²) */
    w->contact_count = 0;
    for (i = 0; i < w->body_count; i++) {
        i32 j;
        for (j = i+1; j < w->body_count; j++) {
            physics_body *a = &w->bodies[i], *b = &w->bodies[j];
            if (a->inverse_mass <= 0.0f && b->inverse_mass <= 0.0f) continue;
            contact c;
            i32 hit = 0;
            if (a->rigid_body->shape == RIGID_SHAPE_SPHERE && b->rigid_body->shape == RIGID_SHAPE_SPHERE)
                hit = sphere_sphere_contact(w, i, j, &c);
            else if (a->rigid_body->shape == RIGID_SHAPE_SPHERE && b->rigid_body->shape == RIGID_SHAPE_BOX)
                hit = sphere_box_contact(w, i, j, &c);
            else if (a->rigid_body->shape == RIGID_SHAPE_BOX && b->rigid_body->shape == RIGID_SHAPE_SPHERE) {
                hit = sphere_box_contact(w, j, i, &c);
                if (hit) { i32 tmp = c.body_a; c.body_a = c.body_b; c.body_b = tmp; }
            }
            if (hit && w->contact_count < PHYSICS_MAX_CONTACTS)
                w->contacts[w->contact_count++] = c;
        }
    }

    /* solver */
    real inv_dt = dt > 1e-6f ? 1.0f/dt : 0.0f;
    i32 iter;
    for (iter = 0; iter < PHYSICS_SOLVER_ITERATIONS; iter++) {
        i32 ci;
        for (ci = 0; ci < w->contact_count; ci++) {
            contact *cn = &w->contacts[ci];
            physics_body *a = &w->bodies[cn->body_a];
            physics_body *b = &w->bodies[cn->body_b];
            if (a->inverse_mass <= 0.0f && b->inverse_mass <= 0.0f) continue;
            if (a->asleep && b->asleep) continue;
            /* wake bodies if asleep */
            a->asleep = 0;
            b->asleep = 0;

            vec3 pa = phys_ent_pos(w, cn->body_a);
            vec3 pb = phys_ent_pos(w, cn->body_b);
            vec4 oa = phys_ent_orient(w, cn->body_a);
            vec4 ob = phys_ent_orient(w, cn->body_b);
            vec3 ra = vec3_sub(cn->point, pa);
            vec3 rb = vec3_sub(cn->point, pb);
            vec3 vrel = vec3_add(
                vec3_sub(b->rigid_body->velocity, a->rigid_body->velocity),
                vec3_sub(vec3_cross(b->rigid_body->angular_velocity, rb),
                         vec3_cross(a->rigid_body->angular_velocity, ra)));
            real vn = vec3_dot(vrel, cn->normal);
            if (vn > 0.0f) continue;

            mat3 invIa = world_inv_inertia_phys(a, oa);
            mat3 invIb = world_inv_inertia_phys(b, ob);
            vec3 rcn_a = vec3_cross(ra, cn->normal);
            vec3 rcn_b = vec3_cross(rb, cn->normal);
            real denom = a->inverse_mass + b->inverse_mass +
                         vec3_dot(mat3_mul_vec3(invIa, rcn_a), rcn_a) +
                         vec3_dot(mat3_mul_vec3(invIb, rcn_b), rcn_b);
            if (denom < 1e-6f) continue;

            real e = (a->rigid_body->restitution + b->rigid_body->restitution) * 0.5f;
            real j = -(1.0f+e)*vn / denom;
            if (j < 0.0f) j = 0.0f;
            vec3 imp = vec3_mul_scalar(cn->normal, j);
            a->rigid_body->velocity = vec3_sub(a->rigid_body->velocity, vec3_mul_scalar(imp, a->inverse_mass));
            b->rigid_body->velocity = vec3_add(b->rigid_body->velocity, vec3_mul_scalar(imp, b->inverse_mass));
            a->rigid_body->angular_velocity = vec3_sub(a->rigid_body->angular_velocity,
                                     mat3_mul_vec3(invIa, vec3_cross(ra, imp)));
            b->rigid_body->angular_velocity = vec3_add(b->rigid_body->angular_velocity,
                                     mat3_mul_vec3(invIb, vec3_cross(rb, imp)));

            /* friction */
            vec3 tangent = vec3_sub(vrel, vec3_mul_scalar(cn->normal, vn));
            real tangent_len = vec3_magnitude(tangent);
            if (tangent_len > 1e-6f) {
                tangent = vec3_div_scalar(tangent, tangent_len);
                real jt = -vec3_dot(vrel, tangent) / denom;
                real mu = real_sqrt(a->rigid_body->friction * b->rigid_body->friction);
                real jtmax = mu * j;
                if (jt > jtmax) jt = jtmax;
                else if (jt < -jtmax) jt = -jtmax;
                vec3 imp_t = vec3_mul_scalar(tangent, jt);
                a->rigid_body->velocity = vec3_sub(a->rigid_body->velocity, vec3_mul_scalar(imp_t, a->inverse_mass));
                b->rigid_body->velocity = vec3_add(b->rigid_body->velocity, vec3_mul_scalar(imp_t, b->inverse_mass));
                a->rigid_body->angular_velocity = vec3_sub(a->rigid_body->angular_velocity,
                                         mat3_mul_vec3(invIa, vec3_cross(ra, imp_t)));
                b->rigid_body->angular_velocity = vec3_add(b->rigid_body->angular_velocity,
                                         mat3_mul_vec3(invIb, vec3_cross(rb, imp_t)));
            }
        }

        /* position correction */
        for (ci = 0; ci < w->contact_count; ci++) {
            contact *cn = &w->contacts[ci];
            physics_body *a = &w->bodies[cn->body_a];
            physics_body *b = &w->bodies[cn->body_b];
            if (a->inverse_mass <= 0.0f && b->inverse_mass <= 0.0f) continue;
            if (a->asleep && b->asleep) continue;
            /* wake bodies if asleep */
            a->asleep = 0;
            b->asleep = 0;
            real slop = 0.005f, percent = 0.4f;
            real pen = cn->penetration;
            if (pen <= slop) continue;
            vec3 pa = phys_ent_pos(w, cn->body_a);
            vec3 pb = phys_ent_pos(w, cn->body_b);
            vec4 oa = phys_ent_orient(w, cn->body_a);
            vec4 ob = phys_ent_orient(w, cn->body_b);
            vec3 ra = vec3_sub(cn->point, pa);
            vec3 rb = vec3_sub(cn->point, pb);
            mat3 invIa = world_inv_inertia_phys(a, oa);
            mat3 invIb = world_inv_inertia_phys(b, ob);
            vec3 rcn_a = vec3_cross(ra, cn->normal);
            vec3 rcn_b = vec3_cross(rb, cn->normal);
            real denom = a->inverse_mass + b->inverse_mass +
                         vec3_dot(mat3_mul_vec3(invIa, rcn_a), rcn_a) +
                         vec3_dot(mat3_mul_vec3(invIb, rcn_b), rcn_b);
            if (denom < 1e-6f) continue;
            real corr = (pen - slop) / denom * percent;
            if (corr > 1.0f) corr = 1.0f;
            vec3 corr_vec = vec3_mul_scalar(cn->normal, corr);
            phys_set_pos(w, cn->body_a, vec3_sub(pa, vec3_mul_scalar(corr_vec, a->inverse_mass)));
            phys_set_pos(w, cn->body_b, vec3_add(pb, vec3_mul_scalar(corr_vec, b->inverse_mass)));
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif /* PHYSICS_H */