#ifndef DEFAULTS_H
#define DEFAULTS_H

#include "reflection.h"
#include "tags/lua_script.h"
#include "tags/material.h"
#include "tags/globals.h"
#include "tags/camera.h"
#include "tags/model.h"
#include "tags/entity.h"
#include "tags/scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

static i32 tag_register_default(const char *name, tag group_tag, const void *data)
{
    fprintf(stderr, "[tag] registering '%s.%c%c%c%c'\n", name, TAG_MAGIC_UNPACK(group_tag));

    tag_group_definition *group;
    i32 handle;

    if (!tag_sys.initialized) return -1;

    group = tag_find_group_internal(group_tag);
    if (!group) return -1;

    handle = tag_alloc_instance(name ? name : "<default>", group);
    if (handle < 0) return -1;

    tag_instance *inst = &tag_sys.instances[handle];
    void *loaded = TAG_MALLOC(group->total_size);
    if (!loaded) {
        tag_sys.instance_count--;
        return -1;
    }
    memcpy(loaded, data, group->total_size);
    inst->backup_data = loaded;
    /* Copy initial data to active before modifications */
    memcpy(inst->active_data, loaded, group->total_size);
    inst->loaded = 1;

    /* reference fix-up */
    if (group->fields) {
        u8 *base = (u8*)loaded;
        const tag_field_definition *f;
        for (f = group->fields; f && f->type != TAG_FIELD_TERMINATOR; ++f) {
            u32 align = tag_field_alignment(f);
            base = (u8*)(((size_t)base + align - 1) & ~(size_t)(align - 1));
            if (f->type != TAG_FIELD_REFERENCE) {
                base += tag_field_size(f);
                continue;
            }
            tag_reference *ref = (tag_reference*)(base);
            if (ref->handle < 0) {
                const char *field_name = f->name;
                if (strcmp(field_name, "globals") == 0) {
                    ref->handle = tag_load("default_globals", TAG_globals);
                } else if (strcmp(field_name, "camera") == 0) {
                    ref->handle = tag_load("default_camera", TAG_camera);
                } else if (strcmp(field_name, "model") == 0) {
                    if (name && strstr(name, "box"))
                        ref->handle = tag_load("default_model_box", TAG_model);
                    else
                        ref->handle = tag_load("default_model_sphere", TAG_model);
                } else if (strcmp(field_name, "rigid_body") == 0) {
                    if (name && strstr(name, "box"))
                        ref->handle = TAG_NULL(i32);
                    else
                        ref->handle = tag_load("default_rigid_body_sphere", TAG_rigid_body);
                }
            }
            base += tag_field_size(f);
        }
    }

    /* Special fix-up for scenario entities block */
    if (group_tag == TAG_scenario) {
        scenario_definition *scn = (scenario_definition*)loaded;
        scn->entities.count = 2; /* Explicitly set the count for the default scenario */
        tag_reference *refs = (tag_reference*)scn->entities.address;
        refs[0].handle = tag_load("default_entity_sphere", TAG_entity);
        refs[1].handle = tag_load("default_entity_box", TAG_entity);
    }

    /* Special fix-up for model materials block */
    if (group_tag == TAG_model) {
        model_definition *mdl = (model_definition*)loaded;
        mdl->materials.count = 1; /* Explicitly set the count for default models */
        tag_reference *refs = (tag_reference*)mdl->materials.address;
        if (name && strstr(name, "sphere"))
            refs[0].handle = tag_load("default_material_metal", TAG_material);
        else if (name && strstr(name, "box"))
            refs[0].handle = tag_load("default_material_rubber", TAG_material);
    }

    /* Update active_data with the modified backup_data */
    memcpy(inst->active_data, inst->backup_data, group->total_size);

    tag_postprocess_tag(handle);
    return handle;
}

static void tag_register_default_all(void){
    /* Set up the tag system if not already done */
    tag_system_init();

    /* Register all tag groups */
    tag_register_group(&material);
    tag_register_group(&model);
    tag_register_group(&entity);
    tag_register_group(&rigid_body);
    tag_register_group(&collision_bsp);
    tag_register_group(&camera);
    tag_register_group(&globals);
    tag_register_group(&scenario);
    tag_register_group(&lua_script);

    /* Default material definitions. */
    tag_register_default("default_material_wireframe",  TAG_material, &DEFAULT_MATERIAL_WIREFRAME);
    tag_register_default("default_material_flat",       TAG_material, &DEFAULT_MATERIAL_FLAT);
    tag_register_default("default_material_gouraud",    TAG_material, &DEFAULT_MATERIAL_GOURAUD);
    tag_register_default("default_material_phong",      TAG_material, &DEFAULT_MATERIAL_PHONG);
    tag_register_default("default_material_water",      TAG_material, &DEFAULT_MATERIAL_WATER);
    tag_register_default("default_material_grass",      TAG_material, &DEFAULT_MATERIAL_GRASS);
    tag_register_default("default_material_cloth",      TAG_material, &DEFAULT_MATERIAL_CLOTH);
    tag_register_default("default_material_wood",       TAG_material, &DEFAULT_MATERIAL_WOOD);
    tag_register_default("default_material_metal",      TAG_material, &DEFAULT_MATERIAL_METAL);
    tag_register_default("default_material_glass",      TAG_material, &DEFAULT_MATERIAL_GLASS);
    tag_register_default("default_material_skin",       TAG_material, &DEFAULT_MATERIAL_SKIN);
    tag_register_default("default_material_rubber",     TAG_material, &DEFAULT_MATERIAL_RUBBER);
    tag_register_default("default_material_ice",        TAG_material, &DEFAULT_MATERIAL_ICE);
    tag_register_default("default_material_stone",      TAG_material, &DEFAULT_MATERIAL_STONE);
    tag_register_default("default_material_lava",       TAG_material, &DEFAULT_MATERIAL_LAVA);
    tag_register_default("default_material_toon",       TAG_material, &DEFAULT_MATERIAL_TOON);
    tag_register_default("default_material_hologram",   TAG_material, &DEFAULT_MATERIAL_HOLOGRAM);
    tag_register_default("default_material_iridescent", TAG_material, &DEFAULT_MATERIAL_IRIDESCENT);
    tag_register_default("default_material_plastic",    TAG_material, &DEFAULT_MATERIAL_PLASTIC);
    tag_register_default("default_material_brick",      TAG_material, &DEFAULT_MATERIAL_BRICK);
    tag_register_default("default_material_leather",    TAG_material, &DEFAULT_MATERIAL_LEATHER);
    tag_register_default("default_material_gold",       TAG_material, &DEFAULT_MATERIAL_GOLD);
    tag_register_default("default_material_snow",       TAG_material, &DEFAULT_MATERIAL_SNOW);
    tag_register_default("default_material_dirt",       TAG_material, &DEFAULT_MATERIAL_DIRT);
    tag_register_default("default_material_neon",       TAG_material, &DEFAULT_MATERIAL_NEON);
    tag_register_default("default_material_velvet",     TAG_material, &DEFAULT_MATERIAL_VELVET);
    tag_register_default("default_material_marble",     TAG_material, &DEFAULT_MATERIAL_MARBLE);
    tag_register_default("default_material_wax",        TAG_material, &DEFAULT_MATERIAL_WAX);
    tag_register_default("default_material_pearl",      TAG_material, &DEFAULT_MATERIAL_PEARL);
    tag_register_default("default_material_ceramic",    TAG_material, &DEFAULT_MATERIAL_CERAMIC);
    tag_register_default("default_material_chalk",      TAG_material, &DEFAULT_MATERIAL_CHALK);
    tag_register_default("default_material_posterized", TAG_material, &DEFAULT_MATERIAL_POSTERIZED);
    tag_register_default("default_material_frost",      TAG_material, &DEFAULT_MATERIAL_FROST);
    tag_register_default("default_material_rust",       TAG_material, &DEFAULT_MATERIAL_RUST);
    tag_register_default("default_material_carbon",     TAG_material, &DEFAULT_MATERIAL_CARBON);
    tag_register_default("default_material_chrome",     TAG_material, &DEFAULT_MATERIAL_CHROME);
    tag_register_default("default_material_emerald",    TAG_material, &DEFAULT_MATERIAL_EMERALD);
    tag_register_default("default_material_oilslick",   TAG_material, &DEFAULT_MATERIAL_OILSLICK);

    /* Default asset tags (defined in their respective tag headers) */
    tag_register_default("default_globals",   TAG_globals,   &DEFAULT_GLOBALS);
    tag_register_default("default_camera",    TAG_camera,    &DEFAULT_CAMERA);

    /* Register models and rigid bodies first, since entities reference them */
    tag_register_default("default_rigid_body_sphere",   TAG_rigid_body, &DEFAULT_RIGID_BODY_SPHERE);
    tag_register_default("default_model_sphere",        TAG_model,      &DEFAULT_MODEL_SPHERE);
    tag_register_default("default_rigid_body_box",      TAG_rigid_body, &DEFAULT_RIGID_BODY_BOX);
    tag_register_default("default_model_box",           TAG_model,      &DEFAULT_MODEL_BOX);

    /* Then entities */
    tag_register_default("default_entity_sphere",   TAG_entity,    &DEFAULT_ENTITY_SPHERE);
    tag_register_default("default_entity_box",      TAG_entity,    &DEFAULT_ENTITY_BOX);

    /* Register the default scenario */
    tag_register_default("default_scenario",  TAG_scenario,  &DEFAULT_SCENARIO);
}

#ifdef __cplusplus
}
#endif

#endif /* DEFAULTS_H */
