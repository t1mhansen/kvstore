#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kv {

// On-disk layout of one log record. All integers are little-endian and
// written byte-by-byte rather than via struct layout, so the format doesn't
// depend on compiler padding or host endianness:
//
//   checksum    4 bytes   CRC32 of everything below (flags..value)
//   flags       1 byte    bit 0 = tombstone (delete marker)
//   key_len     4 bytes
//   value_len   4 bytes   0 for a tombstone
//   expires_at  8 bytes   unix seconds; 0 = never expires (unused for a
//                         tombstone, always written as 0)
//   key         key_len bytes
//   value       value_len bytes (absent for a tombstone)
//
// expires_at was added after the format's first version, which had no TTL
// support and a 13-byte header (no expires_at field) - this is a breaking
// on-disk change. There's no reader for the old format; nothing depends on
// it, so there's nothing to migrate.
inline constexpr std::size_t kLogRecordHeaderSize = 4 + 1 + 4 + 4 + 8;

struct DecodedRecord {
  bool tombstone = false;
  std::string key;
  std::string value;
  std::uint64_t expires_at = 0;
};

struct RecordHeaderFields {
  bool tombstone = false;
  std::uint32_t key_len = 0;
  std::uint32_t value_len = 0;
};

// Serializes a record ready to append to the log. expires_at is unix
// seconds, 0 meaning "never expires"; ignored (written as 0) for a
// tombstone.
std::string EncodeLogRecord(std::string_view key, std::string_view value, bool tombstone,
                             std::uint64_t expires_at = 0);

// Parses only flags/key_len/value_len from the first kLogRecordHeaderSize
// bytes, without checksum validation. Used during log replay to learn how
// many more bytes to read before the full record (and its checksum) can be
// verified.
RecordHeaderFields PeekLogRecordHeader(const char* header, std::size_t length);

// Decodes one record from a buffer containing exactly one record's bytes
// (header + key + value) and verifies its checksum. Throws
// std::runtime_error if the checksum or lengths don't match.
DecodedRecord DecodeLogRecord(const char* data, std::size_t length);

}  // namespace kv
