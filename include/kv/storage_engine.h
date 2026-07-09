#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kv {

// FileHandle is purely an implementation detail (see src/file_handle.h).
// Forward-declaring it here instead of including the header keeps it out of
// the public API entirely - a consumer of StorageEngine never needs to know
// the store is backed by raw POSIX file descriptors.
class FileHandle;

// Bitcask-style storage engine: every write is appended to a single log file
// on disk, and an in-memory hash index maps each key to the offset/length of
// its most recent value in that log. A GET is one pread at a known offset;
// PUT/DELETE are one append + fsync. Opening an existing log file replays it
// from the start to rebuild the index before the store is usable.
//
// Thread-safe for concurrent use by multiple callers (e.g. one thread per
// network connection): GET takes a shared lock, PUT/DELETE take an
// exclusive one. Reads can run in parallel with each other; writes can't run
// in parallel with anything, because the underlying log is a single file and
// two writers can't safely claim overlapping offsets. That serialization
// point is the reason this doesn't use per-key sharded locks instead - it
// wouldn't buy real write concurrency without also splitting the log itself,
// which is a bigger structural change than a locking scheme.
class StorageEngine {
 public:
  explicit StorageEngine(std::filesystem::path log_path);
  ~StorageEngine();

  // Not movable or copyable: it owns a shared_mutex, and there's no
  // sensible meaning for "move" a mutex that might currently be locked by
  // another thread. Nothing in this project needs to move a StorageEngine
  // around - it's constructed once and shared by reference.
  StorageEngine(const StorageEngine&) = delete;
  StorageEngine& operator=(const StorageEngine&) = delete;
  StorageEngine(StorageEngine&&) = delete;
  StorageEngine& operator=(StorageEngine&&) = delete;

  void Put(std::string_view key, std::string_view value);
  std::optional<std::string> Get(std::string_view key) const;
  void Delete(std::string_view key);

  std::size_t KeyCount() const;

 private:
  // Points straight at the value bytes in the log, not the record start, so
  // Get() is exactly one pread with no header parsing on the read path.
  struct IndexEntry {
    std::uint64_t value_offset;
    std::uint32_t value_len;
  };

  void RebuildIndexFromLog();

  // Appends one record and returns its offset. Not locked internally - the
  // caller (Put/Delete) must already hold mutex_ for the whole logical
  // operation, not just this write, otherwise Delete's "does this key
  // exist" check and its later erase could race against another thread.
  std::uint64_t Append(std::string_view key, std::string_view value, bool tombstone);

  std::unique_ptr<FileHandle> file_;
  std::unordered_map<std::string, IndexEntry> index_;
  std::uint64_t next_offset_ = 0;  // end of the log; where the next append lands
  mutable std::shared_mutex mutex_;
};

}  // namespace kv
