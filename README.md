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

## Architecture

```
  client 1 ─┐
  client 2 ─┼── TCP :port ── TcpServer (accept loop, 1 OS thread per connection)
  client N ─┘                        │
                                      │ Put / Get / Delete / Compact
                                      ▼
                               StorageEngine (shared_mutex: shared for reads,
                               │              exclusive for writes/compact)
                               │
                   ┌───────────┴───────────┐
                   ▼                       ▼
            index_ (RAM)             FileHandle (fd)
        {key -> offset, len,               │
         expires_at}                       ▼
                                      <log-path> on disk
                              (append-only, checksummed records)
```

`GET` is a hash lookup plus one `pread` at a known offset - it never scans the
log. `PUT`/`DELETE` append a record and (depending on `SyncPolicy`) `fsync`
before returning. Opening an existing log replays it front-to-back to rebuild
`index_`; a torn record at the very end (the only place a crash can leave one,
since every earlier record was already durable before the next write began)
gets detected and the file truncated back to the last good record.

## Design decisions

The full reasoning for each of these lives as comments next to the code, not
duplicated here - this is a map of where to look, not the argument itself.

| Decision | Choice | Why (see) |
|---|---|---|
| Storage engine | Bitcask-style: append-only log + in-memory hash index | [storage_engine.h](include/kv/storage_engine.h) - simple enough to fully own solo; an LSM-tree's compaction/leveling machinery wasn't worth the risk of a shaky, half-understood implementation |
| File I/O | Raw `pread`/`pwrite`/`fsync`, not iostreams | [file_handle.h](src/file_handle.h) - iostreams don't expose `fsync` at all, and durability needs to be an explicit, visible call |
| Corruption detection | Hand-rolled CRC32 | [crc32.h](src/crc32.h) - detects the burst errors a torn write actually produces; small enough to own outright instead of pulling in zlib |
| Network I/O model | Thread-per-connection | [tcp_server.h](server/tcp_server.h) - gives the concurrency work a real problem to solve; `epoll`/`kqueue` are platform-specific and add a lot of state-machine complexity for a project this size |
| Wire protocol | Line-based text | [protocol.h](server/protocol.h) - demoable by hand with `nc`; a length-prefixed binary format can't be typed at a terminal |
| Locking | Single `shared_mutex`: shared for GET, exclusive for PUT/DELETE/COMPACT | [storage_engine.h](include/kv/storage_engine.h) - reads never block each other; per-key sharded locks wouldn't buy real write concurrency anyway, since every write still funnels through one log file with one offset allocator |
| Compaction | Full rewrite + atomic `rename()` | [storage_engine.cpp](src/storage_engine.cpp) - `rename()`'s atomicity makes this crash-safe for free, no extra bookkeeping; costs a stop-the-world pause that segmented logs would avoid |

## Protocol

One command per line, testable by hand with `nc`:

```
GET key
SET key value
SETEX key seconds value
DEL key
COMPACT
```

Example session (after `kv_server 9000 /tmp/demo.log`):

```
$ nc localhost 9000
SET name tim
OK
GET name
VALUE tim
SETEX temp 5 gone-soon
OK
GET temp
VALUE gone-soon
... wait 5+ seconds ...
GET temp
NOT_FOUND
COMPACT
OK
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

## Limitations

Real, known gaps - not oversights, but scope decisions made explicit rather
than left for someone to discover the hard way:

- **The whole index must fit in RAM.** There's no support for a dataset
  larger than memory, and no plan to add one - that's the fundamental
  tradeoff of a hash-indexed design over a tree-indexed one like an LSM-tree.
- **Point lookups only.** The hash index has no ordering, so there's no range
  scan or prefix iteration - `GET`/`SET`/`DEL`/`SETEX` is the whole query
  surface.
- **`COMPACT` is stop-the-world.** It holds the exclusive lock for the entire
  rewrite, blocking every other connection until it finishes. A segmented log
  (compact old segments in the background while new writes land in a fresh
  one) would avoid this, but that's a bigger structural change than this
  project's single-log-file design.
- **Crash recovery assumes only the tail of the log can be corrupted** - true
  for a single writer that always fsyncs before its next write, but it means
  corruption introduced some other way (disk bit rot, manually editing the
  file) in the *middle* of the log wouldn't be detected or repaired; that
  logic would silently truncate away everything after the first bad record,
  valid or not.
- **Compaction fsyncs the new file's data but not its directory entry**, so a
  power loss at exactly the wrong instant around the `rename()` could in
  theory still lose the rename on some filesystems.
- **POSIX only.** Built directly on `pread`/`pwrite`/`fsync`/`ftruncate`, no
  Windows support.
- **No auth, no TLS.** Anyone who can open a TCP connection to the port has
  full read/write/compact access.
- **No graceful shutdown.** `kv_server` has no `Stop()` - Ctrl-C (or the OS
  reclaiming the process) is the only way to stop it. Fine for a
  demo/benchmark process, not for anything meant to stay up.
