#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "aeplib.h"
#include "init.h"
#include "malloc-clientlevel.h"
#include "recovery.h"
#if !defined(CONFIG_AEP) && !defined(AEP_DSM_DIRECT_LINK)
#include "test.h"
#endif

#define STR_AND_SIZE(s) s, sizeof(s) - 1

int a = 0;

static unsigned long hexstrtoul(const char *s)
{
    unsigned long res = 0;
    char c;
    while ((c = *s++)) {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else {
            c &= ~0x20;
            c -= 'A' - 0xa;
        }
        res = res << 4 | c;
    }
    return res;
}

int add(int x, int y)
{
    return x + y;
}

int foobar(int x)
{
    int b = add(x, a);
    a++;

    return b;
}

#ifdef CONFIG_AEP
__attribute__((section(".text.my_entry_point")))
#endif
unsigned long aep_entry(int call_number, ...) {
    va_list args;
    va_start(args, call_number);

    unsigned long result = -1;

    switch (call_number) {
        case NUM_AEP_MALLOC_INIT: {
            init_aep_dsm();
            break;
        }
        case NUM_AEP_PRINT_BITMAP: {
            result = 0;
            break;
        }
        case NUM_AEP_COUNT: {
            result = 0;
            break;
        }
	    case NUM_AEP_MALLOC: {
            int client_id = va_arg(args, int);
            uint64_t data_size = va_arg(args, uint64_t);
            uint32_t embedded_ref_cnt = va_arg(args, uint32_t);
            result = (unsigned long)level2_malloc(client_id, data_size, embedded_ref_cnt);
            if (result == -1) {
                perror("AEP alloc failed");
            }
            break;
        }
        case NUM_AEP_FREE: {
            int client_id = va_arg(args, int);
            bool special = false;
            void *obj = va_arg(args, void *);
            level2_free(client_id, special, obj);
            break;
        }
        case NUM_AEP_LINK: {
            int client_id = va_arg(args, int);
            uint64_t *_ref = va_arg(args, uint64_t *);
            uint64_t _refed = va_arg(args, uint64_t);
            level2_link_reference(client_id, _ref, _refed);
            break;
        }
        case NUM_AEP_UNLINK: {
            int client_id = va_arg(args, int);
            uint64_t *_ref = va_arg(args, uint64_t *);
            uint64_t _refed = va_arg(args, uint64_t);
            level2_unlink_reference(client_id, _ref, _refed);
            break;
        }
        case NUM_AEP_SEND: {
            int client_id = va_arg(args, int);
            int dst_id = va_arg(args, int);
            void *obj = va_arg(args, void *);
            result = (unsigned long)level2_send_to(client_id, dst_id, obj);
            if (result == 0) {
                printf("NUM_AEP_SEND ERROR!\n");
            }
            break;
        }
        case NUM_AEP_RECV: {
            int client_id = va_arg(args, int);
            int recv_from = va_arg(args, int);
            result = (unsigned long)level2_recv_from(client_id, recv_from);
            if (result == 0) {
                printf("NUM_AEP_RECV ERROR!\n");
            }
            break;
        }
        case NUM_AEP_RECOVER: {
            int client_id = va_arg(args, int);
            aep_recovery_stats_t stats;
            aep_recovery_clear_stats(&stats);
            if (client_id == AEP_DSM_RECOVERY_ALL_CLIENTS)
                aep_recover_all(&stats);
            else
                aep_recover_client(client_id, &stats);
            aep_recovery_print_stats(&stats);
            result = stats.segments_abandoned + stats.mailbox_refs_released + stats.redo_refs_replayed;
            break;
        }
        default:
            fprintf(stderr, "Invalid call number: %d\n", call_number);
            break;
    }

    va_end(args);
    return result;
}

#ifndef AEP_DSM_DIRECT_LINK
int main(int argc, char *argv[])
{
    if (argc == 2) {
        void **func = (void**)hexstrtoul(argv[1]);
        func[0] = foobar;
        foobar(100);
    }

    printf("aep_operations: 0x%lx\n", (unsigned long)aep_entry);
    aep_entry(NUM_AEP_MALLOC_INIT);
    printf("finish pv main.\n");

#ifndef CONFIG_AEP
    threadtest(argc, argv);
#endif

    return 0;
}

#ifdef CONFIG_AEP
void my_entry_point(int argc, char *argv[]) {
    main(argc, argv); //enter pv lib
}
#endif
#endif
