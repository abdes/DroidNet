//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
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

  auto FlagsAndAttributes(const OpenSpec& spec) noexcept -> DWORD
  {
    auto flags = static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL);
    flags |= spec.hint == AccessHint::kSequential
      ? static_cast<DWORD>(FILE_FLAG_SEQUENTIAL_SCAN)
      : static_cast<DWORD>(FILE_FLAG_RANDOM_ACCESS);
    if (spec.unbuffered) {
      flags |= static_cast<DWORD>(FILE_FLAG_NO_BUFFERING);
    }
    return flags;
  }

  auto OffsetOverlapped(const ByteOffset at) noexcept -> OVERLAPPED
  {
    auto overlapped = OVERLAPPED {};
    overlapped.Offset = static_cast<DWORD>(at.get() & 0xFFFF'FFFFU);
    overlapped.OffsetHigh = static_cast<DWORD>(at.get() >> 32U);
    return overlapped;
  }

} // namespace

auto NativeFile::Open(const std::filesystem::path& path,
  const OpenSpec& spec) noexcept -> Result<NativeFile>
{
  try {
    auto logical = path;
    const auto native = base::ToNativePath(path);
    auto* const handle = CreateFileW(native.c_str(), DesiredAccess(spec.access),
      ShareMode(spec.sharing), nullptr, CreationDisposition(spec.creation),
      FlagsAndAttributes(spec), nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return ::oxygen::Err(base::LastSystemError());
    }
    return ::oxygen::Ok(
      NativeFile(windows::ToNativeHandle(handle), std::move(logical)));
  } catch (const std::bad_alloc&) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  } catch (const std::filesystem::filesystem_error& error) {
    return ::oxygen::Err(error.code());
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

auto NativeFile::ReadAt(ByteOffset at, std::span<std::byte> out) const noexcept
  -> Result<ByteCount>
{
  auto total = ByteCount { 0 };
  while (!out.empty()) {
    const auto request
      = static_cast<DWORD>((std::min)(out.size(), kMaxTransfer));
    auto overlapped = OffsetOverlapped(at);
    DWORD read = 0;
    if (ReadFile(
          windows::ToHandle(handle_), out.data(), request, &read, &overlapped)
      == FALSE) {
      const auto error = base::LastSystemError();
      if (error.value() == ERROR_HANDLE_EOF) {
        break;
      }
      return ::oxygen::Err(error);
    }
    if (read == 0) {
      break;
    }
    total += ByteCount { read };
    at += ByteCount { read };
    out = out.subspan(read);
  }
  return ::oxygen::Ok(total);
}

auto NativeFile::WriteAt(ByteOffset at, std::span<const std::byte> in) noexcept
  -> Result<void>
{
  while (!in.empty()) {
    const auto request
      = static_cast<DWORD>((std::min)(in.size(), kMaxTransfer));
    auto overlapped = OffsetOverlapped(at);
    DWORD written = 0;
    if (WriteFile(
          windows::ToHandle(handle_), in.data(), request, &written, &overlapped)
      == FALSE) {
      return ::oxygen::Err(base::LastSystemError());
    }
    if (written == 0) {
      return ::oxygen::Err(std::make_error_code(std::errc::io_error));
    }
    at += ByteCount { written };
    in = in.subspan(written);
  }
  return {};
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
