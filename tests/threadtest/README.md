# AEP-DSM shadow-entry threadtest

This benchmark assumes the AEP-DSM allocator is already mapped in the AEP
address space. It uses the shared client-side interface in
`../aep_dsm_client` and does not link allocator code directly.

Default shadow entry:

```text
0xffffe8fffd801000
```

The address follows `tests/custom.ld` / `allocator/custom.ld`: AEP base is
`0xffffe8fffd800000`, and `.text.my_entry_point` is placed in the first
page-aligned entry section after ELF headers.

Build:

```bash
make
```

Run:

```bash
./threadtest [threads [iterations [objects [work [sz]]]]] [--entry=0xaddr] [--skip-init]
```

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

