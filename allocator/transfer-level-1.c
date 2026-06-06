#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

bool level1_send_to(int client_id, int dst_id, CXLObj *root_obj){
    int dst_node = CLIENT_TOPO[dst_id];
    
    mailbox_cell_t *cell = consume_mailbox_cell(&cxl_shm_root_start->node_list[dst_node].node_mailbox);
    if (!cell){
        printf("level1_send_to cannot get mailbox cell, dst_id: %d, client_id: %d\n", dst_id, client_id);
        return false;
    }
    cell->dst_client = dst_id;
    cell->ref = 0;
    level1_link_reference(client_id, (uint64_t *)&cell->addr, (uint64_t)root_obj);
    return true;
}  

CXLObj *level1_recv_from(int client_id, int recv_from){
    int cur_node = CLIENT_TOPO[client_id];

    mailbox_cell_t *cell = produce_mailbox_cell(&cxl_shm_root_start->node_list[cur_node].node_mailbox);
    CXLObj *result = (CXLObj*)cell->addr;
    return result;
}


