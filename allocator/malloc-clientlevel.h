#ifndef MALLOC_CLIENTLEVEL_H
#define MALLOC_CLIENTLEVEL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>   
#include <stdbool.h>
#include "topo.h"
#include "cxlmalloc-nodelevel.h"

#define BIN_SIZE 66

//all these structures are located in the local DRAM

typedef struct RootRef_s {
    uint64_t pptr; 
    bool     in_use;
    uint16_t ref_cnt;
} RootRef;

typedef struct obj_block_s {
    struct obj_block_s *next;
} obj_block_t;


typedef struct CXLObj_mirror_s {
    uint64_t next;  //<-- should be same as the first member in obj_block_t
    uint64_t ref;   //local ref 
    uint64_t embedded_ref_cnt;
} CXLObj_mirror_t;


typedef struct page_s {
    obj_block_t *free;
    obj_block_t *local_free; 
    uint32_t block_size;
    uint32_t used;
    struct page_s *next;
    struct page_s *prev;
    bool       is_msg_queue_page;
} page_t;

typedef struct segment_s {
    int         client_id;
    uint32_t    used;
    page_t      meta_page[PAGES_PER_SEGMENT];
} segment_t;


typedef struct page_queue_s {
    page_t *first;
    page_t *last;
    size_t block_size;
} page_queue_t;

typedef struct client_local_state_s{
    int             machine_id;
    page_queue_t    pages[BIN_SIZE];
    page_queue_t    free_page;
    uint64_t        sender_queue;
    uint64_t        receiver_queue;
    mailbox_t       client_mailbox;
} client_local_state_t;


typedef struct segment_allocation_state_s {
    _Atomic(uint32_t) client_id;
    uint32_t ver;
    _Atomic(struct state_free_info) info;
} segment_allocation_state_t;


typedef struct local_chunk_cell_s{
    int in_use;
    int chunk_number;
     _Atomic(uint32_t) lock;
} local_chunk_cell_t;


typedef struct node_allocation_state_s{
    local_chunk_cell_t chunk_list[MAX_CHUNK_PER_NODE];
} node_allocation_state_t;

extern client_local_state_t cls_all[NUM_CLIENT];
extern node_allocation_state_t node_all[NUM_NODE];

extern char *local_image;
extern char *local_chunk_start;
extern ptrdiff_t l2d_offset; //cxl_shm_root_start - local_image


void cxl_page_queue_push(page_queue_t *p, page_t* page);
void cxl_page_queue_remove(page_queue_t *p, page_t* page);

obj_block_t *_level2_malloc(int client_id, uint64_t data_size, uint32_t embedded_ref_cnt);
void *level2_malloc(int client_id, uint64_t data_size, uint32_t embedded_ref_cnt);
void _level2_free(int client_id, bool special, obj_block_t* b);
void level2_free(int client_id, bool special, void* obj);

bool level2_send_to(int client_id, int dst_id, void *obj);
void *level2_recv_from(int client_id, int recv_from);

cxl_message_queue_t* msg_queue_alloc(int client_id, uint16_t sender_id, uint16_t receiver_id);

void level2_link_reference(int client_id, uint64_t *_ref, uint64_t _refed);
void level2_unlink_reference(int client_id, uint64_t *_ref, uint64_t _refed);

segment_t* cxl_segment_alloc(int client_id, client_local_state_t *cls);
void cxl_segment_free(client_local_state_t *cls, segment_t* segment, int client_id);

#endif