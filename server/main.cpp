#include <iostream>
#include <string_view>

#include "kv/storage_engine.h"
#include "tcp_server.h"

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: kv_server <port> <log-path> [--no-fsync]\n";
    return 1;
  }

  kv::SyncPolicy sync_policy = kv::SyncPolicy::kAlways;
  if (argc == 4) {
    if (std::string_view(argv[3]) != "--no-fsync") {
      std::cerr << "usage: kv_server <port> <log-path> [--no-fsync]\n";
      return 1;
    }
    sync_policy = kv::SyncPolicy::kNever;
  }

  try {
    const auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    kv::StorageEngine engine(argv[2], sync_policy);
    kv::TcpServer server(engine);

    const std::uint16_t bound_port = server.Listen(port);
    std::cout << "kv_server listening on port " << bound_port
               << (sync_policy == kv::SyncPolicy::kNever ? " (fsync disabled)" : "") << "\n";
    server.AcceptLoop();
  } catch (const std::exception& e) {
    std::cerr << "kv_server: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
