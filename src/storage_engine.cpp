#include "kv/storage_engine.h"

#include <iostream>
#include <stdexcept>
#include <vector>

#include "file_handle.h"
#include "log_record.h"

namespace kv {

StorageEngine::StorageEngine(std::filesystem::path log_path)
    : file_(std::make_unique<FileHandle>(log_path)) {
  RebuildIndexFromLog();
}

StorageEngine::~StorageEngine() = default;
StorageEngine::StorageEngine(StorageEngine&&) noexcept = default;
StorageEngine& StorageEngine::operator=(StorageEngine&&) noexcept = default;

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
    } else {
      const std::uint64_t value_offset = offset + kLogRecordHeaderSize + record.key.size();
      index_[record.key] =
          IndexEntry{value_offset, static_cast<std::uint32_t>(record.value.size())};
    }

    offset += record_len;
  }

  next_offset_ = offset;
}

// fsync on every single write is the safest, slowest option - it's the
// difference between "durable" and "buffered by the OS and gone on a
// crash." Doing it unconditionally for now; batching/async fsync is exactly
// the kind of thing to measure and tune once there's a benchmark harness,
// not guess at up front.
std::uint64_t StorageEngine::Append(std::string_view key, std::string_view value, bool tombstone) {
  const std::string record = EncodeLogRecord(key, value, tombstone);
  const std::uint64_t offset = next_offset_;
  file_->Write(offset, record.data(), record.size());
  file_->Sync();
  next_offset_ += record.size();
  return offset;
}

void StorageEngine::Put(std::string_view key, std::string_view value) {
  const std::uint64_t offset = Append(key, value, /*tombstone=*/false);
  const std::uint64_t value_offset = offset + kLogRecordHeaderSize + key.size();
  index_[std::string(key)] = IndexEntry{value_offset, static_cast<std::uint32_t>(value.size())};
}

// No checksum check here on purpose - by the time a key is in the index, its
// record was already validated either just now by Put() in this process, or
// by RebuildIndexFromLog() when the store was opened. Re-verifying on every
// single Get would mean re-reading and re-hashing the value on every read,
// which defeats the point of the index.
std::optional<std::string> StorageEngine::Get(std::string_view key) const {
  const auto it = index_.find(std::string(key));
  if (it == index_.end()) {
    return std::nullopt;
  }
  std::string value(it->second.value_len, '\0');
  file_->Read(it->second.value_offset, value.data(), it->second.value_len);
  return value;
}

void StorageEngine::Delete(std::string_view key) {
  if (index_.find(std::string(key)) == index_.end()) {
    return;  // key doesn't exist anywhere in the log; no tombstone needed
  }
  Append(key, {}, /*tombstone=*/true);
  index_.erase(std::string(key));
}

}  // namespace kv
