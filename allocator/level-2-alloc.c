#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"
#include "tool.h"


// Collect the local `thread_free` list using an atomic exchange.
void cxl_thread_free_collect(page_t* page)
{
    segment_allocation_state_t* sas = get_sas(page);

    struct state_free_info info = atomic_load_explicit(&sas->info, memory_order_acquire);
    
    if (info.node_free == 0) return;
    struct state_free_info new_info;
    do {
        info = atomic_load_explicit(&sas->info, memory_order_acquire);
        new_info.state = info.state;
        new_info.node_free = 0;
    } while (!atomic_compare_exchange_weak_explicit( //交换成功的人拿到修改权，可以后续进行清理
        &sas->info,          // 原子变量指针
        &info,               // 期望值指针（会被更新为实际值）
        new_info,            // 新值
        memory_order_release, // 成功时的内存序
        memory_order_relaxed  // 失败时的内存序
    ));

    uint64_t head_offset = info.node_free;
    obj_block_t *head = (head_offset == 0) ? NULL : (obj_block_t *)head_offset;
    if (head == NULL) return;

    obj_block_t *tail = head;
    obj_block_t *next;
    while(tail != NULL){
        next = tail->next;
        page_t* p = cxl_ptr_page(tail);
        tail->next = p->local_free;
        p->local_free = tail;
        p->used -= 1;
        tail = next;
    }
}

// collect all frees to ensure up-to-date `used` count and find free block
void cxl_page_free_collect(page_t* page)
{
    cxl_thread_free_collect(page);
    if(page->local_free != 0){
        if(page->free == 0){
            page->free = page->local_free;
            page->local_free = 0;
        }
    }
}

page_t* cxl_segment_page_alloc(int client_id, client_local_state_t *cls, uint64_t block_size)
{
    // find a free page
    page_t* page = cls->free_page.first;
    if(page == NULL)
    {
        // no free page, allocate a new segment and try again
        if(cxl_segment_alloc(client_id, cls) == NULL)
        {
            return NULL;
        }
        else
        {
            // otherwise try again
            return cxl_segment_page_alloc(client_id, cls, block_size);
        }
    }
    cxl_page_queue_remove(&cls->free_page, page);
    segment_t* segment = cxl_ptr_segment((void*) page);
    segment->used++;
    return page;
}

// Initialize a fresh page
void cxl_page_init(bool special, page_t* page, uint64_t block_size)
{
    page->local_free = 0;
    page->block_size = block_size;
    page->used = 0;
    page->next = 0;
    page->prev = 0;
    page->is_msg_queue_page = false;


    void* page_area = cxl_page_start(cxl_ptr_segment(page), page);
    obj_block_t* start_block = cxl_page_block_at(page_area, block_size, 0);
    obj_block_t* last_block = cxl_page_block_at(page_area, block_size, PAGE_SIZE / block_size - 1);
    obj_block_t* block = start_block;
    while (block < last_block){
        if(special) memset(block, 0, sizeof(block_size));
        else memset(block, 0, sizeof(CXLObj));

        obj_block_t* next = (obj_block_t*)((char*)block + block_size);

        block->next = next;
        block = next;
    }

    last_block->next = page->free;
    page->free = start_block;
}

// Get a fresh page to use
static page_t* cxl_page_fresh(int client_id, client_local_state_t *cls, page_queue_t* pq)
{
    bool special = false;
    if(pq == &cls->pages[0] || pq == &cls->pages[1]) special = true;


    page_t* page = cxl_segment_page_alloc(client_id, cls, pq->block_size);

    cxl_page_init(special, page, pq->block_size);
    if(pq == &cls->pages[1])
        page->is_msg_queue_page = true;

    cxl_page_queue_push(pq, page);
    return page;
}

// Find a page with free blocks of `page->block_size`.
static page_t* cxl_page_queue_find_free_ex(int client_id, client_local_state_t *cls, page_queue_t* pq)
{
    page_t* page = pq->first;
    while (page != NULL){
        page_t* next = page->next;
        cxl_page_free_collect(page);
        if(page->free != 0) break;

        page = next;
    }

    if(page == NULL){
        page = cxl_page_fresh(client_id, cls, pq);
    }

    return page;
}

static obj_block_t* cxl_page_malloc(int client_id, client_local_state_t *cls, page_queue_t* pq, page_t** page)
{
    if((*page) == NULL){
        (*page) = cxl_page_queue_find_free_ex(client_id, cls, pq);
        if((*page) == NULL)
            return NULL;
    }
    return (*page)->free;
}

static page_t* cxl_find_page(page_queue_t* pq)
{
    page_t* page = pq->first;
    if(page != NULL){
        obj_block_t* block = page->free;
        if(block != NULL) return page;
        else return NULL;
    }
    return NULL;
}

static page_queue_t* cxl_page_queue(client_local_state_t *cls, bool special, uint64_t size)
{
    if(special){
        if(size == 16){
            return &cls->pages[0];
        }
        else if(size == sizeof(cxl_message_queue_t)){
            return &cls->pages[1];
        }
    }
    return &cls->pages[((size-1)>>4)+2];
}


static void init_cxlobj(obj_block_t *b, page_t* page, uint64_t embedded_ref_cnt)
{
    page->used ++;  //todo
    page->free = b->next;
    CXLObj_mirror_t *k = (CXLObj_mirror_t *)b;
    k->embedded_ref_cnt = embedded_ref_cnt;
    CXLObj *p = (CXLObj *)local_to_dsm(b);
    p->embedded_ref_cnt = embedded_ref_cnt;
}

static obj_block_t *cxl_ref_alloc(int client_id, client_local_state_t *cls, uint64_t data_size, uint64_t embedded_ref_cnt)
{
    uint64_t true_size = data_size + sizeof(CXLObj) + embedded_ref_cnt * sizeof(uint64_t);
    page_queue_t* pq = cxl_page_queue(cls, false, true_size);
    page_t* page = cxl_find_page(pq);
    obj_block_t* block = cxl_page_malloc(client_id, cls, pq, &page);


    init_cxlobj(block, page, embedded_ref_cnt);
    return block;
}


//alloc interface for client
obj_block_t *_level2_malloc(int client_id, uint64_t data_size, uint32_t embedded_ref_cnt)
{
    client_local_state_t *cls = &cls_all[client_id];
    return cxl_ref_alloc(client_id, cls, data_size, embedded_ref_cnt);
}

//return a address in cxl
void *level2_malloc(int client_id, uint64_t data_size, uint32_t embedded_ref_cnt){
    obj_block_t *obj = _level2_malloc(client_id, data_size, embedded_ref_cnt); //obj is in local mem
    return (void*)local_to_dsm((void *)(obj+1));
}

cxl_message_queue_t* block_to_msg_queue(obj_block_t* b, uint16_t sender_id, uint16_t receiver_id)
{
    page_t* page = cxl_ptr_page((void*) b);
    page->used ++;
    page->free = b->next;
    cxl_message_queue_t* q = (cxl_message_queue_t*) b;
    q->sender_id = sender_id;
    q->receiver_id = 0;
    q->start = 0;
    q->end = 0;
    q->sender_next = 0;
    q->receiver_next = 0;
    memset(q->buffer, 0, sizeof(q->buffer));
    return q;
}

//return local ram address
cxl_message_queue_t* msg_queue_alloc(int client_id, uint16_t sender_id, uint16_t receiver_id)
{
    client_local_state_t *cls = &cls_all[client_id];
    page_queue_t* pq = cxl_page_queue(cls, true, sizeof(cxl_message_queue_t));
    page_t* page = cxl_find_page(pq);
    obj_block_t* block = cxl_page_malloc(client_id, cls, pq, &page);
    if(block == NULL)
    {
        return NULL;
    }
    cxl_message_queue_t* q = block_to_msg_queue(block, sender_id, receiver_id);
    return q;
}

// Free a page with no more free blocks
void cxl_page_free(client_local_state_t *cls, page_t* page, page_queue_t* pq, int client_id)
{
    cxl_page_queue_remove(pq, page);
    segment_t* segment = cxl_ptr_segment((void*) page);
    memset(page, 0, sizeof(page_t));
    cxl_page_queue_push(&cls->free_page, page);
    segment->used --;
    if(segment->used == 0)
    {
        cxl_segment_free(cls, segment, client_id);
    }
    return;
}

void _level2_free(int client_id, bool special, obj_block_t* b)
{
    client_local_state_t *cls = &cls_all[client_id];

    page_t* page = cxl_ptr_page((void*) b);
    segment_t* segment = cxl_ptr_segment((void*) b);
    if(!special) 
        memset(b, 0, sizeof(CXLObj));
    else
    {
        if(page->block_size == 16) 
            memset(b, 0, sizeof(RootRef));
        else 
            memset(b, 0, sizeof(cxl_message_queue_t));
    }

    if(segment->client_id == client_id)
    {
        b->next = page->local_free;
        page->local_free = b;
        page->used--;
        if(page->used == 0)
        {
            cxl_page_free(cls, page, cxl_page_queue(cls, special, page->block_size), client_id);
        }
    }else{// located in the same node but different client
        segment_allocation_state_t* sas = get_sas(page);

        struct state_free_info info = atomic_load_explicit(&sas->info, memory_order_acquire);
        struct state_free_info new_info;
        obj_block_t *b_offset = b;
        do {
            info = atomic_load_explicit(&sas->info, memory_order_acquire);
            //if(info.state == POTENTIAL_LEAKING) break;  //in aep we don't have POTENTIAL_LEAKING situation
            b->next = (obj_block_t*)info.node_free;
            new_info.state = info.state;
            new_info.node_free = (uint64_t)b_offset;
        } while (!atomic_compare_exchange_weak_explicit(
            &sas->info,          // 原子变量指针
            &info,               // 期望值指针（会被更新为实际值）
            new_info,            // 新值
            memory_order_release, // 成功时的内存序
            memory_order_relaxed  // 失败时的内存序
        ));
    }

    // in the end, readers may release object group in cross-node senarios
    //
}

//free a block in cxl mem
void level2_free(int client_id, bool special, void *obj){
    obj_block_t *b = (obj_block_t *)dsm_to_local(obj);
    b = b - 1;
    _level2_free(client_id, false, b);
}
