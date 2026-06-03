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
#include "tags/collision_bsp.h"

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
    collision_bsp_definition *static_bsp;
} physics_world;

/* Entity collision BSP accessor */
static collision_bsp_definition *get_entity_collision_bsp(physics_world *w, i32 entity_index) {
    entity_definition *ent = w->entities[entity_index];
    if (!ent || ent->collision_bsp.handle < 0) return NULL;
    return (collision_bsp_definition*)tag_get(ent->collision_bsp.handle, TAG_collision_bsp);
}

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
    w->static_bsp   = NULL;
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
        c->point = vec3_add(pa, vec3_mul_scalar(c->normal, a->rigid_body->sphere_radius));
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
        c->point = vec3_add(sp, vec3_mul_scalar(c->normal, r));
    }
    c->body_a = sphere_idx; c->body_b = box_idx;
    return 1;
}

/* ------------------------------------------------------------------------
   BSP collision detection
   ------------------------------------------------------------------------ */
static real vec3_distance_squared(vec3 src0, vec3 src1)
{
    vec3 diff = vec3_sub(src0, src1);
    return vec3_dot(diff, diff);
}

static vec3 closest_point_on_segment(vec3 p, vec3 a, vec3 b)
{
    vec3 ap = vec3_sub(p, a);
    vec3 ab = vec3_sub(b, a);
    real ab2 = vec3_dot(ab, ab);
    if (ab2 < 1e-6f) return a;
    real t = vec3_dot(ap, ab) / ab2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return vec3_add(a, vec3_mul_scalar(ab, t));
}

static i32 point_in_triangle(vec3 p, vec3 a, vec3 b, vec3 c, vec3 normal)
{
    vec3 ab = vec3_sub(b, a);
    vec3 bc = vec3_sub(c, b);
    vec3 ca = vec3_sub(a, c);

    if (vec3_dot(normal, vec3_cross(ab, vec3_sub(p, a))) < 0) return 0;
    if (vec3_dot(normal, vec3_cross(bc, vec3_sub(p, b))) < 0) return 0;
    if (vec3_dot(normal, vec3_cross(ca, vec3_sub(p, c))) < 0) return 0;
    return 1;
}

static i32 sphere_triangle_contact(vec3 sphere_pos, real radius, bsp_triangle *tri, contact *c)
{
    vec3 ab = vec3_sub(tri->b, tri->a);
    vec3 ac = vec3_sub(tri->c, tri->a);
    vec3 n = vec3_cross(ab, ac);
    real len = vec3_magnitude(n);

    if (len < 1e-6f) return 0;
    n = vec3_div_scalar(n, len);

    real dist = vec3_dot(n, sphere_pos) - vec3_dot(n, tri->a);

    vec3 closest = vec3_sub(sphere_pos, vec3_mul_scalar(n, dist));

    if (point_in_triangle(closest, tri->a, tri->b, tri->c, n)) {
        c->point = closest;
        c->normal = n;
        if (dist < 0) c->normal = vec3_negate(n);
        c->penetration = radius - real_abs(dist);
        return 1;
    }

    real r2 = radius * radius;
    if (vec3_distance_squared(sphere_pos, tri->a) <= r2) {
        vec3 d = vec3_sub(sphere_pos, tri->a);
        real dlen = vec3_magnitude(d);
        c->point = tri->a;
        if (dlen > 1e-6f) c->normal = vec3_div_scalar(d, dlen);
        else c->normal = n;
        c->penetration = radius - dlen;
        return 1;
    }

    if (vec3_distance_squared(sphere_pos, tri->b) <= r2) {
        vec3 d = vec3_sub(sphere_pos, tri->b);
        real dlen = vec3_magnitude(d);
        c->point = tri->b;
        if (dlen > 1e-6f) c->normal = vec3_div_scalar(d, dlen);
        else c->normal = n;
        c->penetration = radius - dlen;
        return 1;
    }

    if (vec3_distance_squared(sphere_pos, tri->c) <= r2) {
        vec3 d = vec3_sub(sphere_pos, tri->c);
        real dlen = vec3_magnitude(d);
        c->point = tri->c;
        if (dlen > 1e-6f) c->normal = vec3_div_scalar(d, dlen);
        else c->normal = n;
        c->penetration = radius - dlen;
        return 1;
    }

    vec3 closest_ab = closest_point_on_segment(sphere_pos, tri->a, tri->b);
    vec3 diff_ab = vec3_sub(sphere_pos, closest_ab);
    real dlen_ab = vec3_magnitude(diff_ab);
    if (dlen_ab <= radius && dlen_ab > 0) {
        c->point = closest_ab;
        c->normal = vec3_div_scalar(diff_ab, dlen_ab);
        c->penetration = radius - dlen_ab;
        return 1;
    }

    vec3 closest_bc = closest_point_on_segment(sphere_pos, tri->b, tri->c);
    vec3 diff_bc = vec3_sub(sphere_pos, closest_bc);
    real dlen_bc = vec3_magnitude(diff_bc);
    if (dlen_bc <= radius && dlen_bc > 0) {
        c->point = closest_bc;
        c->normal = vec3_div_scalar(diff_bc, dlen_bc);
        c->penetration = radius - dlen_bc;
        return 1;
    }

    vec3 closest_ca = closest_point_on_segment(sphere_pos, tri->c, tri->a);
    vec3 diff_ca = vec3_sub(sphere_pos, closest_ca);
    real dlen_ca = vec3_magnitude(diff_ca);
    if (dlen_ca <= radius && dlen_ca > 0) {
        c->point = closest_ca;
        c->normal = vec3_div_scalar(diff_ca, dlen_ca);
        c->penetration = radius - dlen_ca;
        return 1;
    }

    return 0;
}

static i32 sphere_bsp_contact(physics_world *w, i32 sphere_idx, collision_bsp_definition *bsp, vec3 pos, contact *c)
{
    physics_body *sphere = &w->bodies[sphere_idx];
    real radius = sphere->rigid_body->sphere_radius;
    i32 i;
    real min_pen = 1e10f;
    i32 best_idx = -1;

    if (!bsp->triangles.address || bsp->triangles.count == 0) return 0;

    for (i = 0; i < (i32)bsp->triangles.count; i++) {
        bsp_triangle *tri = TAG_BLOCK_GET_ELEMENT(&bsp->triangles, (u32)i, bsp_triangle);
        contact temp;
        temp.body_a = sphere_idx;
        temp.body_b = -1;
        if (sphere_triangle_contact(pos, radius, tri, &temp) && temp.penetration < min_pen && temp.penetration > 0) {
            min_pen = temp.penetration;
            best_idx = i;
            *c = temp;
        }
    }

    return (best_idx >= 0) ? 1 : 0;
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

        /* BSP collision for dynamic bodies - check other entity's collision BSPs */
        physics_body *dynam_body = &w->bodies[i];
        if (dynam_body->inverse_mass > 0.0f) {
            i32 j;
            for (j = 0; j < w->max_entities; j++) {
                if (j == dynam_body->entity_index) continue;
                collision_bsp_definition *bsp = get_entity_collision_bsp(w, j);
                if (!bsp) continue;
                
                vec3 other_pos = w->entities[j]->position;
                vec4 other_orient = w->entities[j]->orientation;
                vec3 sphere_wpos = phys_ent_pos(w, i);
                vec3 local_pos = quat_rotate_vec3(quat_conjugate(other_orient), vec3_sub(sphere_wpos, other_pos));
                
                contact c;
                i32 hit = sphere_bsp_contact(w, i, bsp, local_pos, &c);
                if (hit) {
                    c.body_b = -1;  /* Mark as static BSP contact */
                    c.point = vec3_add(other_pos, quat_rotate_vec3(other_orient, c.point));
                    c.normal = quat_rotate_vec3(other_orient, c.normal);
                    if (w->contact_count < PHYSICS_MAX_CONTACTS)
                        w->contacts[w->contact_count++] = c;
                }
            }
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
            i32 body_b_is_static = (cn->body_b < 0);

            if (a->inverse_mass <= 0.0f && !body_b_is_static) continue;
            if (a->asleep) continue;
            a->asleep = 0;

            vec3 pa = phys_ent_pos(w, cn->body_a);
            vec3 ra = vec3_sub(cn->point, pa);
            vec3 vrel;
            if (body_b_is_static) {
                vrel = a->rigid_body->velocity;
            } else {
                physics_body *b = &w->bodies[cn->body_b];
                b->asleep = 0;
                vec3 pb = phys_ent_pos(w, cn->body_b);
                vec3 rb = vec3_sub(cn->point, pb);
                vrel = vec3_add(
                    vec3_sub(b->rigid_body->velocity, a->rigid_body->velocity),
                    vec3_sub(vec3_cross(b->rigid_body->angular_velocity, rb),
                             vec3_cross(a->rigid_body->angular_velocity, ra)));
            }
            real vn = vec3_dot(vrel, cn->normal);
            if (vn > 0.0f) continue;

            mat3 invIa = world_inv_inertia_phys(a, phys_ent_orient(w, cn->body_a));
            vec3 rcn_a = vec3_cross(ra, cn->normal);
            real denom = a->inverse_mass + vec3_dot(mat3_mul_vec3(invIa, rcn_a), rcn_a);
            if (denom < 1e-6f) continue;

            real e = a->rigid_body->restitution;
            if (!body_b_is_static) {
                physics_body *b = &w->bodies[cn->body_b];
                vec3 pb = phys_ent_pos(w, cn->body_b);
                vec3 rb = vec3_sub(cn->point, pb);
                vec4 ob = phys_ent_orient(w, cn->body_b);
                mat3 invIb = world_inv_inertia_phys(b, ob);
                vec3 rcn_b = vec3_cross(rb, cn->normal);
                denom += b->inverse_mass + vec3_dot(mat3_mul_vec3(invIb, rcn_b), rcn_b);
                e = (e + b->rigid_body->restitution) * 0.5f;
            }
real j = -(1.0f+e)*vn / denom;
            if (j < 0.0f) j = 0.0f;
            vec3 imp = vec3_mul_scalar(cn->normal, j);
            if (body_b_is_static) {
                /* For static BSP, normal points OUT from surface, add to push sphere away */
                a->rigid_body->velocity = vec3_add(a->rigid_body->velocity, vec3_mul_scalar(imp, a->inverse_mass));
            } else {
                a->rigid_body->velocity = vec3_sub(a->rigid_body->velocity, vec3_mul_scalar(imp, a->inverse_mass));
                physics_body *b = &w->bodies[cn->body_b];
                b->rigid_body->velocity = vec3_add(b->rigid_body->velocity, vec3_mul_scalar(imp, b->inverse_mass));
            }
            a->rigid_body->angular_velocity = vec3_sub(a->rigid_body->angular_velocity,
                                          mat3_mul_vec3(invIa, vec3_cross(ra, imp)));
            if (!body_b_is_static) {
                physics_body *b = &w->bodies[cn->body_b];
                vec3 pb = phys_ent_pos(w, cn->body_b);
                vec3 rb = vec3_sub(cn->point, pb);
                vec4 ob = phys_ent_orient(w, cn->body_b);
                mat3 invIb = world_inv_inertia_phys(b, ob);
                b->rigid_body->angular_velocity = vec3_add(b->rigid_body->angular_velocity,
                                             mat3_mul_vec3(invIb, vec3_cross(rb, imp)));
            }

            /* friction */
            {
                real raw_f = a->rigid_body->friction;
                if (!body_b_is_static) {
                    physics_body *b = &w->bodies[cn->body_b];
                    raw_f = real_sqrt(raw_f * b->rigid_body->friction);
                    if (raw_f > 1.0f) raw_f = 1.0f;
                }
                if (raw_f <= 0.0f) continue;

                vec3 tangent = vec3_sub(vrel, vec3_mul_scalar(cn->normal, vn));
                real tangent_len = vec3_magnitude(tangent);
                if (tangent_len < 1e-6f) continue;
                tangent = vec3_div_scalar(tangent, tangent_len);

                vec3 rcn_a_t = vec3_cross(ra, tangent);
                real denom_t = a->inverse_mass + vec3_dot(mat3_mul_vec3(invIa, rcn_a_t), rcn_a_t);
                if (!body_b_is_static) {
                    physics_body *b = &w->bodies[cn->body_b];
                    vec3 pb = phys_ent_pos(w, cn->body_b);
                    vec3 rb = vec3_sub(cn->point, pb);
                    vec3 rcn_b_t = vec3_cross(rb, tangent);
                    vec4 ob = phys_ent_orient(w, cn->body_b);
                    mat3 invIb = world_inv_inertia_phys(b, ob);
                    denom_t += b->inverse_mass + vec3_dot(mat3_mul_vec3(invIb, rcn_b_t), rcn_b_t);
                }
                if (denom_t < 1e-6f) continue;

                real jt_stick = -vec3_dot(vrel, tangent) / denom_t;
                real jt = jt_stick;
                real jt_max = (raw_f >= 1.0f ? raw_f : raw_f / (1.0f - raw_f)) * real_abs(j);
                if (jt > jt_max) jt = jt_max;
                else if (jt < -jt_max) jt = -jt_max;

                vec3 imp_t = vec3_mul_scalar(tangent, jt);
                a->rigid_body->velocity = vec3_sub(a->rigid_body->velocity, vec3_mul_scalar(imp_t, a->inverse_mass));
                a->rigid_body->angular_velocity = vec3_sub(a->rigid_body->angular_velocity,
                                                    mat3_mul_vec3(invIa, vec3_cross(ra, imp_t)));
                if (!body_b_is_static) {
                    physics_body *b = &w->bodies[cn->body_b];
                    vec3 pb = phys_ent_pos(w, cn->body_b);
                    vec3 rb = vec3_sub(cn->point, pb);
                    vec4 ob = phys_ent_orient(w, cn->body_b);
                    mat3 invIb = world_inv_inertia_phys(b, ob);
                    b->rigid_body->velocity = vec3_add(b->rigid_body->velocity, vec3_mul_scalar(imp_t, b->inverse_mass));
                    b->rigid_body->angular_velocity = vec3_add(b->rigid_body->angular_velocity,
                                             mat3_mul_vec3(invIb, vec3_cross(rb, imp_t)));
                }
            }
        }

        /* position correction */
        for (ci = 0; ci < w->contact_count; ci++) {
            contact *cn = &w->contacts[ci];
            physics_body *a = &w->bodies[cn->body_a];
            i32 body_b_is_static = (cn->body_b < 0);

            if (a->inverse_mass <= 0.0f && !body_b_is_static) continue;
            if (a->asleep) continue;
            a->asleep = 0;

            real slop = 0.005f, percent = 0.4f;
            real pen = cn->penetration;
            if (pen <= slop) continue;
            vec3 pa = phys_ent_pos(w, cn->body_a);
            vec3 ra = vec3_sub(cn->point, pa);
            mat3 invIa = world_inv_inertia_phys(a, phys_ent_orient(w, cn->body_a));
            vec3 rcn_a = vec3_cross(ra, cn->normal);
            real denom = a->inverse_mass + vec3_dot(mat3_mul_vec3(invIa, rcn_a), rcn_a);
            if (denom < 1e-6f) continue;
            real corr = (pen - slop) / denom * percent;
            if (corr > 1.0f) corr = 1.0f;
            vec3 corr_vec = vec3_mul_scalar(cn->normal, corr);
            if (body_b_is_static) {
                /* Push sphere OUT from static BSP */
                phys_set_pos(w, cn->body_a, vec3_add(pa, vec3_mul_scalar(corr_vec, a->inverse_mass)));
            } else {
                phys_set_pos(w, cn->body_a, vec3_sub(pa, vec3_mul_scalar(corr_vec, a->inverse_mass)));
                physics_body *b = &w->bodies[cn->body_b];
                vec3 pb = phys_ent_pos(w, cn->body_b);
                phys_set_pos(w, cn->body_b, vec3_add(pb, vec3_mul_scalar(corr_vec, b->inverse_mass)));
            }
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif /* PHYSICS_H */