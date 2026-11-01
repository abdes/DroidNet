//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <span>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/Detail/BlockingIo.h>
#include <Oxygen/Serio/NativeFile.h>

namespace oxygen::serio {
namespace {

  auto ToDescriptor(const NativeHandle handle) noexcept -> int
  {
    return static_cast<int>(handle);
  }

  auto OpenFlags(const OpenSpec& spec) noexcept -> int
  {
    int flags = O_CLOEXEC;
    switch (spec.access) {
    case Access::kRead:
      flags |= O_RDONLY;
      break;
    case Access::kWrite:
      flags |= O_WRONLY;
      break;
    case Access::kReadWrite:
      flags |= O_RDWR;
      break;
    }
    switch (spec.creation) {
    case Creation::kOpenExisting:
      break;
    case Creation::kOpenAlways:
      flags |= O_CREAT;
      break;
    case Creation::kCreateNew:
      flags |= O_CREAT | O_EXCL;
      break;
    case Creation::kCreateAlways:
      flags |= O_CREAT | O_TRUNC;
      break;
    }
#ifdef O_DIRECT
    if (spec.unbuffered) {
      flags |= O_DIRECT;
    }
#endif
    return flags;
  }

  auto ApplyHint(const int descriptor, const OpenSpec& spec) noexcept -> void
  {
#ifdef POSIX_FADV_SEQUENTIAL
    ::posix_fadvise(descriptor, 0, 0,
      spec.hint == AccessHint::kSequential ? POSIX_FADV_SEQUENTIAL
                                           : POSIX_FADV_RANDOM);
#else
    static_cast<void>(descriptor);
    static_cast<void>(spec);
#endif
  }

  auto OpenDescriptor(const std::filesystem::path& path,
    const OpenSpec& spec) noexcept -> Result<NativeHandle>
  {
    try {
      const auto native = base::ToNativePath(path);
      const auto descriptor
        = ::open(native.c_str(), OpenFlags(spec), S_IRUSR | S_IWUSR | S_IRGRP);
      if (descriptor < 0) {
        return ::oxygen::Err(base::LastSystemError());
      }
      // A folder opens read-only on POSIX; it is not a file to read.
      struct stat opened {};
      if (::fstat(descriptor, &opened) == 0 && S_ISDIR(opened.st_mode)) {
        static_cast<void>(::close(descriptor));
        return ::oxygen::Err(std::make_error_code(std::errc::is_a_directory));
      }
      ApplyHint(descriptor, spec);
      return ::oxygen::Ok(static_cast<NativeHandle>(descriptor));
    } catch (const std::bad_alloc&) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    } catch (const std::filesystem::filesystem_error& error) {
      return ::oxygen::Err(error.code());
    }
  }

} // namespace

auto NativeFile::Open(const std::filesystem::path& path,
  const OpenSpec& spec) noexcept -> Result<NativeFile>
{
  return Adopt(path, OpenDescriptor(path, spec));
}

// io_uring takes any descriptor: a driver opens files as any other.
auto NativeFile::OpenForDriver(const std::filesystem::path& path,
  const OpenSpec& spec) noexcept -> Result<NativeFile>
{
  return Open(path, spec);
}

auto NativeFile::Adopt(const std::filesystem::path& path,
  const Result<NativeHandle>& handle) noexcept -> Result<NativeFile>
{
  if (!handle) {
    return ::oxygen::Err(handle.error());
  }
  // The file owns the handle before anything can throw, so a failed path
  // copy closes it.
  auto file = NativeFile(*handle, {});
  try {
    file.path_ = path;
  } catch (const std::bad_alloc&) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  return ::oxygen::Ok(std::move(file));
}

NativeFile::NativeFile(
  const NativeHandle handle, std::filesystem::path path) noexcept
  : handle_(handle)
  , path_(std::move(path))
{
}

NativeFile::~NativeFile() noexcept { Close(); }

NativeFile::NativeFile(NativeFile&& other) noexcept
  : handle_(std::exchange(other.handle_, NativeHandle::kInvalid))
  , path_(std::move(other.path_))
{
}

auto NativeFile::operator=(NativeFile&& other) noexcept -> NativeFile&
{
  if (this != &other) {
    Close();
    handle_ = std::exchange(other.handle_, NativeHandle::kInvalid);
    path_ = std::move(other.path_);
  }
  return *this;
}

auto NativeFile::Close() noexcept -> void
{
  if (handle_ != NativeHandle::kInvalid) {
    ::close(ToDescriptor(handle_));
    handle_ = NativeHandle::kInvalid;
  }
}

auto NativeFile::Size() const noexcept -> Result<ByteCount>
{
  struct stat info {};
  if (::fstat(ToDescriptor(handle_), &info) != 0) {
    return ::oxygen::Err(base::LastSystemError());
  }
  return ::oxygen::Ok(ByteCount { static_cast<uint64_t>(info.st_size) });
}

namespace detail {

  auto ReadAtHandle(const NativeHandle file, ByteOffset at,
    std::span<std::byte> out) noexcept -> Result<ByteCount>
  {
    auto total = ByteCount { 0 };
    while (!out.empty()) {
      const auto read = ::pread(ToDescriptor(file), out.data(), out.size(),
        static_cast<off_t>(at.get()));
      if (read < 0) {
        if (errno == EINTR) {
          continue;
        }
        return ::oxygen::Err(base::LastSystemError());
      }
      if (read == 0) {
        break;
      }
      const auto count = ByteCount { static_cast<uint64_t>(read) };
      total += count;
      at += count;
      out = out.subspan(static_cast<size_t>(read));
    }
    return ::oxygen::Ok(total);
  }

  auto WriteAtHandle(const NativeHandle file, ByteOffset at,
    std::span<const std::byte> in) noexcept -> Result<void>
  {
    while (!in.empty()) {
      const auto written = ::pwrite(
        ToDescriptor(file), in.data(), in.size(), static_cast<off_t>(at.get()));
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        return ::oxygen::Err(base::LastSystemError());
      }
      if (written == 0) {
        return ::oxygen::Err(std::make_error_code(std::errc::io_error));
      }
      at += ByteCount { static_cast<uint64_t>(written) };
      in = in.subspan(static_cast<size_t>(written));
    }
    return {};
  }

} // namespace detail

auto NativeFile::ReadAt(ByteOffset at, std::span<std::byte> out) const noexcept
  -> Result<ByteCount>
{
  return detail::ReadAtHandle(handle_, at, out);
}

auto NativeFile::WriteAt(ByteOffset at, std::span<const std::byte> in) noexcept
  -> Result<void>
{
  return detail::WriteAtHandle(handle_, at, in);
}

auto NativeFile::Reserve(const ByteCount size) noexcept -> Result<void>
{
#ifdef FALLOC_FL_KEEP_SIZE
  // `fallocate` refuses an empty range, which reserves nothing anyway.
  if (size == ByteCount { 0 }) {
    return {};
  }
  if (::fallocate(ToDescriptor(handle_), FALLOC_FL_KEEP_SIZE, 0,
        static_cast<off_t>(size.get()))
    != 0) {
    return ::oxygen::Err(base::LastSystemError());
  }
#else
  // Preallocation without changing the size has no portable POSIX call; the
  // reservation is a hint the platform does not take.
  static_cast<void>(size);
#endif
  return {};
}

auto NativeFile::Flush(const Durability durability) noexcept -> Result<void>
{
  if (durability == Durability::kStable
    && ::fsync(ToDescriptor(handle_)) != 0) {
    return ::oxygen::Err(base::LastSystemError());
  }
  return {};
}

} // namespace oxygen::serio
