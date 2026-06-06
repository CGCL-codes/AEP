#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aep_dsm_client.h"

#define NUM_CLIENT      40
#define KV_PARTITIONS   64
#define KV_HASH_BITS    17
#define KV_BUCKETS      (1ULL << KV_HASH_BITS)

typedef struct kv_node_s {
    uint64_t key;
    uint64_t value;
    struct kv_node_s *next;
} kv_node_t;

typedef struct kv_bucket_s {
    pthread_mutex_t lock;
    kv_node_t *head;
} kv_bucket_t;

typedef struct worker_arg_s {
    int task_id;
    int client_id;
} worker_arg_t;

static int niterations = 1000000;
static int nthreads = 8;
static int read_ratio = 9;
static uint64_t *preload_keys;
static int skip_init = 0;
static int write_only = 0;
static uintptr_t shadow_entry_addr = AEP_DSM_SHADOW_ENTRY_ADDR;
static aep_dsm_client_t aep_client;
static kv_bucket_t *table;
static pthread_barrier_t barrier;
static struct timespec bench_start;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [threads [read_ratio [iterations]]] "
            "[--entry=0xaddr] [--skip-init]\n"
            "  note: read_ratio is accepted for CXL-SHM CLI compatibility; "
            "this test runs a preload phase followed by read-only hits.\n",
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
        } else if (strcmp(argv[i], "--mode=write") == 0 || strcmp(argv[i], "--write-only") == 0) {
            write_only = 1;
        } else if (strcmp(argv[i], "--mode=readhit") == 0) {
            write_only = 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        } else {
            long value = (long)parse_ulong(argv[i], "argument");
            switch (positional++) {
            case 0: nthreads = (int)value; break;
            case 1: read_ratio = (int)value; break;
            case 2: niterations = (int)value; break;
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
    if (read_ratio < 0 || niterations <= 0) {
        fprintf(stderr, "read_ratio must be non-negative and iterations must be positive\n");
        exit(EXIT_FAILURE);
    }
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static uint64_t rng_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static kv_bucket_t *bucket_for(uint64_t key)
{
    uint64_t partition = (key >> 56) & (KV_PARTITIONS - 1);
    uint64_t hash = key & (KV_BUCKETS - 1);
    return &table[partition * KV_BUCKETS + hash];
}

static void kv_put(int client_id, uint64_t key, uint64_t value)
{
    kv_bucket_t *bucket = bucket_for(key);

    pthread_mutex_lock(&bucket->lock);
    for (kv_node_t *node = bucket->head; node != NULL; node = node->next) {
        if (node->key == key) {
            node->value = value;
            pthread_mutex_unlock(&bucket->lock);
            return;
        }
    }

    kv_node_t *node = aep_dsm_malloc(&aep_client, client_id, sizeof(*node), 0);
    if (node == NULL) {
        pthread_mutex_unlock(&bucket->lock);
        fprintf(stderr, "aep_dsm_malloc failed in kv_put\n");
        exit(EXIT_FAILURE);
    }
    node->key = key;
    node->value = value;
    node->next = bucket->head;
    bucket->head = node;
    pthread_mutex_unlock(&bucket->lock);
}

static int kv_get(uint64_t key, uint64_t *value)
{
    kv_bucket_t *bucket = bucket_for(key);
    int found = 0;

    pthread_mutex_lock(&bucket->lock);
    for (kv_node_t *node = bucket->head; node != NULL; node = node->next) {
        if (node->key == key) {
            *value = node->value;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&bucket->lock);
    return found;
}

static uint64_t value_for_key(uint64_t key)
{
    return key ^ 0x9e3779b97f4a7c15ULL;
}

static uint64_t preload_key_for(int task_id, int index)
{
    return ((uint64_t)task_id << 56) | (uint64_t)(index + 1);
}

static void preload_worker_keys(int task_id, int client_id)
{
    size_t base = (size_t)task_id * (size_t)niterations;

    for (int i = 0; i < niterations; i++) {
        uint64_t key = preload_key_for(task_id, i);
        preload_keys[base + (size_t)i] = key;
        kv_put(client_id, key, value_for_key(key));
    }
}

static void read_existing_key(uint64_t *rng)
{
    uint64_t owner = rng_next(rng) % (uint64_t)nthreads;
    uint64_t index = rng_next(rng) % (uint64_t)niterations;
    uint64_t key = preload_keys[owner * (uint64_t)niterations + index];
    uint64_t expected = value_for_key(key);
    uint64_t actual = 0;

    if (!kv_get(key, &actual)) {
        fprintf(stderr, "kv_get missed preloaded key 0x%lx\n", (unsigned long)key);
        exit(EXIT_FAILURE);
    }
    if (actual != expected) {
        fprintf(stderr,
                "kv_get value mismatch for key 0x%lx: expected 0x%lx, got 0x%lx\n",
                (unsigned long)key, (unsigned long)expected, (unsigned long)actual);
        exit(EXIT_FAILURE);
    }
}

static void write_worker_keys(int task_id, int client_id)
{
    for (int i = 0; i < niterations; i++) {
        uint64_t key = preload_key_for(task_id, i);
        kv_put(client_id, key, value_for_key(key));
    }
}

static void *worker(void *arg)
{
    worker_arg_t *worker_arg = (worker_arg_t *)arg;
    int task_id = worker_arg->task_id;
    int client_id = worker_arg->client_id;
    uint64_t rng = 0xd1b54a32d192ed03ULL ^ ((uint64_t)task_id << 32) ^ (uint64_t)niterations;

    if (!write_only)
        preload_worker_keys(task_id, client_id);

    pthread_barrier_wait(&barrier);
    if (task_id == 0)
        clock_gettime(CLOCK_MONOTONIC, &bench_start);
    pthread_barrier_wait(&barrier);

    if (write_only) {
        write_worker_keys(task_id, client_id);
    } else {
        for (int i = 0; i < niterations; i++)
            read_existing_key(&rng);
    }

    return NULL;
}

static void init_table(void)
{
    size_t bucket_count = KV_PARTITIONS * KV_BUCKETS;
    size_t key_count = (size_t)nthreads * (size_t)niterations;

    table = calloc(bucket_count, sizeof(*table));
    if (table == NULL) {
        perror("calloc kv table");
        exit(EXIT_FAILURE);
    }
    preload_keys = malloc(key_count * sizeof(*preload_keys));
    if (preload_keys == NULL) {
        perror("malloc preload keys");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < bucket_count; i++)
        pthread_mutex_init(&table[i].lock, NULL);
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

    init_table();
    pthread_barrier_init(&barrier, NULL, (unsigned)nthreads);

    pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)nthreads);
    worker_arg_t *args = malloc(sizeof(worker_arg_t) * (size_t)nthreads);
    if (threads == NULL || args == NULL) {
        perror("malloc thread metadata");
        exit(EXIT_FAILURE);
    }

    if (write_only) {
        printf("Running AEP-DSM %s KV write-only test for %d threads, "
               "%d write ops/thread...\n",
               aep_dsm_entry_mode(), nthreads, niterations);
    } else {
        printf("Running AEP-DSM %s KV two-phase read-hit test for %d threads, "
               "%d preload keys/thread, %d read ops/thread...\n",
               aep_dsm_entry_mode(), nthreads, niterations, niterations);
        printf("Compatibility read_ratio argument = %d (not used in read-hit phase)\n", read_ratio);
    }

    for (int i = 0; i < nthreads; i++) {
        args[i].task_id = i;
        args[i].client_id = i + 1;
        if (pthread_create(&threads[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create failed");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    struct timespec bench_end;
    clock_gettime(CLOCK_MONOTONIC, &bench_end);
    double seconds = elapsed_sec(&bench_start, &bench_end);
    double ops = (double)niterations * (double)nthreads;

    printf("Time elapsed = %.6f\n", seconds);
    printf("Total operations = %.0f\n", ops);
    printf("Throughput = %.2f ops/sec\n", ops / seconds);
    printf("Throughput = %.6f MOPS\n", ops / 1000000.0 / seconds);

    free(preload_keys);
    free(args);
    free(threads);
    return 0;
}
