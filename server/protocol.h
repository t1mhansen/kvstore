#pragma once

#include <string>
#include <string_view>

namespace kv {

enum class CommandType { kGet, kSet, kDel, kInvalid };

struct Command {
  CommandType type = CommandType::kInvalid;
  std::string key;
  std::string value;  // only meaningful for kSet
  std::string error;  // only meaningful for kInvalid, human-readable reason
};

// Parses one line of the wire protocol (no trailing \r\n expected - that's
// stripped by the caller during line buffering):
//
//   GET <key>
//   SET <key> <value>
//   DEL <key>
//
// A key can't contain spaces. A value is everything after the key to the
// end of the line, so it may contain spaces - but never a literal newline,
// since a newline always ends the record on the wire.
Command ParseCommand(std::string_view line);

}  // namespace kv
