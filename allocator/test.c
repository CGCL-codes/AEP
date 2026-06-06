#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "aeplib.h"
#include "topo.h"

static int niterations = 1000;
static int nobjects = 100000;
static int nthreads = 16;
static int work = 0;
static int sz = 8;

typedef struct Foo_s {
    int x;
    int y;
} Foo;

typedef struct worker_arg_s {
    int client_id;
} worker_arg_t;

static void *aep_malloc(int client_id, uint64_t size)
{
    return (void *)aep_entry(NUM_AEP_MALLOC, client_id, size, 0);
}

static void aep_free(int client_id, void *obj)
{
    aep_entry(NUM_AEP_FREE, client_id, obj);
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
            objects[i] = aep_malloc(client_id, (uint64_t)sz * sizeof(Foo));
            assert(objects[i] != NULL);
            do_work(work);
        }

        for (int i = 0; i < per_thread; i++) {
            aep_free(client_id, objects[i]);
            do_work(work);
        }
    }

    free(objects);
    return NULL;
}

void threadtest(int argc, char *argv[])
{
    if (argc >= 2) nthreads = atoi(argv[1]);
    if (argc >= 3) niterations = atoi(argv[2]);
    if (argc >= 4) nobjects = atoi(argv[3]);
    if (argc >= 5) work = atoi(argv[4]);
    if (argc >= 6) sz = atoi(argv[5]);

    if (nthreads <= 0 || nthreads >= NUM_CLIENT) {
        fprintf(stderr, "nthreads must be in [1, %d]\n", NUM_CLIENT - 1);
        exit(EXIT_FAILURE);
    }
    if (niterations <= 0 || nobjects <= 0 || sz <= 0) {
        fprintf(stderr, "niterations, nobjects, and sz must be positive\n");
        exit(EXIT_FAILURE);
    }
    if (nobjects < nthreads) {
        fprintf(stderr, "nobjects must be >= nthreads\n");
        exit(EXIT_FAILURE);
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)nthreads);
    worker_arg_t *args = malloc(sizeof(worker_arg_t) * (size_t)nthreads);
    if (threads == NULL || args == NULL) {
        perror("malloc thread metadata");
        exit(EXIT_FAILURE);
    }

    printf("Running AEP-DSM threadtest for %d threads, %d iterations, %d objects, %d work and %d sz...\n",
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

    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = elapsed_sec(&start, &end);
    double ops = (double)niterations * (double)(nobjects / nthreads) *
                 (double)nthreads * 2.0;

    printf("Time elapsed = %.6f\n", seconds);
    printf("Total operations = %.0f\n", ops);
    printf("Throughput = %.2f ops/sec\n", ops / seconds);

    free(args);
    free(threads);
}
