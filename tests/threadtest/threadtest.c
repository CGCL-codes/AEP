#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "aep_dsm_client.h"

#define NUM_CLIENT              40

static int niterations = 1000;
static int nobjects = 100000;
static int nthreads = 16;
static int work = 0;
static int sz = 8;
static int skip_init = 0;
static uintptr_t shadow_entry_addr = AEP_DSM_SHADOW_ENTRY_ADDR;
static aep_dsm_client_t aep_client;

typedef struct Foo_s {
    int x;
    int y;
} Foo;

typedef struct worker_arg_s {
    int client_id;
} worker_arg_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [threads [iterations [objects [work [sz]]]]] "
            "[--entry=0xaddr] [--skip-init]\n",
            prog);
}

static unsigned long parse_ulong(const char *s, const char *name)
{
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

static void parse_args(int argc, char *argv[])
{
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--entry=", 8) == 0) {
            shadow_entry_addr = (uintptr_t)parse_ulong(argv[i] + 8, "entry");
        } else if (strcmp(argv[i], "--skip-init") == 0) {
            skip_init = 1;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        } else {
            long value = (long)parse_ulong(argv[i], "argument");
            switch (positional++) {
            case 0: nthreads = (int)value; break;
            case 1: niterations = (int)value; break;
            case 2: nobjects = (int)value; break;
            case 3: work = (int)value; break;
            case 4: sz = (int)value; break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
    }

    if (nthreads <= 0 || nthreads >= NUM_CLIENT) {
        fprintf(stderr, "threads must be in [1, %d]\n", NUM_CLIENT - 1);
        exit(EXIT_FAILURE);
    }
    if (niterations <= 0 || nobjects <= 0 || sz <= 0 || work < 0) {
        fprintf(stderr, "iterations, objects, and sz must be positive; work must be non-negative\n");
        exit(EXIT_FAILURE);
    }
    if (nobjects < nthreads) {
        fprintf(stderr, "objects must be >= threads\n");
        exit(EXIT_FAILURE);
    }
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void do_work(int loops)
{
    for (volatile int d = 0; d < loops; d++) {
        volatile int f = 1;
        f = f + f;
        f = f * f;
        f = f + f;
        f = f * f;
    }
}


static void *worker(void *arg)
{
    worker_arg_t *worker_arg = (worker_arg_t *)arg;
    int client_id = worker_arg->client_id;
    int per_thread = nobjects / nthreads;
    void **objects = malloc(sizeof(void *) * (size_t)per_thread);
    if (objects == NULL) {
        perror("malloc objects");
        exit(EXIT_FAILURE);
    }

    for (int j = 0; j < niterations; j++) {
        for (int i = 0; i < per_thread; i++) {
            objects[i] = aep_dsm_malloc(&aep_client, client_id, (uint64_t)sz * sizeof(Foo), 0);
            assert(objects[i] != NULL);
            do_work(work);
        }

        for (int i = 0; i < per_thread; i++) {
            aep_dsm_free(&aep_client, client_id, objects[i]);
            do_work(work);
        }
    }

    free(objects);
    return NULL;
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    aep_client = aep_dsm_client_at(shadow_entry_addr);

    printf("AEP-DSM entry mode = %s\n", aep_dsm_entry_mode());
#ifndef AEP_DSM_DIRECT_LINK
    printf("AEP shadow entry = 0x%lx\n", (unsigned long)shadow_entry_addr);
#endif
    if (!skip_init)
        aep_dsm_init(&aep_client);

    pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)nthreads);
    worker_arg_t *args = malloc(sizeof(worker_arg_t) * (size_t)nthreads);
    if (threads == NULL || args == NULL) {
        perror("malloc thread metadata");
        exit(EXIT_FAILURE);
    }

    printf("Running AEP-DSM %s threadtest for %d threads, %d iterations, %d objects, %d work and %d sz...\n",
           aep_dsm_entry_mode(),
           nthreads, niterations, nobjects, work, sz);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < nthreads; i++) {
        args[i].client_id = i + 1;
        if (pthread_create(&threads[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create failed");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = elapsed_sec(&start, &end);
    double ops = (double)niterations * (double)(nobjects / nthreads) *
                 (double)nthreads * 2.0;

    printf("Time elapsed = %.6f\n", seconds);
    printf("Total operations = %.0f\n", ops);
    printf("Throughput = %.2f ops/sec\n", ops / seconds);

    free(args);
    free(threads);
    return 0;
}
