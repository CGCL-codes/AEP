#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "aeplib.h"
#include "tool.h"
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

static int cxl_chunk_alloc(int node_id)
{
    // find the addr of segment allocation state
    chunk_list_t *lt = return_chunk_list();
    struct cxl_chunk_allocation_state* data;
    uint64_t offset = 0;
    uint32_t sas_no_use = 0;
    uint64_t count = 0;
    do {
        if(count >= TOTAL_CHUNK_NUMBER)
        {
            printf("memory alloc fail for no more segment\n");
            return -1; // return a error
        }
        sas_no_use = 0;
        data = &lt->list[count];
        count++;
    } while(!atomic_compare_exchange_weak_explicit(&data->node_id, &sas_no_use, node_id, memory_order_release, memory_order_relaxed));

    data->ver = NORMAL;
    struct state_free_info info = {0, 0};
    data->info = info;

    FLUSH(data);

    return count;
}


segment_allocation_state_t *search_chunk_bitmap(int client_id, local_chunk_cell_t *chunk){
    segment_allocation_state_t* start = 
        (segment_allocation_state_t*)((char*)local_image + SEGMENT_ALLOCATION_VEC_START + chunk->chunk_number * SEGMENT_PER_CHUNK * sizeof(segment_allocation_state_t));
    int count = 0;
    uint32_t sas_no_use = 0;
    segment_allocation_state_t* data;
    do {
        if (count == SEGMENT_PER_CHUNK){
#ifdef AEP_DSM_DEBUG
            printf("chunk: %p full\n", (void *)chunk);
#endif
            return NULL;
        }
        sas_no_use = 0;
        data = &start[count];
        count++;
    } while(!atomic_compare_exchange_weak_explicit(
        &data->client_id, 
        &sas_no_use, 
        client_id, 
        memory_order_release, 
        memory_order_relaxed));

    return data;
}

//natually have lock protection
static void get_chunk_from_level1(int node_id, node_allocation_state_t *nas, int pos){
    int chunk = level1_chunk_alloc(node_id);
    nas->chunk_list[pos].chunk_number = chunk;
    nas->chunk_list[pos].in_use = 1;
}

//bitmap search
static segment_allocation_state_t* _cxl_segment_alloc(int client_id, client_local_state_t *cls){
    node_allocation_state_t *nas = &node_all[CLIENT_TOPO[client_id]];
    segment_allocation_state_t *result;
    for (int i = 0; i < MAX_CHUNK_PER_NODE; i++){
        if (nas->chunk_list[i].in_use == 1){
get_seg:
            result = search_chunk_bitmap(client_id, &nas->chunk_list[i]);
            if (result) return result;
        }else{
            AEP_LOCK(nas->chunk_list[i].lock);
            if (nas->chunk_list[i].in_use == 1){
                AEP_UNLOCK(nas->chunk_list[i].lock);
                goto get_seg;
            }
            //get chunk from level 1
            get_chunk_from_level1(CLIENT_TOPO[client_id], nas, i);
            AEP_UNLOCK(nas->chunk_list[i].lock);
            goto get_seg;
        }
    }
    printf("cxl_segment_alloc, do not have enough node local memory\n");
    return NULL;
}


segment_t* cxl_segment_alloc(int client_id, client_local_state_t *cls){
    segment_allocation_state_t * sas = _cxl_segment_alloc(client_id, cls);
    if (!sas){
        printf("node cannot get segment!\n");
        return NULL;

    }

    sas->ver = NORMAL;
    struct state_free_info _info = {0, 0};
    sas->info = _info;

    segment_t *result = sas_get_segment(sas);
    result->client_id = client_id;
    result->used = 0;

    page_t* page_start = &result->meta_page[1];
    page_t* page = &result->meta_page[PAGES_PER_SEGMENT - 1];

    while (page >= page_start)
    {
        cxl_page_queue_push(&cls->free_page, page);
        page = page - 1;
    }
    return result;
}



void cxl_segment_free(client_local_state_t *cls, segment_t* segment, int client_id)
{
    for(size_t i = 1; i < PAGES_PER_SEGMENT; i++)
    {
        cxl_page_queue_remove(&cls->free_page, &segment->meta_page[i]);
    }
    memset(segment, 0, sizeof(segment_t));

    segment_allocation_state_t *data = segment_get_sas(segment);
    uint32_t sas_no_use = 0;
    uint32_t otid = client_id;
    do {
        otid = client_id;
    } while(!atomic_compare_exchange_weak_explicit(
        &data->client_id, 
        &otid, 
        sas_no_use, 
        memory_order_release, 
        memory_order_relaxed));
}