# AEP-DSM KV Test

A multi-threaded key-value benchmark modeled after CXL-SHM's
`test/benchmark/kv.cpp`.

The test keeps a partitioned hash table in the benchmark process and allocates
all KV nodes through AEP-DSM via `../aep_dsm_client`. Operations enter the AEP
allocator through the shadow-entry function pointer.

Build:

```bash
make
```

Run:

```bash
./kv [threads [read_ratio [iterations]]] [--entry=0xaddr] [--skip-init]
```

Defaults match CXL-SHM's KV benchmark shape: `8` threads, `read_ratio=9`, and
`1,000,000` operations per thread.

## Build modes

Default build keeps the test in shadow-entry mode and expects AEP-DSM to be
loaded in the AEP address space:

```sh
make
```

For allocator debugging, build the same test with direct allocator linkage:

```sh
make DIRECT_LINK=1
```

`DIRECT_LINK=1` builds `../../allocator/libaepmalloc.a` and routes the client
ABI to allocator `aep_entry()` directly, so malloc/free/link/send/recv requests
exercise the allocator without requiring the AEP loader or shadow-entry address.

## Workload

The benchmark runs in two phases:

1. Preload phase, not timed: each worker inserts `iterations` deterministic keys
   into the KV store through AEP-DSM allocation.
2. Read phase, timed: workers issue `iterations` random reads against the
   preloaded key set. Every read must hit and the returned value is checked.

The second positional argument is still parsed as `read_ratio` so the command
line remains compatible with CXL-SHM's KV benchmark shape, but it is not used by
this read-hit workload.

