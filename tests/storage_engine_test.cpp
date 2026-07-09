#include "kv/storage_engine.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace kv {
namespace {

class StorageEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("kv_test_") + test_info->test_suite_name() + "_" + test_info->name() +
             ".log");
    std::filesystem::remove(path_);
  }

  void TearDown() override { std::filesystem::remove(path_); }

  std::filesystem::path path_;
};

TEST_F(StorageEngineTest, GetOnMissingKeyReturnsNullopt) {
  StorageEngine engine(path_);
  EXPECT_EQ(engine.Get("missing"), std::nullopt);
}

TEST_F(StorageEngineTest, PutThenGetReturnsValue) {
  StorageEngine engine(path_);
  engine.Put("key", "value");
  EXPECT_EQ(engine.Get("key"), "value");
}

TEST_F(StorageEngineTest, OverwriteReturnsLatestValue) {
  StorageEngine engine(path_);
  engine.Put("key", "first");
  engine.Put("key", "second");
  EXPECT_EQ(engine.Get("key"), "second");
  EXPECT_EQ(engine.KeyCount(), 1u);
}

TEST_F(StorageEngineTest, DeleteRemovesKey) {
  StorageEngine engine(path_);
  engine.Put("key", "value");
  engine.Delete("key");
  EXPECT_EQ(engine.Get("key"), std::nullopt);
  EXPECT_EQ(engine.KeyCount(), 0u);
}

TEST_F(StorageEngineTest, DeleteOnMissingKeyIsNoop) {
  StorageEngine engine(path_);
  EXPECT_NO_THROW(engine.Delete("missing"));
  EXPECT_EQ(engine.KeyCount(), 0u);
}

TEST_F(StorageEngineTest, EmptyValueIsStoredAndRetrieved) {
  StorageEngine engine(path_);
  engine.Put("key", "");
  EXPECT_EQ(engine.Get("key"), "");
}

TEST_F(StorageEngineTest, ReopenRebuildsIndexFromLog) {
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    engine.Put("b", "2");
    engine.Put("a", "3");  // overwrite
    engine.Delete("b");
  }

  StorageEngine reopened(path_);
  EXPECT_EQ(reopened.Get("a"), "3");
  EXPECT_EQ(reopened.Get("b"), std::nullopt);
  EXPECT_EQ(reopened.KeyCount(), 1u);
}

}  // namespace
}  // namespace kv
