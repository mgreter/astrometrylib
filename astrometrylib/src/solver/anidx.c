/*
 # This file is part of the Astrometry.net suite.
 # Licensed under a 3-clause BSD style license - see LICENSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>

#include "anidx.h"
#include "starutil.h"

static void compute_splitbits(kdtree_t* kd)
{
    int D;
    int bits;
    u32 val;
    D = kd->ndim;
    bits = 0;
    val = 1;
    while (D >= 0 && val < (u32)D) {
        bits++;
        val *= 2;
    }
    kd->dimmask = val - 1;
    kd->dimbits = bits;
    kd->splitmask = ~kd->dimmask;
}

struct anidx_header {

    char magic[8];

    uint32_t healpix;
    uint32_t hpnside;

    double scale_u;
    double scale_l;

    uint32_t indexid;

    uint32_t ckdt_ndat;
    uint32_t ckdt_ndim;
    uint32_t ckdt_nnod;
    uint32_t ckdt_type;

    uint32_t skdt_ndat;
    uint32_t skdt_ndim;
    uint32_t skdt_nnod;
    uint32_t skdt_type;

    uint32_t cutnside;
    double cutdedup;
    uint32_t cutnswep;
    uint32_t cutmarg;

    char cxdx;
    char cxdxlt1;
    char circle;
    char ckdt_linl;
    char skdt_linl;
    char padded6;
    char padded7;
    char padded8;

};

struct anidx_table_header {

    uint32_t type;
    uint32_t naxis1;
    uint32_t naxis2;

};

void anidx_load(const char* fname, index_t* index)
{

#ifdef _WIN32

    HANDLE fh = CreateFileA(
        fname,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (!fh) {
        printf("Could not open %s\n", fname);
        exit(1);
    }

    LARGE_INTEGER sz;
    GetFileSizeEx(fh, &sz);

    HANDLE* mmap = CreateFileMappingA(
        fh,
        NULL, //  LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
        PAGE_READONLY, // DWORD flProtect,
        0, // DWORD dwMaximumSizeHigh,
        0, // DWORD dwMaximumSizeLow,
        NULL // LPCSTR lpName
    );

    if (mmap == 0) {
        printf("Could not create mmap %d\n", GetLastError());
        exit(1);
    }

    uint8_t* dataptr = (uint8_t*)(MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, sz.QuadPart));

    if (dataptr == 0) {
        printf("Could not create map view %d\n", GetLastError());
        exit(1);
    }

    index->datamap = mmap;
    index->dataptr = 0;

    CloseHandle(fh);

#else

    FILE* fh = fopen(fname, "rb");
    if (!fh) {
        printf("Could not open %s\n", fname);
        exit(1);
    }
    fseek(fh, 0L, SEEK_END);
    int sz = ftell(fh);
    fseek(fh, 0L, SEEK_SET);
    uint8_t* dataptr = calloc(1, sz);
    index->dataptr = dataptr;
    if (fread(dataptr, sz, 1, fh) != 1) {
        printf("Error reading file %s\n", fname);
        exit(1);
    }
    fseek(fh, 0L, SEEK_SET);

#endif

    struct anidx_header* header = (struct anidx_header*) dataptr;

    int datapos = 96;

    // fread(header, 96, 1, fh);

    index->healpix = header->healpix;
    index->hpnside = header->hpnside;

    index->indexid = header->indexid;
    index->nquads = header->ckdt_ndat;
    index->nstars = header->skdt_ndat;

    index->dimquads = MAX(
        header->ckdt_ndim,
        header->skdt_ndim);

    index->cutdedup = header->cutdedup;
    index->cutnside = header->cutnside;
    index->cutnsweep = header->cutnswep;
    index->cutmargin = header->cutmarg;

    index->index_scale_upper = header->scale_u;
    index->index_scale_lower = header->scale_l;

    index->circle = header->circle == 'T';
    index->cx_less_than_dx = header->cxdx == 'T';
    index->meanx_less_than_half = header->cxdxlt1 == 'T';

    if (index->index_jitter == 0.0) {
        index->index_jitter = DEFAULT_INDEX_JITTER;
    }

    index->codekd = calloc(1, sizeof(kdtree_t));
    index->starkd = calloc(1, sizeof(startree_t));

    index->starkd->tree = calloc(1, sizeof(kdtree_t));

    index->codekd->ndim = header->ckdt_ndim;
    index->codekd->ndata = header->ckdt_ndat;
    index->codekd->nnodes = header->ckdt_nnod;
    index->codekd->treetype = header->ckdt_type;

    index->starkd->tree->ndim = header->skdt_ndim;
    index->starkd->tree->ndata = header->skdt_ndat;
    index->starkd->tree->nnodes = header->skdt_nnod;
    index->starkd->tree->treetype = header->skdt_type;

    index->starkd->tree->has_linear_lr = header->skdt_linl == 'T';
    index->codekd->has_linear_lr = header->ckdt_linl == 'T';

    // index->quads->numstars = header->skdt_ndat;

    for (int i = 0; i < 11; i += 1) {
        struct anidx_table_header* tblhead = (struct anidx_table_header*)(dataptr + datapos);
        datapos += 12;
        uint32_t size = tblhead->naxis1 * tblhead->naxis2;
        if (size == 0) continue;
        void* buffer = dataptr + datapos;
        datapos += size;
        switch (tblhead->type) {
        case 0: // kdtree_header_codes
            printf("Nothing to be read\n");
            break;
        case 1: // kdtree_lr_codes
            index->codekd->lr = buffer;
            break;
        case 2: // kdtree_split_codes
            index->codekd->split.any = buffer;
            break;
        case 3: { // kdtree_range_codes
            double* r = buffer;
            int ndim = index->codekd->ndim;
            index->codekd->minval = r;
            index->codekd->maxval = r + ndim;
            index->codekd->scale = r[ndim * 2];
            index->codekd->invscale = 1.0 / r[ndim * 2];
        } break;
        case 4: // kdtree_data_codes
            index->codekd->data.any = buffer;
            break;
        case 5: // kdtree_header_stars
            printf("Nothing to be read\n");
            break;
        case 6: // kdtree_lr_stars
            index->starkd->tree->lr = buffer;
            break;
        case 7: // kdtree_split_stars
            index->starkd->tree->split.any = buffer;
            break;
        case 8: { // kdtree_range_stars
            double* r = buffer;
            int ndim = index->starkd->tree->ndim;
            index->starkd->tree->minval = r;
            index->starkd->tree->maxval = r + ndim;
            index->starkd->tree->scale = r[ndim * 2];
            index->starkd->tree->invscale = 1.0 / r[ndim * 2];
        } break;
        case 9: // kdtree_data_stars
            index->starkd->tree->data.any = buffer;
            break;
        case 10: // quads
            index->nquads = tblhead->naxis2;
            index->quads = buffer;
            break;
        case 11: // sweep
            index->starkd->sweep = buffer;
            break;
        }

    }

#ifndef _WIN32
    fclose(fh);
#endif

    int nnodes, nbottom;

    nnodes = index->codekd->nnodes;
    index->codekd->nbottom = (nnodes + 1) / 2;
    nbottom = index->codekd->nbottom;
    index->codekd->ninterior = nnodes - nbottom;
    index->codekd->nlevels = kdtree_nnodes_to_nlevels(nnodes);

    nnodes = index->starkd->tree->nnodes;
    index->starkd->tree->nbottom = (nnodes + 1) / 2;
    nbottom = index->starkd->tree->nbottom;
    index->starkd->tree->ninterior = nnodes - nbottom;
    index->starkd->tree->nlevels = kdtree_nnodes_to_nlevels(nnodes);

    index->index_scale_upper = rad2arcsec(index->index_scale_upper);
    index->index_scale_lower = rad2arcsec(index->index_scale_lower);

    if (index->starkd->tree->split.any) {
        if (index->starkd->tree->splitdim)
            index->starkd->tree->splitmask = UINT32_MAX;
        else
            compute_splitbits(index->starkd->tree);
    }

    if (index->codekd->split.any) {
        if (index->codekd->splitdim)
            index->codekd->splitmask = UINT32_MAX;
        else
            compute_splitbits(index->codekd);
    }

    // kdtree_update_funcs(index->codekd);
    // kdtree_update_funcs(index->starkd->tree);

}

void anindex_free(index_t* index) {

    // We use one single buffer
    // Original used various buffers
    // Therefore reset before cleanup
    index->codekd->lr = 0;
    index->codekd->split.any = 0;
    index->codekd->minval = 0;
    index->codekd->maxval = 0;
    index->codekd->data.any = 0;
    index->starkd->tree->lr = 0;
    index->starkd->tree->split.any = 0;
    index->starkd->tree->minval = 0;
    index->starkd->tree->maxval = 0;
    index->starkd->tree->data.any = 0;
    index->starkd->sweep = 0;
    index->quads = 0;
    
    kdtree_free(index->starkd->tree);
    startree_close(index->starkd);
    kdtree_free(index->codekd);
    free(index->indexfn);
    free(index->dataptr);
    index->dataptr = 0;

#ifdef _WIN32
    CloseHandle(index->datamap);
#endif

}
