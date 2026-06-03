#ifndef CBSP_BUILDER_H
#define CBSP_BUILDER_H

#include "../tags/collision_bsp.h"
#include "../tags/model.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

static void cbsp_free(collision_bsp_definition *cbsp);

static collision_bsp_definition* cbsp_build_from_model(model_definition *model)
{
    i32 p;
    u32 total_tris;
    u32 tri_idx;
    collision_bsp_definition *cbsp;
    model_primitive *prim;
    model_vertex *verts;
    u16 *idx;
    u32 i;
    bsp_triangle *t;

    if (!model) return NULL;

    total_tris = 0;
    for (p = 0; p < (i32)model->primitives.count; p++) {
        prim = TAG_BLOCK_GET_ELEMENT(&model->primitives, p, model_primitive);
        if (prim && prim->vertices.address && prim->indices.address) {
            total_tris += prim->indices.count / 3;
        }
    }

    fprintf(stderr, "[build_cbsp] extracted %u triangles\n", total_tris);

    cbsp = (collision_bsp_definition*)TAG_MALLOC(sizeof(collision_bsp_definition));
    if (!cbsp) return NULL;
    memset(cbsp, 0, sizeof(collision_bsp_definition));

    cbsp->planes.address = NULL;
    cbsp->planes.count = 0;
    cbsp->nodes.address = NULL;
    cbsp->nodes.count = 0;
    cbsp->leaves.address = NULL;
    cbsp->leaves.count = 0;

    cbsp->triangles.address = TAG_MALLOC(total_tris * sizeof(bsp_triangle));
    cbsp->triangles.count = 0;

    if (!cbsp->triangles.address) {
        fprintf(stderr, "[build_cbsp] failed to allocate triangles\n");
        cbsp_free(cbsp);
        return NULL;
    }

    tri_idx = 0;
    for (p = 0; p < (i32)model->primitives.count; p++) {
        prim = TAG_BLOCK_GET_ELEMENT(&model->primitives, p, model_primitive);
        if (!prim || !prim->vertices.address || !prim->indices.address) continue;
        verts = (model_vertex*)prim->vertices.address;
        idx = (u16*)prim->indices.address;

        for (i = 0; i + 2 < prim->indices.count; i += 3) {
            t = TAG_BLOCK_GET_ELEMENT(&cbsp->triangles, tri_idx, bsp_triangle);
            t->a = verts[idx[i]].position;
            t->b = verts[idx[i+1]].position;
            t->c = verts[idx[i+2]].position;
            tri_idx++;
        }
    }
    cbsp->triangles.count = tri_idx;
    cbsp->bounds = model->bounding_box;

    fprintf(stderr, "[build_cbsp] BSP built: triangles=%u\n", cbsp->triangles.count);
    return cbsp;
}

static void cbsp_free(collision_bsp_definition *cbsp)
{
    if (!cbsp) return;
    if (cbsp->planes.address) TAG_FREE(cbsp->planes.address);
    if (cbsp->nodes.address) TAG_FREE(cbsp->nodes.address);
    if (cbsp->leaves.address) TAG_FREE(cbsp->leaves.address);
    if (cbsp->triangles.address) TAG_FREE(cbsp->triangles.address);
    TAG_FREE(cbsp);
}

static void cbsp_detach(collision_bsp_definition *cbsp)
{
    if (!cbsp) return;
    TAG_FREE(cbsp);
}

#ifdef __cplusplus
}
#endif

#endif /* CBSP_BUILDER_H */