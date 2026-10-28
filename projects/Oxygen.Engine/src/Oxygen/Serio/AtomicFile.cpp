//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AtomicFile.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <AccCtrl.h>
#  include <AclAPI.h>
#  include <errhandlingapi.h>
#  include <fileapi.h>
#  include <handleapi.h>
#  include <minwindef.h>
#  include <winbase.h>
#  include <winnt.h>
#else
#  include <cerrno>

#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace oxygen::serio {
namespace {

  auto WriteTemporary(const std::filesystem::path& temporary,
    std::span<const std::byte> bytes) -> Result<void>
  {
#ifdef OXYGEN_WINDOWS
    auto* const handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0,
      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    const auto descriptor = ::open(temporary.c_str(),
      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
      return Result<void>::Err(std::error_code(errno, std::generic_category()));
    }
    const auto close
      = ScopeGuard([descriptor]() noexcept { ::close(descriptor); });
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

#ifdef OXYGEN_WINDOWS
  auto LastWindowsError() -> std::error_code
  {
    return { static_cast<int>(GetLastError()), std::system_category() };
  }

  //! Renames the temporary over the destination with POSIX semantics: readers
  //! holding the destination open with shared deletion keep the file they
  //! opened, and the name moves to the new file without waiting for them. A
  //! file system without POSIX renames gets a classic replacing move, which
  //! fails while any reader holds the destination.
  auto ReplaceDestination(const std::filesystem::path& temporary,
    const std::filesystem::path& destination) -> Result<AtomicFileCommit>
  {
    {
      auto* const handle
        = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE | SYNCHRONIZE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle == INVALID_HANDLE_VALUE) {
        return Result<AtomicFileCommit>::Err(LastWindowsError());
      }
      const auto close
        = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });

      // FILE_RENAME_INFO ends with a variable-length name. FileNameLength
      // excludes the terminator, but Windows converts the name as a
      // NUL-terminated path, so the zeroed storage must hold one.
      const auto target = std::filesystem::absolute(destination).native();
      const auto name_bytes = target.size() * sizeof(wchar_t);
      const auto info_bytes
        = offsetof(FILE_RENAME_INFO, FileName) + name_bytes + sizeof(wchar_t);
      auto storage = std::vector<uint64_t>(
        (info_bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
      // NOLINTNEXTLINE(*-reinterpret-cast) - Win32 variable-length record
      auto* const info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
      info->Flags
        = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
      info->RootDirectory = nullptr;
      info->FileNameLength = static_cast<DWORD>(name_bytes);
      std::memcpy(
        static_cast<void*>(info->FileName), target.data(), name_bytes);
      if (SetFileInformationByHandle(handle, FileRenameInfoEx, info,
            static_cast<DWORD>(storage.size() * sizeof(uint64_t)))
        != FALSE) {
        // The handle now names the destination; flushing it commits the
        // rename as MOVEFILE_WRITE_THROUGH would.
        if (FlushFileBuffers(handle) == FALSE) {
          return Ok(
            AtomicFileCommit { .durability_error = LastWindowsError() });
        }
        return Ok(AtomicFileCommit {});
      }
      const auto error = GetLastError();
      if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED
        && error != ERROR_INVALID_FUNCTION) {
        return Result<AtomicFileCommit>::Err(
          std::error_code(static_cast<int>(error), std::system_category()));
      }
    }
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
      == FALSE) {
      return Result<AtomicFileCommit>::Err(LastWindowsError());
    }
    return Ok(AtomicFileCommit {});
  }

  //! Gives the temporary the destination's DACL, keeping its protection, so
  //! the rename preserves access permissions. A missing destination has none.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) -> Result<void>
  {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto queried = GetNamedSecurityInfoW(destination.c_str(),
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
      nullptr, &descriptor);
    if (queried == ERROR_FILE_NOT_FOUND || queried == ERROR_PATH_NOT_FOUND) {
      return {};
    }
    if (queried != ERROR_SUCCESS) {
      return Result<void>::Err(
        std::error_code(static_cast<int>(queried), std::system_category()));
    }
    const auto release
      = ScopeGuard([descriptor] noexcept -> void { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (GetSecurityDescriptorControl(descriptor, &control, &revision)
      == FALSE) {
      return Result<void>::Err(std::error_code(
        static_cast<int>(GetLastError()), std::system_category()));
    }
    // Inherited ACEs in the copied DACL are recomputed from the shared parent.
    const DWORD protection = (control & SE_DACL_PROTECTED) != 0
      ? PROTECTED_DACL_SECURITY_INFORMATION
      : UNPROTECTED_DACL_SECURITY_INFORMATION;
    const auto applied = SetNamedSecurityInfoW(
      const_cast<wchar_t*>(temporary.c_str()), // NOLINT(*-const-cast)
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | protection, nullptr, nullptr,
      dacl, nullptr);
    if (applied != ERROR_SUCCESS) {
      return Result<void>::Err(
        std::error_code(static_cast<int>(applied), std::system_category()));
    }
    return {};
  }
#else
  //! Gives the temporary the destination's mode so the rename preserves access
  //! permissions. A missing destination has none.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) -> Result<void>
  {
    struct stat original {};
    if (::stat(destination.c_str(), &original) == 0
      && ::chmod(temporary.c_str(), original.st_mode) != 0) {
      return Result<void>::Err(std::error_code(errno, std::generic_category()));
    }
    return {};
  }
#endif

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
  const auto written = WriteTemporary(temporary, bytes);
  if (!written) {
    return Result<AtomicFileCommit>::Err(written.error());
  }
  const auto access = CopyAccessControl(native_path, temporary);
  if (!access) {
    return Result<AtomicFileCommit>::Err(access.error());
  }
#ifdef OXYGEN_WINDOWS
  // One same-directory rename that always replaces: concurrent publishers
  // never see a missing destination or race an existence check.
  return ReplaceDestination(temporary, native_path);
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
  return Ok(AtomicFileCommit {});
#endif
}

auto ReadPublishedFile(const std::filesystem::path& path)
  -> Result<std::vector<std::byte>>
{
#ifdef OXYGEN_WINDOWS
  auto* const handle = CreateFileW(base::ToNativePath(path).c_str(),
    GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return Result<std::vector<std::byte>>::Err(LastWindowsError());
  }
  const auto close
    = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });
  LARGE_INTEGER size {};
  if (GetFileSizeEx(handle, &size) == FALSE) {
    return Result<std::vector<std::byte>>::Err(LastWindowsError());
  }
  if (static_cast<uint64_t>(size.QuadPart)
    > std::numeric_limits<size_t>::max()) {
    return Result<std::vector<std::byte>>::Err(
      std::make_error_code(std::errc::file_too_large));
  }
  auto bytes = std::vector<std::byte>(static_cast<size_t>(size.QuadPart));
  auto remaining = std::span(bytes);
  while (!remaining.empty()) {
    const auto count = static_cast<DWORD>((std::min)(remaining.size(),
      static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    DWORD read = 0;
    if (ReadFile(handle, remaining.data(), count, &read, nullptr) == FALSE) {
      return Result<std::vector<std::byte>>::Err(LastWindowsError());
    }
    if (read == 0U) {
      return Result<std::vector<std::byte>>::Err(
        std::make_error_code(std::errc::io_error));
    }
    remaining = remaining.subspan(read);
  }
#else
  const auto descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    return Result<std::vector<std::byte>>::Err(
      std::error_code(errno, std::generic_category()));
  }
  const auto close
    = ScopeGuard([descriptor]() noexcept { ::close(descriptor); });
  struct stat status {};
  if (::fstat(descriptor, &status) != 0) {
    return Result<std::vector<std::byte>>::Err(
      std::error_code(errno, std::generic_category()));
  }
  auto bytes = std::vector<std::byte>(static_cast<size_t>(status.st_size));
  auto remaining = std::span(bytes);
  while (!remaining.empty()) {
    const auto read = ::read(descriptor, remaining.data(), remaining.size());
    if (read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return Result<std::vector<std::byte>>::Err(
        std::error_code(errno, std::generic_category()));
    }
    if (read == 0) {
      return Result<std::vector<std::byte>>::Err(
        std::make_error_code(std::errc::io_error));
    }
    remaining = remaining.subspan(static_cast<size_t>(read));
  }
#endif
  return Ok(std::move(bytes));
}

} // namespace oxygen::serio
