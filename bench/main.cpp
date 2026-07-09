// Load generator for kv_server: N threads, each holding its own persistent
// connection, hammering the real wire protocol with a mixed GET/SET
// workload for a fixed duration. Reports throughput and latency
// percentiles. This talks to a *running* kv_server over a real socket -
// it's not linked against kv_core, since the whole point is to measure the
// same thing a real client would see (including the network round trip).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
  std::string host = "127.0.0.1";
  int port = 0;
  int threads = 4;
  int duration_seconds = 5;
  int num_keys = 1000;
  int value_size = 100;
  double read_ratio = 0.9;
};

Options ParseArgs(int argc, char** argv) {
  Options opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
      return argv[++i];
    };
    if (arg == "--host") {
      opts.host = next();
    } else if (arg == "--port") {
      opts.port = std::stoi(next());
    } else if (arg == "--threads") {
      opts.threads = std::stoi(next());
    } else if (arg == "--duration") {
      opts.duration_seconds = std::stoi(next());
    } else if (arg == "--keys") {
      opts.num_keys = std::stoi(next());
    } else if (arg == "--value-size") {
      opts.value_size = std::stoi(next());
    } else if (arg == "--read-ratio") {
      opts.read_ratio = std::stod(next());
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (opts.port == 0) {
    throw std::runtime_error("--port is required");
  }
  return opts;
}

class Connection {
 public:
  Connection(const std::string& host, int port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      throw std::runtime_error("could not connect to " + host + ":" + std::to_string(port));
    }
  }

  ~Connection() {
    if (fd_ != -1) ::close(fd_);
  }

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // One full request/response round trip. Returns the response line.
  std::string SendLine(const std::string& line) {
    const std::string with_newline = line + "\n";
    std::size_t sent = 0;
    while (sent < with_newline.size()) {
      const ssize_t n = ::send(fd_, with_newline.data() + sent, with_newline.size() - sent, 0);
      if (n <= 0) throw std::runtime_error("send failed");
      sent += static_cast<std::size_t>(n);
    }

    std::string response;
    char buf[4096];
    while (response.find('\n') == std::string::npos) {
      const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
      if (n <= 0) throw std::runtime_error("recv failed / connection closed");
      response.append(buf, static_cast<std::size_t>(n));
    }
    return response;
  }

 private:
  int fd_ = -1;
};

struct WorkerResult {
  std::uint64_t ops = 0;
  std::vector<double> latencies_us;
};

WorkerResult RunWorker(const Options& opts, int worker_id, const std::atomic<bool>& stop) {
  Connection conn(opts.host, opts.port);
  std::mt19937 rng(static_cast<unsigned>(worker_id) ^ static_cast<unsigned>(std::random_device{}()));
  std::uniform_int_distribution<int> key_dist(0, opts.num_keys - 1);
  std::uniform_real_distribution<double> op_dist(0.0, 1.0);
  const std::string value(static_cast<std::size_t>(opts.value_size), 'x');

  WorkerResult result;
  result.latencies_us.reserve(1 << 16);

  while (!stop.load(std::memory_order_relaxed)) {
    const std::string key = "bench_key_" + std::to_string(key_dist(rng));
    const bool is_read = op_dist(rng) < opts.read_ratio;

    const auto start = std::chrono::steady_clock::now();
    if (is_read) {
      conn.SendLine("GET " + key);
    } else {
      conn.SendLine("SET " + key + " " + value);
    }
    const auto end = std::chrono::steady_clock::now();

    result.latencies_us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    result.ops++;
  }
  return result;
}

double Percentile(const std::vector<double>& sorted_us, double p) {
  if (sorted_us.empty()) return 0.0;
  const std::size_t index = static_cast<std::size_t>(p * static_cast<double>(sorted_us.size() - 1));
  return sorted_us[index];
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  try {
    opts = ParseArgs(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "kv_bench: " << e.what() << "\n";
    std::cerr << "usage: kv_bench --port <port> [--host 127.0.0.1] [--threads 4] "
                 "[--duration 5] [--keys 1000] [--value-size 100] [--read-ratio 0.9]\n";
    return 1;
  }

  std::cout << "warming up: writing " << opts.num_keys << " keys...\n";
  try {
    Connection warmup(opts.host, opts.port);
    const std::string value(static_cast<std::size_t>(opts.value_size), 'x');
    for (int i = 0; i < opts.num_keys; ++i) {
      warmup.SendLine("SET bench_key_" + std::to_string(i) + " " + value);
    }
  } catch (const std::exception& e) {
    std::cerr << "kv_bench: warm-up failed: " << e.what() << "\n";
    return 1;
  }

  std::cout << "running " << opts.threads << " threads for " << opts.duration_seconds
            << "s (read_ratio=" << opts.read_ratio << ", value_size=" << opts.value_size << ")...\n";

  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;
  std::vector<WorkerResult> results(static_cast<std::size_t>(opts.threads));

  for (int t = 0; t < opts.threads; ++t) {
    workers.emplace_back([&, t] { results[static_cast<std::size_t>(t)] = RunWorker(opts, t, stop); });
  }

  const auto benchmark_start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(opts.duration_seconds));
  stop.store(true, std::memory_order_relaxed);
  for (auto& w : workers) w.join();
  const auto benchmark_end = std::chrono::steady_clock::now();

  std::uint64_t total_ops = 0;
  std::vector<double> all_latencies_us;
  for (const auto& r : results) {
    total_ops += r.ops;
    all_latencies_us.insert(all_latencies_us.end(), r.latencies_us.begin(), r.latencies_us.end());
  }
  std::sort(all_latencies_us.begin(), all_latencies_us.end());

  const double wall_seconds = std::chrono::duration<double>(benchmark_end - benchmark_start).count();
  const double ops_per_sec = static_cast<double>(total_ops) / wall_seconds;

  std::cout << "\n--- results ---\n";
  std::cout << "total ops:   " << total_ops << "\n";
  std::cout << "duration:    " << wall_seconds << "s\n";
  std::cout << "throughput:  " << static_cast<std::uint64_t>(ops_per_sec) << " ops/sec\n";
  std::cout << "latency (us): p50=" << Percentile(all_latencies_us, 0.50) << " p95=" << Percentile(all_latencies_us, 0.95)
            << " p99=" << Percentile(all_latencies_us, 0.99) << " p999=" << Percentile(all_latencies_us, 0.999)
            << " max=" << (all_latencies_us.empty() ? 0.0 : all_latencies_us.back()) << "\n";

  return 0;
}
