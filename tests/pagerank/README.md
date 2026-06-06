# AEP-DSM PageRank Benchmark

Synthetic PageRank benchmark for AEP-DSM. Each stage doubles the vertex count,
allocates a larger graph/rank working set in AEP-DSM, and runs several PageRank
iterations. Workers exchange partition contributions with AEP-DSM `send`/`recv`
message objects.

Build for allocator debugging:

```bash
make DIRECT_LINK=1
```

Run:

```bash
./pagerank [workers [initial_vertices [stages [iterations [edges_per_vertex]]]]]
```

Defaults: `4 4096 4 5 8`.

The worker count is capped at 16 so the all-to-all message phase fits the
current mailbox depth.
