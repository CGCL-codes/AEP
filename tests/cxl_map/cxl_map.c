#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __NR_aep_init
#define __NR_aep_init 463
#endif
#ifndef __NR_aep_exit_template
#define __NR_aep_exit_template 464
#endif
#ifndef __NR_aep_join
#define __NR_aep_join 465
#endif
#ifndef __NR_aep_map_cxl
#define __NR_aep_map_cxl 468
#endif

#define AEP_MAP_CXL_PROT_EXEC 0x1UL
#define DEFAULT_RW_PATTERN 0xaed5c0015eed1234ULL

typedef struct options_s {
    unsigned long phys;
    unsigned long size;
    unsigned long prot_flags;
    unsigned long long pattern;
    int have_phys;
    int have_size;
    int join;
    int rw_test;
    int no_exit;
} options_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --phys=0xADDR --size=0xSIZE [--exec] [--join] "
            "[--rw-test] [--pattern=0xVALUE] [--no-exit]\n",
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

static unsigned long long parse_ullong(const char *s, const char *name)
{
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

static void parse_args(int argc, char **argv, options_t *opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->pattern = DEFAULT_RW_PATTERN;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--phys=", 7) == 0) {
            opt->phys = parse_ulong(argv[i] + 7, "phys");
            opt->have_phys = 1;
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            opt->size = parse_ulong(argv[i] + 7, "size");
            opt->have_size = 1;
        } else if (strncmp(argv[i], "--pattern=", 10) == 0) {
            opt->pattern = parse_ullong(argv[i] + 10, "pattern");
        } else if (strcmp(argv[i], "--exec") == 0) {
            opt->prot_flags |= AEP_MAP_CXL_PROT_EXEC;
        } else if (strcmp(argv[i], "--join") == 0) {
            opt->join = 1;
        } else if (strcmp(argv[i], "--rw-test") == 0) {
            opt->rw_test = 1;
            opt->join = 1;
        } else if (strcmp(argv[i], "--no-exit") == 0) {
            opt->no_exit = 1;
        } else {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (!opt->have_phys || !opt->have_size || opt->size == 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    if (opt->rw_test && opt->size < sizeof(uint64_t)) {
        fprintf(stderr, "--rw-test requires at least %zu mapped bytes\n",
                sizeof(uint64_t));
        exit(EXIT_FAILURE);
    }
}

static int is_syscall_error(long ret)
{
    return (unsigned long)ret >= (unsigned long)-4095;
}

static long call0(long nr, const char *name)
{
    errno = 0;
    long ret = syscall(nr);
    if (is_syscall_error(ret)) {
        fprintf(stderr, "%s failed: ret=%ld errno=%d (%s)\n",
                name, ret, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ret;
}

static long call1(long nr, const char *name, unsigned long a0)
{
    errno = 0;
    long ret = syscall(nr, a0);
    if (is_syscall_error(ret)) {
        fprintf(stderr, "%s failed: ret=%ld errno=%d (%s)\n",
                name, ret, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ret;
}

static long call4(long nr, const char *name, unsigned long a0, unsigned long a1,
                  unsigned long a2, unsigned long a3)
{
    errno = 0;
    long ret = syscall(nr, a0, a1, a2, a3);
    if (is_syscall_error(ret)) {
        fprintf(stderr, "%s failed: ret=%ld errno=%d (%s)\n",
                name, ret, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ret;
}

static void run_rw_child(unsigned long aep_addr, unsigned long long pattern)
{
    volatile uint64_t *slot = (volatile uint64_t *)aep_addr;
    uint64_t old_value = slot[0];
    uint64_t first = (uint64_t)pattern;
    uint64_t second = ~first;

    slot[0] = first;
    if (slot[0] != first) {
        fprintf(stderr, "rw-test failed after first write: got=0x%016llx want=0x%016llx\n",
                (unsigned long long)slot[0], (unsigned long long)first);
        _exit(2);
    }

    slot[0] = second;
    if (slot[0] != second) {
        fprintf(stderr, "rw-test failed after second write: got=0x%016llx want=0x%016llx\n",
                (unsigned long long)slot[0], (unsigned long long)second);
        _exit(3);
    }

    printf("rw-test child: addr=0x%lx old=0x%016llx first=0x%016llx second=0x%016llx\n",
           aep_addr, (unsigned long long)old_value,
           (unsigned long long)first, (unsigned long long)second);
    fflush(stdout);
    _exit(0);
}

static void wait_for_child(pid_t pid, int rw_test)
{
    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }

    printf("aep child exit status=0x%x\n", status);
    if (rw_test) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "rw-test child failed\n");
            exit(EXIT_FAILURE);
        }
        printf("rw-test: PASS\n");
    }
}

int main(int argc, char **argv)
{
    options_t opt;
    parse_args(argc, argv, &opt);

    long template_id = call0(__NR_aep_init, "aep_init");
    printf("aep_init: template_id=%ld\n", template_id);

    long aep_addr = call4(__NR_aep_map_cxl, "aep_map_cxl",
                          (unsigned long)template_id, opt.phys, opt.size,
                          opt.prot_flags);
    printf("aep_map_cxl: phys=0x%lx size=0x%lx prot=0x%lx -> aep_addr=0x%lx\n",
           opt.phys, opt.size, opt.prot_flags, (unsigned long)aep_addr);

    if (opt.join) {
        long pid = call1(__NR_aep_join, "aep_join", (unsigned long)template_id);
        if (pid == 0) {
            if (opt.rw_test)
                run_rw_child((unsigned long)aep_addr, opt.pattern);
            _exit(0);
        }

        printf("aep_join: child_pid=%ld\n", pid);
        wait_for_child((pid_t)pid, opt.rw_test);
    }

    if (!opt.no_exit) {
        call1(__NR_aep_exit_template, "aep_exit_template", (unsigned long)template_id);
        printf("aep_exit_template: template_id=%ld\n", template_id);
    }

    return 0;
}
