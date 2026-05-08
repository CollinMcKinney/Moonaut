#ifndef RUNTIME_H
#define RUNTIME_H

#include "common.h"
#include "physics.h"
#include "rasterizer.h"
#include "scripts.h"

#include "tags/model.h"
#include "tags/entity.h"
#include "tags/rigid_body.h"
#include "tags/collision_bsp.h"
#include "tags/scenario.h"
#include "tags/globals.h"
#include "tags/camera.h"
#include "tags/lua_script.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENARIO_MAX_ENTITIES PHYSICS_MAX_BODIES

/* Render info for a single entity that has a model */
typedef struct entity_render_info {
    i32  model_handle;          /* tag handle to model_definition */
    i32  entity_index;          /* which entity in `entities[]` */
    vec3 position;              /* world‑space (updated from entity) */
    vec4 orientation;
} entity_render_info;

/* ------------------------------------------------------------------------
   Scenario world
   ------------------------------------------------------------------------ */
typedef struct scenario_world {
    physics_world physics;

    /* All entities in the loaded scenario */
    entity_definition entities[SCENARIO_MAX_ENTITIES];
    i32               entity_count;

    /* Entities that are drawn (have models) */
    entity_render_info render_list[SCENARIO_MAX_ENTITIES];
    i32               render_count;

    i32 width, height;
} scenario_world;

static scenario_world *g_scene_world = NULL;

/* Default render settings (overridden by globals / tags) */
static vec3 sc_cam_eye    = {0, 0, 0};
static vec3 sc_cam_center = {0, 0, -1};
static vec3 sc_cam_up     = {0, 1, 0};
static real sc_cam_fov    = 90.0f;
static u8   sc_clear_r = 16, sc_clear_g = 24, sc_clear_b = 40;
static i32  sc_pause_physics = 0;
static real sc_fixed_dt = 1.0f / 60.0f;

/* ------------------------------------------------------------------------
   Lua helper – return the internal physics_body for a given entity index,
   or NULL if it isn't dynamic.
   ------------------------------------------------------------------------ */
static physics_body *scenario_get_physics_body(scenario_world *w, i32 entity_index) {
    i32 idx = w->physics.entity_to_body[entity_index];
    if (idx < 0 || idx >= w->physics.body_count) return NULL;
    return &w->physics.bodies[idx];
}

/* ------------------------------------------------------------------------
   Load a scenario tag and populate the world
   ------------------------------------------------------------------------ */
static i32 scenario_load_tag(scenario_world *w, const char *scenario_name) {
    i32 scn_handle = tag_load(scenario_name, TAG_scenario);
    if (scn_handle < 0) return -1;

    scenario_definition *scn = (scenario_definition*)tag_get(scn_handle, TAG_scenario);
    if (!scn) return -1;

    fprintf(stderr, "scenario loaded, globals handle=%d\n", scn->globals.handle);
    fprintf(stderr, "entities block count=%u address=%p\n", scn->entities.count, scn->entities.address);


    /* - Apply globals - */
    if (scn->globals.handle >= 0) {
        globals_definition *g = (globals_definition*)tag_get(scn->globals.handle, TAG_globals);
        if (g) {
            physics_init(&w->physics, w->entities, SCENARIO_MAX_ENTITIES, g->gravity);
            sc_pause_physics = g->pause_physics;
            sc_fixed_dt = 1.0f / g->physics_rate;
            sc_clear_r = color_to_u8(g->clear_color.position.x);
            sc_clear_g = color_to_u8(g->clear_color.position.y);
            sc_clear_b = color_to_u8(g->clear_color.position.z);
            render_set_fog(g->fog_color, g->fog_start, g->fog_end);
            render_set_light(g->light_dir, g->light_col, g->ambient_col);
        }
    }

    /* - Apply camera - */
    if (scn->camera.handle >= 0) {
        camera_definition *cam = (camera_definition*)tag_get(scn->camera.handle, TAG_camera);
        if (cam) {
            sc_cam_eye    = cam->eye;
            sc_cam_center = cam->center;
            sc_cam_up     = cam->up;
            sc_cam_fov    = cam->fov;
        }
    }

    /* - Process entities (now references) - */
    w->entity_count = 0;
    w->render_count = 0;

    tag_reference *entity_refs = (tag_reference*)scn->entities.address;
   
    /* Temporary: try to read the first two handles before the loop */
    if (scn->entities.count >= 1) {
        fprintf(stderr, "  entity[0] handle = %d\n", entity_refs[0].handle);
    }
    if (scn->entities.count >= 2) {
        fprintf(stderr, "  entity[1] handle = %d\n", entity_refs[1].handle);
    }

    u32 i;
    for (i = 0; i < scn->entities.count && w->entity_count < SCENARIO_MAX_ENTITIES; ++i) {
        i32 ent_handle = entity_refs[i].handle;
        if (ent_handle < 0) continue;

        entity_definition *src = (entity_definition*)tag_get(ent_handle, TAG_entity);
        if (!src) continue;

        i32 ent_idx = w->entity_count;
        w->entities[ent_idx] = *src;

        if (src->rigid_body.handle >= 0) {
            physics_add_entity(&w->physics, ent_idx);
        }

        if (src->model.handle >= 0 && w->render_count < SCENARIO_MAX_ENTITIES) {
            w->render_list[w->render_count].model_handle = src->model.handle;
            w->render_list[w->render_count].entity_index = ent_idx;
            w->render_list[w->render_count].position     = src->position;
            w->render_list[w->render_count].orientation  = src->orientation;
            w->render_count++;
        }

        w->entity_count++;
    }
    return scn_handle;
}

/* ------------------------------------------------------------------------
   Update all render‑list transforms from their entities
   (called after physics step, which already updated entity transforms)
   ------------------------------------------------------------------------ */
static void scenario_update_render_transforms(scenario_world *w) {
    i32 i;
    for (i = 0; i < w->render_count; ++i) {
        i32 ent_idx = w->render_list[i].entity_index;
        w->render_list[i].position    = w->entities[ent_idx].position;
        w->render_list[i].orientation = w->entities[ent_idx].orientation;
    }
}

/* ------------------------------------------------------------------------
   Draw one model primitive with its material
   ------------------------------------------------------------------------ */
static void scenario_draw_primitive(model_primitive *prim, model_definition *mod,
                                    vec3 pos, vec4 orient) {
    if (prim->vertices.count == 0 || prim->indices.count == 0) return;

    material_definition *mat = NULL;
    if (prim->material_index >= 0 && mod->materials.address) {
        tag_reference *refs = (tag_reference*)mod->materials.address;
        i32 mat_handle = refs[prim->material_index].handle;
        if (mat_handle >= 0)
            mat = (material_definition*)tag_get(mat_handle, TAG_material);
    }
    if (!mat) {
        static material_definition fallback = DEFAULT_MATERIAL_FLAT;
        mat = &fallback;
    }

    render_set_shading_mode((shading_mode)mat->mode);

    model_vertex *verts = (model_vertex*)prim->vertices.address;
    u16 *indices = (u16*)prim->indices.address;
    u32 tri_count = prim->indices.count / 3;

    u32 t;
    for (t = 0; t < tri_count; ++t) {
        u16 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
        vec3 v0 = verts[i0].position, v1 = verts[i1].position, v2 = verts[i2].position;
        vec3 n0 = verts[i0].normal,   n1 = verts[i1].normal,   n2 = verts[i2].normal;

        /* Apply entity transform */
        v0 = vec3_add(pos, quat_rotate_vec3(orient, v0));
        v1 = vec3_add(pos, quat_rotate_vec3(orient, v1));
        v2 = vec3_add(pos, quat_rotate_vec3(orient, v2));
        n0 = quat_rotate_vec3(orient, n0);
        n1 = quat_rotate_vec3(orient, n1);
        n2 = quat_rotate_vec3(orient, n2);

        draw_triangle_shaded(v0, v1, v2, n0, n1, n2, mat, mat->mode);
    }
}

/* ------------------------------------------------------------------------
   Main render call
   ------------------------------------------------------------------------ */
static void scenario_render(scenario_world *w) {
    shading_mode saved_mode = render_get_shading_mode();
    real aspect = (real)w->width / (real)w->height;
    i32 i;

    render_set_camera(sc_cam_eye, sc_cam_center, sc_cam_up,
                      sc_cam_fov * VECTORS_DEG2RAD, aspect);
    render_set_light(light_dir, light_col, ambient_col);
    render_clear(sc_clear_r, sc_clear_g, sc_clear_b);

    for (i = 0; i < w->render_count; i++) {
        entity_render_info *rinfo = &w->render_list[i];
        model_definition *mod = (model_definition*)tag_get(rinfo->model_handle, TAG_model);
        if (!mod) continue;

        u32 p;
        for (p = 0; p < mod->primitives.count; ++p) {
            model_primitive *prim = TAG_BLOCK_GET_ELEMENT(&mod->primitives, p, model_primitive);
            scenario_draw_primitive(prim, mod, rinfo->position, rinfo->orientation);
        }
    }

    render_set_shading_mode(saved_mode);
    render_finish();   /* sorts & draws transparent, swaps buffers */
}

/* ====================================================================
   Lua runtime API – adapted to entity‑based physics
   ==================================================================== */

/* - Vector conversion helpers (unchanged) - */
static i32 lua_to_vec3_table(lua_State *L, i32 index, vec3 *out) {
    i32 abs_index = lua_absindex(L, index);
    if (!lua_istable(L, abs_index)) return 0;
    lua_getfield(L, abs_index, "x");
    lua_getfield(L, abs_index, "y");
    lua_getfield(L, abs_index, "z");
    if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
        lua_pop(L, 3);
        return 0;
    }
    *out = vec3_init_from_3((real)lua_tonumber(L, -3),
                            (real)lua_tonumber(L, -2),
                            (real)lua_tonumber(L, -1));
    lua_pop(L, 3);
    return 1;
}

static i32 lua_to_vec3_or_error(lua_State *L, i32 index, vec3 *out, const char *message) {
    if (lua_to_vec3_table(L, index, out)) return 1;
    luaL_error(L, "%s", message);
    return 0;
}

static i32 lua_check_entity_id(lua_State *L, i32 index) {
    i32 id = (i32)luaL_checkinteger(L, index);
    if (id < 0 || id >= g_scene_world->entity_count)
        luaL_error(L, "invalid entity id");
    return id;
}

static i32 lua_builtin_vec2(lua_State *L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
    return 1;
}

static i32 lua_builtin_vec3(lua_State *L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_Number z = luaL_checknumber(L, 3);
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
    return 1;
}

static i32 lua_builtin_vec4(lua_State *L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_Number z = luaL_checknumber(L, 3);
    lua_Number w = luaL_checknumber(L, 4);
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, w); lua_setfield(L, -2, "w");
    return 1;
}

static i32 lua_sphere(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (g_scene_world->entity_count >= SCENARIO_MAX_ENTITIES)
        return luaL_error(L, "too many entities");
    i32 ent_idx = g_scene_world->entity_count++;
    entity_definition *ent = &g_scene_world->entities[ent_idx];
    memset(ent, 0, sizeof(*ent));
    ent->type = ENTITY_DYNAMIC;
    ent->orientation = quat_identity();
    real radius;

    if (argc == 2) {
        lua_to_vec3_or_error(L, 1, &ent->position, "sphere(center, radius) requires a vec3 center");
        radius = (real)luaL_checknumber(L, 2);
    } else if (argc == 4) {
        ent->position = vec3_init_from_3((real)luaL_checknumber(L, 1),
                                         (real)luaL_checknumber(L, 2),
                                         (real)luaL_checknumber(L, 3));
        radius = (real)luaL_checknumber(L, 4);
    } else {
        g_scene_world->entity_count--;
        return luaL_error(L, "sphere(center, radius) or sphere(x,y,z,radius)");
    }

    physics_add_simple_sphere(&g_scene_world->physics, ent_idx, radius, 1.0f);
    lua_pushinteger(L, ent_idx);
    return 1;
}

static i32 lua_box(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (g_scene_world->entity_count >= SCENARIO_MAX_ENTITIES)
        return luaL_error(L, "too many entities");
    i32 ent_idx = g_scene_world->entity_count++;
    entity_definition *ent = &g_scene_world->entities[ent_idx];
    memset(ent, 0, sizeof(*ent));
    ent->type = ENTITY_DYNAMIC;
    ent->orientation = quat_identity();
    vec3 half_ext;

    if (argc == 2) {
        lua_to_vec3_or_error(L, 1, &ent->position, "box(center, half_ext) requires a vec3 center");
        lua_to_vec3_or_error(L, 2, &half_ext, "box(center, half_ext) requires a vec3 half_ext");
    } else if (argc == 6) {
        ent->position = vec3_init_from_3((real)luaL_checknumber(L, 1),
                                         (real)luaL_checknumber(L, 2),
                                         (real)luaL_checknumber(L, 3));
        half_ext = vec3_init_from_3((real)luaL_checknumber(L, 4),
                                    (real)luaL_checknumber(L, 5),
                                    (real)luaL_checknumber(L, 6));
    } else {
        g_scene_world->entity_count--;
        return luaL_error(L, "box(center, half_ext) or box(x,y,z, halfX, halfY, halfZ)");
    }

    physics_add_simple_box(&g_scene_world->physics, ent_idx, half_ext, 0.0f);
    lua_pushinteger(L, ent_idx);
    return 1;
}

static i32 lua_dynamic_box(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (g_scene_world->entity_count >= SCENARIO_MAX_ENTITIES)
        return luaL_error(L, "too many entities");
    i32 ent_idx = g_scene_world->entity_count++;
    entity_definition *ent = &g_scene_world->entities[ent_idx];
    memset(ent, 0, sizeof(*ent));
    ent->type = ENTITY_DYNAMIC;
    ent->orientation = quat_identity();
    vec3 half_ext;
    real mass_val;

    if (argc == 3) {
        lua_to_vec3_or_error(L, 1, &ent->position, "dynamic_box(center, half_ext, mass) requires a vec3 center");
        lua_to_vec3_or_error(L, 2, &half_ext, "dynamic_box(center, half_ext, mass) requires a vec3 half_ext");
        mass_val = (real)luaL_checknumber(L, 3);
    } else if (argc == 7) {
        ent->position = vec3_init_from_3((real)luaL_checknumber(L, 1),
                                         (real)luaL_checknumber(L, 2),
                                         (real)luaL_checknumber(L, 3));
        half_ext = vec3_init_from_3((real)luaL_checknumber(L, 4),
                                    (real)luaL_checknumber(L, 5),
                                    (real)luaL_checknumber(L, 6));
        mass_val = (real)luaL_checknumber(L, 7);
    } else {
        g_scene_world->entity_count--;
        return luaL_error(L, "dynamic_box(center, half_ext, mass) or dynamic_box(x,y,z, halfX, halfY, halfZ, mass)");
    }

    physics_add_simple_box(&g_scene_world->physics, ent_idx, half_ext, mass_val);
    lua_pushinteger(L, ent_idx);
    return 1;
}

/* These operate on the physics body fields using the entity index */
static i32 lua_impulse(lua_State *L) {
    i32 id = lua_check_entity_id(L, 1);
    physics_body *b = scenario_get_physics_body(g_scene_world, id);
    if (!b || b->inverse_mass <= 0.0f) return 0;
    vec3 impulse;
    if (lua_gettop(L) == 2) {
        lua_to_vec3_or_error(L, 2, &impulse, "impulse(id, vec3) requires a vec3 impulse");
    } else {
        impulse = vec3_init_from_3((real)luaL_checknumber(L, 2),
                                   (real)luaL_checknumber(L, 3),
                                   (real)luaL_checknumber(L, 4));
    }
    b->velocity = vec3_add(b->velocity, vec3_mul_scalar(impulse, b->inverse_mass));
    return 0;
}

static i32 lua_velocity(lua_State *L) {
    i32 id = lua_check_entity_id(L, 1);
    physics_body *b = scenario_get_physics_body(g_scene_world, id);
    if (!b) return luaL_error(L, "entity has no physics body");
    if (lua_gettop(L) == 2) {
        lua_to_vec3_or_error(L, 2, &b->velocity, "velocity(id, vec3) requires a vec3");
    } else {
        b->velocity = vec3_init_from_3((real)luaL_checknumber(L, 2),
                                       (real)luaL_checknumber(L, 3),
                                       (real)luaL_checknumber(L, 4));
    }
    return 0;
}

static i32 lua_mass(lua_State *L) {
    i32 id = lua_check_entity_id(L, 1);
    physics_body *b = scenario_get_physics_body(g_scene_world, id);
    if (!b) return luaL_error(L, "entity has no physics body");
    real new_mass = (real)luaL_checknumber(L, 2);
    if (new_mass <= 0.0f) {
        b->mass = 0.0f;
        b->inverse_mass = 0.0f;
    } else {
        b->mass = new_mass;
        b->inverse_mass = 1.0f / new_mass;
    }
    return 0;
}

static i32 lua_restitution(lua_State *L) {
    i32 id = lua_check_entity_id(L, 1);
    physics_body *b = scenario_get_physics_body(g_scene_world, id);
    if (!b) return luaL_error(L, "entity has no physics body");
    b->restitution = (real)luaL_checknumber(L, 2);
    return 0;
}

static i32 lua_friction(lua_State *L) {
    i32 id = lua_check_entity_id(L, 1);
    physics_body *b = scenario_get_physics_body(g_scene_world, id);
    if (!b) return luaL_error(L, "entity has no physics body");
    b->friction = (real)luaL_checknumber(L, 2);
    return 0;
}

static i32 lua_gravity(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &g_scene_world->physics.gravity, "gravity(vec3) requires a vec3");
    } else if (argc == 3) {
        g_scene_world->physics.gravity = vec3_init_from_3(
            (real)luaL_checknumber(L, 1),
            (real)luaL_checknumber(L, 2),
            (real)luaL_checknumber(L, 3));
    } else {
        return luaL_error(L, "gravity(vec3) or gravity(x,y,z)");
    }
    return 0;
}

static i32 lua_clear(lua_State *L) {
    (void)L;
    g_scene_world->physics.body_count = 0;
    g_scene_world->entity_count = 0;
    return 0;
}

static i32 lua_camera_eye(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &sc_cam_eye, "camera_eye(vec3) requires a vec3");
    } else if (argc == 3) {
        sc_cam_eye = vec3_init_from_3((real)luaL_checknumber(L, 1),
                                      (real)luaL_checknumber(L, 2),
                                      (real)luaL_checknumber(L, 3));
    } else {
        return luaL_error(L, "camera_eye(vec3) or camera_eye(x,y,z)");
    }
    return 0;
}

static i32 lua_camera_lookat(lua_State *L) {
    i32 argc = lua_gettop(L);
    if (argc == 2) {
        lua_to_vec3_or_error(L, 1, &sc_cam_center, "camera_lookat(center, up) requires a vec3 center");
        lua_to_vec3_or_error(L, 2, &sc_cam_up, "camera_lookat(center, up) requires a vec3 up");
    } else if (argc == 6) {
        sc_cam_center = vec3_init_from_3((real)luaL_checknumber(L, 1),
                                         (real)luaL_checknumber(L, 2),
                                         (real)luaL_checknumber(L, 3));
        sc_cam_up = vec3_init_from_3((real)luaL_checknumber(L, 4),
                                     (real)luaL_checknumber(L, 5),
                                     (real)luaL_checknumber(L, 6));
    } else {
        return luaL_error(L, "camera_lookat(center, up) or camera_lookat(cx,cy,cz, ux,uy,uz)");
    }
    return 0;
}

static i32 lua_camera_fov(lua_State *L) {
    sc_cam_fov = (real)luaL_checknumber(L, 1);
    return 0;
}

static i32 lua_light(lua_State *L) {
    i32 argc = lua_gettop(L);
    vec3 dir, col, amb;
    if (argc == 3) {
        lua_to_vec3_or_error(L, 1, &dir, "light(dir, color, ambient) requires a vec3 direction");
        lua_to_vec3_or_error(L, 2, &col, "light(dir, color, ambient) requires a vec3 color");
        lua_to_vec3_or_error(L, 3, &amb, "light(dir, color, ambient) requires a vec3 ambient");
    } else if (argc == 9) {
        dir = vec3_init_from_3((real)luaL_checknumber(L, 1),
                               (real)luaL_checknumber(L, 2),
                               (real)luaL_checknumber(L, 3));
        col = vec3_init_from_3((real)luaL_checknumber(L, 4),
                               (real)luaL_checknumber(L, 5),
                               (real)luaL_checknumber(L, 6));
        amb = vec3_init_from_3((real)luaL_checknumber(L, 7),
                               (real)luaL_checknumber(L, 8),
                               (real)luaL_checknumber(L, 9));
    } else {
        return luaL_error(L, "light(dir, color, ambient) or light(dirx,diry,dirz, colr,colg,colb, ambr,ambg,ambb)");
    }
    render_set_light(dir, col, amb);
    return 0;
}

static i32 lua_light_direction(lua_State *L) {
    i32 argc = lua_gettop(L);
    vec3 dir;
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &dir, "light_direction(vec3) requires a vec3");
    } else if (argc == 3) {
        dir = vec3_init_from_3((real)luaL_checknumber(L, 1),
                               (real)luaL_checknumber(L, 2),
                               (real)luaL_checknumber(L, 3));
    } else {
        return luaL_error(L, "light_direction(vec3) or light_direction(x,y,z)");
    }
    render_set_light(dir, light_col, ambient_col);
    return 0;
}

static i32 lua_light_color(lua_State *L) {
    i32 argc = lua_gettop(L);
    vec3 col;
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &col, "light_color(vec3) requires a vec3");
    } else if (argc == 3) {
        col = vec3_init_from_3((real)luaL_checknumber(L, 1),
                               (real)luaL_checknumber(L, 2),
                               (real)luaL_checknumber(L, 3));
    } else {
        return luaL_error(L, "light_color(vec3) or light_color(r,g,b)");
    }
    render_set_light(light_dir, col, ambient_col);
    return 0;
}

static i32 lua_light_ambient(lua_State *L) {
    i32 argc = lua_gettop(L);
    vec3 amb;
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &amb, "light_ambient(vec3) requires a vec3");
    } else if (argc == 3) {
        amb = vec3_init_from_3((real)luaL_checknumber(L, 1),
                               (real)luaL_checknumber(L, 2),
                               (real)luaL_checknumber(L, 3));
    } else {
        return luaL_error(L, "light_ambient(vec3) or light_ambient(r,g,b)");
    }
    render_set_light(light_dir, light_col, amb);
    return 0;
}

static i32 lua_shading_mode(lua_State *L) {
    i32 mode = (i32)luaL_checkinteger(L, 1);
    if (mode < (i32)SHADE_WIREFRAME || mode > (i32)SHADE_PHONG)
        return luaL_error(L, "invalid shading mode");
    render_set_shading_mode((shading_mode)mode);
    return 0;
}

static i32 lua_clear_color(lua_State *L) {
    i32 argc = lua_gettop(L);
    vec3 color;
    if (argc == 1) {
        lua_to_vec3_or_error(L, 1, &color, "clear_color(color) requires a vec3 color");
        sc_clear_r = color_to_u8(color.position.x);
        sc_clear_g = color_to_u8(color.position.y);
        sc_clear_b = color_to_u8(color.position.z);
    } else if (argc == 3) {
        sc_clear_r = (u8)(real)luaL_checknumber(L, 1);
        sc_clear_g = (u8)(real)luaL_checknumber(L, 2);
        sc_clear_b = (u8)(real)luaL_checknumber(L, 3);
    } else {
        return luaL_error(L, "clear_color(color) or clear_color(r, g, b)");
    }
    return 0;
}

static i32 lua_pause_physics(lua_State *L) {
    sc_pause_physics = lua_toboolean(L, 1) ? 1 : 0;
    return 0;
}

static i32 lua_physics_rate(lua_State *L) {
    real hz = (real)luaL_checknumber(L, 1);
    if (hz <= 0.0f) return luaL_error(L, "physics_rate must be > 0");
    sc_fixed_dt = 1.0f / hz;
    return 0;
}

static i32 lua_load_scenario(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    i32 handle = scenario_load_tag(g_scene_world, name);
    if (handle < 0) return luaL_error(L, "failed to load scenario '%s'", name);
    lua_pushinteger(L, handle);
    return 1;
}

/* ------------------------------------------------------------------------
   Tag reflection Lua API
   ------------------------------------------------------------------------ */

/* Helper: push a field value onto the Lua stack */
static void push_field_value(lua_State *L, const tag_field_definition *f, void *field_ptr)
{
    switch (f->type) {
        case TAG_FIELD_BOOL:        lua_pushboolean(L, *(bool*)field_ptr); break;
        case TAG_FIELD_I8:          lua_pushinteger(L, *(i8*)field_ptr); break;
        case TAG_FIELD_U8:          lua_pushinteger(L, *(u8*)field_ptr); break;
        case TAG_FIELD_I16:         lua_pushinteger(L, *(i16*)field_ptr); break;
        case TAG_FIELD_U16:         lua_pushinteger(L, *(u16*)field_ptr); break;
        case TAG_FIELD_I32:         lua_pushinteger(L, *(i32*)field_ptr); break;
        case TAG_FIELD_U32:         lua_pushinteger(L, *(u32*)field_ptr); break;
        case TAG_FIELD_REAL:        lua_pushnumber(L, *(real*)field_ptr); break;
        case TAG_FIELD_ENUM:        lua_pushinteger(L, *(i32*)field_ptr); break;
        case TAG_FIELD_FLAGS:       lua_pushinteger(L, *(i32*)field_ptr); break;
        case TAG_FIELD_VEC2: {
            vec2 *v = (vec2*)field_ptr;
            lua_createtable(L, 0, 2);
            lua_pushnumber(L, v->position.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, v->position.y); lua_setfield(L, -2, "y");
            break;
        }
        case TAG_FIELD_VEC3: {
            vec3 *v = (vec3*)field_ptr;
            lua_createtable(L, 0, 3);
            lua_pushnumber(L, v->position.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, v->position.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, v->position.z); lua_setfield(L, -2, "z");
            break;
        }
        case TAG_FIELD_VEC4: {
            vec4 *v = (vec4*)field_ptr;
            lua_createtable(L, 0, 4);
            lua_pushnumber(L, v->position.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, v->position.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, v->position.z); lua_setfield(L, -2, "z");
            lua_pushnumber(L, v->rotation.w); lua_setfield(L, -2, "w");
            break;
        }
        case TAG_FIELD_STRING_ID: {
            string_id id = *(string_id*)field_ptr;
            const char *str = string_id_lookup(id);
            lua_pushstring(L, str ? str : "");
            break;
        }
        case TAG_FIELD_REFERENCE: {
            tag_reference *ref = (tag_reference*)field_ptr;
            lua_pushinteger(L, ref->handle);
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }
}

/* Helper: set a field from a Lua value */
static void set_field_from_lua(lua_State *L, const tag_field_definition *f,
                               void *field_ptr, i32 value_index)
{
    void *ptr = field_ptr;   /* fix: was missing assignment */
    switch (f->type) {
        case TAG_FIELD_BOOL:    *(bool*)ptr = (bool)lua_toboolean(L, value_index); break;
        case TAG_FIELD_I8:      *(i8*)ptr = (i8)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_U8:      *(u8*)ptr = (u8)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_I16:     *(i16*)ptr = (i16)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_U16:     *(u16*)ptr = (u16)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_I32:     *(i32*)ptr = (i32)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_U32:     *(u32*)ptr = (u32)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_REAL:    *(real*)ptr = (real)luaL_checknumber(L, value_index); break;
        case TAG_FIELD_ENUM:    *(i32*)ptr = (i32)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_FLAGS:   *(i32*)ptr = (i32)luaL_checkinteger(L, value_index); break;
        case TAG_FIELD_VEC2: {
            if (lua_istable(L, value_index)) {
                real x, y;
                lua_getfield(L, value_index, "x"); x = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "y"); y = (real)lua_tonumber(L, -1);
                lua_pop(L, 2);
                ((vec2*)ptr)->position.x = x;
                ((vec2*)ptr)->position.y = y;
            }
            break;
        }
        case TAG_FIELD_VEC3: {
            if (lua_istable(L, value_index)) {
                real x, y, z;
                lua_getfield(L, value_index, "x"); x = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "y"); y = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "z"); z = (real)lua_tonumber(L, -1);
                lua_pop(L, 3);
                ((vec3*)ptr)->position.x = x;
                ((vec3*)ptr)->position.y = y;
                ((vec3*)ptr)->position.z = z;
            }
            break;
        }
        case TAG_FIELD_VEC4: {
            if (lua_istable(L, value_index)) {
                real x, y, z, w;
                lua_getfield(L, value_index, "x"); x = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "y"); y = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "z"); z = (real)lua_tonumber(L, -1);
                lua_getfield(L, value_index, "w"); w = (real)lua_tonumber(L, -1);
                lua_pop(L, 4);
                ((vec4*)ptr)->position.x = x;
                ((vec4*)ptr)->position.y = y;
                ((vec4*)ptr)->position.z = z;
                ((vec4*)ptr)->rotation.w = w;
            }
            break;
        }
        case TAG_FIELD_STRING_ID:   /* cannot set via Lua yet */   break;
        case TAG_FIELD_REFERENCE:   /* read‑only from Lua */     break;
        default: break;
    }
}

/* tag_load(name, group_fourcc) -> handle */
static i32 lua_tag_load(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    tag group_tag = (tag)luaL_checkinteger(L, 2);
    i32 handle = tag_load(name, group_tag);
    if (handle < 0) return luaL_error(L, "tag '%s' not found", name);
    lua_pushinteger(L, handle);
    return 1;
}

/* tag_get_field(handle, field_name) -> value */
static i32 lua_tag_get_field(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *field_name = luaL_checkstring(L, 2);
    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");
    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (strcmp(f->name, field_name) == 0) {
            push_field_value(L, f, ptr);
            return 1;
        }
        ptr += tag_field_size(f);
    }
    return luaL_error(L, "field '%s' not found", field_name);
}

/* tag_set_field(handle, field_name, value) */
static i32 lua_tag_set_field(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *field_name = luaL_checkstring(L, 2);
    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");
    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (strcmp(f->name, field_name) == 0) {
            set_field_from_lua(L, f, ptr, 3);
            return 0;
        }
        ptr += tag_field_size(f);
    }
    return luaL_error(L, "field '%s' not found", field_name);
}

/* tag_get_script(handle) -> function (or nil) */
static i32 lua_tag_get_script(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");

    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (f->type == TAG_FIELD_REFERENCE && strcmp(f->name, "script") == 0) {
            tag_reference *ref = (tag_reference*)ptr;
            if (ref->handle < 0) { lua_pushnil(L); return 1; }

            struct lua_script_definition *lscr =
                (struct lua_script_definition*)tag_get(ref->handle, TAG_lua_script);
            if (!lscr || !lscr->source.address) { lua_pushnil(L); return 1; }

            char *src = (char*)malloc(lscr->source.count + 1);
            memcpy(src, lscr->source.address, lscr->source.count);
            src[lscr->source.count] = '\0';
            i32 status = luaL_loadstring(L, src);
            free(src);
            if (status != LUA_OK) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return 1;
            }
            return 1;
        }
        ptr += tag_field_size(f);
    }
    lua_pushnil(L);
    return 1;
}

/* tag_get_block_count(handle, block_name) -> count */
static i32 lua_tag_get_block_count(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *block_name = luaL_checkstring(L, 2);
    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");
    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (f->type == TAG_FIELD_BLOCK && strcmp(f->name, block_name) == 0) {
            tag_block *blk = (tag_block*)ptr;
            lua_pushinteger(L, blk->count);
            return 1;
        }
        ptr += tag_field_size(f);
    }
    return luaL_error(L, "block '%s' not found", block_name);
}

/* tag_get_block_field(handle, block_name, index, field_name) -> value */
static i32 lua_tag_get_block_field(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *block_name = luaL_checkstring(L, 2);
    i32 index = (i32)luaL_checkinteger(L, 3);
    const char *field_name = luaL_checkstring(L, 4);

    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");

    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    const tag_block_definition *block_def = NULL;
    tag_block *blk = NULL;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (f->type == TAG_FIELD_BLOCK && strcmp(f->name, block_name) == 0) {
            blk = (tag_block*)ptr;
            block_def = (const tag_block_definition*)f->extra;
            break;
        }
        ptr += tag_field_size(f);
    }
    if (!blk || !block_def) return luaL_error(L, "block '%s' not found", block_name);
    if (index < 0 || (u32)index >= blk->count) return luaL_error(L, "index out of range");

    u8 *elem = (u8*)blk->address + index * block_def->element_size;
    const tag_field_definition *bf;
    for (bf = block_def->fields; bf->type != TAG_FIELD_TERMINATOR; ++bf) {
        if (strcmp(bf->name, field_name) == 0) {
            push_field_value(L, bf, elem);
            return 1;
        }
        elem += tag_field_size(bf);
    }
    return luaL_error(L, "field '%s' not found in block element", field_name);
}

/* tag_set_block_field(handle, block_name, index, field_name, value) */
static i32 lua_tag_set_block_field(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *block_name = luaL_checkstring(L, 2);
    i32 index = (i32)luaL_checkinteger(L, 3);
    const char *field_name = luaL_checkstring(L, 4);

    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");

    u8 *ptr = (u8*)inst->data;
    const tag_field_definition *f;
    const tag_block_definition *block_def = NULL;
    tag_block *blk = NULL;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        if (f->type == TAG_FIELD_BLOCK && strcmp(f->name, block_name) == 0) {
            blk = (tag_block*)ptr;
            block_def = (const tag_block_definition*)f->extra;
            break;
        }
        ptr += tag_field_size(f);
    }
    if (!blk || !block_def) return luaL_error(L, "block '%s' not found", block_name);
    if (index < 0 || (u32)index >= blk->count) return luaL_error(L, "index out of range");

    u8 *elem = (u8*)blk->address + index * block_def->element_size;
    const tag_field_definition *bf;
    for (bf = block_def->fields; bf->type != TAG_FIELD_TERMINATOR; ++bf) {
        if (strcmp(bf->name, field_name) == 0) {
            set_field_from_lua(L, bf, elem, 5);
            return 0;
        }
        elem += tag_field_size(bf);
    }
    return luaL_error(L, "field '%s' not found in block element", field_name);
}

/* ------------------------------------------------------------------------
   Lua registration (add new functions, remove old object_color/shader)
   ------------------------------------------------------------------------ */
static void scenario_register_lua_functions(lua_state *state) {
    lua_register_builtin(state, "vec2",            lua_builtin_vec2);
    lua_register_builtin(state, "vec3",            lua_builtin_vec3);
    lua_register_builtin(state, "vec4",            lua_builtin_vec4);
    lua_register_builtin(state, "sphere",          lua_sphere);
    lua_register_builtin(state, "box",             lua_box);
    lua_register_builtin(state, "dynamic_box",     lua_dynamic_box);
    lua_register_builtin(state, "gravity",         lua_gravity);
    lua_register_builtin(state, "clear",           lua_clear);
    lua_register_builtin(state, "camera_eye",      lua_camera_eye);
    lua_register_builtin(state, "camera_lookat",   lua_camera_lookat);
    lua_register_builtin(state, "camera_fov",      lua_camera_fov);
    lua_register_builtin(state, "light",           lua_light);
    lua_register_builtin(state, "light_direction", lua_light_direction);
    lua_register_builtin(state, "light_color",     lua_light_color);
    lua_register_builtin(state, "light_ambient",   lua_light_ambient);
    lua_register_builtin(state, "shading_mode",    lua_shading_mode);
    lua_register_builtin(state, "clear_color",     lua_clear_color);
    lua_register_builtin(state, "pause_physics",   lua_pause_physics);
    lua_register_builtin(state, "physics_rate",    lua_physics_rate);
    lua_register_builtin(state, "impulse",         lua_impulse);
    lua_register_builtin(state, "velocity",        lua_velocity);
    lua_register_builtin(state, "mass",            lua_mass);
    lua_register_builtin(state, "restitution",     lua_restitution);
    lua_register_builtin(state, "friction",        lua_friction);
    lua_register_builtin(state, "load_scenario",   lua_load_scenario);
    lua_register_builtin(state, "tag_load",        lua_tag_load);
    lua_register_builtin(state, "tag_get_field",   lua_tag_get_field);
    lua_register_builtin(state, "tag_set_field",   lua_tag_set_field);
    lua_register_builtin(state, "tag_get_block_count", lua_tag_get_block_count);
    lua_register_builtin(state, "tag_get_block_field", lua_tag_get_block_field);
    lua_register_builtin(state, "tag_set_block_field", lua_tag_set_block_field);
    lua_register_builtin(state, "tag_get_script",  lua_tag_get_script);

    /* constants */
    lua_set_global_number(state, "SHADE_WIREFRAME", SHADE_WIREFRAME);
    lua_set_global_number(state, "SHADE_FLAT",      SHADE_FLAT);
    lua_set_global_number(state, "SHADE_GOURAUD",   SHADE_GOURAUD);
    lua_set_global_number(state, "SHADE_PHONG",     SHADE_PHONG);
    lua_set_global_number(state, "TAG_material",     TAG_material);
    lua_set_global_number(state, "TAG_model",        TAG_model);
    lua_set_global_number(state, "TAG_entity",       TAG_entity);
    lua_set_global_number(state, "TAG_rigid_body",   TAG_rigid_body);
    lua_set_global_number(state, "TAG_scenario",     TAG_scenario);
    lua_set_global_number(state, "TAG_globals",      TAG_globals);
    lua_set_global_number(state, "TAG_camera",       TAG_camera);
    lua_set_global_number(state, "TAG_lua_script",   TAG_lua_script);
}

static void scenario_bind_lua_state(lua_state *state, void *userdata) {
    (void)userdata;
    scenario_register_lua_functions(state);
}

/* ------------------------------------------------------------------------
   Init / Update / Shutdown
   ------------------------------------------------------------------------ */
static void scenario_init(scenario_world *w, i32 width, i32 height) {
    w->width = width;
    w->height = height;
    w->entity_count = 0;
    w->render_count = 0;
    physics_init(&w->physics, w->entities, SCENARIO_MAX_ENTITIES,
                 vec3_init_from_3(0, -9.8f, 0));
    scripts_init();
    g_scene_world = w;
    if (scripts_add_lua("script.lua", scenario_bind_lua_state, w) < 0)
        fprintf(stderr, "Failed to load Lua script: script.lua\n");
}

static void scenario_update(scenario_world *w, real dt) {
    (void)w;
    scripts_update(dt);
    if (!sc_pause_physics) {
        physics_step(&w->physics, dt);
        /* physics_step already updates entity transforms directly;
           we just need to refresh the render list */
        scenario_update_render_transforms(w);
    }
}

static void scenario_shutdown(scenario_world *w) {
    (void)w;
    scripts_shutdown();
}

static real scenario_get_fixed_dt(void) { return sc_fixed_dt; }

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_H */