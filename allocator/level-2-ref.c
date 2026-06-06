#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

//_ref and _refed are both CXL memory pointer.
void level2_link_reference(int client_id, uint64_t *_ref, uint64_t _refed)
{
    CXLObj_mirror_t* refed = (CXLObj_mirror_t*) _refed;

    // run in aep, protected by aep
    *_ref = _refed;
    refed->ref++;
}

void level2_unlink_reference(int client_id, uint64_t *_ref, uint64_t _refed)
{
    CXLObj_mirror_t* refed = (CXLObj_mirror_t*) _refed;

    // run in aep, protected by aep
    *_ref = 0;
    refed->ref--;

    if(refed->ref > 0) return;
    _level2_free(client_id, false, (obj_block_t *) refed);
}
