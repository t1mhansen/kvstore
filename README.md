# kv

A persistent key-value store written in modern C++20, built to understand
storage engines and network servers from the ground up rather than to be
production-ready. Scope is roughly Bitcask-meets-Redis: an append-only
durable log on disk, an in-memory index for fast lookups, and a TCP server
speaking a small custom protocol.

## Design goals

- **Durability**: writes are crash-safe via a write-ahead/append-only log;
  reopening the store after a crash recovers to the last complete write.
- **Fast reads**: an in-memory index (not a full re-scan) resolves `GET`
  against the on-disk log.
- **Concurrency**: many simultaneous client connections, with an explicit,
  justified locking strategy rather than a global lock by default.
- **Honest benchmarks**: throughput and p99 latency numbers measured with a
  real benchmark harness, not estimated.

Non-goals: distributed consensus/replication, a query language beyond
GET/SET/DEL(+TTL), and production hardening (auth, TLS, ACLs).

## Protocol

One command per line, testable by hand with `nc`:

```
GET key
SET key value
SETEX key seconds value
DEL key
COMPACT
```

## Building

Requires CMake 3.21+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Running

```sh
./build/server/kv_server <port> <log-path> [--no-fsync]
./build/bench/kv_bench --port <port> [--threads 4] [--duration 5] [--keys 1000] [--value-size 100] [--read-ratio 0.9]
```

## Benchmarks

Measured with `kv_bench` against a `Release`-built `kv_server` (`-DCMAKE_BUILD_TYPE=Release` -
a Debug build's numbers aren't representative). 8 client threads, 10s runs, 10,000-key
keyspace, 100-byte values. Machine: Apple M5, 10 cores, 16GB RAM, macOS 26.5.1.

| Workload                    | Sync policy | Throughput   | p50      | p99       |
|------------------------------|-------------|--------------|----------|-----------|
| 90% GET / 10% SET             | fsync always (default) | 128,071 ops/sec | 54.3 µs  | 151.4 µs  |
| 90% GET / 10% SET             | fsync disabled          | 167,047 ops/sec | 44.7 µs  | 94.5 µs   |
| 100% SET                      | fsync always (default) | 34,033 ops/sec  | 191.0 µs | 816.3 µs  |
| 100% SET                      | fsync disabled          | 92,821 ops/sec  | 70.6 µs  | 284.4 µs  |

Two things worth calling out:

- **fsync is a write-path cost, and it shows**: on a pure-SET workload, calling
  `fsync` on every write (the default - see `SyncPolicy` in
  [storage_engine.h](include/kv/storage_engine.h)) costs about 2.7x throughput
  and 2.7x p50 latency versus letting the OS decide when to flush. That's the
  real, measured price of the power-loss durability guarantee described there -
  not a guess.
- **The write lock's exclusivity means slow writes throttle reads too**: even in
  the 90%-read workload, disabling fsync sped things up by ~30%, not 0%. `Put`
  holds the exclusive lock for the entire append including the fsync call, so
  the 10% of operations that are slow writes were stalling the other 90% that
  could otherwise have run fully in parallel under the shared lock.

`--no-fsync` trades power-loss durability for throughput; it doesn't change
process-crash recovery, which works identically either way (see the
`SyncPolicy` comment for why those are different guarantees).
