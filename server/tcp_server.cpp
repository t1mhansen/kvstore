#include "tcp_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>
#include <system_error>
#include <thread>

#include "protocol.h"

namespace kv {

namespace {

// A client that never sends a newline would otherwise make the per-connection
// buffer grow without bound; this is just a sanity cap, not a protocol limit.
constexpr std::size_t kMaxLineLength = 1 << 20;  // 1 MiB

[[noreturn]] void ThrowErrno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

// send(), like pwrite(), isn't guaranteed to write everything in one call.
bool SendAll(int fd, std::string_view data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

TcpServer::TcpServer(StorageEngine& engine) : engine_(engine) {}

TcpServer::~TcpServer() {
  if (listen_fd_ != -1) {
    ::close(listen_fd_);
  }
}

std::uint16_t TcpServer::Listen(std::uint16_t port) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == -1) {
    ThrowErrno("socket");
  }

  // Without this, restarting the server right after a previous run can fail
  // to bind with "address already in use" while the OS still holds the old
  // socket in TIME_WAIT - annoying during dev/demo, harmless to allow.
  const int yes = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
    ThrowErrno("bind");
  }
  if (::listen(listen_fd_, /*backlog=*/128) == -1) {
    ThrowErrno("listen");
  }

  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &bound_len) == -1) {
    ThrowErrno("getsockname");
  }
  return ntohs(bound.sin_port);
}

void TcpServer::AcceptLoop() {
  for (;;) {
    const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
    if (client_fd == -1) {
      std::cerr << "kv_server: accept failed: " << std::strerror(errno) << "\n";
      // If the listening socket is in a bad state, accept() can start
      // failing instantly and repeatedly - without a pause this would spin
      // the CPU at 100% retrying forever instead of just logging once.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    std::thread(&TcpServer::HandleConnection, this, client_fd).detach();
  }
}

void TcpServer::HandleConnection(int client_fd) {
  std::string buffer;
  char chunk[4096];

  for (;;) {
    const ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      break;  // 0 = client closed the connection; -1 = a real error either way
    }
    buffer.append(chunk, static_cast<std::size_t>(n));

    if (buffer.size() > kMaxLineLength) {
      SendAll(client_fd, "ERROR line too long\r\n");
      break;
    }

    // A single recv() can contain zero, one, or several complete lines, plus
    // a trailing partial one - so drain every full line currently buffered
    // before going back to recv() for more.
    std::size_t newline;
    while ((newline = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, newline);
      buffer.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (!SendAll(client_fd, HandleLine(line))) {
        ::close(client_fd);
        return;
      }
    }
  }

  ::close(client_fd);
}

std::string TcpServer::HandleLine(const std::string& line) {
  const Command command = ParseCommand(line);

  switch (command.type) {
    case CommandType::kGet: {
      const auto value = engine_.Get(command.key);
      return value ? ("VALUE " + *value + "\r\n") : "NOT_FOUND\r\n";
    }
    case CommandType::kSet:
      engine_.Put(command.key, command.value);
      return "OK\r\n";
    case CommandType::kSetEx:
      engine_.Put(command.key, command.value, std::chrono::seconds(command.ttl_seconds));
      return "OK\r\n";
    case CommandType::kDel:
      engine_.Delete(command.key);
      return "OK\r\n";
    case CommandType::kCompact:
      engine_.Compact();
      return "OK\r\n";
    case CommandType::kInvalid:
      return "ERROR " + command.error + "\r\n";
  }
  return "ERROR internal\r\n";
}

}  // namespace kv
