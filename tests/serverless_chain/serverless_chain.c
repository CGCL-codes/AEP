#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aep_dsm_client.h"

#define DEFAULT_CLIENTS 10
#define MAX_CLIENTS 10
#define DEFAULT_ROUNDS 1000000
#define DEFAULT_OBJECT_SIZE 64

typedef struct chain_payload_s {
    uint64_t magic;
    uint64_t round;
    uint64_t hop;
    uint64_t checksum;
    unsigned char data[];
} chain_payload_t;

static int nclients = DEFAULT_CLIENTS;
static int rounds = DEFAULT_ROUNDS;
static size_t object_size = DEFAULT_OBJECT_SIZE;
static int skip_init = 0;
static uintptr_t shadow_entry_addr = AEP_DSM_SHADOW_ENTRY_ADDR;
static aep_dsm_client_t aep_client;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [rounds [object_size [clients]]] [--entry=0xaddr] [--skip-init]\n",
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
            case 0: rounds = (int)value; break;
            case 1: object_size = (size_t)value; break;
            case 2: nclients = (int)value; break;
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
    if (object_size < sizeof(chain_payload_t))
        object_size = sizeof(chain_payload_t);
    if (nclients < 2 || nclients > MAX_CLIENTS) {
        fprintf(stderr, "clients must be in [2, %d]\n", MAX_CLIENTS);
        exit(EXIT_FAILURE);
    }
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static uint64_t checksum_for(uint64_t round, uint64_t hop)
{
    return 0xaed55a5aULL ^ (round * 0x9e3779b97f4a7c15ULL) ^ (hop << 32);
}

static void init_payload(chain_payload_t *payload)
{
    memset(payload, 0, object_size);
    payload->magic = 0xaed500000000c001ULL;
}

static void verify_payload(chain_payload_t *payload, int expected_round, int expected_hop)
{
    if (payload->magic != 0xaed500000000c001ULL ||
        payload->round != (uint64_t)expected_round ||
        payload->hop != (uint64_t)expected_hop ||
        payload->checksum != checksum_for((uint64_t)expected_round, (uint64_t)expected_hop)) {
        fprintf(stderr,
                "payload mismatch: round=%lu hop=%lu checksum=0x%lx, expected round=%d hop=%d\n",
                (unsigned long)payload->round, (unsigned long)payload->hop,
                (unsigned long)payload->checksum, expected_round, expected_hop);
        exit(EXIT_FAILURE);
    }
}

static void stamp_payload(chain_payload_t *payload, int round, int hop)
{
    payload->round = (uint64_t)round;
    payload->hop = (uint64_t)hop;
    payload->checksum = checksum_for((uint64_t)round, (uint64_t)hop);
}

static void *recv_required(int client_id, int from_id)
{
    void *obj = NULL;
    while (obj == NULL)
        obj = aep_dsm_recv(&aep_client, client_id, from_id);
    return obj;
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    aep_client = aep_dsm_client_at(shadow_entry_addr);

    printf("AEP-DSM entry mode = %s\n", aep_dsm_entry_mode());
#ifndef AEP_DSM_DIRECT_LINK
    printf("AEP shadow entry = 0x%lx\n", (unsigned long)shadow_entry_addr);
#endif
    if (!skip_init)
        aep_dsm_init(&aep_client);

    chain_payload_t *payload = aep_dsm_malloc(&aep_client, 1, object_size, 0);
    if (payload == NULL) {
        fprintf(stderr, "aep_dsm_malloc failed\n");
        return EXIT_FAILURE;
    }
    init_payload(payload);

    printf("Running serverless chain: rounds=%d object_size=%zu clients=%d hops/round=%d\n",
           rounds, object_size, nclients, nclients - 1);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int r = 0; r < rounds; r++) {
        stamp_payload(payload, r, 0);

        for (int client = 1; client < nclients; client++) {
            if (!aep_dsm_send(&aep_client, client, client + 1, payload)) {
                fprintf(stderr, "send failed: %d -> %d\n", client, client + 1);
                return EXIT_FAILURE;
            }

            payload = recv_required(client + 1, client);
            verify_payload(payload, r, client - 1);
            stamp_payload(payload, r, client);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = elapsed_sec(&start, &end);
    double transfers = (double)rounds * (double)(nclients - 1);
    double ns_per_transfer = seconds * 1000000000.0 / transfers;

    printf("Time elapsed = %.6f\n", seconds);
    printf("Total ownership transfers = %.0f\n", transfers);
    printf("Throughput = %.2f transfers/sec\n", transfers / seconds);
    printf("Throughput = %.6f Mtransfers/sec\n", transfers / 1000000.0 / seconds);
    printf("Latency = %.2f ns/transfer\n", ns_per_transfer);

    return 0;
}
