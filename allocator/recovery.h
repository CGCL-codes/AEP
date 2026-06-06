#ifndef AEP_DSM_RECOVERY_H
#define AEP_DSM_RECOVERY_H

#include <stdint.h>

#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

#define AEP_DSM_RECOVERY_ALL_CLIENTS (-1)

typedef struct aep_recovery_stats_s {
    uint64_t redo_logs_checked;
    uint64_t redo_refs_replayed;
    uint64_t mailbox_cells_scanned;
    uint64_t mailbox_refs_released;
    uint64_t segments_scanned;
    uint64_t segments_abandoned;
    uint64_t chunks_scanned;
    uint64_t chunks_released;
} aep_recovery_stats_t;

void aep_recovery_clear_stats(aep_recovery_stats_t *stats);
void aep_recovery_print_stats(const aep_recovery_stats_t *stats);
void aep_recover_client(int client_id, aep_recovery_stats_t *stats);
void aep_recover_all(aep_recovery_stats_t *stats);

#endif
