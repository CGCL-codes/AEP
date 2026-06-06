#include <stdio.h>
#include <stdatomic.h>
#include "cxlmalloc-nodelevel.h"
#include "tool.h"

// Allocate a segment from the shm, return a chunk index
static int cxl_chunk_alloc(int node_id)
{
    chunk_list_t *lt = return_chunk_list();
    struct cxl_chunk_allocation_state* data;
    uint32_t sas_no_use;
    uint64_t count;

    for (count = 0; count < TOTAL_CHUNK_NUMBER; count++) {
        sas_no_use = 0;
        data = &lt->list[count];
        if (atomic_compare_exchange_weak_explicit(&data->node_id, &sas_no_use,
                node_id + 1, memory_order_release, memory_order_relaxed))
            break;
    }
    if (count == TOTAL_CHUNK_NUMBER) {
        printf("memory alloc fail for no more chunk\n");
        return -1;
    }

    data->ver = NORMAL;
    struct state_free_info info = {0, 0};
    data->info = info;

    FLUSH(data);

    return (int)count;
}
static void cxl_chunk_free(int chunk_idx, int node_id)
{
    //todo: clear chunk
    chunk_list_t *lt = return_chunk_list();
    struct cxl_chunk_allocation_state* data = &lt->list[chunk_idx];
    uint32_t sas_no_use = 0;
    uint32_t otid = node_id + 1;
    do {
        otid = node_id + 1;
    } while(!atomic_compare_exchange_weak_explicit(&data->node_id, &otid, sas_no_use, memory_order_release, memory_order_relaxed));

    FLUSH(data);
}

static struct node_local_state *get_nls(int node_id){
    return &cxl_shm_root_start->node_list[node_id];
}

//need node local lock
int level1_chunk_alloc(int node_id){ 
    int chunk = cxl_chunk_alloc(node_id);
    if (chunk == -1) return -1;
    return chunk;
}

//idx: chunk index
void level1_chunk_free(int node_id, int idx){
    cxl_chunk_free(idx, node_id);
}

