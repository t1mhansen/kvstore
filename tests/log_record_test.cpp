#include <gtest/gtest.h>

#include <stdexcept>

#include "log_record.h"

namespace kv {
namespace {

TEST(LogRecordTest, RoundTripsKeyAndValue) {
  const std::string record = EncodeLogRecord("hello", "world", /*tombstone=*/false);
  const DecodedRecord decoded = DecodeLogRecord(record.data(), record.size());

  EXPECT_FALSE(decoded.tombstone);
  EXPECT_EQ(decoded.key, "hello");
  EXPECT_EQ(decoded.value, "world");
}

TEST(LogRecordTest, TombstoneHasNoValue) {
  const std::string record = EncodeLogRecord("hello", "ignored", /*tombstone=*/true);
  const DecodedRecord decoded = DecodeLogRecord(record.data(), record.size());

  EXPECT_TRUE(decoded.tombstone);
  EXPECT_EQ(decoded.key, "hello");
  EXPECT_TRUE(decoded.value.empty());
}

TEST(LogRecordTest, EmptyValueRoundTrips) {
  const std::string record = EncodeLogRecord("k", "", /*tombstone=*/false);
  const DecodedRecord decoded = DecodeLogRecord(record.data(), record.size());

  EXPECT_FALSE(decoded.tombstone);
  EXPECT_EQ(decoded.value, "");
}

TEST(LogRecordTest, CorruptedByteFailsChecksum) {
  std::string record = EncodeLogRecord("hello", "world", /*tombstone=*/false);
  record[record.size() - 1] ^= 0xFF;  // flip a bit in the value

  EXPECT_THROW(DecodeLogRecord(record.data(), record.size()), std::runtime_error);
}

TEST(LogRecordTest, PeekHeaderMatchesDecode) {
  const std::string record = EncodeLogRecord("k", "value", /*tombstone=*/false);
  const RecordHeaderFields fields = PeekLogRecordHeader(record.data(), kLogRecordHeaderSize);

  EXPECT_FALSE(fields.tombstone);
  EXPECT_EQ(fields.key_len, 1u);
  EXPECT_EQ(fields.value_len, 5u);
}

}  // namespace
}  // namespace kv
