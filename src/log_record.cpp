#include "log_record.h"

#include <stdexcept>

#include "crc32.h"

namespace kv {

namespace {

constexpr std::uint8_t kTombstoneFlag = 0x1;

void AppendU32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>(value & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::uint32_t ReadU32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

std::string EncodeLogRecord(std::string_view key, std::string_view value, bool tombstone) {
  std::string body;
  body.reserve(1 + 4 + 4 + key.size() + (tombstone ? 0 : value.size()));
  body.push_back(static_cast<char>(tombstone ? kTombstoneFlag : 0));
  AppendU32(body, static_cast<std::uint32_t>(key.size()));
  AppendU32(body, static_cast<std::uint32_t>(tombstone ? 0 : value.size()));
  body.append(key);
  if (!tombstone) {
    body.append(value);
  }

  const std::uint32_t checksum = Crc32(body.data(), body.size());

  std::string record;
  record.reserve(4 + body.size());
  AppendU32(record, checksum);
  record.append(body);
  return record;
}

RecordHeaderFields PeekLogRecordHeader(const char* header, std::size_t length) {
  if (length < kLogRecordHeaderSize) {
    throw std::runtime_error("log record header truncated");
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(header);

  RecordHeaderFields fields;
  fields.tombstone = (bytes[4] & kTombstoneFlag) != 0;
  fields.key_len = ReadU32(bytes + 5);
  fields.value_len = ReadU32(bytes + 9);
  return fields;
}

DecodedRecord DecodeLogRecord(const char* data, std::size_t length) {
  if (length < kLogRecordHeaderSize) {
    throw std::runtime_error("log record shorter than header");
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(data);

  const std::uint32_t stored_checksum = ReadU32(bytes);
  const std::size_t body_len = length - 4;
  const std::uint32_t actual_checksum = Crc32(data + 4, body_len);
  if (stored_checksum != actual_checksum) {
    throw std::runtime_error("log record checksum mismatch");
  }

  const std::uint8_t flags = bytes[4];
  const bool tombstone = (flags & kTombstoneFlag) != 0;
  const std::uint32_t key_len = ReadU32(bytes + 5);
  const std::uint32_t value_len = ReadU32(bytes + 9);

  const std::size_t expected = kLogRecordHeaderSize + key_len + (tombstone ? 0 : value_len);
  if (length != expected) {
    throw std::runtime_error("log record length does not match header");
  }

  DecodedRecord result;
  result.tombstone = tombstone;
  result.key.assign(data + kLogRecordHeaderSize, key_len);
  if (!tombstone) {
    result.value.assign(data + kLogRecordHeaderSize + key_len, value_len);
  }
  return result;
}

}  // namespace kv
