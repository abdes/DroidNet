//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <span>
#include <system_error>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AtomicFile.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <errhandlingapi.h>
#  include <fileapi.h>
#  include <handleapi.h>
#  include <minwindef.h>
#  include <winbase.h>
#  include <winnt.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace oxygen::serio {
namespace {

  auto WriteTemporary(const std::filesystem::path& path,
    const std::filesystem::path& destination, std::span<const std::byte> bytes)
    -> Result<void>
  {
#ifdef OXYGEN_WINDOWS
    static_cast<void>(destination);
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return Result<void>::Err(std::error_code(
        static_cast<int>(GetLastError()), std::system_category()));
    }
    const auto close
      = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });
    while (!bytes.empty()) {
      const auto count = static_cast<DWORD>((std::min)(bytes.size(),
        static_cast<size_t>(std::numeric_limits<DWORD>::max())));
      DWORD written = 0;
      if (WriteFile(handle, bytes.data(), count, &written, nullptr) == FALSE) {
        return Result<void>::Err(std::error_code(
          static_cast<int>(GetLastError()), std::system_category()));
      }
      if (written == 0U) {
        return Result<void>::Err(std::make_error_code(std::errc::io_error));
      }
      bytes = bytes.subspan(written);
    }
    if (FlushFileBuffers(handle) == FALSE) {
      return Result<void>::Err(std::error_code(
        static_cast<int>(GetLastError()), std::system_category()));
    }
#else
    const auto descriptor = ::open(
      path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
      return Result<void>::Err(std::error_code(errno, std::generic_category()));
    }
    const auto close
      = ScopeGuard([descriptor]() noexcept { ::close(descriptor); });
    struct stat original {};
    if (::stat(destination.c_str(), &original) == 0
      && ::fchmod(descriptor, original.st_mode) != 0) {
      return Result<void>::Err(std::error_code(errno, std::generic_category()));
    }
    while (!bytes.empty()) {
      const auto written = ::write(descriptor, bytes.data(), bytes.size());
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        return Result<void>::Err(
          std::error_code(errno, std::generic_category()));
      }
      if (written == 0) {
        return Result<void>::Err(std::make_error_code(std::errc::io_error));
      }
      bytes = bytes.subspan(static_cast<size_t>(written));
    }
    if (::fsync(descriptor) != 0) {
      return Result<void>::Err(std::error_code(errno, std::generic_category()));
    }
#endif
    return {};
  }

} // namespace

auto WriteFileAtomically(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> Result<AtomicFileCommit>
{
  if (path.filename().empty()) {
    return Result<AtomicFileCommit>::Err(
      std::make_error_code(std::errc::invalid_argument));
  }
  const auto temporary_name = ".oxygen-" + Uuid::Generate().ToString() + ".tmp";
  const auto native_path = base::ToNativePath(path);
  const auto temporary
    = base::ToNativePath(path.parent_path() / temporary_name);
  const auto cleanup = ScopeGuard([&temporary] noexcept -> void {
    std::error_code error;
    std::filesystem::remove(temporary, error);
  });
  const auto written = WriteTemporary(temporary, native_path, bytes);
  if (!written) {
    return Result<AtomicFileCommit>::Err(written.error());
  }
#ifdef OXYGEN_WINDOWS
  const bool exists
    = GetFileAttributesW(native_path.c_str()) != INVALID_FILE_ATTRIBUTES;
  const auto replaced = exists
    ? ReplaceFileW(
        native_path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)
    : MoveFileExW(
        temporary.c_str(), native_path.c_str(), MOVEFILE_WRITE_THROUGH);
  if (replaced == FALSE) {
    return Result<AtomicFileCommit>::Err(std::error_code(
      static_cast<int>(GetLastError()), std::system_category()));
  }
#else
  if (::rename(temporary.c_str(), path.c_str()) != 0) {
    return Result<AtomicFileCommit>::Err(
      std::error_code(errno, std::generic_category()));
  }
  const auto parent
    = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
  const auto directory
    = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) {
    return Ok(AtomicFileCommit {
      .durability_error = std::error_code(errno, std::generic_category()) });
  }
  const auto close = ScopeGuard([directory]() noexcept { ::close(directory); });
  if (::fsync(directory) != 0) {
    return Ok(AtomicFileCommit {
      .durability_error = std::error_code(errno, std::generic_category()) });
  }
#endif
  return Ok(AtomicFileCommit {});
}

} // namespace oxygen::serio
