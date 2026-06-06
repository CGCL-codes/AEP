# AEP-DSM Client Interface

This directory contains the shared client-side ABI used by tests under
`tests/`. Test programs include `aep_dsm_client.h` and dispatch allocator
operations through the AEP shadow entry instead of linking allocator objects.

Default shadow entry:

```text
0xffffe8fffd801000
```

The value follows `tests/custom.ld` / `allocator/custom.ld`, where the AEP base
is `0xffffe8fffd800000` and `.text.my_entry_point` is placed in the first
page-aligned entry section.

Typical usage:

```c
#include "aep_dsm_client.h"

aep_dsm_client_t client = aep_dsm_default_client();
aep_dsm_init(&client);
void *p = aep_dsm_malloc(&client, 1, 64, 0);
aep_dsm_free(&client, 1, p);
```

A test may override the entry address at compile time with
`-DAEP_DSM_SHADOW_ENTRY_ADDR=0x...` or at runtime by constructing a client with
`aep_dsm_client_at(addr)`.
