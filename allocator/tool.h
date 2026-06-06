#ifndef AEP_TOOL_H
#define AEP_TOOL_H


#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"


static inline chunk_list_t *return_chunk_list(void){
    chunk_list_t *p = (chunk_list_t *)((char *)cxl_shm_root_start + CHUNK_ALLOCATION_VEC_START);
    return p;
}

static inline void *local_to_dsm(void *local)
{
    return (void *)((char *)local + l2d_offset);
}

static inline void *dsm_to_local(void *dsm)
{
    return (void *)((char *)dsm - l2d_offset);
}

static inline segment_t* cxl_ptr_segment(void* p)
{
    uint64_t offset = (uint64_t)((char*)p - local_image);
    return (segment_t*)(local_image + (offset & ~SEGMENT_MASK));
}

static inline segment_allocation_state_t * segment_get_sas(segment_t* segment)
{
    uint64_t idx = ((uint64_t)segment - SEGMENTS_AREA_START - (uint64_t)local_image) / SEGMENT_SIZE;
    segment_allocation_state_t* sas = 
        (segment_allocation_state_t*)((char*)local_image + SEGMENT_ALLOCATION_VEC_START + idx * sizeof(segment_allocation_state_t));
    return sas;
}

//p: obj pointer in local memory 
static inline segment_allocation_state_t * get_sas(void *p)
{
    segment_t* segment = cxl_ptr_segment(p);
    return segment_get_sas(segment);
}

static inline segment_t * sas_get_segment(segment_allocation_state_t* sas)
{
    uint64_t idx = ((uint64_t)sas - SEGMENT_ALLOCATION_VEC_START - (uint64_t)local_image) / sizeof(segment_allocation_state_t);
    segment_t *result = (segment_t*)((char*)local_image + SEGMENTS_AREA_START + idx * SEGMENT_SIZE);
    return result;
}

static inline page_t* cxl_segment_page_of(segment_t* segment, void* p)
{
    ptrdiff_t diff = (uint8_t*)p - (uint8_t*)segment;
    size_t idx = (size_t)diff >> PAGE_SHIFT;
    return &segment->meta_page[idx];
}

static inline page_t* cxl_ptr_page(void* p){
    return cxl_segment_page_of(cxl_ptr_segment(p), p);
}

static inline void* cxl_page_start(segment_t* segment, page_t* page)
{
    ptrdiff_t idx = page - segment->meta_page;
    return(void*)((char*)segment + (idx*PAGE_SIZE));
}

static inline obj_block_t* cxl_page_block_at(void* page_start, uint64_t block_size, uint64_t i)
{
    return (obj_block_t*)((char*)page_start + (i * block_size));
}

#endif