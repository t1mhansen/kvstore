#include "crc32.h"

#include <array>

namespace kv {

namespace {

// Table-driven CRC32 processes a byte at a time using a precomputed table of
// what each possible byte contributes to the checksum, instead of looping
// over all 8 bits of every byte by hand. Standard way to implement this -
// same idea zlib/gzip use, just built here instead of pulling in a
// dependency for ~15 lines of math.
std::array<std::uint32_t, 256> MakeTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}

}  // namespace

std::uint32_t Crc32(const void* data, std::size_t length) {
  // Function-local static: built once on first call, and the standard
  // guarantees that init is thread-safe without needing a mutex here.
  static const std::array<std::uint32_t, 256> table = MakeTable();

  const auto* bytes = static_cast<const unsigned char*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace kv
