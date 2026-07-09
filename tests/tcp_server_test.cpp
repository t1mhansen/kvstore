#include "tcp_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
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

class TcpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("kv_server_test_") + test_info->test_suite_name() + "_" +
             test_info->name() + ".log");
    std::filesystem::remove(path_);

    engine_ = std::make_unique<StorageEngine>(path_);
    server_ = std::make_unique<TcpServer>(*engine_);
    port_ = server_->Listen(/*port=*/0);

    // Intentionally leaked: the accept loop blocks forever, and there's no
    // Stop() (see tcp_server.h). Fine for a short-lived test binary - the
    // process exiting reclaims the thread and socket regardless.
    std::thread(&TcpServer::AcceptLoop, server_.get()).detach();
  }

  void TearDown() override { std::filesystem::remove(path_); }

  std::filesystem::path path_;
  std::unique_ptr<StorageEngine> engine_;
  std::unique_ptr<TcpServer> server_;
  std::uint16_t port_ = 0;
};

TEST_F(TcpServerTest, GetOnMissingKeyReturnsNotFound) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("GET missing"), "NOT_FOUND\r\n");
}

TEST_F(TcpServerTest, SetThenGetRoundTrips) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("SET foo bar"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET foo"), "VALUE bar\r\n");
}

TEST_F(TcpServerTest, ValueWithSpacesRoundTrips) {
  RawClient client(port_);
  EXPECT_EQ(client.SendLine("SET foo bar baz"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET foo"), "VALUE bar baz\r\n");
}

TEST_F(TcpServerTest, DeleteRemovesKey) {
  RawClient client(port_);
  client.SendLine("SET foo bar");
  EXPECT_EQ(client.SendLine("DEL foo"), "OK\r\n");
  EXPECT_EQ(client.SendLine("GET foo"), "NOT_FOUND\r\n");
}

TEST_F(TcpServerTest, MalformedCommandReturnsError) {
  RawClient client(port_);
  const std::string response = client.SendLine("GET");
  EXPECT_EQ(response.substr(0, 6), "ERROR ");
}

TEST_F(TcpServerTest, WritesFromOneClientAreVisibleToAnother) {
  RawClient writer(port_);
  writer.SendLine("SET shared value");

  RawClient reader(port_);
  EXPECT_EQ(reader.SendLine("GET shared"), "VALUE value\r\n");
}

TEST_F(TcpServerTest, DataSurvivesServerRestart) {
  {
    RawClient client(port_);
    client.SendLine("SET durable yes");
  }

  StorageEngine reopened(path_);
  EXPECT_EQ(reopened.Get("durable"), "yes");
}

}  // namespace
}  // namespace kv
