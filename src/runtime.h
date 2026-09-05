#ifndef RUNTIME_H
#define RUNTIME_H

#include "window.h"
#include "input.h"
#include "common.h"
#include "physics.h"

#define USE_GL
#if defined(USE_DX11)
#define RASTERIZER_DX11_IMPLEMENTATION
#include "rasterizer_DX11.h"
#elif defined(USE_GL)
#define RASTERIZER_GL_IMPLEMENTATION
#include "rasterizer_GL.h"
#else
#define RASTERIZER_SW_IMPLEMENTATION
#include "rasterizer_SW.h"
#endif

#include "scripts.h"
#include "clock.h"
#include "cpu_threads.h"
#include "defaults.h"

#include "tags/model.h"
#include "tags/entity.h"
#include "tags/rigid_body.h"
#include "tags/collision_bsp.h"
#include "tags/scenario.h"
#include "tags/globals.h"
#include "tags/camera.h"
#include "tags/lua_script.h"
#include "tags/particle_emitter.h"

#include "toolbox/model_importer.h"
#include "toolbox/cbsp_builder.h"

#include "../libs/audiopipe/wavloader.h"
#include "../libs/audiopipe/audiomixer.h"
#include "../libs/audiopipe/audiopipe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENARIO_MAX_ENTITIES PHYSICS_MAX_BODIES
#define SCENARIO_WINDOW_SCALE 4
#define SCENARIO_DEFAULT_WIDTH  (256 * SCENARIO_WINDOW_SCALE)
#define SCENARIO_DEFAULT_HEIGHT (144 * SCENARIO_WINDOW_SCALE)

/* ------------------------------------------------------------------------
   Globals used by Lua and render loop
   ------------------------------------------------------------------------ */
static vec3 light_dir, light_col, ambient_col;

/* ------------------------------------------------------------------------
   Scenario world
   ------------------------------------------------------------------------ */
typedef struct scenario_world {
    physics_world physics;

    /* Pointers to actual entity data (either Tag Instance memory or pool) */
    entity_definition *entities[SCENARIO_MAX_ENTITIES];
    i32               entity_count;

    /* Backing store for entities created at runtime that aren't Tags */
    entity_definition entity_pool[SCENARIO_MAX_ENTITIES];
} scenario_world;

static scenario_world *g_scene_world = NULL;
static scenario_world g_world_storage;

/* Default render settings (overridden by globals / tags) */
static vec3 sc_cam_eye    = {0, 0, 0};
static vec3 sc_cam_center = {0, 0, -1};
static vec3 sc_cam_up     = {0, 1, 0};
static real sc_cam_fov    = 90.0f;
static u8   sc_clear_r = 16, sc_clear_g = 24, sc_clear_b = 40;
static i32  sc_pause_physics = 0;
static real sc_fixed_dt = 1.0f / 60.0f;

/* ---- Debug: track printed materials ---- */
static int g_printed_materials[256];
static int g_print_material_count = 0;

/* FIX: Window resize tracking */
static int g_last_width = 0;
static int g_last_height = 0;

/* ---- Particle system state ---- */
static i32 g_particle_emitter_handle = -1;   /* current emitter tag handle */

/* ========================================================================
   AUDIO SYSTEM STATE (UPDATED FOR GEOMETRY)
   ======================================================================== */
#define MAX_AUDIO_VOICES AP_MAX_MIXER_VOICES

typedef struct audio_voice_slot {
    voice_t voice;
    float *samples;               /* pointer to loaded sample data (owned by this slot) */
    int   sample_frames;
    int   channels;
    int   sample_rate;
    int   loaded;                 /* 1 if slot contains a valid sample */
} audio_voice_slot_t;

static audio_voice_slot_t g_audio_slots[MAX_AUDIO_VOICES];
static effect_mixer_ctx_t g_audio_ctx;
static int g_audio_initialized = 0;

/* ---- Per‑frame voice positions for geometry analysis ---- */
static vec3 g_audio_voice_positions[MAX_AUDIO_VOICES];
static int g_audio_active_count = 0;

/* ========================================================================
   NEW HELPER: apply GPU propagation results to a mixer voice
   ======================================================================== */
static inline void audio_voice_set_propagation(voice_t *v, const audio_propagation_output_t *prop) {
    if (!v || !prop) return;
    v->occlusion = prop->occlusion;
}

/* ------------------------------------------------------------------------
   Lua helper - return the internal physics_body for a given entity index,
   or NULL if it isn't dynamic.
   ------------------------------------------------------------------------ */
static physics_body *scenario_get_physics_body(i32 entity_index) {
    i32 idx = g_scene_world->physics.entity_to_body[entity_index];
    if (idx < 0 || idx >= g_scene_world->physics.body_count) return NULL;
    return &g_scene_world->physics.bodies[idx];
}

/* Helper to find field offset to avoid pointer drift if possible,
   though our simple system relies on sequential FIELD definitions matching C structs. */
static void* get_field_ptr(void* struct_base, const tag_field_definition* fields, const char* name) {
    u8* ptr = (u8*)struct_base;
    const tag_field_definition* f;
    for (f = fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
        if (strcmp(f->name, name) == 0) return ptr;
        ptr += tag_field_size(f);
    }
    return NULL;
}

/* ------------------------------------------------------------------------
   Load a scenario tag and populate the world
   ------------------------------------------------------------------------ */
static i32 scenario_load_tag(const char *scenario_name) {
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
            physics_init(&g_scene_world->physics, g_scene_world->entities, SCENARIO_MAX_ENTITIES, g->gravity);
            sc_pause_physics = g->pause_physics;
            sc_fixed_dt = 1.0f / g->physics_rate;
            sc_clear_r = color_to_u8(g->clear_color.position.x);
            sc_clear_g = color_to_u8(g->clear_color.position.y);
            sc_clear_b = color_to_u8(g->clear_color.position.z);
            render_set_fog(g->fog_color, g->fog_start, g->fog_end);
            render_set_light(g->light_dir, g->light_col, g->ambient_col);

            /* DEBUG: Print globals values */
            printf("Globals loaded:\n");
            printf("  Light dir: (%f, %f, %f)\n", g->light_dir.position.x, g->light_dir.position.y, g->light_dir.position.z);
            printf("  Light col: (%f, %f, %f)\n", g->light_col.position.x, g->light_col.position.y, g->light_col.position.z);
            printf("  Ambient col: (%f, %f, %f)\n", g->ambient_col.position.x, g->ambient_col.position.y, g->ambient_col.position.z);
            printf("  Clear color: (%f, %f, %f)\n", g->clear_color.position.x, g->clear_color.position.y, g->clear_color.position.z);
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
    g_scene_world->entity_count = 0;

    tag_reference *entity_refs = (tag_reference*)scn->entities.address;

    /* Temporary: try to read the first two handles before the loop */
    if (scn->entities.count >= 1) {
        fprintf(stderr, "  entity[0] handle = %d\n", entity_refs[0].handle);
    }
    if (scn->entities.count >= 2) {
        fprintf(stderr, "  entity[1] handle = %d\n", entity_refs[1].handle);
    }

    u32 i;
    for (i = 0; i < scn->entities.count && g_scene_world->entity_count < SCENARIO_MAX_ENTITIES; ++i) {
        i32 ent_handle = entity_refs[i].handle;
        if (ent_handle < 0) continue;

        entity_definition *src = (entity_definition*)tag_get(ent_handle, TAG_entity);
        if (!src) continue;

        i32 ent_idx = g_scene_world->entity_count;
        g_scene_world->entities[ent_idx] = src; /* Point directly to tag data */

        if (src->rigid_body.handle >= 0) {
            physics_add_entity(&g_scene_world->physics, ent_idx);
        }
        g_scene_world->entity_count++;
    }
    g_scene_world->physics.entity_count = g_scene_world->entity_count;
    return scn_handle;
}

/* ------------------------------------------------------------------------
   Draw one model primitive with its material
   ------------------------------------------------------------------------ */
static void scenario_draw_primitive(model_primitive *prim, model_definition *mod,
                                    vec3 pos, vec4 orient) {
    if (prim->vertices.count == 0 || prim->indices.count == 0) return;

    material_definition *mat = NULL;
    i32 resolved_mat_handle = -1;

    if (prim->material_index >= 0 && mod->materials.address) {
        tag_reference *refs = (tag_reference*)mod->materials.address;
        i32 mat_handle = refs[prim->material_index].handle;
        resolved_mat_handle = mat_handle;
        if (mat_handle >= 0)
            mat = (material_definition*)tag_get(mat_handle, TAG_material);
    }

    if (!mat) {
        static material_definition fallback = DEFAULT_MATERIAL_WIREFRAME;
        mat = &fallback;
    }

    /* ---- DEBUG: Print material info once per unique material ---- */
    {
        int already_printed = 0;
        int j;
        for (j = 0; j < g_print_material_count; j++) {
            if (g_printed_materials[j] == resolved_mat_handle) {
                already_printed = 1;
                break;
            }
        }
        if (!already_printed && g_print_material_count < 256) {
            g_printed_materials[g_print_material_count++] = resolved_mat_handle;
            printf("Primitive material: handle=%d color=(%f,%f,%f) render_method=0x%x ambient_factor=%f\n",
                   resolved_mat_handle,
                   mat->color.color.r, mat->color.color.g, mat->color.color.b,
                   mat->render_method,
                   mat->ambient_light_factor);
        }
    }

    model_vertex *verts = (model_vertex*)prim->vertices.address;
    u16 *indices = (u16*)prim->indices.address;
    u32 tri_count = prim->indices.count / 3;

    u32 t;
    for (t = 0; t < tri_count; ++t) {
        u16 i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
        vec3 local_v0 = verts[i0].position, local_v1 = verts[i1].position, local_v2 = verts[i2].position;
        vec3 v0 = local_v0, v1 = local_v1, v2 = local_v2;
        vec3 n0 = verts[i0].normal,   n1 = verts[i1].normal,   n2 = verts[i2].normal;

        /* Apply entity transform */
        v0 = vec3_add(pos, quat_rotate_vec3(orient, v0));
        v1 = vec3_add(pos, quat_rotate_vec3(orient, v1));
        v2 = vec3_add(pos, quat_rotate_vec3(orient, v2));
        n0 = quat_rotate_vec3(orient, n0);
        n1 = quat_rotate_vec3(orient, n1);
        n2 = quat_rotate_vec3(orient, n2);

        draw_triangle_shaded(v0, v1, v2, n0, n1, n2,
                             local_v0, local_v1, local_v2,
                             mat);
    }
}

/* ------------------------------------------------------------------------
   Main render call
   ------------------------------------------------------------------------ */
static void scenario_render(void) {
    real aspect = (real)render_get_render_width() / (real)render_get_render_height();

    render_set_camera(sc_cam_eye, sc_cam_center, sc_cam_up,
                      sc_cam_fov * VECTORS_DEG2RAD, aspect);
    render_set_light(light_dir, light_col, ambient_col);
    render_clear(sc_clear_r, sc_clear_g, sc_clear_b);

    render_draw_entities(g_scene_world->entities, g_scene_world->entity_count);

    /* ---- Set particle camera ---- */
    vec3 forward = vec3_normalize(vec3_sub(sc_cam_center, sc_cam_eye));
    vec3 right = vec3_normalize(vec3_cross(forward, sc_cam_up));
    vec3 up = vec3_normalize(vec3_cross(right, forward));
    render_particle_system_set_camera(&gl_view_proj, right, up);

    render_finish();
}

/* ====================================================================
   Lua runtime API - adapted to entity‑based physics
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

static i32 lua_clear(lua_State *L) {
    (void)L;
    g_scene_world->physics.body_count = 0;
    g_scene_world->physics.entity_count = 0;
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
    light_dir = dir;
    light_col = col;
    ambient_col = amb;
    render_set_light(light_dir, light_col, ambient_col);
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
    light_dir = dir;
    render_set_light(light_dir, light_col, ambient_col);
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
    light_col = col;
    render_set_light(light_dir, light_col, ambient_col);
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
    ambient_col = amb;
    render_set_light(light_dir, light_col, ambient_col);
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

static i32 lua_load_scenario(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    i32 handle = scenario_load_tag(name);
    if (handle < 0) return luaL_error(L, "failed to load scenario '%s'", name);
    lua_pushinteger(L, handle);
    return 1;
}

/* ------------------------------------------------------------------------
   Tag reflection Lua API (unchanged)
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
    void *ptr = field_ptr;
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
        case TAG_FIELD_REFERENCE: {
            i32 ref_handle = (i32)luaL_checkinteger(L, value_index);
            tag_reference_definition *ref_def = (tag_reference_definition*)f->extra;
            if (ref_handle >= 0 && ref_def && !tag_get(ref_handle, ref_def->allowed_group_tag)) {
                luaL_error(L, "invalid reference handle for field '%s'", f->name);
            }
            ((tag_reference*)ptr)->handle = ref_handle;
            break;
        }
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

/* import_model(path [, material_handle]) -> model tag handle */
static i32 lua_import_model(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    i32 material_handle = -1;
    i32 handle;

    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        material_handle = (i32)luaL_checkinteger(L, 2);

    handle = model_importer_import_model_with_material(path, material_handle);
    if (handle < 0) {
        const char *error = model_importer_last_error();
        if (!error || error[0] == '\0')
            error = "unknown importer error";
        return luaL_error(L, "failed to import model '%s': %s", path, error);
    }

    lua_pushinteger(L, handle);
    return 1;
}

/* build_cbsp(model_handle) -> cbsp tag handle */
static i32 lua_build_cbsp(lua_State *L)
{
    i32 model_handle = (i32)luaL_checkinteger(L, 1);
    model_definition *model = (model_definition*)tag_get(model_handle, TAG_model);
    if (!model) {
        fprintf(stderr, "[lua_build_cbsp] invalid model handle %d\n", model_handle);
        return luaL_error(L, "invalid model handle");
    }

    fprintf(stderr, "[lua_build_cbsp] model handle=%d, primitives.count=%u\n", model_handle, model->primitives.count);

    collision_bsp_definition *cbsp = cbsp_build_from_model(model);
    if (!cbsp) {
        fprintf(stderr, "[lua_build_cbsp] cbsp_build_from_model returned NULL\n");
        return luaL_error(L, "failed to build cbsp from model (no triangles?)");
    }

    fprintf(stderr, "[lua_build_cbsp] BSP built: triangles=%u\n", cbsp->triangles.count);

    tag_group_definition *group = tag_find_group_internal(TAG_collision_bsp);
    if (!group) {
        fprintf(stderr, "[lua_build_cbsp] tag group not found\n");
        cbsp_free(cbsp);
        return luaL_error(L, "could not find cbsp tag group");
    }

    i32 cbsp_handle = tag_alloc_instance("", group);
    if (cbsp_handle < 0) {
        fprintf(stderr, "[lua_build_cbsp] tag_alloc_instance failed\n");
        cbsp_free(cbsp);
        return luaL_error(L, "could not allocate cbsp tag instance");
    }

    tag_instance *inst = &tag_sys.instances[cbsp_handle];
    collision_bsp_definition *dst = (collision_bsp_definition*)inst->active_data;
    dst->triangles.address = cbsp->triangles.address;
    dst->triangles.count = cbsp->triangles.count;
    dst->bounds = cbsp->bounds;
    inst->loaded = 1;

    cbsp->triangles.address = NULL;
    cbsp_detach(cbsp);

    fprintf(stderr, "[lua_build_cbsp] returning cbsp_handle=%d\n", cbsp_handle);
    lua_pushinteger(L, cbsp_handle);
    return 1;
}

/* tag_get_field(handle, field_name) -> value */
static i32 lua_tag_get_field(lua_State *L)
{
    i32 handle = (i32)luaL_checkinteger(L, 1);
    const char *field_name = luaL_checkstring(L, 2);
    tag_instance *inst = &tag_sys.instances[handle];
    if (!inst->loaded) return luaL_error(L, "tag not loaded");
    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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
    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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

    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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
    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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

    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    const tag_block_definition *block_def = NULL;
    tag_block *blk = NULL;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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
        u32 balign = tag_field_alignment(bf);
        elem = (u8*)(((size_t)elem + balign - 1) & ~(size_t)(balign - 1));
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

    u8 *ptr = (u8*)inst->active_data;
    const tag_field_definition *f;
    const tag_block_definition *block_def = NULL;
    tag_block *blk = NULL;
    for (f = inst->group->fields; f->type != TAG_FIELD_TERMINATOR; ++f) {
        u32 align = tag_field_alignment(f);
        ptr = (u8*)(((size_t)ptr + align - 1) & ~(size_t)(align - 1));
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
        u32 balign = tag_field_alignment(bf);
        elem = (u8*)(((size_t)elem + balign - 1) & ~(size_t)(balign - 1));
        if (strcmp(bf->name, field_name) == 0) {
            set_field_from_lua(L, bf, elem, 5);
            return 0;
        }
        elem += tag_field_size(bf);
    }
    return luaL_error(L, "field '%s' not found in block element", field_name);
}

/* ========================================================================
   Particle Lua bindings
   ======================================================================== */

/* particle_load_emitter(name) -> handle */
static i32 lua_particle_load_emitter(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    i32 handle = tag_load(name, TAG_particle_emitter);
    if (handle < 0) return luaL_error(L, "particle emitter '%s' not found", name);
    g_particle_emitter_handle = handle;
    particle_emitter_definition *def = (particle_emitter_definition*)tag_get(handle, TAG_particle_emitter);
    if (def) {
        render_particle_system_set_emitter(def);
    }
    lua_pushinteger(L, handle);
    return 1;
}

/* particle_set_position(x, y, z) */
static i32 lua_particle_set_position(lua_State *L)
{
    real x = (real)luaL_checknumber(L, 1);
    real y = (real)luaL_checknumber(L, 2);
    real z = (real)luaL_checknumber(L, 3);
    g_emitter_pos = vec3_init_from_3(x, y, z);
    /* Also update the tag if loaded */
    if (g_particle_emitter_handle >= 0) {
        particle_emitter_definition *def = (particle_emitter_definition*)tag_get(g_particle_emitter_handle, TAG_particle_emitter);
        if (def) {
            def->position = g_emitter_pos;
            render_particle_system_set_emitter(def);
        }
    }
    return 0;
}

/* particle_set_color(r, g, b) */
static i32 lua_particle_set_color(lua_State *L)
{
    real r = (real)luaL_checknumber(L, 1);
    real g = (real)luaL_checknumber(L, 2);
    real b = (real)luaL_checknumber(L, 3);
    g_emitter_color = vec3_init_from_3(r, g, b);
    if (g_particle_emitter_handle >= 0) {
        particle_emitter_definition *def = (particle_emitter_definition*)tag_get(g_particle_emitter_handle, TAG_particle_emitter);
        if (def) {
            def->color = g_emitter_color;
            render_particle_system_set_emitter(def);
        }
    }
    return 0;
}

/* particle_set_alpha(a) */
static i32 lua_particle_set_alpha(lua_State *L)
{
    real a = (real)luaL_checknumber(L, 1);
    g_emitter_alpha = a;
    if (g_particle_emitter_handle >= 0) {
        particle_emitter_definition *def = (particle_emitter_definition*)tag_get(g_particle_emitter_handle, TAG_particle_emitter);
        if (def) {
            def->alpha = a;
            render_particle_system_set_emitter(def);
        }
    }
    return 0;
}

/* particle_set_rate(rate) */
static i32 lua_particle_set_rate(lua_State *L)
{
    real rate = (real)luaL_checknumber(L, 1);
    g_emission_rate = rate;
    if (g_particle_emitter_handle >= 0) {
        particle_emitter_definition *def = (particle_emitter_definition*)tag_get(g_particle_emitter_handle, TAG_particle_emitter);
        if (def) {
            def->emission_rate = rate;
            render_particle_system_set_emitter(def);
        }
    }
    return 0;
}

/* particle_emit_burst(count) */
static i32 lua_particle_emit_burst(lua_State *L)
{
    int count = (int)luaL_checkinteger(L, 1);
    if (count <= 0) return 0;
    render_particle_system_emit_burst(count);
    return 0;
}

/* ========================================================================
   Light Lua bindings
   ======================================================================== */

static i32 lua_light_clear(lua_State *L) {
    (void)L;
    render_clear_lights();
    return 0;
}

static i32 lua_light_set_type(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    int type = (int)luaL_checkinteger(L, 2);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
    }
    g_lights[index].type = (enum32)type;
    g_lights[index].enabled = 1;
    return 0;
}

static i32 lua_light_set_position(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
        g_lights[index].enabled = 1;
    }
    vec3 pos = vec3_init_from_3((real)luaL_checknumber(L, 2),
                                (real)luaL_checknumber(L, 3),
                                (real)luaL_checknumber(L, 4));
    g_lights[index].position = pos;
    return 0;
}

static i32 lua_light_set_color(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
        g_lights[index].enabled = 1;
    }
    vec3 col = vec3_init_from_3((real)luaL_checknumber(L, 2),
                                (real)luaL_checknumber(L, 3),
                                (real)luaL_checknumber(L, 4));
    g_lights[index].color = col;
    return 0;
}

static i32 lua_light_set_direction(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
        g_lights[index].enabled = 1;
    }
    vec3 dir = vec3_init_from_3((real)luaL_checknumber(L, 2),
                                (real)luaL_checknumber(L, 3),
                                (real)luaL_checknumber(L, 4));
    g_lights[index].direction = vec3_normalize(dir);
    return 0;
}

static i32 lua_light_set_range(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
        g_lights[index].enabled = 1;
    }
    g_lights[index].range = (real)luaL_checknumber(L, 2);
    return 0;
}

static i32 lua_light_set_spot_params(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
        g_lights[index].enabled = 1;
    }
    g_lights[index].spot_inner_angle = (real)luaL_checknumber(L, 2);
    g_lights[index].spot_outer_angle = (real)luaL_checknumber(L, 3);
    g_lights[index].spot_falloff = (real)luaL_checknumber(L, 4);
    return 0;
}

static i32 lua_light_set_enabled(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= RENDER_MAX_LIGHTS)
        return luaL_error(L, "light index out of range");
    if (index >= g_light_count) {
        g_light_count = index + 1;
        memset(&g_lights[index], 0, sizeof(light_definition));
    }
    g_lights[index].enabled = (bool)lua_toboolean(L, 2);
    return 0;
}

/* ========================================================================
   Audio Lua bindings
   ======================================================================== */

static i32 lua_audio_load_wav(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    int sample_rate, channels, num_frames;
    float *samples = load_wav(filename, &sample_rate, &channels, &num_frames);
    if (!samples) {
        lua_pushinteger(L, -1);
        return 1;
    }
    // Find a free slot
    int slot = -1;
    for (int i = 0; i < MAX_AUDIO_VOICES; i++) {
        if (!g_audio_slots[i].loaded) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        free(samples);
        lua_pushinteger(L, -1);
        return 1;
    }
    audio_voice_slot_t *s = &g_audio_slots[slot];
    s->samples = samples;
    s->sample_frames = num_frames;
    s->channels = channels;
    s->sample_rate = sample_rate;
    s->loaded = 1;

    // Initialize voice_t
    voice_t *v = &s->voice;
    memset(v, 0, sizeof(voice_t));
    v->samples = samples;
    v->num_frames = num_frames;
    v->channels = channels;
    v->source_sample_rate = sample_rate;
    v->position = 0.0;
    v->pitch = 1.0f;
    v->volume = 1.0f;
    v->rolloff = 1.0f;
    v->active = 0; // not playing yet
    v->looping = 0;
    memset(v->reflection_buffer, 0, sizeof(v->reflection_buffer));
    v->reflection_write_pos = 0;

    lua_pushinteger(L, slot);
    return 1;
}

static i32 lua_audio_play(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    int loop = lua_toboolean(L, 2);
    voice_t *v = &g_audio_slots[slot].voice;
    v->position = 0.0;
    v->active = 1;
    v->releasing = 0;
    v->looping = loop ? 1 : 0;
    return 0;
}

static i32 lua_audio_stop(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->active = 0;
    v->releasing = 0;
    return 0;
}

static i32 lua_audio_set_position(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->pos_x = (float)luaL_checknumber(L, 2);
    v->pos_y = (float)luaL_checknumber(L, 3);
    v->pos_z = (float)luaL_checknumber(L, 4);
    return 0;
}

static i32 lua_audio_set_velocity(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->vel_x = (float)luaL_checknumber(L, 2);
    v->vel_y = (float)luaL_checknumber(L, 3);
    v->vel_z = (float)luaL_checknumber(L, 4);
    return 0;
}

static i32 lua_audio_set_listener_velocity(lua_State *L) {
    g_audio_ctx.listener.vel_x = (float)luaL_checknumber(L, 1);
    g_audio_ctx.listener.vel_y = (float)luaL_checknumber(L, 2);
    g_audio_ctx.listener.vel_z = (float)luaL_checknumber(L, 3);
    return 0;
}

static i32 lua_audio_set_volume(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->volume = (float)luaL_checknumber(L, 2);
    return 0;
}

static i32 lua_audio_set_pitch(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->pitch = (float)luaL_checknumber(L, 2);
    return 0;
}

static i32 lua_audio_set_rolloff(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= MAX_AUDIO_VOICES || !g_audio_slots[slot].loaded)
        return luaL_error(L, "invalid audio slot");
    voice_t *v = &g_audio_slots[slot].voice;
    v->rolloff = (float)luaL_checknumber(L, 2);
    return 0;
}

/* ========================================================================
   Lua registration
   ======================================================================== */
static void runtime_register_lua_functions(lua_state *state) {
    lua_register_builtin(state, "clear",                lua_clear);
    lua_register_builtin(state, "camera_eye",           lua_camera_eye);
    lua_register_builtin(state, "camera_lookat",        lua_camera_lookat);
    lua_register_builtin(state, "camera_fov",           lua_camera_fov);
    lua_register_builtin(state, "light",                lua_light);
    lua_register_builtin(state, "light_direction",      lua_light_direction);
    lua_register_builtin(state, "light_color",          lua_light_color);
    lua_register_builtin(state, "light_ambient",        lua_light_ambient);
    lua_register_builtin(state, "clear_color",          lua_clear_color);
    
    lua_register_builtin(state, "import_model",         lua_import_model);
    lua_register_builtin(state, "build_cbsp",           lua_build_cbsp);
    
    lua_register_builtin(state, "load_scenario",        lua_load_scenario);
    lua_register_builtin(state, "tag_load",             lua_tag_load);
    lua_register_builtin(state, "tag_get_field",        lua_tag_get_field);
    lua_register_builtin(state, "tag_set_field",        lua_tag_set_field);
    lua_register_builtin(state, "tag_get_block_count",  lua_tag_get_block_count);
    lua_register_builtin(state, "tag_get_block_field",  lua_tag_get_block_field);
    lua_register_builtin(state, "tag_set_block_field",  lua_tag_set_block_field);
    lua_register_builtin(state, "tag_get_script",       lua_tag_get_script);

    /* Particle bindings */
    lua_register_builtin(state, "particle_load_emitter",    lua_particle_load_emitter);
    lua_register_builtin(state, "particle_set_position",    lua_particle_set_position);
    lua_register_builtin(state, "particle_set_color",       lua_particle_set_color);
    lua_register_builtin(state, "particle_set_alpha",       lua_particle_set_alpha);
    lua_register_builtin(state, "particle_set_rate",        lua_particle_set_rate);
    lua_register_builtin(state, "particle_emit_burst",      lua_particle_emit_burst);

    /* Light bindings */
    lua_register_builtin(state, "light_clear",          lua_light_clear);
    lua_register_builtin(state, "light_set_type",       lua_light_set_type);
    lua_register_builtin(state, "light_set_position",   lua_light_set_position);
    lua_register_builtin(state, "light_set_color",      lua_light_set_color);
    lua_register_builtin(state, "light_set_direction",  lua_light_set_direction);
    lua_register_builtin(state, "light_set_range",      lua_light_set_range);
    lua_register_builtin(state, "light_set_spot_params", lua_light_set_spot_params);
    lua_register_builtin(state, "light_set_enabled",    lua_light_set_enabled);

    /* Audio bindings */
    lua_register_builtin(state, "audio_load_wav",       lua_audio_load_wav);
    lua_register_builtin(state, "audio_play",           lua_audio_play);
    lua_register_builtin(state, "audio_stop",           lua_audio_stop);
    lua_register_builtin(state, "audio_set_position",   lua_audio_set_position);
    lua_register_builtin(state, "audio_set_velocity",   lua_audio_set_velocity);
    lua_register_builtin(state, "audio_set_listener_velocity", lua_audio_set_listener_velocity);
    lua_register_builtin(state, "audio_set_volume",     lua_audio_set_volume);
    lua_register_builtin(state, "audio_set_pitch",      lua_audio_set_pitch);
    lua_register_builtin(state, "audio_set_rolloff",    lua_audio_set_rolloff);

    lua_register_builtin(state, "vec2",                 lua_builtin_vec2);
    lua_register_builtin(state, "vec3",                 lua_builtin_vec3);
    lua_register_builtin(state, "vec4",                 lua_builtin_vec4);

    /* Constants */
    lua_set_global_integer(state, "MODE_WIREFRAME",    MODE_WIREFRAME);
    lua_set_global_integer(state, "MODE_FLAT",         MODE_FLAT);
    lua_set_global_integer(state, "MODE_GOURAUD",      MODE_GOURAUD);
    lua_set_global_integer(state, "MODE_PHONG",        MODE_PHONG);

    lua_set_global_integer(state, "TAG_material",       TAG_material);
    lua_set_global_integer(state, "TAG_model",          TAG_model);
    lua_set_global_integer(state, "TAG_collision_bsp",  TAG_collision_bsp);
    lua_set_global_integer(state, "TAG_particle_emitter", TAG_particle_emitter);
    lua_set_global_integer(state, "TAG_light",          TAG_light);

    lua_set_global_integer(state, "LIGHT_DIRECTIONAL",  LIGHT_DIRECTIONAL);
    lua_set_global_integer(state, "LIGHT_POINT",        LIGHT_POINT);
    lua_set_global_integer(state, "LIGHT_SPOT",         LIGHT_SPOT);
}

static void runtime_bind_lua_state(lua_state *state, void *userdata) {
    (void)userdata;
    runtime_register_lua_functions(state);
}

/* ------------------------------------------------------------------------
   Init / Update / Shutdown
   ------------------------------------------------------------------------ */

static i32 runtime_bootstrap_thread_count(void)
{
    i32 physical = get_physical_core_count();
    i32 logical  = get_logical_thread_count();
    
    if (physical < 1) physical = 1;

    i32 count = physical;

    if (count < 1) count = 1;
    if (count > logical) count = logical;

    fprintf(stderr, "[CPU] %d physical cores, %d logical threads\n",
            (int)physical, (int)logical);
    fprintf(stderr, "[CPU] Selected thread count: %d\n", (int)count);
    return count;
}

/* Forward declaration needed because runtime_init calls runtime_start */
static void runtime_start(void);

static void runtime_init(void) {
    const int window_size = SCENARIO_WINDOW_SCALE;
    const int width  = 256 * window_size;
    const int height = 144 * window_size;

    g_scene_world = &g_world_storage;
    g_scene_world->entity_count = 0;

    if (window_init("Moonaut Engine", width, height) != 0) {
        fprintf(stderr, "Failed to initialise window\n");
        return;
    }

    if (render_init(width, height) == 0) {
        fprintf(stderr, "Failed to initialise renderer\n");
        window_shutdown();
        return;
    }

    /* ---- FORCE BRIGHT AMBIENT FOR TESTING ---- */
    printf("Forcing bright ambient for testing...\n");
    light_dir = vec3_init_from_3(0.0f, -1.0f, -1.0f);
    light_col = vec3_init_from_3(1.0f, 1.0f, 1.0f);
    ambient_col = vec3_init_from_3(0.5f, 0.5f, 0.5f);
    render_set_light(light_dir, light_col, ambient_col);
    /* ------------------------------------------- */

    tag_register_default_all();
    clock_init();

    g_thread_count = runtime_bootstrap_thread_count();
    if (g_thread_count < 1) g_thread_count = 1;
    g_jobgraph = jobgraph_create(g_thread_count);

    physics_init(&g_scene_world->physics, g_scene_world->entities, SCENARIO_MAX_ENTITIES,
                 vec3_init_from_3(0, -9.8f, 0));

    /* ---- Initialize Audio ---- */
    {
        int audio_sample_rate = 48000; /* Will be adjusted by the device */
        int audio_channels = 2;
        int audio_buffer_frames = 1056;

        memset(&g_audio_ctx, 0, sizeof(g_audio_ctx));
        g_audio_ctx.sample_rate = audio_sample_rate;
        g_audio_ctx.count = MAX_AUDIO_VOICES;
        for (int i = 0; i < MAX_AUDIO_VOICES; i++) {
            g_audio_slots[i].loaded = 0;
            g_audio_slots[i].samples = NULL;
            g_audio_ctx.voices[i] = &g_audio_slots[i].voice;
        }
        /* Listener starts at origin, facing -Z */
        g_audio_ctx.listener.pos_x = 0;
        g_audio_ctx.listener.pos_y = 0;
        g_audio_ctx.listener.pos_z = 0;
        g_audio_ctx.listener.forward_x = 0;
        g_audio_ctx.listener.forward_y = 0;
        g_audio_ctx.listener.forward_z = -1;

        if (ap_init(audio_sample_rate, audio_channels, audio_buffer_frames,
                    effect_mixer, &g_audio_ctx) == 0) {
            printf("Audio initialized successfully.\n");
            g_audio_initialized = 1;
            
            int actual_rate = ap_get_actual_sample_rate();
            if (actual_rate > 0) {
                g_audio_ctx.sample_rate = actual_rate;
                if (g_audio_ctx.reverb.initialized) {
                    g_audio_ctx.reverb.initialized = 0;
                }
            }
            ap_start();
        } else {
            printf("Failed to initialize audio pipe.\n");
        }
    }

    scripts_init();
    if (scripts_add_lua("script.lua", runtime_bind_lua_state, g_scene_world) < 0)
        fprintf(stderr, "Failed to load Lua script: script.lua\n");

    /* ---- Particle system init ---- */
    render_particle_system_init(4096);
    g_particle_emitter_handle = tag_load("default_particle_emitter", TAG_particle_emitter);
    if (g_particle_emitter_handle >= 0) {
        particle_emitter_definition *def = (particle_emitter_definition*)tag_get(g_particle_emitter_handle, TAG_particle_emitter);
        if (def) {
            render_particle_system_set_emitter(def);
            /* Store parameters for Lua access */
            g_emitter_pos = def->position;
            g_emitter_color = def->color;
            g_emitter_alpha = def->alpha;
            g_emitter_size = def->size;
            g_emitter_lifetime = def->lifetime;
            g_emitter_speed = def->speed;
            g_emitter_spread = def->spread;
            g_emitter_gravity = def->gravity;
            g_emitter_loop = def->loop;
            g_emission_rate = def->emission_rate;
        }
    } else {
        fprintf(stderr, "Warning: default_particle_emitter not found, using defaults\n");
        /* Set default parameters manually */
        g_emitter_pos = vec3_init_from_3(0,0,0);
        g_emitter_color = vec3_init_from_3(1,0.8f,0.4f);
        g_emitter_alpha = 1.0f;
        g_emitter_size = 0.1f;
        g_emitter_lifetime = 0.5f;
        g_emitter_speed = 2.0f;
        g_emitter_spread = 0.5f;
        g_emitter_gravity = 0.0f;
        g_emitter_loop = 1;
        g_emission_rate = 30.0f;
        particle_emitter_definition tmp = DEFAULT_PARTICLE_EMITTER;
        tmp.position = g_emitter_pos;
        tmp.color = g_emitter_color;
        tmp.alpha = g_emitter_alpha;
        tmp.size = g_emitter_size;
        tmp.lifetime = g_emitter_lifetime;
        tmp.speed = g_emitter_speed;
        tmp.spread = g_emitter_spread;
        tmp.gravity = g_emitter_gravity;
        tmp.loop = g_emitter_loop;
        tmp.emission_rate = g_emission_rate;
        render_particle_system_set_emitter(&tmp);
    }

    runtime_start();
}

static void runtime_reconfigure_thread_count(i32 new_thread_count)
{
    if (new_thread_count < 1) new_thread_count = 1;
    if (g_thread_count == new_thread_count && g_jobgraph != ((void*)0)) {
        return;
    }

    if (g_jobgraph) {
        jobgraph_destroy(g_jobgraph);
        g_jobgraph = ((jobgraph_t*)0);
    }

    g_thread_count = new_thread_count;
    g_jobgraph = jobgraph_create(g_thread_count);
}

static void runtime_shutdown(void) {
    /* ---- Audio shutdown ---- */
    ap_stop();
    ap_shutdown();
    for (int i = 0; i < MAX_AUDIO_VOICES; i++) {
        if (g_audio_slots[i].samples) {
            free(g_audio_slots[i].samples);
            g_audio_slots[i].samples = NULL;
        }
        g_audio_slots[i].loaded = 0;
    }
    g_audio_initialized = 0;

    render_particle_system_shutdown();
    scripts_shutdown();
    if (g_jobgraph) {
        jobgraph_destroy(g_jobgraph);
        g_jobgraph = ((jobgraph_t*)0);
    }
    render_shutdown();
    window_shutdown();
}

static real runtime_get_fixed_dt(void) { return sc_fixed_dt; }

/* ------------------------------------------------------------------------
   FPS counter (debug)
   ------------------------------------------------------------------------ */
static void print_fps(double now)
{
    static double last = 0.0;
    static int frames = 0;

    if (last == 0.0) last = now;

    frames++;
    {
        double elapsed = now - last;
        if (elapsed >= 1.0) {
            printf("FPS: %d\n", (int)(frames / elapsed));
            frames = 0;
            last = now;
        }
    }
}

/* ------------------------------------------------------------------------
   runtime_start()  -  main game loop (fixed‑timestep accumulator)
   ------------------------------------------------------------------------ */
static void runtime_start(void)
{
    scenario_world *w = g_scene_world;
    const double max_frame_time = 0.25;
    double last_time = clock_monotonic();
    double accumulator = 0.0;

    /* FIX: Initialize last known window size */
    C89FW_window_t* win = window_get();
    if (win) {
        g_last_width = win->width;
        g_last_height = win->height;
    }

    while (is_running()) {
        real fixed_dt;
        double now;
        double frame_time;
        i32 step_count;

        input_process_events(window_get());

        /* FIX: Handle window resize */
        win = window_get();
        if (win && (win->width != g_last_width || win->height != g_last_height)) {
            g_last_width = win->width;
            g_last_height = win->height;
            render_resize(g_last_width, g_last_height);
        }

        now = clock_monotonic();
        frame_time = now - last_time;
        last_time = now;
        if (frame_time < 0.0) frame_time = 0.0;
        if (frame_time > max_frame_time) frame_time = max_frame_time;

        fixed_dt = runtime_get_fixed_dt();
        accumulator += frame_time;
        step_count = 0;
        while (accumulator >= fixed_dt) {
            
            scripts_update(fixed_dt);
            if (!sc_pause_physics) {
                physics_step(&g_scene_world->physics, fixed_dt);
            }
            /* Update particles */
            render_particle_system_update((float)fixed_dt);

            accumulator -= fixed_dt;
            step_count++;
            if (step_count >= 15) {
                accumulator = 0.0;
                break;
            }
        }

        /* ================================================================
           AUDIO GEOMETRY – Collect voice positions and dispatch analysis
           ================================================================ */
        /* ---- Collect active audio voice positions ---- */
        g_audio_active_count = 0;
        for (int i = 0; i < MAX_AUDIO_VOICES && g_audio_active_count < MAX_AUDIO_VOICES; i++) {
            voice_t *v = &g_audio_slots[i].voice;
            if (v->active && v->volume > 0.0f) {
                g_audio_voice_positions[g_audio_active_count] = vec3_init_from_3(v->smooth_x, v->smooth_y, v->smooth_z);
                g_audio_active_count++;
            }
        }

        /* ---- Debug: print active voices occasionally ---- */
        static int voice_count_debug_frames = 0;
        if (++voice_count_debug_frames % 60 == 0) {
            /*printf("Active audio voices: %d\n", g_audio_active_count);*/
        }

        #ifdef AUDIO_OCCLUSION
        /* ---- Feed positions to the GPU compute shader ---- */
        render_set_audio_voice_data(g_audio_voice_positions, g_audio_active_count);
        #endif

        /* ---- Render scene (depth pre‑pass triggers audio analysis) ---- */
        render_set_time((real)now);
        scenario_render();

        #ifdef AUDIO_OCCLUSION
        /* ---- Poll audio analysis results and update mixer ---- */
        if (g_audio_active_count > 0) {
            audio_propagation_output_t props[MAX_AUDIO_VOICES];
            int received = render_poll_audio_propagation(props, g_audio_active_count);
            if (received > 0) {
                int i;
                /* Update occlusion for each voice */
                for (i = 0; i < received && i < MAX_AUDIO_VOICES; i++) {
                    voice_t *v = &g_audio_slots[i].voice;
                    audio_voice_set_propagation(v, &props[i]);
                }

                #ifdef AUDIO_PORTAL
                /* Check if any voice is occluded */
                int any_occluded = 0;
                for (i = 0; i < received; i++) {
                    if (props[i].occlusion > 0.05f) {
                        any_occluded = 1;
                        break;
                    }
                }

                /* Trigger portal search if needed */
                if (any_occluded) {
                    render_trigger_portal_search();
                }

                /* Poll portal result (non‑blocking) */
                vec3 portal_pos;
                int portal_active = 0;
                if (render_poll_audio_portal(&portal_pos, &portal_active)) {
                    for (int i = 0; i < received && i < MAX_AUDIO_VOICES; i++) {
                        voice_t *v = &g_audio_slots[i].voice;
                        if (props[i].occlusion > 0.05f && portal_active) {
                            v->portal_active = 1;
                            v->portal_pos_x = portal_pos.position.x;
                            v->portal_pos_y = portal_pos.position.y;
                            v->portal_pos_z = portal_pos.position.z;
                            v->portal_dampening = 0.1f;  /* fixed dampening for now */
                        } else {
                            v->portal_active = 0;
                        }
                    }
                }
                #endif /* AUDIO_PORTAL */
            }
        }
        #endif /* AUDIO_OCCLUSION */

        /* ---- Poll global stats for reverb ---- */
        #ifdef AUDIO_REVERB
        audio_global_stats_t stats;
        if (render_poll_audio_global_stats(&stats) == 1) {
            audio_set_room_geometry(stats.max_depth, stats.avg_depth, stats.variance);
        } else {
            static int stat_fail_count = 0;
            if (++stat_fail_count % 60 == 0) {
                /*printf("Stats not ready yet (waiting for fence)\n");*/
            }
        }
        #endif

        /* ---- Update audio listener and feed pipe ---- */
        if (g_audio_initialized) {
            vec3 forward = vec3_normalize(vec3_sub(sc_cam_center, sc_cam_eye));
            g_audio_ctx.listener.pos_x = sc_cam_eye.position.x;
            g_audio_ctx.listener.pos_y = sc_cam_eye.position.y;
            g_audio_ctx.listener.pos_z = sc_cam_eye.position.z;
            g_audio_ctx.listener.forward_x = forward.position.x;
            g_audio_ctx.listener.forward_y = forward.position.y;
            g_audio_ctx.listener.forward_z = forward.position.z;
            /* Velocities remain zero for now */
            ap_update();
        }

        /* ---- Present frame ---- */
        const u32 *fb = render_get_fb();
        present_frame((void*)fb);

        print_fps(now);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_H */