# AEP-DSM Transfer Benchmark

This benchmark follows the AEP paper's memory-transfer workload shape for allocator testing.
It measures two cases:

- single-object ownership transfer with ping-pong between two clients;
- binary-tree root transfer, where only the root object is transferred and the receiver then traverses every tree node.

Build in convenient direct-link mode:

```bash
make DIRECT_LINK=1
```

Run:

```bash
./transfer_bench [rounds [object_size [tree_depth]]] [--mode=all|single|tree] [--src=N] [--dst=N]
```

The default clients are `1 -> 2`, which are on the same node in the current topology. Use `--dst=20` to exercise the cross-node path.
