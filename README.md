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

## Building

Requires CMake 3.21+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
