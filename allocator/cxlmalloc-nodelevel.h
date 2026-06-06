#ifndef CXLMALLOC_NODELEVEL_H
#define CXLMALLOC_NODELEVEL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h> 
#include <stdbool.h>  
#include "topo.h"

# define ZU(x)  x##ULL
# define ZI(x)  x##LL

# define INTPTR_SHIFT   (3)
# define PAGE_SHIFT     (13 + INTPTR_SHIFT)  // 64KiB
# define SEGMENT_SHIFT  (10 + PAGE_SHIFT)    // 64MiB

# define INTPTR_SIZE    (1<<INTPTR_SHIFT)
# define PAGE_SIZE      (ZU(1)<<PAGE_SHIFT)
# define SEGMENT_SIZE   (ZU(1)<<SEGMENT_SHIFT)
# define SEGMENT_MASK   (SEGMENT_SIZE - 1)

# define PAGES_PER_SEGMENT    (SEGMENT_SIZE / PAGE_SIZE) // 1024

#define CHUNK_SIZE (SEGMENT_SIZE * 16) //64MB * 16 = 1GB
#define SEGMENT_PER_CHUNK (CHUNK_SIZE / SEGMENT_SIZE) //16
#define TOTAL_CHUNK_NUMBER (4) //4GB
#define MAX_CHUNK_PER_NODE TOTAL_CHUNK_NUMBER
#define TOTAL_SEGMENT_NUMBER (SEGMENT_PER_CHUNK * TOTAL_CHUNK_NUMBER)

# define LOG_START     (ZU(1)<<21)
# define CHUNK_ALLOCATION_VEC_START     (ZU(1)<<22) //level-1 chunk_allocation_state list
# define CHUNK_ALLOCATION_VEC_SIZE     (ZU(1)<<21)
# define SEGMENT_ALLOCATION_VEC_START   (ZU(1)<<23) //level-2 segment_allocation_state list
# define SEGMENT_ALLOCATION_VEC_SIZE (ZU(1)<<22)
# define ERA_ARRAY_START             (ZU(1)<<25)
# define HASH_TABLE_START            (ZU(1)<<28)
# define HASH_TABLE_SIZE             (ZU(1)<<20)
#define SEGMENTS_AREA_START          (ZU(1)<<30)

#define TOTAL_MEM_SIZE (TOTAL_CHUNK_NUMBER * CHUNK_SIZE + SEGMENTS_AREA_START)

#define MESSAGE_BUFFER_SIZE 16

#define MAILBOX_SIZE 512
# define REDO_CACHELINE_CNT (8)

#define FENCE  __asm__ __volatile__ ("sfence" ::: "memory")
#define FLUSH(addr) __asm__ __volatile__ ("clwb (%0)" :: "r"(addr))

#define LINK_REF (1)
#define UNLINK_REF (2)

#define NORMAL 0
#define ABANDON 1
#define POTENTIAL_LEAKING 2

//all these structures are located in the CXL memory

typedef struct __attribute__ ((aligned(64))) cxl_message_queue_s {
    volatile size_t start;
    volatile size_t end;
    uint16_t sender_id;
    uint16_t receiver_id;
    uint64_t sender_next;
    uint64_t receiver_next;
    uint64_t buffer[MESSAGE_BUFFER_SIZE];
} cxl_message_queue_t;

typedef struct CXLObj_s { //size same as CXLObj_mirror_t
    uint64_t next;
    _Atomic(uint64_t) ref_info;         // 8 bytes for lcid(2), ref_cnt(2), lenum(4) 
    uint64_t embedded_ref_cnt;
} CXLObj;

typedef struct mailbox_cell{
    CXLObj *addr;
    _Atomic(int) ref;
    int dst_client;
} mailbox_cell_t;

typedef struct mailbox{
    mailbox_cell_t cell_list[MAILBOX_SIZE];
    _Atomic(int) start; 
    _Atomic(int) end;
} mailbox_t;

/*
struct chunk_cell{
    void *addr; //chunk address
    int chunk_number;
    int in_used;
};

struct node_local_state{
    struct chunk_cell list[MAX_CHUNK_PER_NODE];
    int free_num;
    mailbox_t node_mailbox;

};
*/

struct node_local_state{
    mailbox_t node_mailbox;

};

//each client have one
typedef struct redo_log{
    char list[64*REDO_CACHELINE_CNT];
} redo_log_t;

typedef struct log_cell {
    uint16_t func_id;
    uint16_t saved_ref_cnt;
    uint32_t cur_era;
    uint64_t ref;
    uint64_t refed;
    uint64_t old_refed;
} log_cell_t;

struct state_free_info
{
    uint64_t state;
    uint64_t node_free; //client or node
};

//cxl_segment_allocation_state_t
struct cxl_chunk_allocation_state {
    _Atomic(uint32_t) node_id; //owner
    uint32_t ver;
    _Atomic(struct state_free_info) info;
};


struct cxl_shm_root{
    unsigned long size; //cxl shared memory total size 
    struct node_local_state node_list[NUM_NODE];
    //next are chunk and their local state (split)...
};

typedef struct chunk_list_s{
    struct cxl_chunk_allocation_state list[TOTAL_CHUNK_NUMBER];
}chunk_list_t;

extern struct cxl_shm_root *cxl_shm_root_start;

int level1_chunk_alloc(int node_id);
void level1_chunk_free(int node_id, int idx);

void level1_link_reference(int client_id, uint64_t *_ref, uint64_t _refed);
void level1_unlink_reference(int client_id, uint64_t *_ref, uint64_t _refed);

bool level1_send_to(int client_id, int dst_id, CXLObj *root_obj);
CXLObj *level1_recv_from(int client_id, int recv_from);

mailbox_cell_t *consume_mailbox_cell(mailbox_t *box);
mailbox_cell_t *produce_mailbox_cell(mailbox_t *box);


#endif