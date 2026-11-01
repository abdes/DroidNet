//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/Detail/BlockingIo.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Platform/Windows/Handles.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {
namespace {

  //! The largest transfer one `ReadFile`/`WriteFile` call takes.
  constexpr auto kMaxTransfer
    = static_cast<size_t>(std::numeric_limits<DWORD>::max());

  auto DesiredAccess(const Access access) noexcept -> DWORD
  {
    switch (access) {
    case Access::kRead:
      return GENERIC_READ;
    case Access::kWrite:
      return GENERIC_WRITE;
    case Access::kReadWrite:
      return GENERIC_READ | GENERIC_WRITE;
    }
    return GENERIC_READ;
  }

  auto CreationDisposition(const Creation creation) noexcept -> DWORD
  {
    switch (creation) {
    case Creation::kOpenExisting:
      return OPEN_EXISTING;
    case Creation::kOpenAlways:
      return OPEN_ALWAYS;
    case Creation::kCreateNew:
      return CREATE_NEW;
    case Creation::kCreateAlways:
      return CREATE_ALWAYS;
    }
    return OPEN_EXISTING;
  }

  auto ShareMode(const Sharing sharing) noexcept -> DWORD
  {
    switch (sharing) {
    case Sharing::kNone:
      return 0;
    case Sharing::kRead:
      return FILE_SHARE_READ;
    case Sharing::kReadDelete:
      return FILE_SHARE_READ | FILE_SHARE_DELETE;
    case Sharing::kReadWriteDelete:
      return FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    }
    return FILE_SHARE_READ | FILE_SHARE_DELETE;
  }

  auto FlagsAndAttributes(const OpenSpec& spec, const bool overlapped) noexcept
    -> DWORD
  {
    auto flags = static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL);
    if (overlapped) {
      flags |= static_cast<DWORD>(FILE_FLAG_OVERLAPPED);
    }
    flags |= spec.hint == AccessHint::kSequential
      ? static_cast<DWORD>(FILE_FLAG_SEQUENTIAL_SCAN)
      : static_cast<DWORD>(FILE_FLAG_RANDOM_ACCESS);
    if (spec.unbuffered) {
      flags |= static_cast<DWORD>(FILE_FLAG_NO_BUFFERING);
    }
    return flags;
  }

  //! An event each thread waits on for its blocking operations.
  class ThreadEvent {
  public:
    ThreadEvent() noexcept
      : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr))
    {
    }

    ~ThreadEvent() noexcept
    {
      if (event_ != nullptr) {
        CloseHandle(event_);
      }
    }

    ThreadEvent(const ThreadEvent&) = delete;
    ThreadEvent(ThreadEvent&&) = delete;
    auto operator=(const ThreadEvent&) -> ThreadEvent& = delete;
    auto operator=(ThreadEvent&&) -> ThreadEvent& = delete;

    [[nodiscard]] auto Get() const noexcept -> HANDLE { return event_; }

  private:
    HANDLE event_;
  };

  //! The OVERLAPPED of a blocking operation at `at`.
  /*!
   A file an `IoDriver` opened is overlapped and bound to a completion port.
   A blocking operation on it waits on the thread's event, whose low bit set
   keeps the completion off the port, where the driver's backend expects only
   its own requests. On any other file the operation completes synchronously
   and the event is merely signalled.
  */
  auto BlockingOverlapped(const ByteOffset at) noexcept -> Result<OVERLAPPED>
  {
    thread_local const auto event = ThreadEvent {};
    if (event.Get() == nullptr) {
      return ::oxygen::Err(base::LastSystemError());
    }
    auto overlapped = OVERLAPPED {};
    overlapped.Offset = static_cast<DWORD>(at.get() & 0xFFFF'FFFFU);
    overlapped.OffsetHigh = static_cast<DWORD>(at.get() >> 32U);
    overlapped.hEvent = std::bit_cast<HANDLE>(
      std::bit_cast<uintptr_t>(event.Get()) | uintptr_t { 1 });
    return ::oxygen::Ok(overlapped);
  }

  //! The bytes a blocking `ReadFile` or `WriteFile` transferred, waiting for
  //! it when it is pending. `completed` is the count the call returned when
  //! it succeeded at once, empty when it returned FALSE.
  auto Finish(HANDLE handle, OVERLAPPED& overlapped,
    const std::optional<DWORD> completed) noexcept -> Result<DWORD>
  {
    if (completed) {
      return ::oxygen::Ok(*completed);
    }
    if (GetLastError() != ERROR_IO_PENDING) {
      return ::oxygen::Err(base::LastSystemError());
    }
    DWORD transferred = 0;
    if (GetOverlappedResult(handle, &overlapped, &transferred, TRUE) == FALSE) {
      return ::oxygen::Err(base::LastSystemError());
    }
    return ::oxygen::Ok(transferred);
  }

  auto OpenFile(const std::filesystem::path& path, const OpenSpec& spec,
    const bool overlapped) noexcept -> Result<NativeHandle>
  {
    try {
      const auto native = base::ToNativePath(path);
      auto* const handle
        = CreateFileW(native.c_str(), DesiredAccess(spec.access),
          ShareMode(spec.sharing), nullptr, CreationDisposition(spec.creation),
          FlagsAndAttributes(spec, overlapped), nullptr);
      if (handle == INVALID_HANDLE_VALUE) {
        const auto error = base::LastSystemError();
        // Windows refuses to open a folder as a file with a denied access.
        auto ignored = std::error_code {};
        if (static_cast<DWORD>(error.value()) == ERROR_ACCESS_DENIED
          && std::filesystem::is_directory(native, ignored)) {
          return ::oxygen::Err(std::make_error_code(std::errc::is_a_directory));
        }
        return ::oxygen::Err(error);
      }
      return ::oxygen::Ok(windows::ToNativeHandle(handle));
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
  return Adopt(path, OpenFile(path, spec, false));
}

auto NativeFile::OpenForDriver(const std::filesystem::path& path,
  const OpenSpec& spec) noexcept -> Result<NativeFile>
{
  return Adopt(path, OpenFile(path, spec, true));
}

auto NativeFile::Adopt(const std::filesystem::path& path,
  const Result<NativeHandle>& handle) noexcept -> Result<NativeFile>
{
  try {
    if (!handle) {
      return ::oxygen::Err(handle.error());
    }
    return ::oxygen::Ok(NativeFile(*handle, path));
  } catch (const std::bad_alloc&) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
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
    CloseHandle(windows::ToHandle(handle_));
    handle_ = NativeHandle::kInvalid;
  }
}

auto NativeFile::Size() const noexcept -> Result<ByteCount>
{
  auto size = LARGE_INTEGER {};
  if (GetFileSizeEx(windows::ToHandle(handle_), &size) == FALSE) {
    return ::oxygen::Err(base::LastSystemError());
  }
  return ::oxygen::Ok(ByteCount { static_cast<uint64_t>(size.QuadPart) });
}

namespace detail {

  auto ReadAtHandle(const NativeHandle file, ByteOffset at,
    std::span<std::byte> out) noexcept -> Result<ByteCount>
  {
    auto total = ByteCount { 0 };
    while (!out.empty()) {
      const auto request
        = static_cast<DWORD>((std::min)(out.size(), kMaxTransfer));
      auto overlapped = BlockingOverlapped(at);
      if (!overlapped) {
        return ::oxygen::Err(overlapped.error());
      }
      auto* const handle = windows::ToHandle(file);
      DWORD immediate = 0;
      const auto finished = Finish(handle, *overlapped,
        ReadFile(handle, out.data(), request, &immediate, &*overlapped) != FALSE
          ? std::optional { immediate }
          : std::nullopt);
      if (!finished) {
        if (finished.error().value() == ERROR_HANDLE_EOF) {
          break;
        }
        return ::oxygen::Err(finished.error());
      }
      const auto read = *finished;
      if (read == 0) {
        break;
      }
      total += ByteCount { read };
      at += ByteCount { read };
      out = out.subspan(read);
    }
    return ::oxygen::Ok(total);
  }

  auto WriteAtHandle(const NativeHandle file, ByteOffset at,
    std::span<const std::byte> in) noexcept -> Result<void>
  {
    while (!in.empty()) {
      const auto request
        = static_cast<DWORD>((std::min)(in.size(), kMaxTransfer));
      auto overlapped = BlockingOverlapped(at);
      if (!overlapped) {
        return ::oxygen::Err(overlapped.error());
      }
      auto* const handle = windows::ToHandle(file);
      DWORD immediate = 0;
      const auto finished = Finish(handle, *overlapped,
        WriteFile(handle, in.data(), request, &immediate, &*overlapped) != FALSE
          ? std::optional { immediate }
          : std::nullopt);
      if (!finished) {
        return ::oxygen::Err(finished.error());
      }
      const auto written = *finished;
      if (written == 0) {
        return ::oxygen::Err(std::make_error_code(std::errc::io_error));
      }
      at += ByteCount { written };
      in = in.subspan(written);
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
  auto info = FILE_ALLOCATION_INFO {};
  info.AllocationSize.QuadPart = static_cast<LONGLONG>(size.get());
  if (SetFileInformationByHandle(
        windows::ToHandle(handle_), FileAllocationInfo, &info, sizeof(info))
    == FALSE) {
    return ::oxygen::Err(base::LastSystemError());
  }
  return {};
}

auto NativeFile::Flush(const Durability durability) noexcept -> Result<void>
{
  if (durability == Durability::kStable
    && FlushFileBuffers(windows::ToHandle(handle_)) == FALSE) {
    return ::oxygen::Err(base::LastSystemError());
  }
  return {};
}

} // namespace oxygen::serio
