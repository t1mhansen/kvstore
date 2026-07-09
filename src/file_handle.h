#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace kv {

// RAII wrapper around a POSIX file descriptor.
//
// Deliberately uses pread/pwrite with explicit offsets instead of the C++
// iostream file API (ifstream/ofstream). Two reasons: iostreams don't expose
// fsync at all, and durability is the entire point of a write-ahead log, so
// that call needs to be visible and explicit rather than hidden behind
// buffered-stream flushing. And pread/pwrite take the offset as an argument
// instead of relying on one shared file cursor, which is what will let
// multiple readers/writers safely share this fd down the line without
// racing on seek+read/write.
class FileHandle {
 public:
  explicit FileHandle(const std::filesystem::path& path);
  ~FileHandle();

  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;
  FileHandle(FileHandle&&) noexcept;
  FileHandle& operator=(FileHandle&&) noexcept;

  void Write(std::uint64_t offset, const void* data, std::size_t length) const;
  void Read(std::uint64_t offset, void* out, std::size_t length) const;
  void Sync() const;
  std::uint64_t Size() const;
  void Truncate(std::uint64_t new_size) const;

 private:
  void Close() noexcept;

  int fd_ = -1;
};

}  // namespace kv
