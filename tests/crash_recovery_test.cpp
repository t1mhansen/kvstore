#include "kv/storage_engine.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace kv {
namespace {

// No need to actually crash a process to test this - a crash just leaves a
// partial/garbled record at the end of an otherwise-valid log, and that's
// easy to recreate directly by writing real records and then truncating or
// corrupting the file afterward.
class CrashRecoveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("kv_crash_test_") + test_info->test_suite_name() + "_" +
             test_info->name() + ".log");
    std::filesystem::remove(path_);
  }

  void TearDown() override { std::filesystem::remove(path_); }

  // Chops `bytes_to_cut` off the end of the log, simulating a crash that hit
  // partway through flushing the last write.
  void CutBytesFromEnd(std::uintmax_t bytes_to_cut) {
    const auto size = std::filesystem::file_size(path_);
    std::filesystem::resize_file(path_, size - bytes_to_cut);
  }

  // Flips a bit in the very last byte of the log without changing its
  // length, simulating a write where only part of a disk sector made it out
  // before the crash.
  void FlipLastByte() {
    const auto size = std::filesystem::file_size(path_);
    std::fstream file(path_, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(static_cast<std::streamoff>(size) - 1);
    char byte = 0;
    file.read(&byte, 1);
    byte ^= 0xFF;
    file.seekp(static_cast<std::streamoff>(size) - 1);
    file.write(&byte, 1);
  }

  std::filesystem::path path_;
};

TEST_F(CrashRecoveryTest, TornValueIsDiscardedButEarlierRecordsSurvive) {
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    engine.Put("b", "2");
  }

  CutBytesFromEnd(3);  // eats into record b's value, leaves record a intact

  StorageEngine recovered(path_);
  EXPECT_EQ(recovered.Get("a"), "1");
  EXPECT_EQ(recovered.Get("b"), std::nullopt);
  EXPECT_EQ(recovered.KeyCount(), 1u);
}

TEST_F(CrashRecoveryTest, TornHeaderIsDiscarded) {
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    engine.Put("b", "2");  // header alone is 13 bytes
  }

  CutBytesFromEnd(14);  // cuts entirely through record b's header

  StorageEngine recovered(path_);
  EXPECT_EQ(recovered.Get("a"), "1");
  EXPECT_EQ(recovered.Get("b"), std::nullopt);
  EXPECT_EQ(recovered.KeyCount(), 1u);
}

TEST_F(CrashRecoveryTest, CorruptedChecksumOnTailIsDiscarded) {
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    engine.Put("b", "2");
  }

  FlipLastByte();  // same length, but record b's checksum no longer matches

  StorageEngine recovered(path_);
  EXPECT_EQ(recovered.Get("a"), "1");
  EXPECT_EQ(recovered.Get("b"), std::nullopt);
}

TEST_F(CrashRecoveryTest, RecoveryPhysicallyTruncatesTheFile) {
  std::uintmax_t size_after_first_record = 0;
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    size_after_first_record = std::filesystem::file_size(path_);
    engine.Put("b", "2");
  }

  CutBytesFromEnd(3);
  { StorageEngine recovered(path_); }  // recovery runs in the constructor

  EXPECT_EQ(std::filesystem::file_size(path_), size_after_first_record);
}

TEST_F(CrashRecoveryTest, CanWriteAfterRecovering) {
  {
    StorageEngine engine(path_);
    engine.Put("a", "1");
    engine.Put("b", "2");
  }
  CutBytesFromEnd(3);

  {
    StorageEngine recovered(path_);
    recovered.Put("c", "3");
  }

  StorageEngine reopened(path_);
  EXPECT_EQ(reopened.Get("a"), "1");
  EXPECT_EQ(reopened.Get("b"), std::nullopt);
  EXPECT_EQ(reopened.Get("c"), "3");
  EXPECT_EQ(reopened.KeyCount(), 2u);
}

TEST_F(CrashRecoveryTest, TornFirstRecordLeavesEmptyStore) {
  {
    StorageEngine engine(path_);
    engine.Put("only", "value");
  }
  CutBytesFromEnd(3);

  StorageEngine recovered(path_);
  EXPECT_EQ(recovered.KeyCount(), 0u);
  EXPECT_EQ(std::filesystem::file_size(path_), 0u);
}

}  // namespace
}  // namespace kv
