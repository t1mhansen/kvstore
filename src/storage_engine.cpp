#include "kv/storage_engine.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "file_handle.h"
#include "log_record.h"

namespace kv {

namespace {

std::uint64_t NowUnixSeconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

bool IsExpired(std::uint64_t expires_at, std::uint64_t now) { return expires_at != 0 && expires_at <= now; }

}  // namespace

StorageEngine::StorageEngine(std::filesystem::path log_path, SyncPolicy sync_policy)
    : log_path_(std::move(log_path)),
      sync_policy_(sync_policy),
      file_(std::make_unique<FileHandle>(log_path_)) {
  RebuildIndexFromLog();
}

StorageEngine::~StorageEngine() = default;

// Walks the whole log front to back and replays it into the index. Each
// record's length depends on its own key_len/value_len, which live inside
// the header, so this has to read the 13-byte header first just to find out
// how many more bytes make up the rest of the record - there's no way to
// know a record's size up front. Records are applied in file order, so a
// later Put for the same key naturally overwrites an earlier one, and a
// tombstone erases whatever came before it.
//
// Because every write does pwrite-then-fsync before returning, a record
// already fully in the file is durable - the only thing a crash can leave
// behind is a partial/garbled record at the very end, from whatever write
// was in flight when the process died. So the recovery rule is simple: the
// first bad record found is always the torn tail, and everything from that
// point on gets discarded (and the file physically truncated back to the
// last good offset). This assumes corruption never happens mid-file - a
// single writer that always fsyncs can't produce that. If it ever did (bit
// rot, someone editing the file by hand), this logic would silently drop
// any valid records after the bad one too - a real limitation, but a
// different problem than crash recovery.
void StorageEngine::RebuildIndexFromLog() {
  const std::uint64_t file_size = file_->Size();
  const std::uint64_t now = NowUnixSeconds();
  std::uint64_t offset = 0;
  std::vector<char> buffer;

  const auto discard_torn_tail = [&](std::uint64_t good_offset) {
    std::cerr << "kv: discarding " << (file_size - good_offset)
              << " byte(s) of a torn write at the end of the log\n";
    file_->Truncate(good_offset);
    next_offset_ = good_offset;
  };

  while (offset < file_size) {
    const std::uint64_t remaining = file_size - offset;
    if (remaining < kLogRecordHeaderSize) {
      discard_torn_tail(offset);  // not even a full header made it to disk
      return;
    }

    char header[kLogRecordHeaderSize];
    file_->Read(offset, header, kLogRecordHeaderSize);
    const RecordHeaderFields fields = PeekLogRecordHeader(header, kLogRecordHeaderSize);

    const std::size_t record_len =
        kLogRecordHeaderSize + fields.key_len + (fields.tombstone ? 0 : fields.value_len);
    if (record_len > remaining) {
      discard_torn_tail(offset);  // header is intact but key/value got cut off
      return;
    }

    buffer.resize(record_len);
    file_->Read(offset, buffer.data(), record_len);

    DecodedRecord record;
    try {
      record = DecodeLogRecord(buffer.data(), record_len);
    } catch (const std::runtime_error&) {
      discard_torn_tail(offset);  // all the bytes are there, but the checksum is wrong
      return;
    }

    if (record.tombstone) {
      index_.erase(record.key);
    } else if (IsExpired(record.expires_at, now)) {
      // Already expired by the time we're loading it (e.g. the process was
      // down past a short TTL) - treat it as absent rather than inserting
      // it just to have Get() immediately hide it again.
      index_.erase(record.key);
    } else {
      const std::uint64_t value_offset = offset + kLogRecordHeaderSize + record.key.size();
      index_[record.key] = IndexEntry{value_offset, static_cast<std::uint32_t>(record.value.size()),
                                       record.expires_at};
    }

    offset += record_len;
  }

  next_offset_ = offset;
}

// See the SyncPolicy comment in storage_engine.h for exactly what fsync
// buys here (power-loss durability, not process-crash durability) and what
// skipping it trades away.
std::uint64_t StorageEngine::Append(std::string_view key, std::string_view value, bool tombstone,
                                     std::uint64_t expires_at) {
  const std::string record = EncodeLogRecord(key, value, tombstone, expires_at);
  const std::uint64_t offset = next_offset_;
  file_->Write(offset, record.data(), record.size());
  if (sync_policy_ == SyncPolicy::kAlways) {
    file_->Sync();
  }
  next_offset_ += record.size();
  return offset;
}

void StorageEngine::Put(std::string_view key, std::string_view value, std::optional<std::chrono::seconds> ttl) {
  const std::uint64_t expires_at = ttl.has_value() ? NowUnixSeconds() + static_cast<std::uint64_t>(ttl->count()) : 0;

  std::unique_lock lock(mutex_);
  const std::uint64_t offset = Append(key, value, /*tombstone=*/false, expires_at);
  const std::uint64_t value_offset = offset + kLogRecordHeaderSize + key.size();
  index_[std::string(key)] = IndexEntry{value_offset, static_cast<std::uint32_t>(value.size()), expires_at};
}

// No checksum check here on purpose - by the time a key is in the index, its
// record was already validated either just now by Put() in this process, or
// by RebuildIndexFromLog() when the store was opened. Re-verifying on every
// single Get would mean re-reading and re-hashing the value on every read,
// which defeats the point of the index.
//
// Shared lock: any number of Gets can run at once, since none of them
// mutate the index or the file - they just need to not overlap with a
// Put/Delete that's actively changing the index out from under them. That's
// also why an expired key found here isn't erased from the index: doing
// that would need a write lock, which would mean upgrading a shared lock to
// an exclusive one mid-read (its own can of worms) for a cleanup that
// Compact() already handles.
std::optional<std::string> StorageEngine::Get(std::string_view key) const {
  std::shared_lock lock(mutex_);
  const auto it = index_.find(std::string(key));
  if (it == index_.end()) {
    return std::nullopt;
  }
  if (IsExpired(it->second.expires_at, NowUnixSeconds())) {
    return std::nullopt;
  }
  std::string value(it->second.value_len, '\0');
  file_->Read(it->second.value_offset, value.data(), it->second.value_len);
  return value;
}

// The exists-check, the append, and the erase all have to happen under one
// lock acquisition, not one lock per step - otherwise two threads deleting
// the same key could both pass the exists-check before either erases it,
// and both would append a redundant tombstone. Locking the whole operation
// makes it atomic from every other thread's point of view.
void StorageEngine::Delete(std::string_view key) {
  std::unique_lock lock(mutex_);
  if (index_.find(std::string(key)) == index_.end()) {
    return;  // key doesn't exist anywhere in the log; no tombstone needed
  }
  Append(key, {}, /*tombstone=*/true);
  index_.erase(std::string(key));
}

// Rewrites the log from scratch into a temp file containing only what the
// index says is currently live, then atomically renames it over the real
// log. rename() replacing an existing path is atomic on POSIX, so this is
// crash-safe without any extra bookkeeping: either the crash happens before
// the rename (original log untouched, nothing lost) or after it succeeds
// (new compacted log fully in place) - there's no window where the log on
// disk is a partial mix of both. The one gap this doesn't close: the
// temp file's data is fsynced before the rename, but the containing
// directory entry isn't separately fsynced, so a power loss at exactly the
// wrong instant could still lose the rename on some filesystems. That's
// outside this project's threat model so far (process crashes, not power
// loss), so it's accepted rather than solved.
void StorageEngine::Compact() {
  std::unique_lock lock(mutex_);

  const std::filesystem::path tmp_path = log_path_.string() + ".compact";
  auto tmp_file = std::make_unique<FileHandle>(tmp_path);
  tmp_file->Truncate(0);  // in case a previous compaction crashed partway through

  const std::uint64_t now = NowUnixSeconds();
  std::unordered_map<std::string, IndexEntry> new_index;
  std::uint64_t new_offset = 0;

  for (const auto& [key, entry] : index_) {
    if (IsExpired(entry.expires_at, now)) {
      continue;  // compaction is also where expired keys actually get reclaimed
    }

    std::string value(entry.value_len, '\0');
    file_->Read(entry.value_offset, value.data(), entry.value_len);

    const std::string record = EncodeLogRecord(key, value, /*tombstone=*/false, entry.expires_at);
    tmp_file->Write(new_offset, record.data(), record.size());

    const std::uint64_t value_offset = new_offset + kLogRecordHeaderSize + key.size();
    new_index[key] = IndexEntry{value_offset, entry.value_len, entry.expires_at};
    new_offset += record.size();
  }
  tmp_file->Sync();
  tmp_file.reset();  // close before renaming over it

  std::filesystem::rename(tmp_path, log_path_);

  // The fd file_ currently holds still refers to the pre-compaction data
  // (rename doesn't retarget an already-open fd), so it has to be reopened
  // against the now-compacted file at log_path_. Dropping the old FileHandle
  // closes the last reference to the old (now unlinked) log, which is what
  // actually frees its disk space.
  file_ = std::make_unique<FileHandle>(log_path_);
  index_ = std::move(new_index);
  next_offset_ = new_offset;
}

std::size_t StorageEngine::KeyCount() const {
  std::shared_lock lock(mutex_);
  return index_.size();
}

}  // namespace kv
