#include "protocol.h"

#include <charconv>

namespace kv {

namespace {

Command Invalid(std::string message) {
  Command command;
  command.type = CommandType::kInvalid;
  command.error = std::move(message);
  return command;
}

}  // namespace

Command ParseCommand(std::string_view line) {
  if (line.empty()) {
    return Invalid("empty command");
  }

  const auto first_space = line.find(' ');
  const std::string_view verb = line.substr(0, first_space);
  const std::string_view rest =
      first_space == std::string_view::npos ? std::string_view{} : line.substr(first_space + 1);

  if (verb == "GET" || verb == "DEL") {
    if (rest.empty() || rest.find(' ') != std::string_view::npos) {
      return Invalid(std::string(verb) + " requires exactly one key");
    }
    Command command;
    command.type = verb == "GET" ? CommandType::kGet : CommandType::kDel;
    command.key = std::string(rest);
    return command;
  }

  if (verb == "SET") {
    const auto value_space = rest.find(' ');
    if (rest.empty() || value_space == std::string_view::npos) {
      return Invalid("SET requires a key and a value");
    }
    Command command;
    command.type = CommandType::kSet;
    command.key = std::string(rest.substr(0, value_space));
    command.value = std::string(rest.substr(value_space + 1));
    return command;
  }

  if (verb == "SETEX") {
    const auto key_space = rest.find(' ');
    if (rest.empty() || key_space == std::string_view::npos) {
      return Invalid("SETEX requires a key, a TTL in seconds, and a value");
    }
    const std::string_view key = rest.substr(0, key_space);
    const std::string_view after_key = rest.substr(key_space + 1);

    const auto seconds_space = after_key.find(' ');
    if (after_key.empty() || seconds_space == std::string_view::npos) {
      return Invalid("SETEX requires a key, a TTL in seconds, and a value");
    }
    const std::string_view seconds_str = after_key.substr(0, seconds_space);

    std::uint64_t seconds = 0;
    const auto result = std::from_chars(seconds_str.data(), seconds_str.data() + seconds_str.size(), seconds);
    if (result.ec != std::errc() || result.ptr != seconds_str.data() + seconds_str.size()) {
      return Invalid("SETEX TTL must be a non-negative integer number of seconds");
    }

    Command command;
    command.type = CommandType::kSetEx;
    command.key = std::string(key);
    command.ttl_seconds = seconds;
    command.value = std::string(after_key.substr(seconds_space + 1));
    return command;
  }

  if (verb == "COMPACT") {
    if (!rest.empty()) {
      return Invalid("COMPACT takes no arguments");
    }
    Command command;
    command.type = CommandType::kCompact;
    return command;
  }

  return Invalid("unknown command");
}

}  // namespace kv
