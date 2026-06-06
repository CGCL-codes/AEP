#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

redo_log_t *get_log(int client_id){
    redo_log_t *start = (redo_log_t*)((char *)cxl_shm_root_start + LOG_START);
    return start + client_id;
}

static inline uint64_t pack_ref_info(uint64_t lcid, uint64_t ref_cnt, uint32_t lenum)
{
    return (lcid << 48) + (ref_cnt << 32) + lenum;
}

static inline uint64_t get_ref_cnt_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1)<<16) - 1;
    return (ref_info >> 32) & mask;
}

static inline uint64_t get_lenum_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1)<<32) - 1;
    return ref_info & mask;
}

static inline uint64_t get_lcid_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1)<<16) - 1;
    return (ref_info >> 48) & mask;
}

// thread id count from 1
_Atomic(uint32_t) *era(int x, int y)
{
    _Atomic(uint32_t) * era_start = (_Atomic(uint32_t)*) (void*)((char*)cxl_shm_root_start + ERA_ARRAY_START);
    return era_start + (x-1)*NUM_CLIENT + (y-1);
}


//_refed is a pointer to a CXLObj structure
void level1_link_reference(int client_id, uint64_t *_ref, uint64_t _refed)
{
    redo_log_t *redo = get_log(client_id);
    // IncRefCnt
    CXLObj* refed = (CXLObj*) _refed;
    uint64_t ref_info;
    uint64_t new_ref_info;
    uint16_t ref_info_cnt;
    do {
        ref_info = refed->ref_info;
        ref_info_cnt = get_ref_cnt_from_info(ref_info);
        uint32_t saw_cid = get_lcid_from_info(ref_info);
        uint32_t saw_era = get_lenum_from_info(ref_info);
        _Atomic(uint32_t) *ele = era(client_id, saw_cid);
        uint32_t cur_era = *era(client_id, client_id);
        if(saw_era > *ele) 
        {
            *ele = saw_era;
            FLUSH(ele);
        }
        // todo redo flush redo log
        char *r = &redo->list[(cur_era & (REDO_CACHELINE_CNT - 1))];

        log_cell_t k = {LINK_REF, ref_info_cnt, cur_era, (uint64_t)_ref, _refed, 0};
        *((log_cell_t*) r) = k;
        new_ref_info = pack_ref_info(client_id, ref_info_cnt+1, cur_era); 
        FLUSH(r);
    } while (!atomic_compare_exchange_weak_explicit(
            &refed->ref_info,          // 原子变量指针
            &ref_info,               // 期望值指针（会被更新为实际值）
            new_ref_info,            // 新值
            memory_order_release, // 成功时的内存序
            memory_order_relaxed  // 失败时的内存序
        ));

    *_ref = _refed;

    *era(client_id, client_id) += 1;
}

//_refed is a pointer to a CXLObj structure
void level1_unlink_reference(int client_id, uint64_t *_ref, uint64_t _refed)
{
    redo_log_t *redo = get_log(client_id);
    // IncRefCnt
    CXLObj* refed = (CXLObj*) _refed;
    uint64_t ref_info;
    uint64_t new_ref_info;
    uint16_t ref_info_cnt;
    do {
        ref_info = refed->ref_info;
        ref_info_cnt = get_ref_cnt_from_info(ref_info);
        uint32_t saw_cid = get_lcid_from_info(ref_info);
        uint32_t saw_era = get_lenum_from_info(ref_info);
        _Atomic(uint32_t) *ele = era(client_id, saw_cid);
        uint32_t cur_era = *era(client_id, client_id);
        if(saw_era > *ele) 
        {
            *ele = saw_era;
            FLUSH(ele);
        }
        // todo redo flush redo log
        char *r = &redo->list[(cur_era & (REDO_CACHELINE_CNT - 1))];

        log_cell_t k = {UNLINK_REF, ref_info_cnt, cur_era, (uint64_t)_ref, _refed, 0};
        *((log_cell_t*) r) = k;
        new_ref_info = pack_ref_info(client_id, ref_info_cnt-1, cur_era); 
        FLUSH(r);
    } while (!atomic_compare_exchange_weak_explicit(
            &refed->ref_info,          // 原子变量指针
            &ref_info,               // 期望值指针（会被更新为实际值）
            new_ref_info,            // 新值
            memory_order_release, // 成功时的内存序
            memory_order_relaxed  // 失败时的内存序
        ));

    *_ref = 0;

    *era(client_id, client_id) += 1;
/*
    if(get_ref_cnt_from_info(ref_info)-1 > 0) return;
    cxl_free(false, (cxl_block*) refed);
*/
}
