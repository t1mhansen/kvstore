#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
// Single-threaded/single-writer for now - no internal locking. Concurrent
// access is a problem for a later stage of this project, once there's a
// network layer that actually needs it.
class StorageEngine {
 public:
  explicit StorageEngine(std::filesystem::path log_path);
  ~StorageEngine();

  // FileHandle is move-only, and unique_ptr<FileHandle> as a member is what
  // forces StorageEngine's own special members to be declared here and
  // defined out-of-line in the .cpp (where FileHandle is a complete type).
  // A defaulted destructor in the header would fail to compile: the
  // unique_ptr deleter needs sizeof(FileHandle) and this header never
  // includes file_handle.h.
  StorageEngine(StorageEngine&&) noexcept;
  StorageEngine& operator=(StorageEngine&&) noexcept;
  StorageEngine(const StorageEngine&) = delete;
  StorageEngine& operator=(const StorageEngine&) = delete;

  void Put(std::string_view key, std::string_view value);
  std::optional<std::string> Get(std::string_view key) const;
  void Delete(std::string_view key);

  std::size_t KeyCount() const { return index_.size(); }

 private:
  // Points straight at the value bytes in the log, not the record start, so
  // Get() is exactly one pread with no header parsing on the read path.
  struct IndexEntry {
    std::uint64_t value_offset;
    std::uint32_t value_len;
  };

  void RebuildIndexFromLog();
  std::uint64_t Append(std::string_view key, std::string_view value, bool tombstone);

  std::unique_ptr<FileHandle> file_;
  std::unordered_map<std::string, IndexEntry> index_;
  std::uint64_t next_offset_ = 0;  // end of the log; where the next append lands
};

}  // namespace kv
