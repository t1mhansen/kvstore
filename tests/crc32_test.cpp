#include <gtest/gtest.h>

#include "crc32.h"

namespace kv {
namespace {

TEST(Crc32Test, MatchesStandardCheckValue) {
  // "123456789" is the standard CRC-32/ISO-HDLC check-value test vector.
  const char* data = "123456789";
  EXPECT_EQ(Crc32(data, 9), 0xCBF43926u);
}

TEST(Crc32Test, DifferentDataProducesDifferentChecksum) {
  EXPECT_NE(Crc32("abc", 3), Crc32("abd", 3));
}

TEST(Crc32Test, EmptyInputIsZero) {
  EXPECT_EQ(Crc32(nullptr, 0), 0u);
}

}  // namespace
}  // namespace kv
