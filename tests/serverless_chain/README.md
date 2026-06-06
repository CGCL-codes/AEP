# AEP-DSM serverless chain test

Measures ownership-transfer cost by passing one allocated object through a chain
of clients. The default chain contains 10 clients, so each round performs 9
send/recv hops: client 1 -> client 2 -> ... -> client 10.

```sh
make DIRECT_LINK=1
./serverless_chain [rounds [object_size [clients]]] [--entry=0xaddr] [--skip-init]
```

`clients` must be in `[2, 10]` by default. The test verifies that the same object
arrives at every hop and updates payload metadata at each client.
