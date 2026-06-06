#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"
#include "tool.h"

static bool _level2_send_to(int client_id, int dst_id, CXLObj_mirror_t *root_obj){
    if (CLIENT_TOPO[client_id] == CLIENT_TOPO[dst_id]){ //intra-node transfer
        mailbox_cell_t *cell = consume_mailbox_cell(&cls_all[dst_id].client_mailbox);
        cell->dst_client = dst_id;
        cell->ref = 0;
        level2_link_reference(client_id, (uint64_t *)&cell->addr, (uint64_t)root_obj);
        return true;
    }else{    //cross-node transfer
        CXLObj *dsm_obj = local_to_dsm(root_obj);
        memcpy(dsm_obj, root_obj, sizeof(CXLObj));
        return level1_send_to(client_id, dst_id, dsm_obj);
    }
    return false;
}

//obj: memory address passed by the client, CXL memory address.
bool level2_send_to(int client_id, int dst_id, void *obj){
    obj_block_t *_obj = (obj_block_t *)dsm_to_local(obj);
    _obj = _obj - 1;
    CXLObj_mirror_t *root_obj = (CXLObj_mirror_t *)_obj;
    return _level2_send_to(client_id, dst_id, root_obj);
}

//return a pointer in CXL memory area.
void *level2_recv_from(int client_id, int recv_from){
    if (CLIENT_TOPO[client_id] == CLIENT_TOPO[recv_from]){ //intra-node transfer
        mailbox_cell_t *cell = produce_mailbox_cell(&cls_all[client_id].client_mailbox);
        if (cell){
            CXLObj_mirror_t *result = (CXLObj_mirror_t*)cell->addr;
            return local_to_dsm((void *)((obj_block_t *)result + 1));
        }
    }else{    //cross-node transfer
        CXLObj *result = level1_recv_from(client_id, recv_from);
        if (result)
            return (void *)((obj_block_t *)result + 1);
        return NULL;
    }
    return NULL;
}

cxl_message_queue_t* level2_create_msg_queue(uint16_t client_id, uint16_t dst_id)
{
    cxl_message_queue_t* q = msg_queue_alloc(client_id, client_id, dst_id);
    client_local_state_t *cls = &cls_all[client_id];
    q->sender_next = cls->sender_queue;
    cls->sender_queue = (uint64_t)q;
    if (CLIENT_TOPO[client_id] != CLIENT_TOPO[dst_id])
        q = local_to_dsm(q); 
    return q;
}

