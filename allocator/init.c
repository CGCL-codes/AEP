#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <string.h>
#include "aeplib.h"

#include "malloc-clientlevel.h"
#include "cxlmalloc-nodelevel.h"
#include "init.h"

//dsm state in cxl memory
struct cxl_shm_root *cxl_shm_root_start;


//node local state in local memory
client_local_state_t cls_all[NUM_CLIENT];
node_allocation_state_t node_all[NUM_NODE];

char *local_image; // 1:1 map to CXL memory
char *local_chunk_start;
ptrdiff_t l2d_offset; //cxl_shm_root_start - local_image

#ifndef __NR_aep_map_cxl
#define __NR_aep_map_cxl 468
#endif

#define AEP_ALLOCATOR_TEMPLATE_ID 1
#define AEP_DAX_RESOURCE_PATH "/sys/bus/dax/devices/dax0.0/resource"

static unsigned long read_cxl_phys_addr(void)
{
    FILE *fp;
    char buf[64];
    unsigned long phys_addr;

    fp = fopen(AEP_DAX_RESOURCE_PATH, "r");
    if (!fp) {
        perror("open dax resource");
        return 0;
    }

    if (!fgets(buf, sizeof(buf), fp)) {
        perror("read dax resource");
        fclose(fp);
        return 0;
    }
    fclose(fp);

    errno = 0;
    phys_addr = strtoul(buf, NULL, 0);
    if (errno != 0) {
        perror("parse dax resource");
        return 0;
    }

    return phys_addr;
}

static void *aepmmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
#ifdef CONFIG_AEP
    unsigned long phys_addr = read_cxl_phys_addr();

    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;

    if (!phys_addr)
        return MAP_FAILED;

    return (void *)syscall(__NR_aep_map_cxl, AEP_ALLOCATOR_TEMPLATE_ID,
                           phys_addr, length, 0UL);
#else
    return mmap(addr, length, prot, flags, fd, offset);
#endif
}

static void init_client_state(client_local_state_t *cls, int client_id)
{
    memset(cls, 0, sizeof(*cls));
    cls->machine_id = CLIENT_TOPO[client_id];
    cls->pages[0].block_size = 16;
    cls->pages[1].block_size = 16 * ((sizeof(cxl_message_queue_t) - 1) >> 4) + 16;
    for (uint64_t i = 2; i < BIN_SIZE; i++)
        cls->pages[i].block_size = 16 * (i - 1);
}

static void init_allocator_state(void)
{
    memset(node_all, 0, sizeof(node_all));
    for (int i = 0; i < NUM_CLIENT; i++)
        init_client_state(&cls_all[i], i);
}

void init_aep_dsm(void){
    printf("enter init_aep_dsm, TOTAL_MEM_SIZE: %ld GB\n", (long unsigned int)(TOTAL_MEM_SIZE/ (1024 * 1024 * 1024)));
    //map CXL memory
    cxl_shm_root_start = aepmmap(NULL, TOTAL_MEM_SIZE, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE|MAP_ANON, -1, 0);

    if (cxl_shm_root_start != MAP_FAILED){
        printf("dsm memory pool at: %lx\n", (unsigned long)cxl_shm_root_start);
    }else{
        printf("dsm memory pool map failed !\n");
        return;
    }

    local_image = mmap(NULL, TOTAL_MEM_SIZE, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE|MAP_ANON, -1, 0);//|MAP_USE_PV

    if (local_image != MAP_FAILED){
        printf("local_image at: %lx\n", (unsigned long)local_image);
    }else{
        printf("local_image map failed !\n");
        return;
    }

    l2d_offset = (char *)cxl_shm_root_start - local_image;
    printf("l2d_offset: %lx\n", l2d_offset);
    init_allocator_state();
    return;
}
