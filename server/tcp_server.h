#pragma once

#include <cstdint>

#include "kv/storage_engine.h"

namespace kv {

// Accepts TCP connections and serves them one OS thread per connection,
// each thread doing blocking reads/writes and calling straight into a
// shared StorageEngine. StorageEngine is thread-safe on its own (a
// shared_mutex internally), so this class doesn't do any locking of its
// own - concurrent connections just call into it directly.
//
// No graceful shutdown: Run() never returns. For a demo/benchmark process,
// Ctrl-C killing the whole thing is the expected way to stop it, and the OS
// reclaims every socket and thread on exit.
class TcpServer {
 public:
  explicit TcpServer(StorageEngine& engine);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Binds and starts listening immediately, so a failure like "port already
  // in use" surfaces here rather than inside the accept loop. Passing 0
  // lets the OS pick a free port; returns whichever port actually got
  // bound, which is how tests avoid hardcoding/colliding on a port number.
  std::uint16_t Listen(std::uint16_t port);

  // Accepts connections forever, spawning a detached thread per client.
  [[noreturn]] void AcceptLoop();

 private:
  void HandleConnection(int client_fd);
  std::string HandleLine(const std::string& line);

  int listen_fd_ = -1;
  StorageEngine& engine_;
};

}  // namespace kv
