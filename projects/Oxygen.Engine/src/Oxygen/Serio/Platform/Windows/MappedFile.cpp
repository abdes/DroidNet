//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

#include <excpt.h>
#include <fileapi.h>
#include <handleapi.h>
#include <memoryapi.h>
#include <minwindef.h>
#include <winbase.h>
#include <winnt.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/Detail/GuardedCopy.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace {
  //! The share mode of an open that lets others do `sharing`.
  auto ShareModeOf(const Sharing sharing) noexcept -> DWORD
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
} // namespace

namespace detail {

  // Structured exception handling cannot share a frame with objects that need
  // unwinding, so the guarded copy is a function of its own.
  auto GuardedCopy(std::byte* destination, const std::byte* source,
    const size_t size) noexcept -> bool
  {
    __try {
      std::memcpy(destination, source, size);
    } __except (GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER
        : EXCEPTION_CONTINUE_SEARCH) {
      return false;
    }
    return true;
  }

} // namespace detail

auto MappedFile::Open(const std::filesystem::path& path,
  const Sharing sharing) noexcept -> Result<MappedFile>
{
  try {
    auto logical = path;
    const auto native = base::ToNativePath(path);
    auto* const file
      = CreateFileW(native.c_str(), GENERIC_READ, ShareModeOf(sharing), nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return ::oxygen::Err(base::LastSystemError());
    }
    // The view keeps the section, and the section the file, alive: both
    // handles close once the view is mapped.
    const auto close_file
      = ScopeGuard([file] noexcept -> void { CloseHandle(file); });

    auto size = LARGE_INTEGER {};
    if (GetFileSizeEx(file, &size) == FALSE) {
      return ::oxygen::Err(base::LastSystemError());
    }
    if (static_cast<uint64_t>(size.QuadPart)
      > std::numeric_limits<size_t>::max()) {
      return ::oxygen::Err(std::make_error_code(std::errc::value_too_large));
    }
    if (size.QuadPart == 0) {
      return ::oxygen::Ok(MappedFile(nullptr, 0, std::move(logical)));
    }

    auto* const section
      = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (section == nullptr) {
      return ::oxygen::Err(base::LastSystemError());
    }
    const auto close_section
      = ScopeGuard([section] noexcept -> void { CloseHandle(section); });

    auto* const view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
      return ::oxygen::Err(base::LastSystemError());
    }
    return ::oxygen::Ok(MappedFile(static_cast<std::byte*>(view),
      static_cast<size_t>(size.QuadPart), std::move(logical)));
  } catch (const std::bad_alloc&) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  } catch (const std::filesystem::filesystem_error& error) {
    return ::oxygen::Err(error.code());
  }
}

auto MappedFile::Close() noexcept -> void
{
  if (view_ != nullptr) {
    UnmapViewOfFile(view_);
    view_ = nullptr;
    size_ = 0;
  }
}

auto MappedFile::Prefetch(const ByteRange range) const noexcept -> void
{
  if (range.offset.get() >= size_ || range.count.get() == 0) {
    return;
  }
  const auto offset = static_cast<size_t>(range.offset.get());
  const auto count = static_cast<size_t>(
    (std::min)(range.count.get(), uint64_t { size_ - offset }));
  auto entry = WIN32_MEMORY_RANGE_ENTRY {};
  entry.VirtualAddress = Pages().subspan(offset).data();
  entry.NumberOfBytes = count;
  PrefetchVirtualMemory(GetCurrentProcess(), 1, &entry, 0);
}

} // namespace oxygen::serio
