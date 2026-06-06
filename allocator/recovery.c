#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

#include "aeplib.h"
#include "recovery.h"
#include "tool.h"

static inline uint64_t pack_ref_info(uint64_t lcid, uint64_t ref_cnt, uint32_t lenum)
{
    return (lcid << 48) + (ref_cnt << 32) + lenum;
}

static inline uint64_t get_ref_cnt_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1) << 16) - 1;
    return (ref_info >> 32) & mask;
}

static inline uint64_t get_lenum_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1) << 32) - 1;
    return ref_info & mask;
}

static inline uint64_t get_lcid_from_info(uint64_t ref_info)
{
    uint64_t mask = (ZU(1) << 16) - 1;
    return (ref_info >> 48) & mask;
}

static inline _Atomic(uint32_t) *recovery_era(int x, int y)
{
    _Atomic(uint32_t) *era_start = (_Atomic(uint32_t) *)(void *)((char *)cxl_shm_root_start + ERA_ARRAY_START);
    return era_start + (x - 1) * NUM_CLIENT + (y - 1);
}

static inline redo_log_t *recovery_log(int client_id)
{
    redo_log_t *start = (redo_log_t *)((char *)cxl_shm_root_start + LOG_START);
    return start + client_id;
}

void aep_recovery_clear_stats(aep_recovery_stats_t *stats)
{
    if (stats)
        memset(stats, 0, sizeof(*stats));
}

void aep_recovery_print_stats(const aep_recovery_stats_t *stats)
{
    if (!stats)
        return;
    printf("AEP recovery: redo_logs=%lu redo_refs=%lu mailbox_cells=%lu mailbox_refs=%lu "
           "segments=%lu abandoned=%lu chunks=%lu chunk_released=%lu\n",
           (unsigned long)stats->redo_logs_checked,
           (unsigned long)stats->redo_refs_replayed,
           (unsigned long)stats->mailbox_cells_scanned,
           (unsigned long)stats->mailbox_refs_released,
           (unsigned long)stats->segments_scanned,
           (unsigned long)stats->segments_abandoned,
           (unsigned long)stats->chunks_scanned,
           (unsigned long)stats->chunks_released);
}

static void mark_segment_state(segment_allocation_state_t *sas, uint64_t state)
{
    struct state_free_info info;
    struct state_free_info new_info;

    do {
        info = atomic_load_explicit(&sas->info, memory_order_acquire);
        new_info.state = state;
        new_info.node_free = info.node_free;
    } while (!atomic_compare_exchange_weak_explicit(&sas->info, &info, new_info,
                                                    memory_order_release,
                                                    memory_order_relaxed));
}

static void mark_segment_for_object(CXLObj *obj, uint64_t state)
{
    if (!obj)
        return;
    segment_t *segment = cxl_ptr_segment((void *)obj);
    segment_allocation_state_t *sas = segment_get_sas(segment);
    mark_segment_state(sas, state);
}

static bool redo_log_needs_replay(int client_id, const log_cell_t *log, CXLObj *refed)
{
    uint64_t ref_info = atomic_load_explicit(&refed->ref_info, memory_order_acquire);
    uint32_t owner = get_lcid_from_info(ref_info);
    uint32_t era = get_lenum_from_info(ref_info);

    if (owner == (uint32_t)client_id && era == log->cur_era)
        return true;

    uint32_t max_seen = 0;
    for (int i = 1; i < NUM_CLIENT; i++) {
        if (i == client_id)
            continue;
        uint32_t seen = atomic_load_explicit(recovery_era(i, client_id), memory_order_acquire);
        if (seen > max_seen)
            max_seen = seen;
    }
    return log->cur_era <= max_seen;
}

static void redo_last_level1_ref_op(int client_id, aep_recovery_stats_t *stats)
{
    redo_log_t *redo = recovery_log(client_id);
    log_cell_t *latest = NULL;

    for (size_t i = 0; i < REDO_CACHELINE_CNT; i++) {
        log_cell_t *cur = (log_cell_t *)(void *)(redo->list + 64 * i);
        if (cur->func_id != LINK_REF && cur->func_id != UNLINK_REF)
            continue;
        if (!latest || cur->cur_era > latest->cur_era)
            latest = cur;
    }

    if (stats)
        stats->redo_logs_checked++;
    if (!latest || latest->refed == 0 || latest->ref == 0)
        return;

    CXLObj *refed = (CXLObj *)latest->refed;
    if (!redo_log_needs_replay(client_id, latest, refed))
        return;

    uint64_t *ref_slot = (uint64_t *)latest->ref;
    if (latest->func_id == LINK_REF) {
        *ref_slot = latest->refed;
        FLUSH(ref_slot);
        if (stats)
            stats->redo_refs_replayed++;
    } else if (latest->func_id == UNLINK_REF) {
        *ref_slot = 0;
        FLUSH(ref_slot);
        if (get_ref_cnt_from_info(latest->saved_ref_cnt) == 1)
            mark_segment_for_object(refed, POTENTIAL_LEAKING);
        if (stats)
            stats->redo_refs_replayed++;
    }
}

static void release_cross_node_mailbox_ref(int recovery_client_id, mailbox_cell_t *cell,
                                           aep_recovery_stats_t *stats)
{
    CXLObj *obj = cell->addr;
    if (!obj)
        return;

    level1_unlink_reference(recovery_client_id, (uint64_t *)&cell->addr, (uint64_t)obj);
    cell->dst_client = 0;
    atomic_store_explicit(&cell->ref, 0, memory_order_release);
    FLUSH(cell);

    if (stats)
        stats->mailbox_refs_released++;
}

static void recover_node_mailbox_for_client(int client_id, aep_recovery_stats_t *stats)
{
    int node_id = CLIENT_TOPO[client_id];
    mailbox_t *box = &cxl_shm_root_start->node_list[node_id].node_mailbox;

    for (int i = 0; i < MAILBOX_SIZE; i++) {
        mailbox_cell_t *cell = &box->cell_list[i];
        if (stats)
            stats->mailbox_cells_scanned++;
        if (cell->dst_client == client_id)
            release_cross_node_mailbox_ref(client_id, cell, stats);
    }
}

static void abandon_client_segments(int client_id, aep_recovery_stats_t *stats)
{
    segment_allocation_state_t *start =
        (segment_allocation_state_t *)(void *)(local_image + SEGMENT_ALLOCATION_VEC_START);

    for (uint64_t i = 0; i < TOTAL_SEGMENT_NUMBER; i++) {
        segment_allocation_state_t *sas = &start[i];
        uint32_t owner = atomic_load_explicit(&sas->client_id, memory_order_acquire);
        if (stats)
            stats->segments_scanned++;
        if (owner != (uint32_t)client_id)
            continue;
        mark_segment_state(sas, ABANDON);
        if (stats)
            stats->segments_abandoned++;
    }
}

static void release_empty_node_chunks(int node_id, aep_recovery_stats_t *stats)
{
    chunk_list_t *chunks = return_chunk_list();
    uint32_t owner = (uint32_t)(node_id + 1);

    for (int i = 0; i < TOTAL_CHUNK_NUMBER; i++) {
        struct cxl_chunk_allocation_state *chunk = &chunks->list[i];
        if (stats)
            stats->chunks_scanned++;
        if (atomic_load_explicit(&chunk->node_id, memory_order_acquire) != owner)
            continue;

        bool has_live_segment = false;
        segment_allocation_state_t *seg_start =
            (segment_allocation_state_t *)(void *)(local_image + SEGMENT_ALLOCATION_VEC_START +
                                                   (uint64_t)i * SEGMENT_PER_CHUNK * sizeof(segment_allocation_state_t));
        for (int j = 0; j < SEGMENT_PER_CHUNK; j++) {
            uint32_t client = atomic_load_explicit(&seg_start[j].client_id, memory_order_acquire);
            struct state_free_info info = atomic_load_explicit(&seg_start[j].info, memory_order_acquire);
            if (client != 0 && info.state != ABANDON && info.state != POTENTIAL_LEAKING) {
                has_live_segment = true;
                break;
            }
        }
        if (has_live_segment)
            continue;

        uint32_t expected = owner;
        if (atomic_compare_exchange_strong_explicit(&chunk->node_id, &expected, 0,
                                                    memory_order_release,
                                                    memory_order_relaxed)) {
            struct state_free_info info = {0, 0};
            for (int j = 0; j < SEGMENT_PER_CHUNK; j++) {
                atomic_store_explicit(&seg_start[j].client_id, 0, memory_order_release);
                seg_start[j].ver = NORMAL;
                seg_start[j].info = info;
                FLUSH(&seg_start[j]);
            }
            chunk->info = info;
            chunk->ver = NORMAL;
            FLUSH(chunk);
            if (stats)
                stats->chunks_released++;
        }
    }
}

static void reset_client_local_state(int client_id)
{
    client_local_state_t *cls = &cls_all[client_id];
    memset(cls, 0, sizeof(*cls));
    cls->machine_id = CLIENT_TOPO[client_id];
    cls->pages[0].block_size = 16;
    cls->pages[1].block_size = 16 * ((sizeof(cxl_message_queue_t) - 1) >> 4) + 16;
    for (uint64_t i = 2; i < BIN_SIZE; i++)
        cls->pages[i].block_size = 16 * (i - 1);
}

void aep_recover_client(int client_id, aep_recovery_stats_t *stats)
{
    if (client_id <= 0 || client_id >= NUM_CLIENT) {
        printf("AEP recovery: invalid client_id %d\n", client_id);
        return;
    }

    redo_last_level1_ref_op(client_id, stats);
    recover_node_mailbox_for_client(client_id, stats);
    abandon_client_segments(client_id, stats);
    reset_client_local_state(client_id);
    release_empty_node_chunks(CLIENT_TOPO[client_id], stats);
}

void aep_recover_all(aep_recovery_stats_t *stats)
{
    for (int client_id = 1; client_id < NUM_CLIENT; client_id++)
        aep_recover_client(client_id, stats);
}
