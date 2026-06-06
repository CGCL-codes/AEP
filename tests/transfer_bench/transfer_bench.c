#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aep_dsm_client.h"

#define DEFAULT_ROUNDS 1000000
#define DEFAULT_OBJECT_SIZE 64
#define DEFAULT_TREE_DEPTH 10
#define DEFAULT_SRC_CLIENT 1
#define DEFAULT_DST_CLIENT 2
#define MAX_TREE_DEPTH 20
#define AEP_OBJ_HEADER_BYTES 8
#define AEP_USER_METADATA_BYTES 16

typedef enum bench_mode_e {
    MODE_ALL,
    MODE_SINGLE,
    MODE_TREE,
} bench_mode_t;

typedef struct object_payload_s {
    uint64_t aep_ref;
    uint64_t aep_embedded_ref_cnt;
    uint64_t magic;
    uint64_t seq;
    uint64_t checksum;
    unsigned char data[];
} object_payload_t;

typedef struct tree_node_s {
    uint64_t aep_ref;
    uint64_t aep_embedded_ref_cnt;
    uint64_t left_ref;
    uint64_t right_ref;
    uint64_t index;
    uint64_t checksum;
} tree_node_t;

static int rounds = DEFAULT_ROUNDS;
static size_t object_size = DEFAULT_OBJECT_SIZE;
static int tree_depth = DEFAULT_TREE_DEPTH;
static int src_client = DEFAULT_SRC_CLIENT;
static int dst_client = DEFAULT_DST_CLIENT;
static int skip_init = 0;
static bench_mode_t mode = MODE_ALL;
static uintptr_t shadow_entry_addr = AEP_DSM_SHADOW_ENTRY_ADDR;
static aep_dsm_client_t aep_client;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [rounds [object_size [tree_depth]]] "
            "[--mode=all|single|tree] [--src=N] [--dst=N] "
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

static int parse_int_arg(const char *s, const char *name)
{
    unsigned long value = parse_ulong(s, name);
    if (value > 0x7fffffffUL) {
        fprintf(stderr, "%s is too large: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)value;
}

static void parse_mode(const char *s)
{
    if (strcmp(s, "all") == 0) {
        mode = MODE_ALL;
    } else if (strcmp(s, "single") == 0) {
        mode = MODE_SINGLE;
    } else if (strcmp(s, "tree") == 0) {
        mode = MODE_TREE;
    } else {
        fprintf(stderr, "invalid mode: %s\n", s);
        exit(EXIT_FAILURE);
    }
}

static void parse_args(int argc, char **argv)
{
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--entry=", 8) == 0) {
            shadow_entry_addr = (uintptr_t)parse_ulong(argv[i] + 8, "entry");
        } else if (strncmp(argv[i], "--mode=", 7) == 0) {
            parse_mode(argv[i] + 7);
        } else if (strncmp(argv[i], "--src=", 6) == 0) {
            src_client = parse_int_arg(argv[i] + 6, "src");
        } else if (strncmp(argv[i], "--dst=", 6) == 0) {
            dst_client = parse_int_arg(argv[i] + 6, "dst");
        } else if (strcmp(argv[i], "--skip-init") == 0) {
            skip_init = 1;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        } else {
            unsigned long value = parse_ulong(argv[i], "argument");
            switch (positional++) {
            case 0: rounds = (int)value; break;
            case 1: object_size = (size_t)value; break;
            case 2: tree_depth = (int)value; break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
    }

    if (rounds <= 0) {
        fprintf(stderr, "rounds must be positive\n");
        exit(EXIT_FAILURE);
    }
    if (object_size < sizeof(object_payload_t))
        object_size = sizeof(object_payload_t);
    if (tree_depth < 0 || tree_depth > MAX_TREE_DEPTH) {
        fprintf(stderr, "tree_depth must be in [0, %d]\n", MAX_TREE_DEPTH);
        exit(EXIT_FAILURE);
    }
    if (src_client <= 0 || dst_client <= 0 || src_client == dst_client) {
        fprintf(stderr, "src and dst must be positive and different\n");
        exit(EXIT_FAILURE);
    }
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static uint64_t checksum_for(uint64_t value)
{
    return value ^ 0xaed500005eed1234ULL ^ (value << 17) ^ (value >> 9);
}

static void *recv_required(int client_id, int from_id)
{
    void *obj = NULL;
    while (obj == NULL)
        obj = aep_dsm_recv(&aep_client, client_id, from_id);
    return obj;
}

static void *object_to_ref(void *obj)
{
    return (void *)((char *)obj - AEP_OBJ_HEADER_BYTES);
}

static void *ref_to_object(uint64_t ref)
{
    return (void *)((char *)(uintptr_t)ref + AEP_OBJ_HEADER_BYTES);
}

static void run_single_object(void)
{
    object_payload_t *obj = aep_dsm_malloc(&aep_client, src_client, object_size, 0);
    if (obj == NULL) {
        fprintf(stderr, "single-object malloc failed\n");
        exit(EXIT_FAILURE);
    }
    memset((char *)obj + AEP_USER_METADATA_BYTES, 0,
           object_size - AEP_USER_METADATA_BYTES);
    obj->magic = 0xaed5000000000001ULL;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < rounds; i++) {
        obj->seq = (uint64_t)i;
        obj->checksum = checksum_for(obj->seq);

        if (!aep_dsm_send(&aep_client, src_client, dst_client, obj)) {
            fprintf(stderr, "single-object send failed: %d -> %d\n", src_client, dst_client);
            exit(EXIT_FAILURE);
        }
        obj = recv_required(dst_client, src_client);
        if (obj->magic != 0xaed5000000000001ULL || obj->checksum != checksum_for(obj->seq)) {
            fprintf(stderr, "single-object payload mismatch at round %d\n", i);
            exit(EXIT_FAILURE);
        }

        if (!aep_dsm_send(&aep_client, dst_client, src_client, obj)) {
            fprintf(stderr, "single-object send failed: %d -> %d\n", dst_client, src_client);
            exit(EXIT_FAILURE);
        }
        obj = recv_required(src_client, dst_client);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = elapsed_sec(&start, &end);
    double transfers = (double)rounds * 2.0;

    printf("[single-object]\n");
    printf("  rounds = %d\n", rounds);
    printf("  object_size = %zu\n", object_size);
    printf("  transfers = %.0f\n", transfers);
    printf("  time = %.6f s\n", seconds);
    printf("  throughput = %.6f Mtransfers/sec\n", transfers / 1000000.0 / seconds);
    printf("  latency = %.2f ns/transfer\n", seconds * 1000000000.0 / transfers);
}

static tree_node_t *alloc_tree_node(int client_id, uint64_t index)
{
    tree_node_t *node = aep_dsm_malloc(&aep_client, client_id, sizeof(*node), 2);
    if (node == NULL) {
        fprintf(stderr, "tree node malloc failed\n");
        exit(EXIT_FAILURE);
    }
    node->left_ref = 0;
    node->right_ref = 0;
    node->index = index;
    node->checksum = checksum_for(index);
    return node;
}

static tree_node_t *build_tree(int client_id, int depth, uint64_t index)
{
    tree_node_t *node = alloc_tree_node(client_id, index);
    if (depth == 0)
        return node;

    tree_node_t *left = build_tree(client_id, depth - 1, index * 2);
    tree_node_t *right = build_tree(client_id, depth - 1, index * 2 + 1);
    aep_dsm_link(&aep_client, client_id, &node->left_ref, (uint64_t)(uintptr_t)object_to_ref(left));
    aep_dsm_link(&aep_client, client_id, &node->right_ref, (uint64_t)(uintptr_t)object_to_ref(right));
    return node;
}

static uint64_t expected_nodes_for_depth(int depth)
{
    return (UINT64_C(1) << (depth + 1)) - 1;
}

static uint64_t traverse_tree(tree_node_t *root, uint64_t *checksum)
{
    if (root == NULL)
        return 0;
    if (root->checksum != checksum_for(root->index)) {
        fprintf(stderr, "tree node checksum mismatch at index %lu\n", (unsigned long)root->index);
        exit(EXIT_FAILURE);
    }

    *checksum ^= root->checksum + root->index;
    uint64_t visited = 1;
    if (root->left_ref != 0)
        visited += traverse_tree(ref_to_object(root->left_ref), checksum);
    if (root->right_ref != 0)
        visited += traverse_tree(ref_to_object(root->right_ref), checksum);
    return visited;
}

static void run_binary_tree(void)
{
    tree_node_t *root = build_tree(src_client, tree_depth, 1);
    uint64_t expected_nodes = expected_nodes_for_depth(tree_depth);
    double transfer_seconds = 0.0;
    double traversal_seconds = 0.0;
    uint64_t total_visited = 0;
    uint64_t checksum = 0;

    for (int i = 0; i < rounds; i++) {
        struct timespec t0, t1, t2;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (!aep_dsm_send(&aep_client, src_client, dst_client, root)) {
            fprintf(stderr, "tree root send failed: %d -> %d\n", src_client, dst_client);
            exit(EXIT_FAILURE);
        }
        root = recv_required(dst_client, src_client);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        uint64_t round_checksum = 0;
        uint64_t visited = traverse_tree(root, &round_checksum);
        clock_gettime(CLOCK_MONOTONIC, &t2);

        if (visited != expected_nodes) {
            fprintf(stderr, "tree traversal visited %lu nodes, expected %lu\n",
                    (unsigned long)visited, (unsigned long)expected_nodes);
            exit(EXIT_FAILURE);
        }
        checksum ^= round_checksum;
        total_visited += visited;
        transfer_seconds += elapsed_sec(&t0, &t1);
        traversal_seconds += elapsed_sec(&t1, &t2);

        if (!aep_dsm_send(&aep_client, dst_client, src_client, root)) {
            fprintf(stderr, "tree root return failed: %d -> %d\n", dst_client, src_client);
            exit(EXIT_FAILURE);
        }
        root = recv_required(src_client, dst_client);
    }

    printf("[binary-tree-root-transfer]\n");
    printf("  rounds = %d\n", rounds);
    printf("  depth = %d\n", tree_depth);
    printf("  nodes/tree = %lu\n", (unsigned long)expected_nodes);
    printf("  forward root transfer time = %.6f s\n", transfer_seconds);
    printf("  forward root transfer latency = %.2f ns/transfer\n",
           transfer_seconds * 1000000000.0 / (double)rounds);
    printf("  receiver traversal time = %.6f s\n", traversal_seconds);
    printf("  receiver traversal latency = %.2f ns/tree\n",
           traversal_seconds * 1000000000.0 / (double)rounds);
    printf("  receiver traversal latency = %.2f ns/node\n",
           traversal_seconds * 1000000000.0 / (double)total_visited);
    printf("  traversal checksum = 0x%lx\n", (unsigned long)checksum);
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    aep_client = aep_dsm_client_at(shadow_entry_addr);

    printf("AEP-DSM entry mode = %s\n", aep_dsm_entry_mode());
#ifndef AEP_DSM_DIRECT_LINK
    printf("AEP shadow entry = 0x%lx\n", (unsigned long)shadow_entry_addr);
#endif
    printf("clients = %d -> %d\n", src_client, dst_client);

    if (!skip_init)
        aep_dsm_init(&aep_client);

    if (mode == MODE_ALL || mode == MODE_SINGLE)
        run_single_object();
    if (mode == MODE_ALL || mode == MODE_TREE)
        run_binary_tree();

    return 0;
}
