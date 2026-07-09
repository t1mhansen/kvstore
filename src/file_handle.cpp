#include "file_handle.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kv {

namespace {

[[noreturn]] void ThrowErrno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

FileHandle::FileHandle(const std::filesystem::path& path) {
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ == -1) {
    ThrowErrno("open");
  }
}

FileHandle::~FileHandle() { Close(); }

FileHandle::FileHandle(FileHandle&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

FileHandle& FileHandle::operator=(FileHandle&& other) noexcept {
  if (this != &other) {
    Close();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

void FileHandle::Close() noexcept {
  if (fd_ != -1) {
    ::close(fd_);
    fd_ = -1;
  }
}

// pwrite is not guaranteed to write the whole buffer in one call (short
// writes can happen, e.g. if a signal interrupts partway through), so this
// has to loop until everything requested has actually gone out. Bugs from
// skipping this loop are the kind that only show up under load, so it's
// worth getting right from the start rather than adding it later.
void FileHandle::Write(std::uint64_t offset, const void* data, std::size_t length) const {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t written = 0;
  while (written < length) {
    const ssize_t n =
        ::pwrite(fd_, bytes + written, length - written, static_cast<off_t>(offset + written));
    if (n == -1) {
      if (errno == EINTR) continue;  // interrupted by a signal, not a real error - retry
      ThrowErrno("pwrite");
    }
    written += static_cast<std::size_t>(n);
  }
}

// Same short-read concern as Write. A read returning 0 here means we asked
// for bytes past the end of the file, which should never happen if the
// caller computed the offset/length from the log itself - treated as a bug,
// not a normal "not found" case.
void FileHandle::Read(std::uint64_t offset, void* out, std::size_t length) const {
  auto* bytes = static_cast<unsigned char*>(out);
  std::size_t bytes_read = 0;
  while (bytes_read < length) {
    const ssize_t n =
        ::pread(fd_, bytes + bytes_read, length - bytes_read, static_cast<off_t>(offset + bytes_read));
    if (n == -1) {
      if (errno == EINTR) continue;
      ThrowErrno("pread");
    }
    if (n == 0) {
      throw std::runtime_error("unexpected EOF reading file");
    }
    bytes_read += static_cast<std::size_t>(n);
  }
}

void FileHandle::Sync() const {
  if (::fsync(fd_) == -1) {
    ThrowErrno("fsync");
  }
}

std::uint64_t FileHandle::Size() const {
  struct stat st {};
  if (::fstat(fd_, &st) == -1) {
    ThrowErrno("fstat");
  }
  return static_cast<std::uint64_t>(st.st_size);
}

}  // namespace kv
