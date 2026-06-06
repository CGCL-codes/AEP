#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aep_dsm_client.h"

#define DEFAULT_WORKERS 4
#define DEFAULT_INITIAL_VERTICES 4096
#define DEFAULT_STAGES 4
#define DEFAULT_ITERATIONS 5
#define DEFAULT_EDGES_PER_VERTEX 8
#define MAX_WORKERS 16
#define MAX_CLIENT_ID 39
#define MSG_VALUES 64
#define DAMPING 0.85

typedef struct vertex_obj_s {
    uint64_t aep_ref;
    uint64_t aep_embedded_ref_cnt;
    uint64_t id;
    uint32_t out_degree;
    uint32_t reserved;
    double rank;
    double next_rank;
    uint32_t edges[];
} vertex_obj_t;

typedef struct pr_message_s {
    uint64_t aep_ref;
    uint64_t aep_embedded_ref_cnt;
    uint64_t stage;
    uint64_t iteration;
    uint32_t src_worker;
    uint32_t dst_worker;
    uint64_t offset;
    uint64_t value_count;
    double values[MSG_VALUES];
} pr_message_t;

typedef struct stage_data_s {
    int stage;
    int workers;
    int iterations;
    int edges_per_vertex;
    uint64_t vertices;
    uint64_t *part_start;
    uint64_t *part_count;
    uint64_t *chunk_count;
    vertex_obj_t ***vertex;
} stage_data_t;

typedef struct worker_arg_s {
    int worker_id;
    int client_id;
    stage_data_t *stage;
} worker_arg_t;

static int workers = DEFAULT_WORKERS;
static uint64_t initial_vertices = DEFAULT_INITIAL_VERTICES;
static int stages = DEFAULT_STAGES;
static int iterations = DEFAULT_ITERATIONS;
static int edges_per_vertex = DEFAULT_EDGES_PER_VERTEX;
static int skip_init = 0;
static uintptr_t shadow_entry_addr = AEP_DSM_SHADOW_ENTRY_ADDR;
static aep_dsm_client_t aep_client;
static pthread_barrier_t stage_barrier;
static struct timespec stage_start;
static struct timespec stage_end;
static uint64_t cumulative_aep_bytes = 0;
static uint64_t cumulative_message_bytes = 0;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [workers [initial_vertices [stages [iterations [edges_per_vertex]]]]] "
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

static void parse_args(int argc, char **argv)
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
            unsigned long value = parse_ulong(argv[i], "argument");
            switch (positional++) {
            case 0: workers = (int)value; break;
            case 1: initial_vertices = (uint64_t)value; break;
            case 2: stages = (int)value; break;
            case 3: iterations = (int)value; break;
            case 4: edges_per_vertex = (int)value; break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
    }

    if (workers <= 0 || workers > MAX_WORKERS || workers > MAX_CLIENT_ID) {
        fprintf(stderr, "workers must be in [1, %d]\n", MAX_WORKERS);
        exit(EXIT_FAILURE);
    }
    if (initial_vertices < (uint64_t)workers || stages <= 0 || iterations <= 0 || edges_per_vertex <= 0) {
        fprintf(stderr, "initial_vertices, stages, iterations, and edges_per_vertex must be positive\n");
        exit(EXIT_FAILURE);
    }
    if (sizeof(vertex_obj_t) + (uint64_t)edges_per_vertex * sizeof(uint32_t) > 960) {
        fprintf(stderr, "edges_per_vertex is too large for the allocator small-object bins\n");
        exit(EXIT_FAILURE);
    }
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static int owner_of_vertex(stage_data_t *stage, uint64_t vertex)
{
    uint64_t base = stage->vertices / (uint64_t)stage->workers;
    uint64_t rem = stage->vertices % (uint64_t)stage->workers;
    uint64_t split = (base + 1) * rem;

    if (vertex < split)
        return (int)(vertex / (base + 1));
    return (int)(rem + (vertex - split) / base);
}

static uint64_t local_index(stage_data_t *stage, int owner, uint64_t vertex)
{
    return vertex - stage->part_start[owner];
}

static void *aep_malloc_checked(int client_id, uint64_t size, uint32_t embedded_ref_cnt)
{
    void *obj = aep_dsm_malloc(&aep_client, client_id, size, embedded_ref_cnt);
    if (obj == NULL) {
        fprintf(stderr, "aep_dsm_malloc failed: client=%d size=%lu\n", client_id, (unsigned long)size);
        exit(EXIT_FAILURE);
    }
    cumulative_aep_bytes += size;
    return obj;
}

static void *recv_required(int client_id, int from_id)
{
    void *obj = NULL;
    while (obj == NULL)
        obj = aep_dsm_recv(&aep_client, client_id, from_id);
    return obj;
}

static void send_required(int client_id, int dst_id, void *obj)
{
    while (!aep_dsm_send(&aep_client, client_id, dst_id, obj)) {}
}

static void init_partitions(stage_data_t *stage)
{
    uint64_t next = 0;
    uint64_t base = stage->vertices / (uint64_t)stage->workers;
    uint64_t rem = stage->vertices % (uint64_t)stage->workers;

    for (int w = 0; w < stage->workers; w++) {
        uint64_t count = base + ((uint64_t)w < rem ? 1 : 0);
        stage->part_start[w] = next;
        stage->part_count[w] = count;
        stage->chunk_count[w] = (count + MSG_VALUES - 1) / MSG_VALUES;
        next += count;
    }
}

static vertex_obj_t *alloc_vertex(stage_data_t *stage, int worker_id, uint64_t id, double initial_rank)
{
    uint64_t bytes = sizeof(vertex_obj_t) + (uint64_t)stage->edges_per_vertex * sizeof(uint32_t);
    vertex_obj_t *v = aep_malloc_checked(worker_id + 1, bytes, 0);
    v->id = id;
    v->out_degree = (uint32_t)stage->edges_per_vertex;
    v->reserved = 0;
    v->rank = initial_rank;
    v->next_rank = 0.0;
    for (int e = 0; e < stage->edges_per_vertex; e++)
        v->edges[e] = (uint32_t)(mix64(id * 1315423911ULL + (uint64_t)e) % stage->vertices);
    return v;
}

static void init_stage(stage_data_t *stage, int stage_id, uint64_t vertices)
{
    memset(stage, 0, sizeof(*stage));
    stage->stage = stage_id;
    stage->workers = workers;
    stage->iterations = iterations;
    stage->edges_per_vertex = edges_per_vertex;
    stage->vertices = vertices;
    stage->part_start = calloc((size_t)workers, sizeof(uint64_t));
    stage->part_count = calloc((size_t)workers, sizeof(uint64_t));
    stage->chunk_count = calloc((size_t)workers, sizeof(uint64_t));
    stage->vertex = calloc((size_t)workers, sizeof(vertex_obj_t **));
    if (!stage->part_start || !stage->part_count || !stage->chunk_count || !stage->vertex) {
        fprintf(stderr, "host allocation failed\n");
        exit(EXIT_FAILURE);
    }

    init_partitions(stage);
    double initial_rank = 1.0 / (double)vertices;
    for (int w = 0; w < workers; w++) {
        stage->vertex[w] = calloc((size_t)stage->part_count[w], sizeof(vertex_obj_t *));
        if (!stage->vertex[w]) {
            fprintf(stderr, "host vertex table allocation failed\n");
            exit(EXIT_FAILURE);
        }
        for (uint64_t i = 0; i < stage->part_count[w]; i++) {
            uint64_t id = stage->part_start[w] + i;
            stage->vertex[w][i] = alloc_vertex(stage, w, id, initial_rank);
        }
    }
}

static void free_stage_host(stage_data_t *stage)
{
    for (int w = 0; w < stage->workers; w++)
        free(stage->vertex[w]);
    free(stage->vertex);
    free(stage->chunk_count);
    free(stage->part_start);
    free(stage->part_count);
}

static pr_message_t *alloc_message(int src_worker, int dst_worker, int stage, int iter, uint64_t offset, uint64_t count)
{
    pr_message_t *msg = aep_malloc_checked(src_worker + 1, sizeof(pr_message_t), 0);
    msg->stage = (uint64_t)stage;
    msg->iteration = (uint64_t)iter;
    msg->src_worker = (uint32_t)src_worker;
    msg->dst_worker = (uint32_t)dst_worker;
    msg->offset = offset;
    msg->value_count = count;
    memset(msg->values, 0, sizeof(msg->values));
    cumulative_message_bytes += sizeof(pr_message_t);
    return msg;
}

static pr_message_t ***alloc_out_messages(stage_data_t *stage, int id, int iter)
{
    pr_message_t ***msgs = calloc((size_t)stage->workers, sizeof(pr_message_t **));
    if (!msgs) {
        fprintf(stderr, "message table allocation failed\n");
        exit(EXIT_FAILURE);
    }
    for (int dst = 0; dst < stage->workers; dst++) {
        if (dst == id)
            continue;
        msgs[dst] = calloc((size_t)stage->chunk_count[dst], sizeof(pr_message_t *));
        if (!msgs[dst]) {
            fprintf(stderr, "message chunk table allocation failed\n");
            exit(EXIT_FAILURE);
        }
        for (uint64_t c = 0; c < stage->chunk_count[dst]; c++) {
            uint64_t off = c * MSG_VALUES;
            uint64_t remain = stage->part_count[dst] - off;
            uint64_t count = remain < MSG_VALUES ? remain : MSG_VALUES;
            msgs[dst][c] = alloc_message(id, dst, stage->stage, iter, off, count);
        }
    }
    return msgs;
}

static void free_out_message_tables(stage_data_t *stage, pr_message_t ***msgs)
{
    for (int dst = 0; dst < stage->workers; dst++)
        free(msgs[dst]);
    free(msgs);
}

static void *pagerank_worker(void *arg)
{
    worker_arg_t *worker = (worker_arg_t *)arg;
    stage_data_t *stage = worker->stage;
    int id = worker->worker_id;
    int client_id = worker->client_id;
    uint64_t local_count = stage->part_count[id];
    double *self_accum = calloc((size_t)local_count, sizeof(double));

    if (!self_accum) {
        fprintf(stderr, "worker host allocation failed\n");
        exit(EXIT_FAILURE);
    }

    pthread_barrier_wait(&stage_barrier);
    if (id == 0)
        clock_gettime(CLOCK_MONOTONIC, &stage_start);

    for (int iter = 0; iter < stage->iterations; iter++) {
        double base_rank = (1.0 - DAMPING) / (double)stage->vertices;
        memset(self_accum, 0, (size_t)local_count * sizeof(double));
        for (uint64_t i = 0; i < local_count; i++)
            stage->vertex[id][i]->next_rank = base_rank;

        pr_message_t ***out_msgs = alloc_out_messages(stage, id, iter);

        for (uint64_t i = 0; i < local_count; i++) {
            vertex_obj_t *v = stage->vertex[id][i];
            double share = v->rank / (double)v->out_degree;
            for (uint32_t e = 0; e < v->out_degree; e++) {
                uint64_t dst_vertex = v->edges[e];
                int owner = owner_of_vertex(stage, dst_vertex);
                uint64_t off = local_index(stage, owner, dst_vertex);
                if (owner == id) {
                    self_accum[off] += share;
                } else {
                    uint64_t chunk = off / MSG_VALUES;
                    uint64_t slot = off % MSG_VALUES;
                    out_msgs[owner][chunk]->values[slot] += share;
                }
            }
        }

        for (uint64_t i = 0; i < local_count; i++)
            stage->vertex[id][i]->next_rank += DAMPING * self_accum[i];

        uint64_t max_chunks = 0;
        for (int w = 0; w < stage->workers; w++) {
            if (stage->chunk_count[w] > max_chunks)
                max_chunks = stage->chunk_count[w];
        }

        for (uint64_t chunk = 0; chunk < max_chunks; chunk++) {
            for (int dst = 0; dst < stage->workers; dst++) {
                if (dst != id && chunk < stage->chunk_count[dst])
                    send_required(client_id, dst + 1, out_msgs[dst][chunk]);
            }

            pthread_barrier_wait(&stage_barrier);

            if (chunk < stage->chunk_count[id]) {
                int received = 0;
                int seen[MAX_WORKERS] = {0};
                while (received < stage->workers - 1) {
                    pr_message_t *msg = recv_required(client_id, 0);
                    if (msg->stage != (uint64_t)stage->stage || msg->iteration != (uint64_t)iter ||
                        msg->dst_worker != (uint32_t)id || msg->offset != chunk * MSG_VALUES ||
                        msg->src_worker >= (uint32_t)stage->workers || msg->src_worker == (uint32_t)id ||
                        seen[msg->src_worker]) {
                        fprintf(stderr,
                                "pagerank message mismatch: worker=%d iter=%d got src=%u dst=%u chunk=%lu\n",
                                id, iter, msg->src_worker, msg->dst_worker, (unsigned long)chunk);
                        exit(EXIT_FAILURE);
                    }
                    seen[msg->src_worker] = 1;
                    received++;
                    for (uint64_t i = 0; i < msg->value_count; i++) {
                        uint64_t local = msg->offset + i;
                        stage->vertex[id][local]->next_rank += DAMPING * msg->values[i];
                    }
                    aep_dsm_free(&aep_client, client_id, msg);
                }
            }

            pthread_barrier_wait(&stage_barrier);
        }

        for (uint64_t i = 0; i < local_count; i++) {
            vertex_obj_t *v = stage->vertex[id][i];
            v->rank = v->next_rank;
            v->next_rank = 0.0;
        }
        free_out_message_tables(stage, out_msgs);
    }

    pthread_barrier_wait(&stage_barrier);
    if (id == 0)
        clock_gettime(CLOCK_MONOTONIC, &stage_end);

    free(self_accum);
    return NULL;
}

static double checksum_stage(stage_data_t *stage)
{
    double sum = 0.0;
    for (int w = 0; w < stage->workers; w++) {
        for (uint64_t i = 0; i < stage->part_count[w]; i++)
            sum += stage->vertex[w][i]->rank;
    }
    return sum;
}

static void run_stage(int stage_id, uint64_t vertices)
{
    stage_data_t stage;
    pthread_t *threads = calloc((size_t)workers, sizeof(pthread_t));
    worker_arg_t *args = calloc((size_t)workers, sizeof(worker_arg_t));
    if (!threads || !args) {
        fprintf(stderr, "thread allocation failed\n");
        exit(EXIT_FAILURE);
    }

    uint64_t aep_before = cumulative_aep_bytes;
    uint64_t msg_before = cumulative_message_bytes;
    init_stage(&stage, stage_id, vertices);
    pthread_barrier_init(&stage_barrier, NULL, (unsigned)workers);

    for (int w = 0; w < workers; w++) {
        args[w].worker_id = w;
        args[w].client_id = w + 1;
        args[w].stage = &stage;
        pthread_create(&threads[w], NULL, pagerank_worker, &args[w]);
    }
    for (int w = 0; w < workers; w++)
        pthread_join(threads[w], NULL);
    pthread_barrier_destroy(&stage_barrier);

    double seconds = elapsed_sec(&stage_start, &stage_end);
    double traversed_edges = (double)vertices * (double)edges_per_vertex * (double)iterations;
    double checksum = checksum_stage(&stage);
    uint64_t stage_aep_bytes = cumulative_aep_bytes - aep_before;
    uint64_t stage_message_bytes = cumulative_message_bytes - msg_before;

    printf("[stage %d]\n", stage_id);
    printf("  vertices = %lu\n", (unsigned long)vertices);
    printf("  edges = %lu\n", (unsigned long)(vertices * (uint64_t)edges_per_vertex));
    printf("  iterations = %d\n", iterations);
    printf("  stage AEP allocation = %lu bytes\n", (unsigned long)stage_aep_bytes);
    printf("  cumulative AEP allocation = %lu bytes\n", (unsigned long)cumulative_aep_bytes);
    printf("  message allocation = %lu bytes\n", (unsigned long)stage_message_bytes);
    printf("  time = %.6f s\n", seconds);
    printf("  throughput = %.6f Medges/sec\n", traversed_edges / 1000000.0 / seconds);
    printf("  rank checksum = %.12f\n", checksum);

    free_stage_host(&stage);
    free(args);
    free(threads);
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    aep_client = aep_dsm_client_at(shadow_entry_addr);

    printf("AEP-DSM entry mode = %s\n", aep_dsm_entry_mode());
#ifndef AEP_DSM_DIRECT_LINK
    printf("AEP shadow entry = 0x%lx\n", (unsigned long)shadow_entry_addr);
#endif
    printf("workers = %d, initial_vertices = %lu, stages = %d, iterations = %d, edges_per_vertex = %d\n",
           workers, (unsigned long)initial_vertices, stages, iterations, edges_per_vertex);

    if (!skip_init)
        aep_dsm_init(&aep_client);

    uint64_t vertices = initial_vertices;
    for (int s = 0; s < stages; s++) {
        run_stage(s, vertices);
        vertices *= 2;
    }

    return 0;
}
