#include "protocol.h"

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

  return Invalid("unknown command");
}

}  // namespace kv
