// #pragma warning (disable : 4133)
// #pragma warning (disable : 4018)

/*
 # This file is part of libkd.
 # Licensed under a 3-clause BSD style license - see LICENSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "compat.h"
#include "kdtree.h"
#include "kdtree_internal_common.h"
#include "kdtree_internal.inc.h"
#include "an-fls.h"

kdtree_t* kdtree_build(kdtree_t* kd, void *data, int N, int D, int Nleaf,
                       int treetype, unsigned int options) {
    return kdtree_build_2(kd, data, N, D, Nleaf, treetype, options,
                          NULL, NULL);
}

static inline u8 node_level(int nodeid) {
    return an_flsB(nodeid + 1);
}

void kdtree_copy_data_double(const kdtree_t* kd, int start, int N, double* dest) {
    int i;
    int d, D;
    D = kd->ndim;
    switch (kdtree_datatype(kd)) {
    case KDT_DATA_DOUBLE:
        memcpy(dest, kd->data.d + start*D,
               N * D * sizeof(double));
        break;
    case KDT_DATA_FLOAT:
        for (i=0; i<(N * D); i++)
            dest[i] = kd->data.f[start*D + i];
        break;
    case KDT_DATA_U32:
        for (i=0; i<N; i++)
            for (d=0; d<D; d++)
                dest[i*D + d] = POINT_INVSCALE(kd, d, kd->data.u[(start + i)*D + d]);
        break;
    case KDT_DATA_U16:
        for (i=0; i<N; i++)
            for (d=0; d<D; d++)
                dest[i*D + d] = POINT_INVSCALE(kd, d, kd->data.s[(start + i)*D + d]);
        break;
    default:
        ANERROR("kdtree_copy_data_double: invalid data type %i", kdtree_datatype(kd));
        return;
    }
}

int kdtree_permute(const kdtree_t* tree, int ind) {
    if (!tree->perm)
        return ind;
    return tree->perm[ind];
}

void kdtree_inverse_permutation(const kdtree_t* tree, int* invperm) {
    int i;
    if (!tree->perm) {
        for (i=0; (u32)i<tree->ndata; i++)
            invperm[i] = i;
    } else {
        for (i=0; (u32)i<tree->ndata; i++) {
            assert((int)(tree->perm[i]) >= 0);
            assert((int)(tree->perm[i]) < tree->ndata);
            invperm[tree->perm[i]] = i;
        }
    }
}

const char* kdtree_build_options_to_string(int opts) {
    static char buf[256];
    sprintf_s(buf, 256, "%s%s%s%s%s",
            (opts & KD_BUILD_BBOX) ? "BBOX ":"",
            (opts & KD_BUILD_SPLIT) ? "SPLIT ":"",
            (opts & KD_BUILD_SPLITDIM) ? "SPLITDIM ":"",
            (opts & KD_BUILD_NO_LR) ? "NOLR ":"",
            (opts & KD_BUILD_LINEAR_LR) ? "LINEARLR ":"");
    return buf;
}

const char* kdtree_kdtype_to_string(int kdtype) {
    switch (kdtype) {
    case KDT_DATA_DOUBLE:
    case KDT_TREE_DOUBLE:
    case KDT_EXT_DOUBLE:
        return "double";
    case KDT_DATA_FLOAT:
    case KDT_TREE_FLOAT:
    case KDT_EXT_FLOAT:
        return "float";
    case KDT_DATA_U32:
    case KDT_TREE_U32:
        return "u32";
    case KDT_DATA_U16:
    case KDT_TREE_U16:
        return "u16";
    default:
        return NULL;
    }
}

int kdtree_kdtype_parse_data_string(const char* str) {
    if (!str) return KDT_DATA_NULL;
    if (!strcmp(str, "double")) {
        return KDT_DATA_DOUBLE;
    } else if (!strcmp(str, "float")) {
        return KDT_DATA_FLOAT;
    } else if (!strcmp(str, "u32")) {
        return KDT_DATA_U32;
    } else if (!strcmp(str, "u16")) {
        return KDT_DATA_U16;
    } else
        return KDT_DATA_NULL;
}

int kdtree_kdtype_parse_tree_string(const char* str) {
    if (!str) return KDT_TREE_NULL;
    if (!strcmp(str, "double")) {
        return KDT_TREE_DOUBLE;
    } else if (!strcmp(str, "float")) {
        return KDT_TREE_FLOAT;
    } else if (!strcmp(str, "u32")) {
        return KDT_TREE_U32;
    } else if (!strcmp(str, "u16")) {
        return KDT_TREE_U16;
    } else
        return KDT_TREE_NULL;
}

int kdtree_kdtype_parse_ext_string(const char* str) {
    if (!str) return KDT_EXT_NULL;
    if (!strcmp(str, "double")) {
        return KDT_EXT_DOUBLE;
    } else if (!strcmp(str, "float")) {
        return KDT_EXT_FLOAT;
    } else
        return KDT_EXT_NULL;
}

int kdtree_kdtypes_to_treetype(int exttype, int treetype, int datatype) {
    // HACK - asserts here...
    return exttype | treetype | datatype;
}

kdtree_t* kdtree_new(int N, int D, int Nleaf) {
    kdtree_t* kd;
    int maxlevel, nnodes;
    maxlevel = kdtree_compute_levels(N, Nleaf);
    kd = calloc(1, sizeof(kdtree_t));
    nnodes = (1 << maxlevel) - 1;
    kd->nlevels = maxlevel;
    kd->ndata = N;
    kd->ndim = D;
    kd->nnodes = nnodes;
    kd->nbottom = 1 << (maxlevel - 1);
    kd->ninterior = kd->nbottom - 1;
    assert(kd->nbottom + kd->ninterior == kd->nnodes);
    return kd;
}

void kdtree_set_limits(kdtree_t* kd, double* low, double* high) {
    int D = kd->ndim;
    if (!kd->minval) {
        kd->minval = malloc(D * sizeof(double));
    }
    if (!kd->maxval) {
        kd->maxval = malloc(D * sizeof(double));
    }
    memcpy(kd->minval, low,  D * sizeof(double));
    memcpy(kd->maxval, high, D * sizeof(double));
}

double kdtree_get_conservative_query_radius(const kdtree_t* kd, double radius) {
    if (!kd->minval) {
        return radius;
    }
    return sqrt(radius* radius + kd->ndim * 0.25 * kd->invscale*kd->invscale);
}

int kdtree_compute_levels(int N, int Nleaf) {
    int nnodes = N / Nleaf;
    int maxlevel = 1;
    while (nnodes) {
        nnodes = nnodes >> 1;
        maxlevel++;
    }
    return maxlevel;
}

int kdtree_nnodes_to_nlevels(int Nnodes) {
    return an_flsB(Nnodes + 1);
}

/* This returns the NODE id (not leaf index) */
int kdtree_first_leaf(const kdtree_t* kd, int nodeid) {
    int dlevel;
    dlevel = (kd->nlevels - 1) - node_level(nodeid);
    return ((nodeid+1) << dlevel) - 1;
}

/* This returns the NODE id (not leaf index) */
int kdtree_last_leaf(const kdtree_t* kd, int nodeid) {
    int dlevel, twodl, nodeid_twodl;
    dlevel = (kd->nlevels - 1) - node_level(nodeid);
    twodl = (1 << dlevel);
    nodeid_twodl = (nodeid << dlevel);
    return nodeid_twodl + (twodl - 1)*2;
}

static int calculate_R(const kdtree_t* kd, int leafid) {
    int l;
    unsigned int mask, N, L;

    mask = (1 << (kd->nlevels-1));
    N = kd->ndata;
    L = 0;
    // Compute the L index of the node one to the right of this node.
    int nextguy = leafid + 1;
    if (nextguy == kd->nbottom)
        return kd->ndata-1;
    for (l=0; l<(kd->nlevels-1); l++) {
        mask /= 2;
        if (nextguy & mask) {
            L += N/2;
            N = (N+1)/2;
        } else {
            N = N/2;
        }
    }
    L--;
    return L;
}

static int linear_lr(const kdtree_t* kd, int leafid) {
    return ((u64)leafid * kd->ndata) / kd->nbottom;
}

int kdtree_leaf_left(const kdtree_t* kd, int nodeid) {
    int leafid = nodeid - kd->ninterior;
    if (!leafid)
        return 0;
    if (kd->has_linear_lr)
        return linear_lr(kd, leafid);
    if (kd->lr)
        return kd->lr[leafid-1] + 1;
    return calculate_R(kd, leafid-1) + 1;
}

int kdtree_leaf_right(const kdtree_t* kd, int nodeid) {
    int leafid = nodeid - kd->ninterior;
    if (kd->has_linear_lr)
        return linear_lr(kd, leafid+1) - 1;
    if (kd->lr)
        return kd->lr[leafid];
    return calculate_R(kd, leafid);
}



int kdtree_left(const kdtree_t* kd, int nodeid) {
    if (KD_IS_LEAF(kd, nodeid)) {
        return kdtree_leaf_left(kd, nodeid);
    } else {
        // leftmost child's L.
        int leftmost = kdtree_first_leaf(kd, nodeid);
        return kdtree_leaf_left(kd, leftmost);
    }
}

int kdtree_right(const kdtree_t* kd, int nodeid) {
    if (KD_IS_LEAF(kd, nodeid)) {
        return kdtree_leaf_right(kd, nodeid);
    } else {
        // rightmost child's R.
        int rightmost = kdtree_last_leaf(kd, nodeid);
        return kdtree_leaf_right(kd, rightmost);
    }
}

int kdtree_is_leaf_node_empty(const kdtree_t* kd, int nodeid) {
    int L, R;
    L = kdtree_leaf_left (kd, nodeid);
    R = kdtree_leaf_right(kd, nodeid);
    return (L == R+1);
}

int kdtree_is_node_empty(const kdtree_t* kd, int nodeid) {
    int L, R;
    L = kdtree_left (kd, nodeid);
    R = kdtree_right(kd, nodeid);
    return (L == R+1);
}

int kdtree_npoints(const kdtree_t* kd, int nodeid) {
    return 1 + kdtree_right(kd, nodeid) - kdtree_left(kd, nodeid);
}

void kdtree_free_query(kdtree_qres_t *kq) {
    if (!kq) return;
    free(kq->results.any);
    free(kq->sdists);
    free(kq->inds);
    free(kq);
}

void kdtree_free(kdtree_t *kd) {
    if (!kd) return;
    free(kd->name);
    free(kd->lr);
    free(kd->perm);
    free(kd->bb.any);
    free(kd->split.any);
    free(kd->splitdim);
    if (kd->free_data)
        free(kd->data.any);
    free(kd->minval);
    free(kd->maxval);
    free(kd);
}

int kdtree_nearest_neighbour(const kdtree_t* kd, const void* pt, double* p_mindist2) {
    return kdtree_nearest_neighbour_within(kd, pt, HUGE_VAL, p_mindist2);
}

KD_DECLARE(kdtree_nn, void, (const kdtree_t* kd, const void* vquery, double* p_bestd2, int* p_ibest));

int kdtree_nearest_neighbour_within(const kdtree_t* kd, const void *pt, double maxd2,
                                    double* p_mindist2) {
    double bestd2 = maxd2;
    int ibest = -1;

    KD_DISPATCH(kdtree_nn, kd->treetype, , (kd, pt, &bestd2, &ibest));

    if (p_mindist2 && (ibest != -1))
        *p_mindist2 = bestd2;
    return ibest;
}

KD_DECLARE(kdtree_node_node_mindist2, double, (const kdtree_t* kd1, int node1, const kdtree_t* kd2, int node2));

double kdtree_node_node_mindist2(const kdtree_t* kd1, int node1,
                                 const kdtree_t* kd2, int node2) {
    double res = HUGE_VAL;
    KD_DISPATCH(kdtree_node_node_mindist2, kd1->treetype, res=, (kd1, node1, kd2, node2));
    return res;
}

KD_DECLARE(kdtree_node_node_maxdist2, double, (const kdtree_t* kd1, int node1, const kdtree_t* kd2, int node2));

double kdtree_node_node_maxdist2(const kdtree_t* kd1, int node1,
                                 const kdtree_t* kd2, int node2) {
    double res = HUGE_VAL;
    KD_DISPATCH(kdtree_node_node_maxdist2, kd1->treetype, res=, (kd1, node1, kd2, node2));
    return res;
}

KD_DECLARE(kdtree_node_node_mindist2_exceeds, int, (const kdtree_t* kd1, int node1, const kdtree_t* kd2, int node2, double maxd2));

int kdtree_node_node_mindist2_exceeds(const kdtree_t* kd1, int node1,
                                      const kdtree_t* kd2, int node2,
                                      double dist2) {
    int res = false;
    KD_DISPATCH(kdtree_node_node_mindist2_exceeds, kd1->treetype, res=, (kd1, node1, kd2, node2, dist2));
    return res;
}

KD_DECLARE(kdtree_node_node_maxdist2_exceeds, int, (const kdtree_t* kd1, int node1, const kdtree_t* kd2, int node2, double maxd2));

int kdtree_node_node_maxdist2_exceeds(const kdtree_t* kd1, int node1,
                                      const kdtree_t* kd2, int node2,
                                      double dist2) {
    int res = false;
    KD_DISPATCH(kdtree_node_node_maxdist2_exceeds, kd1->treetype, res=, (kd1, node1, kd2, node2, dist2));
    return res;
}

KD_DECLARE(kdtree_node_point_mindist2, double, (const kdtree_t* kd, int node, const void* query));

double kdtree_node_point_mindist2(const kdtree_t* kd, int node, const void* pt) {
    double res = HUGE_VAL;
    KD_DISPATCH(kdtree_node_point_mindist2, kd->treetype, res=, (kd, node, pt));
    return res;
}

KD_DECLARE(kdtree_node_point_maxdist2, double, (const kdtree_t* kd, int node, const void* query));

double kdtree_node_point_maxdist2(const kdtree_t* kd, int node, const void* pt) {
    double res = HUGE_VAL;
    KD_DISPATCH(kdtree_node_point_maxdist2, kd->treetype, res=, (kd, node, pt));
    return res;
}

KD_DECLARE(kdtree_node_point_mindist2_exceeds, int, (const kdtree_t* kd, int node, const void* query, double maxd2));

int kdtree_node_point_mindist2_exceeds(const kdtree_t* kd, int node, const void* pt,
                                       double dist2) {
    int res = false;
    KD_DISPATCH(kdtree_node_point_mindist2_exceeds, kd->treetype, res=, (kd, node, pt, dist2));
    return res;
}

KD_DECLARE(kdtree_node_point_maxdist2_exceeds, int, (const kdtree_t* kd, int node, const void* query, double maxd2));

int kdtree_node_point_maxdist2_exceeds(const kdtree_t* kd, int node, const void* pt,
                                       double dist2) {
    int res = false;
    KD_DISPATCH(kdtree_node_point_maxdist2_exceeds, kd->treetype, res=, (kd, node, pt, dist2));
    return res;
}
