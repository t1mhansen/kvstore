#include <iostream>

#include "kv/storage_engine.h"
#include "tcp_server.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: kv_server <port> <log-path>\n";
    return 1;
  }

  try {
    const auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    kv::StorageEngine engine(argv[2]);
    kv::TcpServer server(engine);

    const std::uint16_t bound_port = server.Listen(port);
    std::cout << "kv_server listening on port " << bound_port << "\n";
    server.AcceptLoop();
  } catch (const std::exception& e) {
    std::cerr << "kv_server: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
