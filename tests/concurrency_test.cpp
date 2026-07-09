#include "kv/storage_engine.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace kv {
namespace {

// The main thing this test proves is "doesn't crash or corrupt memory under
// contention," especially when run under ThreadSanitizer
// (cmake -DKV_ENABLE_TSAN=ON) - that's what actually catches a missing or
// too-narrowly-scoped lock, not the assertions below. Each thread uses its
// own key prefix, but every operation still contends on the same
// StorageEngine's single mutex, so this genuinely exercises the shared_mutex
// under a mix of concurrent reads and writes.
TEST(ConcurrencyTest, ManyThreadsPutGetDeleteConcurrently) {
  const auto path = std::filesystem::temp_directory_path() / "kv_concurrency_test.log";
  std::filesystem::remove(path);
  StorageEngine engine(path);

  constexpr int kThreads = 8;
  constexpr int kOpsPerThread = 500;
  constexpr int kKeysPerThread = 50;

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&engine, t] {
      for (int i = 0; i < kOpsPerThread; ++i) {
        const std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i % kKeysPerThread);
        engine.Put(key, "v" + std::to_string(i));
        engine.Get(key);
        if (i % 10 == 0) {
          engine.Delete(key);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_LE(engine.KeyCount(), static_cast<std::size_t>(kThreads * kKeysPerThread));

  std::filesystem::remove(path);
}

}  // namespace
}  // namespace kv
