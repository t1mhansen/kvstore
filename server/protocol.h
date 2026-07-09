#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kv {

enum class CommandType { kGet, kSet, kSetEx, kDel, kCompact, kInvalid };

struct Command {
  CommandType type = CommandType::kInvalid;
  std::string key;
  std::string value;          // meaningful for kSet/kSetEx
  std::uint64_t ttl_seconds = 0;  // meaningful for kSetEx
  std::string error;          // meaningful for kInvalid, human-readable reason
};

// Parses one line of the wire protocol (no trailing \r\n expected - that's
// stripped by the caller during line buffering):
//
//   GET <key>
//   SET <key> <value>
//   SETEX <key> <seconds> <value>
//   DEL <key>
//   COMPACT
//
// A key can't contain spaces. A value is everything after the key (and, for
// SETEX, the TTL) to the end of the line, so it may contain spaces - but
// never a literal newline, since a newline always ends the record on the
// wire. SETEX isn't spelled as "SET key value EX seconds" (Redis's syntax)
// because that would be ambiguous with a value that legitimately ends in
// the text "EX 60" - a separate command sidesteps that entirely.
Command ParseCommand(std::string_view line);

}  // namespace kv
