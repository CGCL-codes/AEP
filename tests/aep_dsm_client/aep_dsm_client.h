#ifndef AEP_DSM_CLIENT_H
#define AEP_DSM_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Client-side ABI for tests that call an AEP-DSM allocator already loaded in
 * the AEP address space. Tests should not link allocator objects directly;
 * they enter AEP through the shadow-entry function pointer below.
 *
 * tests/custom.ld and allocator/custom.ld place .text.my_entry_point in the
 * first page-aligned entry section after the AEP base address.
 */
#ifndef AEP_DSM_SHADOW_ENTRY_ADDR
#define AEP_DSM_SHADOW_ENTRY_ADDR 0xffffe8fffd801000ULL
#endif

enum aep_dsm_call_number {
    AEP_DSM_MALLOC_INIT  = 1,
    AEP_DSM_PRINT_BITMAP = 2,
    AEP_DSM_COUNT        = 3,
    AEP_DSM_MALLOC       = 4,
    AEP_DSM_FREE         = 5,
    AEP_DSM_LINK         = 6,
    AEP_DSM_UNLINK       = 7,
    AEP_DSM_SEND         = 8,
    AEP_DSM_RECV         = 9,
    AEP_DSM_RECOVER      = 10,
};

#define AEP_DSM_RECOVER_ALL_CLIENTS (-1)

typedef unsigned long (*aep_dsm_entry_t)(int call_number, ...);

#ifdef AEP_DSM_DIRECT_LINK
extern unsigned long aep_entry(int call_number, ...);
#endif

typedef struct aep_dsm_client_s {
    aep_dsm_entry_t entry;
} aep_dsm_client_t;

static inline aep_dsm_client_t aep_dsm_client_at(uintptr_t shadow_entry_addr)
{
#ifdef AEP_DSM_DIRECT_LINK
    (void)shadow_entry_addr;
    aep_dsm_client_t client = { aep_entry };
#else
    aep_dsm_client_t client = { (aep_dsm_entry_t)shadow_entry_addr };
#endif
    return client;
}

static inline const char *aep_dsm_entry_mode(void)
{
#ifdef AEP_DSM_DIRECT_LINK
    return "direct-link";
#else
    return "shadow-entry";
#endif
}

static inline aep_dsm_client_t aep_dsm_default_client(void)
{
    return aep_dsm_client_at((uintptr_t)AEP_DSM_SHADOW_ENTRY_ADDR);
}

static inline unsigned long aep_dsm_init(aep_dsm_client_t *client)
{
    return client->entry(AEP_DSM_MALLOC_INIT);
}

static inline unsigned long aep_dsm_print_bitmap(aep_dsm_client_t *client)
{
    return client->entry(AEP_DSM_PRINT_BITMAP);
}

static inline unsigned long aep_dsm_count(aep_dsm_client_t *client)
{
    return client->entry(AEP_DSM_COUNT);
}

static inline void *aep_dsm_malloc(aep_dsm_client_t *client, int client_id,
                                   uint64_t size, uint32_t embedded_ref_cnt)
{
    return (void *)client->entry(AEP_DSM_MALLOC, client_id, size, embedded_ref_cnt);
}

static inline void aep_dsm_free(aep_dsm_client_t *client, int client_id, void *obj)
{
    client->entry(AEP_DSM_FREE, client_id, obj);
}

static inline void aep_dsm_link(aep_dsm_client_t *client, int client_id,
                                uint64_t *ref, uint64_t refed)
{
    client->entry(AEP_DSM_LINK, client_id, ref, refed);
}

static inline void aep_dsm_unlink(aep_dsm_client_t *client, int client_id,
                                  uint64_t *ref, uint64_t refed)
{
    client->entry(AEP_DSM_UNLINK, client_id, ref, refed);
}

static inline bool aep_dsm_send(aep_dsm_client_t *client, int client_id,
                                int dst_id, void *obj)
{
    return (bool)client->entry(AEP_DSM_SEND, client_id, dst_id, obj);
}

static inline void *aep_dsm_recv(aep_dsm_client_t *client, int client_id,
                                 int recv_from)
{
    return (void *)client->entry(AEP_DSM_RECV, client_id, recv_from);
}

static inline unsigned long aep_dsm_recover(aep_dsm_client_t *client, int client_id)
{
    return client->entry(AEP_DSM_RECOVER, client_id);
}

#endif
