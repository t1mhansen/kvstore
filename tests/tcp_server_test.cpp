#include "tcp_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "kv/storage_engine.h"

namespace kv {
namespace {

// A minimal raw client for exercising the server end-to-end over a real
// socket, exactly like a human at a terminal with netcat would.
class RawClient {
 public:
  explicit RawClient(std::uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // The listening socket already has a connection backlog as soon as
    // Listen() returns, so this should succeed immediately - the retry loop
    // is just a safety net against a slow/loaded CI machine, not something
    // this depends on for correctness.
    for (int attempt = 0; attempt < 50; ++attempt) {
      if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("could not connect to test server");
  }

  ~RawClient() {
    if (fd_ != -1) {
      ::close(fd_);
    }
  }

  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;

  std::string SendLine(const std::string& line) {
    const std::string with_newline = line + "\n";
    ::send(fd_, with_newline.data(), with_newline.size(), 0);

    std::string response;
    char buf[256];
    while (response.find('\n') == std::string::npos) {
      const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
      if (n <= 0) break;
      response.append(buf, static_cast<std::size_t>(n));
    }
    return response;
  }

 private:
  int fd_ = -1;
};

// TcpServer has no graceful shutdown (see tcp_server.h) - its accept loop
// runs on a background thread forever. That means a TcpServer can never be
// safely destroyed while that thread might still touch it, so this suite
// builds exactly one server for all tests (SetUpTestSuite, not SetUp) and
// never tears it down. An earlier version constructed a fresh TcpServer per
// test and leaked a thread each time; ThreadSanitizer caught the resulting
// race (later tests' destructors closing a socket a still-running accept()
// from an *earlier* test was blocked on).
//
// One shared server for the whole suite means tests must not collide on
// keys, so each test uses its own key prefix.
class TcpServerTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    const auto path = std::filesystem::temp_directory_path() / "kv_server_test_suite.log";
    std::filesystem::remove(path);
    path_ = new std::filesystem::path(path);
    engine_ = new StorageEngine(path);
    server_ = new TcpServer(*engine_);
    port_ = server_->Listen(/*port=*/0);
    std::thread(&TcpServer::AcceptLoop, server_).detach();
  }

  static std::filesystem::path* path_;
  static StorageEngine* engine_;
  static TcpServer* server_;
  static std::uint16_t port_;
};

std::filesystem::path* TcpServerTest::path_ = nullptr;
StorageEngine* TcpServerTest::engine_ = nullptr;
TcpServer* TcpServerTest::server_ = nullptr;
std::uint16_t TcpServerTest::port_ = 0;

TEST_F(TcpServerTest, GetOnMissingKeyReturnsNotFound) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("GET tcp_missing"), "NOT_FOUND\r\n");
}

TEST_F(TcpServerTest, SetThenGetRoundTrips) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("SET tcp_foo bar"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET tcp_foo"), "VALUE bar\r\n");
}

TEST_F(TcpServerTest, ValueWithSpacesRoundTrips) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("SET tcp_spaces bar baz"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET tcp_spaces"), "VALUE bar baz\r\n");
}

TEST_F(TcpServerTest, DeleteRemovesKey) {
  RawClient client(port_);
  client.SendLine("SET tcp_del bar");
  EXPECT_EQ(client.SendLine("DEL tcp_del"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET tcp_del"), "NOT_FOUND\r\n");
}

TEST_F(TcpServerTest, MalformedCommandReturnsError) {
  RawClient client(port_);
  const std::string response = client.SendLine("GET");
  EXPECT_EQ(response.substr(0, 6), "ERROR ");
}

TEST_F(TcpServerTest, WritesFromOneClientAreVisibleToAnother) {
  RawClient writer(port_);
  writer.SendLine("SET tcp_shared value");

  RawClient reader(port_);
  EXPECT_EQ(reader.SendLine("GET tcp_shared"), "VALUE value\r\n");
}

// Proves the network path is actually durable, not just visible in memory:
// Put() fsyncs before the server ever replies OK, so by the time SendLine()
// returns, a second, completely independent StorageEngine opened on the same
// log file should already see the write.
TEST_F(TcpServerTest, WritesAreDurableOnDisk) {
  RawClient client(port_);
  ASSERT_EQ(client.SendLine("SET tcp_durable yes"), "OK\r\n");

  StorageEngine independent_reader(*path_);
  EXPECT_EQ(independent_reader.Get("tcp_durable"), "yes");
}

}  // namespace
}  // namespace kv
