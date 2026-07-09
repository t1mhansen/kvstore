#pragma once

#include <cstddef>
#include <cstdint>

namespace kv {

// Standard CRC-32/ISO-HDLC (the same variant used by zlib, gzip, PNG).
// Used to detect corrupted or torn log records, not for cryptographic
// integrity.
std::uint32_t Crc32(const void* data, std::size_t length);

}  // namespace kv
